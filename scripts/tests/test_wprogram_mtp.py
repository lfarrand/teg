import unittest

from scripts.wprogram_mtp import without_wprogram_mtp


class WprogramMtpTests(unittest.TestCase):
    def test_removes_mtp_include(self):
        src = '#include "usb_flightsim.h"\n#include "MTP_Teensy.h"\n#include "usb_audio.h"\n'
        once = without_wprogram_mtp(src)
        self.assertNotIn("MTP_Teensy.h", once)
        self.assertIn("do not include MTP here", once)
        self.assertEqual(without_wprogram_mtp(once), once)

    def test_clears_previous_fs_insert(self):
        src = (
            '#include "FS.h" // TEG: complete FS before core MTP headers\n'
            '#include "MTP_Teensy.h"\n'
        )
        out = without_wprogram_mtp(src)
        self.assertNotIn("MTP_Teensy.h", out)
        self.assertNotIn("complete FS before core MTP", out)

    def test_rejects_unexpected_file(self):
        with self.assertRaises(ValueError):
            without_wprogram_mtp("#include <Arduino.h>\n")
