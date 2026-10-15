from dataclasses import replace
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from arc_detector.fast_oracle import architecture, calibrate_fast
from arc_detector.fixed_point import FixedDomain, coefficient_codes
from arc_detector.rtl_config import emit_rtl_config


class RtlConfigTests(unittest.TestCase):
    def setUp(self):
        self.domain = FixedDomain(16, "test-unit", 24)
        self.variant = architecture("baseline_4m")
        self.envelope = calibrate_fast(np.zeros(4096, dtype=np.int64), architecture=self.variant,
                                       domain=self.domain, verified_healthy=True,
                                       provenance="constructed numerical fixture")

    def test_static_roms_bind_exact_signed_coefficients_thresholds_and_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = emit_rtl_config(root, self.variant, self.domain, self.envelope)
            self.assertEqual(json.loads((root / "manifest.json").read_text())["parameters"], {"MULTIRATE": 0})
            self.assertFalse(manifest["permit"])
            self.assertFalse(manifest["hardware_qualification"])
            self.assertEqual(manifest["source_period_cycles"], 50)
            for name, expected in manifest["sha256"].items():
                self.assertEqual(hashlib.sha256((root / name).read_bytes()).hexdigest(), expected)
            for band, coefficients in enumerate(self.variant.bank_coefficients):
                encoded = [int(line, 16) for line in (root / f"bank{band}.hex").read_text().splitlines()]
                decoded = tuple(value - (1 << 24) if value & (1 << 23) else value for value in encoded)
                self.assertEqual(decoded, coefficient_codes(coefficients)[:129])
            self.assertEqual((root / "energy.hex").read_text().splitlines(), ["0000000000000001"] * 6)

    def test_config_refuses_other_domain_variant_and_threshold_overflow_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            narrow = FixedDomain(16, "test-unit", 23)
            narrow_envelope = calibrate_fast(np.zeros(4096, dtype=np.int64), architecture=self.variant,
                                             domain=narrow, verified_healthy=True,
                                             provenance="matching narrow-port guard fixture")
            for case, variant, domain, envelope in (
                ("domain", self.variant, FixedDomain(16, "test-unit", 23), self.envelope),
                ("port-width", self.variant, narrow, narrow_envelope),
                ("variant", architecture("multirate_2m"), self.domain, self.envelope),
                ("threshold", self.variant, self.domain, replace(self.envelope, energy_q999=(2 ** 64,) * 6))):
                destination = Path(temporary) / case
                with self.subTest(case=case):
                    with self.assertRaises(ValueError):
                        emit_rtl_config(destination, variant, domain, envelope)
                    self.assertFalse(destination.exists())

    def test_config_does_not_overwrite_existing_rom_or_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            marker = root / "bank4.hex"
            marker.write_text("preserve", encoding="ascii")
            with self.assertRaises(FileExistsError):
                emit_rtl_config(root, self.variant, self.domain, self.envelope)
            self.assertEqual(marker.read_text(), "preserve")
            self.assertFalse((root / "manifest.json").exists())
            self.assertFalse((root / "bank0.hex").exists())


if __name__ == "__main__":
    unittest.main()
