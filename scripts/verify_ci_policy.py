#!/usr/bin/env python3
"""Fail when a workflow weakens the repository's basic Actions policy."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


USES = re.compile(r"^\s*-?\s*uses:\s*([^\s#]+)", re.MULTILINE)
PINNED_REMOTE = re.compile(r"^[^/\s]+/[^@\s]+@[0-9a-fA-F]{40}$")
JOB = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$", re.MULTILINE)
STEP = re.compile(r"^      - (?:name|uses):.*?(?=^      - (?:name|uses):|\Z)", re.MULTILINE | re.DOTALL)


def workflow_errors(path: Path, text: str) -> list[str]:
    errors: list[str] = []
    if re.search(r"^pull_request_target\s*:", text, re.MULTILINE):
        errors.append("pull_request_target is forbidden")
    if not re.search(r"^permissions:\s*\n\s+contents:\s+read\s*$", text, re.MULTILINE):
        errors.append("top-level permissions must be contents: read")
    for use in USES.findall(text):
        if use.startswith("./") or use.startswith("docker://"):
            continue
        if not PINNED_REMOTE.fullmatch(use):
            errors.append(f"action is not pinned to a full commit SHA: {use}")
    for block in STEP.findall(text):
        if "uses: actions/upload-artifact@" not in block or ".pio/" not in block:
            continue
        if not re.search(r"^\s+include-hidden-files:\s*true\s*$", block, re.MULTILINE):
            errors.append("upload-artifact must opt in to hidden .pio paths")
    jobs_marker = re.search(r"^jobs:\s*$", text, re.MULTILINE)
    if jobs_marker is None:
        return [f"{path}: workflow has no jobs section", *[f"{path}: {error}" for error in errors]]
    jobs_text = text[jobs_marker.end():]
    for job in JOB.findall(jobs_text):
        block_start = re.search(rf"^  {re.escape(job)}:\s*$", jobs_text, re.MULTILINE)
        assert block_start is not None
        next_job = re.search(r"^  [A-Za-z0-9_-]+:\s*$", jobs_text[block_start.end():], re.MULTILINE)
        end = block_start.end() + next_job.start() if next_job else len(jobs_text)
        if not re.search(r"^    timeout-minutes:\s*\d+\s*$", jobs_text[block_start.end():end], re.MULTILINE):
            errors.append(f"job {job} has no timeout-minutes")
    return [f"{path}: {error}" for error in errors]


def verify_directory(workflows: Path) -> list[str]:
    errors: list[str] = []
    for path in sorted((*workflows.glob("*.yml"), *workflows.glob("*.yaml"))):
        errors.extend(workflow_errors(path, path.read_text(encoding="utf-8")))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workflows", nargs="?", type=Path, default=Path(".github/workflows"))
    args = parser.parse_args()
    errors = verify_directory(args.workflows)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
