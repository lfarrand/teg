import json
import unittest

from scripts.pio_package_updates import (
    OutdatedPackage,
    apply_ini_pins,
    apply_osv_lock,
    parse_outdated,
    slug_for,
)


OUTDATED = """
Checking

Package     Current    Wanted    Latest    Type      Environments
----------  ---------  --------  --------  --------  --------------
teensy      5.0.0      5.0.0     5.2.0     Platform  teensy41
QNEthernet  0.36.0     0.36.0    0.37.0    Library   teensy41
framework-arduinoteensy  1.159.0  1.159.0  1.162.0  Tool  teensy41
"""


class PioPackageUpdateTests(unittest.TestCase):
    def test_parse_outdated_rows(self):
        items = parse_outdated(OUTDATED)
        self.assertEqual([item.name for item in items], ["teensy", "QNEthernet", "framework-arduinoteensy"])
        self.assertTrue(items[0].skipped)
        self.assertFalse(items[1].skipped)
        self.assertTrue(items[2].skipped)
        self.assertEqual(parse_outdated("Everything is up-to-date!\n"), [])

    def test_apply_keeps_owner_and_skips_framework(self):
        ini = (
            "platform = teensy@5.0.0\n"
            "platform_packages =\n"
            "    framework-arduinoteensy @ 1.159.0\n"
            "lib_deps =\n"
            "\tssilverman/QNEthernet@0.36.0\n"
        )
        items = parse_outdated(OUTDATED)
        rewritten = apply_ini_pins(ini, items)
        self.assertIn("platform = teensy@5.0.0", rewritten)
        self.assertIn("ssilverman/QNEthernet@0.37.0", rewritten)
        self.assertIn("framework-arduinoteensy @ 1.159.0", rewritten)

    def test_osv_lock_rewrites_matching_commit(self):
        lock = json.dumps({
            "results": [{"packages": [
                {"package": {"name": "github.com/ssilverman/QNEthernet", "commit": "a" * 40}},
                {"package": {"name": "github.com/other/lib", "commit": "b" * 40}},
            ]}],
        })
        item = OutdatedPackage("QNEthernet", "0.36.0", "0.37.0", "Library")
        rewritten = json.loads(apply_osv_lock(lock, item, "c" * 40))
        self.assertEqual(rewritten["results"][0]["packages"][0]["package"]["commit"], "c" * 40)
        self.assertEqual(rewritten["results"][0]["packages"][1]["package"]["commit"], "b" * 40)

    def test_slug_omits_skipped_packages(self):
        self.assertEqual(
            slug_for(parse_outdated(OUTDATED)),
            "deps/platformio/qnethernet-0.37.0",
        )

    def test_missing_ini_pin_is_an_error(self):
        with self.assertRaises(ValueError):
            apply_ini_pins("lib_deps =\n", [OutdatedPackage("QNEthernet", "0.36.0", "0.37.0", "Library")])


if __name__ == "__main__":
    unittest.main()
