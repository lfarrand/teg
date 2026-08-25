import unittest

from scripts.link_flags import without_rwx_segment_warning


class LinkFlagTests(unittest.TestCase):
    def test_strips_combined_wl_token(self):
        flags = ["-Wl,--gc-sections,--relax,--no-warn-rwx-segments"]
        self.assertEqual(
            without_rwx_segment_warning(flags),
            ["-Wl,--gc-sections,--relax"],
        )

    def test_drops_standalone_flag(self):
        self.assertEqual(
            without_rwx_segment_warning(["--no-warn-rwx-segments", "-Os"]),
            ["-Os"],
        )

    def test_leaves_unrelated_flags(self):
        flags = ["-mcpu=cortex-m7", "-mthumb"]
        self.assertEqual(without_rwx_segment_warning(flags), flags)
