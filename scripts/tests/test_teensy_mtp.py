import unittest

from scripts.teensy_mtp import (
    is_framework_core_mtp_source,
    patched_core_mtp_source,
    project_mtp_include_dir,
)


class TeensyMtpTests(unittest.TestCase):
    def test_recognises_framework_core_sources(self):
        path = (
            "C:/Users/lee/.platformio/packages/framework-arduinoteensy/"
            "cores/teensy4/MTP_Teensy.cpp"
        )
        self.assertTrue(is_framework_core_mtp_source(path))
        self.assertTrue(
            is_framework_core_mtp_source(path.replace("/", "\\").replace("MTP_Teensy", "MTP_Storage"))
        )

    def test_ignores_project_copies(self):
        path = "D:/git/teg/scripts/mtp_core162/MTP_Teensy.cpp"
        self.assertFalse(is_framework_core_mtp_source(path))

    def test_replacement_and_wdog_paths(self):
        self.assertTrue(
            patched_core_mtp_source("D:/git/teg", "MTP_Teensy.cpp")
            .replace("\\", "/")
            .endswith("scripts/mtp_core162/MTP_Teensy.cpp")
        )
        self.assertTrue(
            project_mtp_include_dir("D:/git/teg").replace("\\", "/").endswith("lib/MTP_Teensy/src")
        )
