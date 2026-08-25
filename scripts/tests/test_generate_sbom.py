import json
import tempfile
import unittest
from pathlib import Path

from scripts.generate_sbom import build_sbom


ROOT = Path(__file__).resolve().parents[2]


class SbomTests(unittest.TestCase):
    def test_is_deterministic_and_contains_pinned_inputs(self):
        first = build_sbom(ROOT, "0123456789abcdef")
        second = build_sbom(ROOT, "0123456789abcdef")
        self.assertEqual(first, second)
        self.assertEqual(first["specVersion"], "1.6")
        names = {item["name"] for item in first["components"]}
        self.assertTrue({"aWOT", "eFlexPwm", "QNEthernet", "MTP_Teensy", "mtp_wdog", "miniz"} <= names)
        mtp = next(item for item in first["components"] if item["name"] == "MTP_Teensy")
        self.assertEqual(mtp["properties"][0]["value"], "patched-core-mtp")
        self.assertEqual(len(mtp["version"]), 40)
        self.assertTrue({"tool-teensy", "tool-scons", "native", "OSV-Scanner", "platformio", "gcovr"} <= names)
        python_tools = [item for item in first["components"] if
                        item["properties"][0]["value"] == "ci-python-tool"]
        expected = {}
        for raw in (ROOT / "requirements-ci.txt").read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "==" in line:
                name, version = line.split("==", 1)
                expected[name.strip()] = version.strip()
        self.assertEqual({item["name"]: item["version"] for item in python_tools}, expected)
        submodules = [item for item in first["components"] if
                      item["properties"][0]["value"] == "git-submodule"]
        self.assertEqual(len(submodules), 2)
        self.assertTrue(all(len(item["version"]) == 40 for item in submodules))
        self.assertTrue(all(item["externalReferences"][0]["url"].startswith("ssh://")
                            for item in submodules))
        source_locks = [item for item in first["components"] if
                        item["properties"][0]["value"] == "upstream-source-lock"]
        self.assertGreaterEqual(len(source_locks), 18)
        self.assertTrue(all(len(item["version"]) == 40 for item in source_locks))

    def test_serial_changes_with_commit(self):
        one = build_sbom(ROOT, "a" * 40)
        two = build_sbom(ROOT, "b" * 40)
        self.assertNotEqual(one["serialNumber"], two["serialNumber"])


if __name__ == "__main__":
    unittest.main()
