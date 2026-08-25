import json
import tempfile
import unittest
from pathlib import Path

from scripts.verify_osv_report import (
    extraction_counts,
    gitlink_commits,
    load_packages,
    required_lock_keys,
    validate,
    validate_extraction_log,
    vulnerability_count,
)

ROOT = Path(__file__).resolve().parents[2]


class OsvReportTests(unittest.TestCase):
    def write_json(self, value):
        temporary = tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", delete=False)
        with temporary:
            json.dump(value, temporary)
        self.addCleanup(Path(temporary.name).unlink, missing_ok=True)
        return Path(temporary.name)

    @staticmethod
    def entry(name="github.com/example/library", commit="a" * 40, vulnerabilities=None):
        value = {"package": {"name": name, "commit": commit}}
        if vulnerabilities is not None:
            value["vulnerabilities"] = vulnerabilities
        return value

    def test_load_and_count(self):
        path = self.write_json({"results": [{"packages": [
            self.entry(vulnerabilities=[{"id": "OSV-1"}]), self.entry("other", "b" * 40)
        ]}]})
        packages = load_packages(path)
        self.assertEqual(len(packages), 2)
        self.assertEqual(vulnerability_count(packages), 1)

    def test_missing_results_is_rejected(self):
        path = self.write_json({"results": None})
        with self.assertRaises(ValueError):
            load_packages(path)

    def test_required_lock_needs_exact_commits(self):
        path = self.write_json({"results": [{"packages": [self.entry()]}]})
        self.assertEqual(required_lock_keys(path), {("github.com/example/library", "a" * 40)})
        bad = self.write_json({"results": [{"packages": [self.entry(commit="tag-1.0")]}]})
        with self.assertRaises(ValueError):
            required_lock_keys(bad)
        duplicate = self.write_json({"results": [{"packages": [self.entry(), self.entry()]}]})
        with self.assertRaises(ValueError):
            required_lock_keys(duplicate)

    def test_clean_report_coverage_comes_from_extraction_log(self):
        temporary = tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", delete=False)
        with temporary:
            temporary.write("Scanned /src/.git file and found 3 packages\n")
            temporary.write("Scanned /src/scripts/osv-dependencies.json file and found 19 packages\n")
        path = Path(temporary.name)
        self.addCleanup(path.unlink, missing_ok=True)
        counts = extraction_counts(path)
        self.assertEqual(validate_extraction_log(
            counts,
            lock_name="osv-dependencies.json",
            lock_count=19,
            project_count=3,
        ), [])
        self.assertEqual(len(validate_extraction_log(
            counts,
            lock_name="osv-dependencies.json",
            lock_count=18,
            project_count=3,
        )), 1)

    def test_repository_lock_covers_runtime_sources_and_gitlinks(self):
        keys = required_lock_keys(ROOT / "scripts" / "osv-dependencies.json")
        names = {name for name, _ in keys}
        self.assertGreaterEqual(len(keys), 18)
        self.assertTrue({
            "github.com/bblanchon/ArduinoJson",
            "github.com/ssilverman/QNEthernet",
            "github.com/PaulStoffregen/cores",
            "github.com/richgel999/miniz",
        } <= names)
        self.assertNotIn("github.com/KurtE/MTP_Teensy", names)
        commits = gitlink_commits(ROOT)
        self.assertEqual(len(commits), 2)
        self.assertTrue(all(len(commit) == 40 for commit in commits))

    def test_validation_requires_every_lock_and_gitlink(self):
        packages = [self.entry()]
        errors = validate(
            packages,
            required_keys={("github.com/example/library", "a" * 40), ("missing", "b" * 40)},
            required_commits={"a" * 40, "c" * 40},
        )
        self.assertEqual(len(errors), 2)
        self.assertIn("missing", errors[0])
        self.assertIn("c" * 40, errors[1])

    def test_sentinel_and_clean_scan_are_opposite_gates(self):
        vulnerable = [self.entry(vulnerabilities=[{"id": "GHSA-test"}])]
        clean = [self.entry()]
        self.assertEqual(validate(vulnerable, require_vulnerabilities=True), [])
        self.assertNotEqual(validate(clean, require_vulnerabilities=True), [])
        self.assertEqual(validate(clean, forbid_vulnerabilities=True), [])
        self.assertNotEqual(validate(vulnerable, forbid_vulnerabilities=True), [])


if __name__ == "__main__":
    unittest.main()
