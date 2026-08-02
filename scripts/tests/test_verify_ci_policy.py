import tempfile
import unittest
from pathlib import Path

from scripts.verify_ci_policy import workflow_errors


GOOD = """name: Test
on: [push]
permissions:
  contents: read
jobs:
  test:
    timeout-minutes: 10
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567
"""


class WorkflowPolicyTests(unittest.TestCase):
    def test_accepts_least_privilege_and_sha_pin(self):
        self.assertEqual(workflow_errors(Path("ci.yml"), GOOD), [])

    def test_rejects_tag_permissions_and_missing_timeout(self):
        bad = GOOD.replace("contents: read", "contents: write").replace(
            "0123456789abcdef0123456789abcdef01234567", "v5"
        ).replace("    timeout-minutes: 10\n", "")
        errors = workflow_errors(Path("ci.yml"), bad)
        self.assertEqual(len(errors), 3)

    def test_rejects_pull_request_target(self):
        errors = workflow_errors(Path("ci.yml"), GOOD.replace("on: [push]", "pull_request_target:"))
        self.assertTrue(any("pull_request_target" in error for error in errors))

    def test_hidden_artifact_path_requires_explicit_opt_in(self):
        upload = GOOD.replace(
            "actions/checkout@0123456789abcdef0123456789abcdef01234567",
            "actions/upload-artifact@0123456789abcdef0123456789abcdef01234567\n"
            "        with:\n"
            "          path: .pio/report.json",
        )
        errors = workflow_errors(Path("ci.yml"), upload)
        self.assertTrue(any("hidden .pio paths" in error for error in errors))
        allowed = upload.replace(
            "          path: .pio/report.json",
            "          path: .pio/report.json\n          include-hidden-files: true",
        )
        self.assertEqual(workflow_errors(Path("ci.yml"), allowed), [])


if __name__ == "__main__":
    unittest.main()
