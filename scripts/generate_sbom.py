#!/usr/bin/env python3
"""Generate a deterministic CycloneDX inventory from the pinned build inputs."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import re
import subprocess
import uuid
from pathlib import Path
from urllib.parse import quote


SBOM_NAMESPACE = uuid.UUID("02df3247-45ab-5cf8-bfee-640c486555d1")


def _lines(value: str) -> list[str]:
    return [line.strip() for line in value.splitlines() if line.strip() and not line.strip().startswith(";")]


def _component(name: str, version: str | None, source: str, *, purl: str | None = None,
               component_type: str = "library", external_url: str | None = None) -> dict:
    ref = f"teg:{source}:{name}:{version or 'bundled'}"
    item: dict = {
        "type": component_type,
        "bom-ref": ref,
        "name": name,
        "properties": [{"name": "teg:source", "value": source}],
    }
    if version:
        item["version"] = version
    if purl:
        item["purl"] = purl
    if external_url:
        item["externalReferences"] = [{"type": "vcs", "url": external_url}]
    return item


def _pio_purl(spec: str) -> str | None:
    match = re.fullmatch(r"([^/]+)/([^@]+)@(.+)", spec)
    if not match:
        return None
    namespace, name, version = match.groups()
    return f"pkg:platformio/{quote(namespace, safe='')}/{quote(name, safe='')}@{quote(version, safe='.+-')}"


def _library_property(path: Path, key: str) -> str | None:
    if not path.exists():
        return None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(key + "="):
            return line.split("=", 1)[1].strip()
    return None


def _vcs_url(url: str) -> str:
    match = re.fullmatch(r"([^@\s]+@[^:\s]+):(.+)", url)
    return f"ssh://{match.group(1)}/{match.group(2)}" if match else url


def _git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()


def build_sbom(root: Path, commit: str | None = None) -> dict:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(root / "platformio.ini", encoding="utf-8")
    components: list[dict] = []

    for section in (name for name in config.sections() if name.startswith("env:")):
        env = config[section]
        platform = env.get("platform", "").strip()
        if "@" in platform:
            name, version = platform.rsplit("@", 1)
            components.append(_component(name, version, "platformio-platform",
                                         purl=f"pkg:platformio/platform/{quote(name)}@{quote(version)}",
                                         component_type="framework"))

        framework = env.get("framework", "").strip()
        if framework:
            components.append(_component(framework, None, "platformio-framework",
                                         component_type="framework"))

        for spec in _lines(env.get("platform_packages", "")):
            match = re.fullmatch(r"([^@\s]+)\s*@\s*(.+)", spec)
            if match:
                name, version = match.groups()
                components.append(_component(name, version, "platformio-package",
                                             purl=f"pkg:platformio/tool/{quote(name)}@{quote(version)}",
                                             component_type="framework"))

        for spec in _lines(env.get("lib_deps", "")):
            if spec.startswith("${"):
                continue
            match = re.fullmatch(r"([^/]+)/([^@]+)@(.+)", spec)
            if not match:
                continue
            namespace, name, version = match.groups()
            components.append(_component(name, version, "platformio-registry", purl=_pio_purl(spec),
                                         external_url=f"https://registry.platformio.org/libraries/{namespace}/{quote(name)}"))

    for spec in _lines(config["teg:sbom"].get("framework_libraries", "")):
        name, version = spec.rsplit("@", 1)
        components.append(_component(name, version, "teensy-framework-library"))

    for spec in _lines(config["teg:sbom"].get("build_tools", "")):
        name, version = spec.rsplit("@", 1)
        components.append(_component(name, version, "ci-build-tool", component_type="application"))

    requirements = root / "requirements-ci.txt"
    for raw in requirements.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "==" not in line:
            continue
        name, version = line.split("==", 1)
        components.append(_component(name.strip(), version.strip(), "ci-python-tool",
                                     component_type="application"))

    source_lock = root / "scripts" / "osv-dependencies.json"
    source_packages = json.loads(source_lock.read_text(encoding="utf-8"))["results"][0]["packages"]
    for entry in source_packages:
        package = entry["package"]
        repository = package["name"]
        revision = package["commit"]
        if not repository.startswith("github.com/") or not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise ValueError(f"invalid OSV source lock: {repository}@{revision}")
        components.append(_component(repository.rsplit("/", 1)[-1], revision,
                                     "upstream-source-lock",
                                     external_url=f"https://{repository}.git"))

    gitmodules = configparser.ConfigParser(interpolation=None)
    gitmodules.read(root / ".gitmodules", encoding="utf-8")
    for section in gitmodules.sections():
        path = gitmodules[section]["path"]
        url = gitmodules[section]["url"]
        tree = _git(root, "ls-tree", "HEAD", path).split()
        if len(tree) < 3 or tree[1] != "commit":
            raise ValueError(f"{path} is not a gitlink in HEAD")
        revision = tree[2]
        name = _library_property(root / path / "library.properties", "name") or Path(path).name
        components.append(_component(name, revision, "git-submodule", external_url=_vcs_url(url)))

    for path, fallback_name, fallback_version in (
        (root / "lib" / "MTP_Teensy" / "library.properties", "MTP_Teensy", "1.0.0"),
        (root / "lib" / "miniz" / "miniz.h", "miniz", "3.0.2"),
    ):
        name = _library_property(path, "name") if path.name == "library.properties" else fallback_name
        version = _library_property(path, "version") if path.name == "library.properties" else fallback_version
        components.append(_component(name or fallback_name, version or fallback_version, "vendored"))

    by_ref = {item["bom-ref"]: item for item in components}
    components = sorted(by_ref.values(), key=lambda item: item["bom-ref"])
    revision = commit or _git(root, "rev-parse", "HEAD")
    root_ref = f"pkg:github/lfarrand/teg@{revision}"
    fingerprint = hashlib.sha256(
        json.dumps(components, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    serial = uuid.uuid5(SBOM_NAMESPACE, revision + ":" + fingerprint)
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{serial}",
        "version": 1,
        "metadata": {
            "component": {
                "type": "firmware",
                "bom-ref": root_ref,
                "name": "teg-firmware",
                "version": revision,
                "purl": root_ref,
            },
            "properties": [
                {"name": "teg:generator", "value": "scripts/generate_sbom.py"},
                {"name": "teg:reproducible", "value": "true"},
            ],
        },
        "components": components,
        "dependencies": [{"ref": root_ref, "dependsOn": [item["bom-ref"] for item in components]}],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--commit")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bom = build_sbom(args.project_root.resolve(), args.commit)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(bom, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
