import json
import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from scripts.check_teensy_size import TeensySize, evaluate_headroom, main, parse_teensy_size


SAMPLE = """
\x1b[31mteensy_size:   FLASH: code:394672, data:87976, headers:8868   free for files:7634948\x1b[0m
teensy_size:    RAM1: variables:73088, code:344668, padding:15780   free for local variables:90752
teensy_size:    RAM2: variables:246592  free for malloc/new:277696
teensy_size:  EXTRAM: variables:7341056
"""


class TeensySizeTests(unittest.TestCase):
    def test_parses_real_shape_and_ansi(self):
        size = parse_teensy_size(SAMPLE)
        self.assertEqual(size.flash_code, 394672)
        self.assertEqual(size.ram1_local_free, 90752)
        self.assertEqual(size.ram2_heap_free, 277696)
        self.assertEqual(size.extram_variables, 7341056)

    def test_missing_section_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "ram2"):
            parse_teensy_size(SAMPLE.replace("teensy_size:    RAM2", "not-ram2"))

    def test_thresholds_are_inclusive(self):
        size = parse_teensy_size(SAMPLE)
        margins, failures = evaluate_headroom(
            size,
            min_flash_files_free=size.flash_files_free,
            min_ram1_local_free=size.ram1_local_free,
            min_ram2_heap_free=size.ram2_heap_free,
            extram_capacity=8388608,
            min_extram_free=8388608 - size.extram_variables,
        )
        self.assertFalse(failures)
        self.assertTrue(all(value == 0 for value in margins.values()))

    def test_each_budget_can_fail(self):
        size = TeensySize(1, 1, 1, 9, 1, 1, 1, 9, 1, 9, 91)
        _, failures = evaluate_headroom(
            size,
            min_flash_files_free=10,
            min_ram1_local_free=10,
            min_ram2_heap_free=10,
            extram_capacity=100,
            min_extram_free=10,
        )
        self.assertEqual(len(failures), 4)

    def test_cli_writes_machine_readable_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            report = Path(directory) / "size.json"
            log.write_text(SAMPLE, encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                result = main([
                    str(log), "--json-out", str(report),
                    "--min-flash-files-free", "8000000",
                    "--min-ram1-local-free", "81920",
                    "--min-ram2-heap-free", "262144",
                    "--extram-capacity", "8388608",
                    "--min-extram-free", "786432",
                ])
            self.assertEqual(result, 1)
            self.assertEqual(json.loads(report.read_text())["status"], "fail")


if __name__ == "__main__":
    unittest.main()
