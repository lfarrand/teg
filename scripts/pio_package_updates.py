#!/usr/bin/env python3
"""Apply PlatformIO registry updates that Dependabot cannot see.

Dependabot has no platformio.ini ecosystem. This script reads `pio pkg outdated`,
rewrites exact pins, and refreshes the matching OSV source-lock commits. A weekly
workflow opens one pull request when anything allowed has a newer registry version.

The Teensy platform, framework, toolchain and tool-teensy pins stay skipped.
teensy@5.2.0 is already selected with 1.159.0 overrides; its defaults pull
Teensyduino 1.62 / GCC 15, which collide with lib/MTP_Teensy and reject imxrt.h.
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

GITHUB_LOCKS = {
    "QNEthernet": ("ssilverman/QNEthernet", "v{version}"),
    "Adafruit GFX Library": ("adafruit/Adafruit-GFX-Library", "{version}"),
    "Adafruit SSD1306": ("adafruit/Adafruit_SSD1306", "{version}"),
    "Adafruit BusIO": ("adafruit/Adafruit_BusIO", "{version}"),
    "ArduinoJson": ("bblanchon/ArduinoJson", "{version}"),
    "DallasTemperature": ("milesburton/Arduino-Temperature-Control-Library", "{version}"),
    "InternalTemperature": ("LAtimes2/InternalTemperature", "{version}"),
    "OneWire": ("PaulStoffregen/OneWire", "v{version}"),
    "PubSubClient": ("knolleary/pubsubclient", "v{version}"),
    "SdFat": ("greiman/SdFat", "{version}"),
    "TeensyID": ("sstaub/TeensyID", "{version}"),
    "Teensy_ADC": ("pedvide/ADC", "{version}"),
}

OUTDATED_ROW = re.compile(
    r"^(?P<name>\S+)\s+(?P<current>\S+)\s+(?P<wanted>\S+)\s+(?P<latest>\S+)\s+"
    r"(?P<kind>Library|Platform|Tool)\s+(?P<environments>\S+)\s*$"
)


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


def parse_outdated(text: str) -> list[OutdatedPackage]:
    if "Everything is up-to-date!" in text:
        return []
    found: list[OutdatedPackage] = []
    for line in text.splitlines():
        match = OUTDATED_ROW.match(line.strip())
        if not match:
            continue
        item = OutdatedPackage(
            name=match.group("name"),
            current=match.group("current"),
            latest=match.group("latest"),
            kind=match.group("kind"),
        )
        if item.has_update:
            found.append(item)
    return found


def run_pio_outdated(root: Path, environment: str = "teensy41") -> str:
    return subprocess.check_output(
        ["pio", "pkg", "outdated", "-e", environment],
        cwd=root,
        text=True,
        stderr=subprocess.STDOUT,
    )


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
    parts = [f"{item.name.lower().replace(' ', '-')}-{item.latest}" for item in updates if not item.skipped]
    return "deps/platformio/" + "-".join(parts) if parts else ""


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
            tag = mapping[1].format(version=item.latest)
            record["commit"] = github_tag_commit(mapping[0], tag, token)
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

    text = args.outdated_file.read_text(encoding="utf-8") if args.outdated_file else run_pio_outdated(args.project_root)
    updates = parse_outdated(text)
    applied: list[dict] = []
    if args.apply:
        applied = apply_updates(args.project_root, updates, token=os.environ.get("GITHUB_TOKEN"))
    report = {
        "outdated": [asdict(item) | {"skipped": item.skipped} for item in updates],
        "applied": applied,
        "slug": slug_for(updates),
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
