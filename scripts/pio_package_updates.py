#!/usr/bin/env python3
"""Apply PlatformIO registry updates that Dependabot cannot see.

Dependabot has no platformio.ini ecosystem. This script reads `pio pkg outdated`
for every environment in platformio.ini, rewrites exact pins, and refreshes the
matching OSV source-lock commits. A weekly workflow opens one pull request on
the stable `deps/platformio-updates` branch when anything allowed has a newer
registry version.

The Teensy platform, framework, toolchain and tool-teensy pins stay skipped.
They already track Teensyduino 1.62 / GCC 15.2.1 together; a later platform
or core bump can re-break MTP exclusion or the imxrt.h/compiler pairing.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path


SKIP_AUTO_UPDATE = frozenset({
    "teensy",
    "framework-arduinoteensy",
    "toolchain-gccarmnoneeabi-teensy",
    "tool-teensy",
})

UPDATE_BRANCH = "deps/platformio-updates"

GITHUB_LOCKS = {
    "QNEthernet": ("ssilverman/QNEthernet", "v{version}"),
    "Adafruit GFX Library": ("adafruit/Adafruit-GFX-Library", "{version}"),
    "Adafruit SSD1306": ("adafruit/Adafruit_SSD1306", "{version}"),
    "Adafruit BusIO": ("adafruit/Adafruit_BusIO", "{version}"),
    "ArduinoJson": ("bblanchon/ArduinoJson", "v{version}"),
    "DallasTemperature": ("milesburton/Arduino-Temperature-Control-Library", "{version}"),
    "InternalTemperature": ("LAtimes2/InternalTemperature", "{version}"),
    "OneWire": ("PaulStoffregen/OneWire", "v{version}"),
    "PubSubClient": ("knolleary/pubsubclient", "v{version}"),
    "SdFat": ("greiman/SdFat", "{version}"),
    "TeensyID": ("sstaub/TeensyID", "{version}"),
    "Teensy_ADC": ("pedvide/ADC", "{version}"),
}

ENV_HEADER = re.compile(r"^\[env:([^\]\s]+)\]\s*$", re.MULTILINE)
ROW_KINDS = frozenset({"Library", "Platform", "Tool"})


@dataclass(frozen=True)
class OutdatedPackage:
    name: str
    current: str
    latest: str
    kind: str

    @property
    def skipped(self) -> bool:
        return self.name in SKIP_AUTO_UPDATE

    @property
    def has_update(self) -> bool:
        return self.current != self.latest


def pio_environments(ini_text: str) -> list[str]:
    return ENV_HEADER.findall(ini_text)


def parse_outdated_row(line: str) -> OutdatedPackage | None:
    parts = line.rsplit(None, 5)
    if len(parts) != 6:
        return None
    name, current, _wanted, latest, kind, _environments = parts
    if kind not in ROW_KINDS:
        return None
    return OutdatedPackage(name=name, current=current, latest=latest, kind=kind)


def parse_outdated(text: str) -> list[OutdatedPackage]:
    found: dict[str, OutdatedPackage] = {}
    for raw in text.splitlines():
        item = parse_outdated_row(raw.strip())
        if item is None or not item.has_update or item.name in found:
            continue
        found[item.name] = item
    return list(found.values())


def run_pio_outdated(root: Path, environments: list[str] | None = None) -> str:
    if environments is None:
        environments = pio_environments((root / "platformio.ini").read_text(encoding="utf-8"))
    if not environments:
        raise ValueError("platformio.ini has no [env:...] sections")
    chunks = []
    for environment in environments:
        chunks.append(
            subprocess.check_output(
                ["pio", "pkg", "outdated", "-e", environment],
                cwd=root,
                text=True,
                stderr=subprocess.STDOUT,
            )
        )
    return "\n".join(chunks)


def apply_ini_pins(text: str, updates: list[OutdatedPackage]) -> str:
    rewritten = text
    for item in updates:
        if item.skipped:
            continue
        patterns = (
            f"{item.name}@{item.current}",
            f"{item.name} @ {item.current}",
            f"{item.name} @{item.current}",
        )
        if not any(pattern in rewritten for pattern in patterns):
            raise ValueError(f"platformio.ini has no pin for {item.name}@{item.current}")
        for pattern in patterns:
            rewritten = rewritten.replace(pattern, pattern.replace(item.current, item.latest, 1))
    return rewritten


def github_tag_candidates(template: str, version: str) -> list[str]:
    primary = template.format(version=version)
    candidates = [primary]
    alternate = primary[1:] if primary.startswith("v") and len(primary) > 1 else f"v{primary}"
    if alternate and alternate not in candidates:
        candidates.append(alternate)
    return candidates


def github_tag_commit(repository: str, tag: str, token: str | None) -> str:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "teg-pio-package-updates",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    payload = _github_json(f"https://api.github.com/repos/{repository}/git/ref/tags/{tag}", headers)
    obj = payload["object"]
    sha = obj["sha"]
    if obj.get("type") == "tag":
        annotated = _github_json(obj["url"], headers)
        sha = annotated["object"]["sha"]
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError(f"unexpected git object for {repository}@{tag}: {sha}")
    return sha


def resolve_github_tag_commit(
    repository: str,
    template: str,
    version: str,
    token: str | None,
) -> str:
    errors: list[str] = []
    for tag in github_tag_candidates(template, version):
        try:
            return github_tag_commit(repository, tag, token)
        except ValueError as error:
            errors.append(str(error))
    raise ValueError(f"GitHub tag lookup failed for {repository}@{version}: {'; '.join(errors)}")


def _github_json(url: str, headers: dict[str, str]) -> dict:
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        raise ValueError(f"GitHub lookup failed for {url}: HTTP {error.code}") from error


def apply_osv_lock(text: str, item: OutdatedPackage, commit: str) -> str:
    mapping = GITHUB_LOCKS.get(item.name)
    if mapping is None:
        return text
    repository = f"github.com/{mapping[0]}"
    data = json.loads(text)
    updated = False
    for entry in data["results"][0]["packages"]:
        if entry["package"]["name"] == repository:
            entry["package"]["commit"] = commit
            updated = True
    if not updated:
        raise ValueError(f"OSV lock has no entry for {repository}")
    return json.dumps(data, indent=4) + "\n"


def slug_for(updates: list[OutdatedPackage]) -> str:
    return UPDATE_BRANCH if any(not item.skipped for item in updates) else ""


def apply_updates(
    root: Path,
    updates: list[OutdatedPackage],
    *,
    token: str | None = None,
    resolve_git: bool = True,
) -> list[dict]:
    applicable = [item for item in updates if not item.skipped]
    if not applicable:
        return []

    ini_path = root / "platformio.ini"
    ini_path.write_text(apply_ini_pins(ini_path.read_text(encoding="utf-8"), applicable), encoding="utf-8")

    osv_path = root / "scripts" / "osv-dependencies.json"
    osv_text = osv_path.read_text(encoding="utf-8")
    applied: list[dict] = []
    for item in applicable:
        record = asdict(item)
        mapping = GITHUB_LOCKS.get(item.name)
        if resolve_git and mapping is not None:
            record["commit"] = resolve_github_tag_commit(mapping[0], mapping[1], item.latest, token)
            osv_text = apply_osv_lock(osv_text, item, record["commit"])
        applied.append(record)
    osv_path.write_text(osv_text, encoding="utf-8")
    return applied


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--outdated-file", type=Path, help="Use a saved `pio pkg outdated` transcript")
    parser.add_argument("--apply", action="store_true", help="Rewrite pins for non-skipped updates")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    ini_text = (args.project_root / "platformio.ini").read_text(encoding="utf-8")
    environments = pio_environments(ini_text)
    text = (
        args.outdated_file.read_text(encoding="utf-8")
        if args.outdated_file
        else run_pio_outdated(args.project_root, environments)
    )
    updates = parse_outdated(text)
    applied: list[dict] = []
    if args.apply:
        applied = apply_updates(args.project_root, updates, token=os.environ.get("GITHUB_TOKEN"))
    report = {
        "outdated": [asdict(item) | {"skipped": item.skipped} for item in updates],
        "applied": applied,
        "slug": slug_for(updates),
        "environments": environments,
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
