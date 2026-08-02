#!/usr/bin/env python3
"""Validate that an OSV report scanned the intended dependencies and matcher."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


COMMIT = re.compile(r"^[0-9a-f]{40}$")
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")
EXTRACTION = re.compile(r"^Scanned (.+) file and found (\d+) packages?$")


def load_packages(path: Path) -> list[dict]:
    document = json.loads(path.read_text(encoding="utf-8"))
    results = document.get("results")
    if not isinstance(results, list):
        raise ValueError(f"{path}: results is absent or not a list")
    packages: list[dict] = []
    for result in results:
        if not isinstance(result, dict):
            raise ValueError(f"{path}: result is not an object")
        entries = result.get("packages") or []
        if not isinstance(entries, list):
            raise ValueError(f"{path}: packages is not a list")
        packages.extend(entry for entry in entries if isinstance(entry, dict))
    return packages


def package_key(entry: dict) -> tuple[str, str]:
    package = entry.get("package") or {}
    return str(package.get("name") or ""), str(package.get("commit") or "")


def vulnerability_count(packages: list[dict]) -> int:
    return sum(len(entry.get("vulnerabilities") or []) for entry in packages)


def required_lock_keys(path: Path) -> set[tuple[str, str]]:
    packages = load_packages(path)
    keys = {package_key(entry) for entry in packages}
    if not keys or any(not name or not COMMIT.fullmatch(commit) for name, commit in keys):
        raise ValueError(f"{path}: every source lock needs a name and 40-character commit")
    if len(keys) != len(packages):
        raise ValueError(f"{path}: source locks must be unique")
    return keys


def extraction_counts(path: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = ANSI_ESCAPE.sub("", raw_line).strip().replace("\\", "/")
        match = EXTRACTION.fullmatch(line)
        if match:
            counts[match.group(1)] = int(match.group(2))
    return counts


def validate_extraction_log(
    counts: dict[str, int], *, lock_name: str, lock_count: int, project_count: int
) -> list[str]:
    errors: list[str] = []
    lock_matches = [count for path, count in counts.items() if path.endswith("/" + lock_name)]
    project_matches = [count for path, count in counts.items() if path.endswith("/.git")]
    if lock_matches != [lock_count]:
        errors.append(
            f"OSV extracted {lock_matches or 'no'} package count for {lock_name}; "
            f"expected exactly [{lock_count}]"
        )
    if project_matches != [project_count]:
        errors.append(
            f"OSV extracted {project_matches or 'no'} project git package count; "
            f"expected exactly [{project_count}]"
        )
    return errors


def gitlink_commits(root: Path) -> set[str]:
    modules = root / ".gitmodules"
    if not modules.exists():
        return set()
    output = subprocess.check_output(
        ["git", "config", "-f", str(modules), "--get-regexp", r"^submodule\..*\.path$"],
        cwd=root,
        text=True,
    )
    commits: set[str] = set()
    for line in output.splitlines():
        _, path = line.split(maxsplit=1)
        fields = subprocess.check_output(
            ["git", "ls-tree", "HEAD", path], cwd=root, text=True
        ).split()
        if len(fields) < 3 or fields[1] != "commit" or not COMMIT.fullmatch(fields[2]):
            raise ValueError(f"{path}: HEAD does not contain a valid gitlink")
        commits.add(fields[2])
    return commits


def validate(
    packages: list[dict],
    *,
    required_keys: set[tuple[str, str]] = frozenset(),
    required_commits: set[str] = frozenset(),
    require_vulnerabilities: bool = False,
    forbid_vulnerabilities: bool = False,
) -> list[str]:
    errors: list[str] = []
    actual_keys = {package_key(entry) for entry in packages}
    actual_commits = {commit for _, commit in actual_keys if COMMIT.fullmatch(commit)}
    missing_keys = sorted(required_keys - actual_keys)
    missing_commits = sorted(required_commits - actual_commits)
    if missing_keys:
        errors.append("OSV report omitted source locks: " + ", ".join(name for name, _ in missing_keys))
    if missing_commits:
        errors.append("OSV report omitted gitlink commits: " + ", ".join(missing_commits))
    count = vulnerability_count(packages)
    if require_vulnerabilities and count == 0:
        errors.append("known-vulnerable sentinel produced no vulnerability matches")
    if forbid_vulnerabilities and count != 0:
        errors.append(f"OSV reported {count} known vulnerability match(es)")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--required-lock", type=Path)
    parser.add_argument("--require-project-gitlinks", type=Path)
    parser.add_argument("--scan-log", type=Path)
    parser.add_argument("--require-vulnerabilities", action="store_true")
    parser.add_argument("--forbid-vulnerabilities", action="store_true")
    args = parser.parse_args()
    if args.require_vulnerabilities and args.forbid_vulnerabilities:
        parser.error("vulnerabilities cannot be both required and forbidden")

    try:
        packages = load_packages(args.report)
        required_keys = required_lock_keys(args.required_lock) if args.required_lock else set()
        required_commits = (
            gitlink_commits(args.require_project_gitlinks.resolve())
            if args.require_project_gitlinks
            else set()
        )
        # Verify both identities in the combined report and extraction counts
        # in the pinned scanner log. The latter prevents an unexpectedly empty
        # scan from looking clean merely because no vulnerabilities were found.
        errors = validate(
            packages,
            required_keys=required_keys,
            required_commits=required_commits,
            require_vulnerabilities=args.require_vulnerabilities,
            forbid_vulnerabilities=args.forbid_vulnerabilities,
        )
        if required_keys or required_commits:
            if not args.scan_log or not args.required_lock or not args.require_project_gitlinks:
                raise ValueError(
                    "source coverage verification needs --scan-log, --required-lock, "
                    "and --require-project-gitlinks"
                )
            errors.extend(validate_extraction_log(
                extraction_counts(args.scan_log),
                lock_name=args.required_lock.name,
                lock_count=len(required_keys),
                project_count=1 + len(required_commits),
            ))
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"OSV report verification failed: {exc}", file=sys.stderr)
        return 2

    print(
        f"OSV report contains {len(packages)} package records and "
        f"{vulnerability_count(packages)} vulnerability matches"
    )
    for error in errors:
        print(f"OSV report verification failed: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
