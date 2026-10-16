"""Required real HDL checks, deliberately separate from pure host discovery.

Missing tools fail rather than silently skipping the requested FPGA evidence.
"""
import json
import os
from pathlib import Path
import tempfile
import unittest

from arc_detector.fpga_build import verify


class RealHdlParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.toolchain = os.environ.get("ARC_FPGA_TOOLS")
        if not cls.toolchain:
            raise RuntimeError("ARC_FPGA_TOOLS must name the installed OSS CAD Suite root")
        artifacts = os.environ.get("ARC_FPGA_ARTIFACTS")
        if artifacts:
            cls.root = Path(artifacts).resolve()
            cls.root.mkdir(parents=True, exist_ok=False)
        else:
            cls.scratch = tempfile.TemporaryDirectory(prefix="teg-arc-hdl-")
            cls.addClassCleanup(cls.scratch.cleanup)
            cls.root = Path(cls.scratch.name)

    def check_case(self, variant, scenario, *, start=0, samples=4096):
        directory = self.root / f"{variant}-{scenario}-{start}-{samples}"
        report = verify(self.toolchain, directory, variant_name=variant, scenario=scenario,
                        start_index=start, sample_count=samples)
        artifact = json.loads((directory / "verification.json").read_text(encoding="utf-8"))
        self.assertTrue(report["passed"])
        self.assertTrue(artifact["passed"])
        self.assertFalse(artifact["permit"])
        self.assertFalse(artifact["hardware_qualification"])
        self.assertEqual(artifact["compile"]["exit_code"], 0)
        self.assertEqual(artifact["simulation"]["exit_code"], 0)
        return report

    def test_both_real_hdl_fast_paths_match_integer_codes_energy_and_candidates(self):
        for variant in ("baseline_4m", "multirate_2m"):
            with self.subTest(architecture=variant):
                report = self.check_case(variant, "broadband")
                self.assertGreater(report["filtered_outputs_compared"], 1000)
                self.assertGreater(report["blocks_compared"], 8)
                self.assertGreater(report["candidate_count"], 0)
                self.assertFalse(report["protocol_fault"])

    def test_real_hdl_quiet_and_signed_impulse_do_not_create_arc_candidates(self):
        for variant in ("baseline_4m", "multirate_2m"):
            for scenario in ("quiet", "impulse"):
                with self.subTest(architecture=variant, scenario=scenario):
                    report = self.check_case(variant, scenario)
                    self.assertEqual(report["candidate_count"], 0)
                    self.assertFalse(report["protocol_fault"])

    def test_real_hdl_busy_and_index_gap_faults_inhibit_candidates(self):
        for variant in ("baseline_4m", "multirate_2m"):
            for scenario in ("busy_fault", "gap_fault"):
                with self.subTest(architecture=variant, scenario=scenario):
                    report = self.check_case(variant, scenario)
                    self.assertTrue(report["protocol_fault"])
                    self.assertEqual(report["candidate_count"], 0)

    def test_real_hdl_reset_clears_candidate_latch_and_inflight_warmup_mac(self):
        for variant in ("baseline_4m", "multirate_2m"):
            for scenario in ("reset", "reset_busy"):
                with self.subTest(architecture=variant, scenario=scenario):
                    samples = 8192 if scenario == "reset" else 4096
                    report = self.check_case(variant, scenario, samples=samples)
                    self.assertEqual(report["reset_count"], 1)
                    self.assertFalse(report["protocol_fault"])
                    self.assertGreater(report["filtered_outputs_compared"], 1000)
                    if scenario == "reset":
                        self.assertGreater(report["candidate_count"], 0, "latch must have been set before reset")
                        self.assertTrue(all(item["source_end_index"] < samples // 2
                                            for item in report["candidates"]))
                        self.assertFalse(report["candidate_latched"])

    def test_real_hdl_absolute_phase_and_wide_sample_clock_are_exact(self):
        self.check_case("multirate_2m", "broadband", start=37)
        self.check_case("baseline_4m", "broadband", start=2 ** 53 + 37)

    def test_real_hdl_partial_decimated_block_cannot_emit_future_evidence(self):
        report = self.check_case("multirate_2m", "broadband", samples=4133)
        self.assertEqual(report["blocks_compared"], 14)


if __name__ == "__main__":
    unittest.main()
