import json
import unittest
from pathlib import Path

from scripts.start_updater_ci import (
    CI_NOTE_END,
    CI_NOTE_START,
    DISPATCH_NOTE,
    PAT_NOTE,
    UpdaterCIError,
    body_with_ci_note,
    dispatch_args,
    env_flag,
    list_runs_args,
    matching_run,
    pr_edit_args,
    pr_list_args,
    require_ref,
    require_sha,
    should_dispatch,
    start_updater_ci,
    wait_for_run,
)


SHA = "0123456789abcdef0123456789abcdef01234567"
REF = "deps/platformio-updates"


class FakeGh:
    def __init__(self) -> None:
        self.calls: list[list[str]] = []
        self.pr_body = "Weekly PlatformIO registry check."
        self.list_replies: list[str] = []
        self.written: list[str] = []

    def __call__(self, args: list[str]) -> str:
        self.calls.append(args)
        if args[:2] == ["workflow", "run"]:
            return ""
        if args[:2] == ["run", "list"]:
            if not self.list_replies:
                return "[]"
            return self.list_replies.pop(0)
        if args[:2] == ["pr", "list"]:
            return json.dumps([{"number": 7, "body": self.pr_body}])
        if args[:2] == ["pr", "edit"]:
            return ""
        raise AssertionError(args)

    def write_text(self, path: Path, text: str, encoding: str = "utf-8") -> None:
        del encoding
        self.written.append(text)
        path.write_text(text, encoding="utf-8")


def queued_run() -> dict:
    return {
        "databaseId": 99,
        "status": "queued",
        "url": "https://github.com/lfarrand/teg/actions/runs/99",
        "headSha": SHA,
        "event": "workflow_dispatch",
        "createdAt": "2026-08-26T15:00:00Z",
    }


class StartUpdaterCITests(unittest.TestCase):
    def test_env_flag_and_dispatch_gate(self):
        self.assertTrue(env_flag("true"))
        self.assertTrue(env_flag("YES"))
        self.assertFalse(env_flag(""))
        self.assertFalse(env_flag("false"))
        self.assertTrue(should_dispatch(use_pat=False))
        self.assertFalse(should_dispatch(use_pat=True))

    def test_rejects_unsafe_ref_and_short_sha(self):
        with self.assertRaises(UpdaterCIError):
            require_ref("deps/platformio-updates; echo pwned")
        with self.assertRaises(UpdaterCIError):
            require_sha("abc")
        self.assertEqual(require_ref(REF), REF)
        self.assertEqual(require_sha(SHA), SHA)

    def test_gh_args(self):
        self.assertEqual(dispatch_args("ci.yml", REF), ["workflow", "run", "ci.yml", "--ref", REF])
        listed = list_runs_args("ci.yml", REF, SHA)
        self.assertEqual(listed[:6], ["run", "list", "--workflow", "ci.yml", "--branch", REF])
        self.assertIn(SHA, listed)
        self.assertEqual(pr_list_args(REF)[3], REF)
        self.assertEqual(pr_edit_args(7, Path("body.md"))[-1], "body.md")

    def test_matching_run_prefers_newest_same_event(self):
        older = queued_run()
        newer = queued_run() | {"createdAt": "2026-08-26T16:00:00Z", "databaseId": 100}
        pull_request = queued_run() | {"event": "pull_request", "databaseId": 101}
        self.assertEqual(matching_run([older, newer, pull_request], SHA, "workflow_dispatch"), newer)
        self.assertIsNone(matching_run([pull_request], SHA, "workflow_dispatch"))
        self.assertIsNone(matching_run([queued_run() | {"headSha": "f" * 40}], SHA))

    def test_body_note_insert_and_replace(self):
        first = body_with_ci_note("Weekly check.", "first")
        self.assertIn("Weekly check.", first)
        self.assertIn(CI_NOTE_START, first)
        self.assertIn("first", first)
        second = body_with_ci_note(first, "second")
        self.assertIn("second", second)
        self.assertNotIn("first", second)
        self.assertEqual(second.count(CI_NOTE_END), 1)
        self.assertEqual(body_with_ci_note("", "only"), f"{CI_NOTE_START}\nonly\n{CI_NOTE_END}\n")

    def test_wait_for_run_sees_run_on_later_poll(self):
        gh = FakeGh()
        gh.list_replies = ["[]", json.dumps([queued_run()])]
        slept: list[float] = []
        found = wait_for_run(
            gh,
            workflow="ci.yml",
            ref=REF,
            sha=SHA,
            event="workflow_dispatch",
            attempts=3,
            delay_s=5,
            sleep=slept.append,
        )
        self.assertEqual(found["databaseId"], 99)
        self.assertEqual(slept, [5])

    def test_wait_for_run_times_out(self):
        gh = FakeGh()
        with self.assertRaises(UpdaterCIError):
            wait_for_run(
                gh,
                workflow="ci.yml",
                ref=REF,
                sha=SHA,
                event="workflow_dispatch",
                attempts=2,
                delay_s=1,
                sleep=lambda _: None,
            )

    def test_pat_path_updates_pr_and_does_not_dispatch(self):
        gh = FakeGh()
        result = start_updater_ci(
            gh,
            workflow="ci.yml",
            ref=REF,
            sha=SHA,
            use_pat=True,
            write_text=gh.write_text,
        )
        self.assertEqual(result["dispatched"], False)
        self.assertEqual(result["pr"], 7)
        self.assertEqual([call[:2] for call in gh.calls], [["pr", "list"], ["pr", "edit"]])
        self.assertIn(PAT_NOTE, gh.written[0])

    def test_dispatch_path_waits_then_records_run_url(self):
        gh = FakeGh()
        gh.list_replies = [json.dumps([queued_run()])]
        result = start_updater_ci(
            gh,
            workflow="ci.yml",
            ref=REF,
            sha=SHA,
            use_pat=False,
            attempts=2,
            delay_s=0,
            sleep=lambda _: None,
            write_text=gh.write_text,
        )
        self.assertTrue(result["dispatched"])
        self.assertEqual(gh.calls[0], ["workflow", "run", "ci.yml", "--ref", REF])
        url = "https://github.com/lfarrand/teg/actions/runs/99"
        self.assertIn(DISPATCH_NOTE.format(url=url), gh.written[0])
        self.assertIn(url, result["note"])


if __name__ == "__main__":
    unittest.main()
