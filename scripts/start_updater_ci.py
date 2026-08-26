#!/usr/bin/env python3
"""Start required CI for a PlatformIO updater pull request.

`main` branch protection requires the `ci.yml` job names on the commit
GitHub evaluates for the pull request. A `GITHUB_TOKEN` push can open the
updater PR, but the resulting `pull_request` workflow is parked for
write-access approval and reports on the test-merge commit. A
`workflow_dispatch` on the updater branch reports the same job names on
the head SHA; required checks use that SHA when the merge commit has no
usable status.

When `PIO_UPDATES_TOKEN` (a PAT or GitHub App installation token) opens
the PR, a normal `pull_request` event already starts CI on the merge
commit and this script does not dispatch. Otherwise it dispatches
`ci.yml` and waits until a run exists for the head SHA.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any


CI_WORKFLOW = "ci.yml"
CI_NOTE_START = "<!-- pio-updates-ci -->"
CI_NOTE_END = "<!-- /pio-updates-ci -->"
SAFE_REF = re.compile(r"^[\w./-]+$")
SHA1 = re.compile(r"^[0-9a-f]{40}$")
TRUE_VALUES = frozenset({"1", "true", "yes", "on"})

PAT_NOTE = (
    "Opened with `PIO_UPDATES_TOKEN`, so the normal `pull_request` CI event "
    "should populate the required checks on the test-merge commit."
)

DISPATCH_NOTE = (
    "Dispatched `CI` via `workflow_dispatch` on the updater head SHA so the "
    "required job names exist without waiting for the parked `GITHUB_TOKEN` "
    "`pull_request` approval. Run: {url}"
)


class UpdaterCIError(RuntimeError):
    pass


def env_flag(value: str | None) -> bool:
    return (value or "").strip().lower() in TRUE_VALUES


def should_dispatch(*, use_pat: bool) -> bool:
    return not use_pat


def require_ref(ref: str) -> str:
    if not SAFE_REF.fullmatch(ref):
        raise UpdaterCIError(f"unsafe git ref: {ref!r}")
    return ref


def require_sha(sha: str) -> str:
    if not SHA1.fullmatch(sha):
        raise UpdaterCIError(f"expected a 40-character SHA, got {sha!r}")
    return sha


def dispatch_args(workflow: str, ref: str) -> list[str]:
    return ["workflow", "run", workflow, "--ref", require_ref(ref)]


def list_runs_args(workflow: str, ref: str, sha: str) -> list[str]:
    return [
        "run",
        "list",
        "--workflow",
        workflow,
        "--branch",
        require_ref(ref),
        "--commit",
        require_sha(sha),
        "--json",
        "databaseId,status,url,headSha,event,createdAt",
        "--limit",
        "10",
    ]


def pr_list_args(head: str) -> list[str]:
    return ["pr", "list", "--head", require_ref(head), "--state", "open", "--json", "number,body"]


def pr_edit_args(number: int, body_file: Path) -> list[str]:
    if number <= 0:
        raise UpdaterCIError(f"invalid pull request number: {number}")
    return ["pr", "edit", str(number), "--body-file", str(body_file)]


def matching_run(runs: list[dict[str, Any]], sha: str, event: str | None = None) -> dict[str, Any] | None:
    require_sha(sha)
    matches = [
        run
        for run in runs
        if run.get("headSha") == sha and (event is None or run.get("event") == event)
    ]
    if not matches:
        return None
    return max(matches, key=lambda run: str(run.get("createdAt") or ""))


def body_with_ci_note(body: str, note: str) -> str:
    block = f"{CI_NOTE_START}\n{note}\n{CI_NOTE_END}"
    if CI_NOTE_START in body:
        return re.sub(
            rf"{re.escape(CI_NOTE_START)}.*?{re.escape(CI_NOTE_END)}",
            block,
            body,
            count=1,
            flags=re.DOTALL,
        )
    stripped = body.rstrip()
    if stripped:
        return stripped + "\n\n" + block + "\n"
    return block + "\n"


def wait_for_run(
    run_gh: Callable[[list[str]], str],
    *,
    workflow: str,
    ref: str,
    sha: str,
    event: str,
    attempts: int,
    delay_s: float,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    last: list[dict[str, Any]] = []
    for attempt in range(attempts):
        last = json.loads(run_gh(list_runs_args(workflow, ref, sha)))
        match = matching_run(last, sha, event)
        if match is not None:
            return match
        if attempt + 1 < attempts:
            sleep(delay_s)
    raise UpdaterCIError(
        f"no {event} run for {sha} on {ref} after {attempts} polls ({len(last)} listed)"
    )


def write_text_utf8(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def _write_pr_note(
    run_gh: Callable[[list[str]], str],
    *,
    head: str,
    note: str,
    write_text: Callable[[Path, str], None],
) -> int:
    prs = json.loads(run_gh(pr_list_args(head)))
    if not prs:
        raise UpdaterCIError(f"no open pull request for {head}")
    number = int(prs[0]["number"])
    body = body_with_ci_note(prs[0].get("body") or "", note)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".md", delete=False) as handle:
        path = Path(handle.name)
    try:
        write_text(path, body)
        run_gh(pr_edit_args(number, path))
    finally:
        path.unlink(missing_ok=True)
    return number


def start_updater_ci(
    run_gh: Callable[[list[str]], str],
    *,
    workflow: str,
    ref: str,
    sha: str,
    use_pat: bool,
    attempts: int = 18,
    delay_s: float = 5.0,
    sleep: Callable[[float], None] = time.sleep,
    write_text: Callable[[Path, str], None] = write_text_utf8,
) -> dict[str, Any]:
    require_ref(ref)
    require_sha(sha)
    if not should_dispatch(use_pat=use_pat):
        number = _write_pr_note(run_gh, head=ref, note=PAT_NOTE, write_text=write_text)
        return {"dispatched": False, "pr": number, "note": PAT_NOTE}
    run_gh(dispatch_args(workflow, ref))
    run = wait_for_run(
        run_gh,
        workflow=workflow,
        ref=ref,
        sha=sha,
        event="workflow_dispatch",
        attempts=attempts,
        delay_s=delay_s,
        sleep=sleep,
    )
    url = str(run.get("url") or "")
    if not url:
        raise UpdaterCIError("dispatched CI run has no URL")
    number = _write_pr_note(
        run_gh,
        head=ref,
        note=DISPATCH_NOTE.format(url=url),
        write_text=write_text,
    )
    return {"dispatched": True, "pr": number, "run": run, "note": DISPATCH_NOTE.format(url=url)}


def run_gh(args: list[str]) -> str:
    completed = subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workflow", default=CI_WORKFLOW)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--attempts", type=int, default=18)
    parser.add_argument("--delay-seconds", type=float, default=5.0)
    args = parser.parse_args()
    try:
        result = start_updater_ci(
            run_gh,
            workflow=args.workflow,
            ref=args.ref,
            sha=args.sha,
            use_pat=env_flag(os.environ.get("PIO_UPDATES_USE_PAT")),
            attempts=args.attempts,
            delay_s=args.delay_seconds,
        )
    except UpdaterCIError as error:
        print(error, file=sys.stderr)
        return 1
    sys.stdout.write(json.dumps(result, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
