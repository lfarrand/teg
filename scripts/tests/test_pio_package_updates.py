import json
import unittest

from scripts.pio_package_updates import (
    GITHUB_LOCKS,
    UPDATE_BRANCH,
    OutdatedPackage,
    apply_ini_pins,
    apply_osv_lock,
    github_tag_candidates,
    parse_outdated,
    pio_environments,
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

SPACED = """
Adafruit GFX Library  1.12.3  1.12.3  1.12.4  Library  teensy41
Adafruit BusIO        1.17.3  1.17.3  1.17.4  Library  teensy41
native                1.2.1   1.2.1   1.2.2   Platform native
"""


class PioPackageUpdateTests(unittest.TestCase):
    def test_parse_outdated_rows(self):
        items = parse_outdated(OUTDATED)
        self.assertEqual([item.name for item in items], ["teensy", "QNEthernet", "framework-arduinoteensy"])
        self.assertTrue(items[0].skipped)
        self.assertFalse(items[1].skipped)
        self.assertTrue(items[2].skipped)
        self.assertEqual(parse_outdated("Everything is up-to-date!\n"), [])

    def test_parse_names_with_spaces_from_the_right(self):
        items = parse_outdated(SPACED)
        self.assertEqual(
            [(item.name, item.current, item.latest, item.kind) for item in items],
            [
                ("Adafruit GFX Library", "1.12.3", "1.12.4", "Library"),
                ("Adafruit BusIO", "1.17.3", "1.17.4", "Library"),
                ("native", "1.2.1", "1.2.2", "Platform"),
            ],
        )

    def test_parse_dedupes_repeated_environment_rows(self):
        repeated = OUTDATED + "\nEverything is up-to-date!\n" + OUTDATED
        self.assertEqual([item.name for item in parse_outdated(repeated)], [
            "teensy",
            "QNEthernet",
            "framework-arduinoteensy",
        ])

    def test_lists_ini_environments(self):
        ini = (
            "[platformio]\ndefault_envs = teensy41\n"
            "[env:teensy41]\nplatform = teensy@5.2.0\n"
            "[env:native]\nplatform = native@1.2.1\n"
            "[env:native-sanitize]\nplatform = native@1.2.1\n"
        )
        self.assertEqual(pio_environments(ini), ["teensy41", "native", "native-sanitize"])

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

    def test_slug_is_stable_and_omits_skipped_only_sets(self):
        self.assertEqual(slug_for(parse_outdated(OUTDATED)), UPDATE_BRANCH)
        self.assertEqual(
            slug_for(parse_outdated(OUTDATED) + parse_outdated(SPACED)),
            UPDATE_BRANCH,
        )
        self.assertEqual(slug_for([OutdatedPackage("teensy", "5.0.0", "5.2.0", "Platform")]), "")

    def test_arduinojson_tag_uses_v_prefix(self):
        self.assertEqual(GITHUB_LOCKS["ArduinoJson"], ("bblanchon/ArduinoJson", "v{version}"))
        self.assertEqual(github_tag_candidates("v{version}", "7.4.3"), ["v7.4.3", "7.4.3"])
        self.assertEqual(github_tag_candidates("{version}", "1.12.4"), ["1.12.4", "v1.12.4"])

    def test_missing_ini_pin_is_an_error(self):
        with self.assertRaises(ValueError):
            apply_ini_pins("lib_deps =\n", [OutdatedPackage("QNEthernet", "0.36.0", "0.37.0", "Library")])


if __name__ == "__main__":
    unittest.main()
