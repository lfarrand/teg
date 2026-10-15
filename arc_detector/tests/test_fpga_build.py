import json
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from arc_detector import fpga_build
from arc_detector.fast_oracle import architecture
from arc_detector.fixed_point import FixedDomain


class FpgaBuildTests(unittest.TestCase):
    def test_toolchain_is_explicit_complete_and_environment_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                fpga_build._tools(root)
            (root / "bin").mkdir()
            suffix = ".exe" if fpga_build.os.name == "nt" else ""
            for name in ("yosys", "iverilog", "vvp", "nextpnr-nexus"):
                (root / "bin" / (name + suffix)).touch()
            with patch.dict(fpga_build.os.environ, {"ARC_TEST_SENTINEL": "preserve"}):
                actual, tools, environment = fpga_build._tools(root)
            self.assertEqual(actual, root.resolve())
            self.assertEqual(set(tools), {"yosys", "iverilog", "vvp", "nextpnr-nexus"})
            self.assertEqual(environment["ARC_TEST_SENTINEL"], "preserve")
            self.assertTrue(environment["PATH"].startswith(str(root / "bin")))

    def test_tool_run_retains_nonzero_exit_and_timeout_as_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch("arc_detector.fpga_build.subprocess.run", return_value=subprocess.CompletedProcess([], 7)):
                result = fpga_build._run(["tool"], temporary, {}, "failed.log")
            self.assertEqual(result["exit_code"], 7)
            self.assertFalse(result["timed_out"])
            with patch("arc_detector.fpga_build.subprocess.run", side_effect=subprocess.TimeoutExpired("tool", 1)):
                result = fpga_build._run(["tool"], temporary, {}, "timeout.log", timeout=1)
            self.assertIsNone(result["exit_code"])
            self.assertTrue(result["timed_out"])
            with self.assertRaises(FileExistsError):
                fpga_build._run(["tool"], temporary, {}, "failed.log")

    def test_reference_fixtures_pin_signed_extrema_reset_and_transient(self):
        healthy, values = fpga_build._fixtures("impulse", 4096)
        self.assertTrue(np.all(healthy == 0))
        self.assertEqual(values[1003], 8_388_607)
        self.assertEqual(values[1004], -8_388_608)
        self.assertEqual(np.count_nonzero(values), 2)
        _, reset = fpga_build._fixtures("reset", 8192)
        self.assertTrue(np.any(reset[1003:4096] != 0))
        self.assertTrue(np.all(reset[4096:] == 0))
        np.testing.assert_array_equal(reset, fpga_build._fixtures("reset", 8192)[1])
        for name, count in (("unknown", 4096), ("quiet", 4095), ("quiet", 32769), ("quiet", True)):
            with self.subTest(case=f"{name}-{count}"):
                with self.assertRaises(ValueError):
                    fpga_build._fixtures(name, count)

    def test_expected_filter_outputs_retain_absolute_phase_and_complete_history(self):
        values = np.zeros(4096, dtype=np.int64)
        domain = FixedDomain(16, "test", 24)
        baseline = fpga_build._expected_filtered(values, 37, architecture("baseline_4m"), domain)
        variant = fpga_build._expected_filtered(values, 37, architecture("multirate_2m"), domain)
        self.assertEqual(baseline[0], (293, (0,) * 6))
        self.assertEqual(baseline[-1][0], 4132)
        self.assertEqual(variant[0], (420, (0,) * 6))
        self.assertEqual(variant[-1][0], 4132)
        self.assertTrue(all(index % 2 == 0 for index, _ in variant))

    def test_trace_parser_preserves_wide_signed_data_and_requires_complete_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "trace.log"
            index = 2 ** 53 + 37
            path.write_text(f"F 50 {index} -1 2 -3 4 -5 6\nC 51 {index + 1}\nDONE 52 0 1\n", encoding="ascii")
            trace = fpga_build._parse_trace(path)
            self.assertEqual(trace["filtered"], [(50, index, (-1, 2, -3, 4, -5, 6))])
            self.assertEqual(trace["candidates"], [(51, index + 1)])
            self.assertEqual(trace["done"], (52, 0, 1))
            for text in ("", "F 1 2 x 0 0 0 0 0\nDONE 2 0 0", "C 1\nDONE 2 0 0", "DONE 1 0 0\nDONE 2 0 0"):
                path.write_text(text, encoding="ascii")
                with self.subTest(case=text[:8]):
                    with self.assertRaises(ValueError):
                        fpga_build._parse_trace(path)

    def test_safe_source_quoting_and_integer_artifact_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "report.json"
            fpga_build._publish(path, {"index": 2 ** 53 + 1, "permit": False})
            self.assertEqual(json.loads(path.read_text())["index"], "9007199254740993")
            with self.assertRaises(FileExistsError):
                fpga_build._publish(path, {})
            self.assertTrue(fpga_build._yosys_quote(path).startswith('"'))
            for name in ('bad;command.sv', 'bad\npath.sv', 'bad"path.sv'):
                with self.subTest(case=name[:3]):
                    with self.assertRaises(ValueError):
                        fpga_build._yosys_quote(Path(temporary) / name)

    def test_synthesis_refuses_unverified_or_changed_inputs_before_creating_output(self):
        from arc_detector.fast_oracle import calibrate_fast
        from arc_detector.rtl_config import emit_rtl_config
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "verified"
            destination = Path(temporary) / "mapped"
            variant, domain = architecture(), FixedDomain(16, "test", 24)
            envelope = calibrate_fast(np.zeros(4096, dtype=np.int64), architecture=variant, domain=domain,
                                      verified_healthy=True, provenance="numerical test fixture")
            manifest = emit_rtl_config(source, variant, domain, envelope)
            report = {"passed": True, "protocol_fault": False,
                      "rtl_sha256": {path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                                     for path in manifest["rtl_sources"]},
                      "config_sha256": {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                                        for name in ("prefix.hex", *(f"bank{band}.hex" for band in range(6)),
                                                     "energy.hex", "activity.hex", "manifest.json")}}
            with patch("arc_detector.fpga_build._tools", return_value=(Path(temporary), {}, {})), \
                    patch("arc_detector.fpga_build._run") as run:
                for label, modified in (("unverified", {**report, "passed": False}),
                                        ("fault", {**report, "protocol_fault": True}),
                                        ("rtl", {**report, "rtl_sha256": {}}),
                                        ("config", {**report, "config_sha256": {}})):
                    (source / "verification.json").write_text(json.dumps(modified), encoding="utf-8")
                    with self.subTest(case=label):
                        with self.assertRaises(ValueError):
                            fpga_build.synthesize(temporary, source, destination)
                        self.assertFalse(destination.exists())
                for change in ({"family": "versal"}, {"route": True, "family": "xcup"},
                               {"route": True}, {"research_pdc": True}, {"research_pdc": 1},
                               {"frequency_mhz": float("nan")}, {"frequency_mhz": 0},
                               {"timeout": 0}, {"timeout": 7201}):
                    with self.subTest(case=next(iter(change))):
                        with self.assertRaises(ValueError):
                            fpga_build.synthesize(temporary, source, destination, **change)
                run.assert_not_called()

    def test_mapping_and_routing_reports_do_not_turn_fit_or_timing_into_permission(self):
        """Reporting unit test; mocks here are not physical implementation proof."""
        from arc_detector.fast_oracle import calibrate_fast
        from arc_detector.rtl_config import emit_rtl_config
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "verified"
            variant, domain = architecture(), FixedDomain(16, "test", 24)
            envelope = calibrate_fast(np.zeros(4096, dtype=np.int64), architecture=variant, domain=domain,
                                      verified_healthy=True, provenance="numerical reporting test")
            manifest = emit_rtl_config(source, variant, domain, envelope)
            report = {"passed": True, "protocol_fault": False,
                      "rtl_sha256": {path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                                     for path in manifest["rtl_sources"]},
                      "config_sha256": {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                                        for name in ("prefix.hex", *(f"bank{band}.hex" for band in range(6)),
                                                     "energy.hex", "activity.hex", "manifest.json")}}
            (source / "verification.json").write_text(json.dumps(report), encoding="utf-8")
            mapped = {"modules": {"arc_fast_synth_top": {
                "attributes": {"top": "1"}, "ports": {"clk": {"direction": "input", "bits": [2]}},
                "cells": {"test": {"type": "MULT36X36"}}}}}
            tools = {name: name for name in ("yosys", "nextpnr-nexus")}
            for label, synthesis_exit, route_exit, achieved, expect_fit, expect_timing in (
                    ("synthesis-failed", 1, 0, 220, False, False),
                    ("routing-failed", 0, 1, 220, False, False),
                    ("timing-failed", 0, 0, 199, True, False),
                    ("missing-artifact", 0, 0, None, False, False),
                    ("closed", 0, 0, 220, True, True)):
                def run(command, directory, environment, log_name, timeout):
                    directory = Path(directory)
                    if log_name == "synthesis.log":
                        (directory / "mapped.json").write_text(json.dumps(mapped), encoding="utf-8")
                        return {"exit_code": synthesis_exit}
                    self.assertIn("--pdc", command)
                    self.assertTrue((directory / "research-only.provenance.json").is_file())
                    if achieved is not None:
                        (directory / "route.fasm").write_text("mock reporting fixture, not actual FPGA data", encoding="ascii")
                        (directory / "route.json").write_text(json.dumps({
                            "fmax": {"detector_clk": {"achieved": achieved, "constraint": 200}},
                            "utilization": {"MULT36_CORE": {"used": 1, "available": 14}}}), encoding="utf-8")
                    return {"exit_code": route_exit}
                with self.subTest(case=label), \
                        patch("arc_detector.fpga_build._tools", return_value=(Path(temporary), tools, {})), \
                        patch("arc_detector.fpga_build.tool_manifest", return_value={"unit_test_mock": True}), \
                        patch("arc_detector.fpga_build._run", side_effect=run):
                    destination = Path(temporary) / label
                    result = fpga_build.synthesize(temporary, source, destination, route=True, research_pdc=True)
                    artifact = json.loads((destination / "synthesis-report.json").read_text(encoding="utf-8"))
                    self.assertEqual(result["synthesis_succeeded"], synthesis_exit == 0)
                    self.assertEqual(artifact["device_fit_proven"], expect_fit)
                    self.assertEqual(artifact["place_and_route_succeeded"], synthesis_exit == 0 and route_exit == 0)
                    self.assertEqual(artifact["timing_target_met"], expect_timing)
                    for field in ("permit", "hardware_qualification", "physical_latency_measured", "board_pinout_constrained"):
                        self.assertIs(artifact[field], False)

    def test_missing_nonfinite_or_differently_constrained_timing_cannot_pass(self):
        self.assertTrue(fpga_build._timing_target_met({"clock": {"achieved": 201, "constraint": 200}}, 200))
        for fmax in (None, {}, [], {"clock": None}, {"clock": {}},
                     {"clock": {"achieved": 150, "constraint": 150}},
                     {"clock": {"achieved": 201, "constraint": 150}},
                     {"clock": {"achieved": 199.9, "constraint": 200}},
                     {"clock": {"achieved": float("nan"), "constraint": 200}},
                     {"clock": {"achieved": float("inf"), "constraint": 200}},
                     {"clock": {"achieved": 201, "constraint": True}},
                     {"clock": {"achieved": 201, "constraint": float("inf")}},
                     {"a": {"achieved": 201, "constraint": 200},
                      "b": {"achieved": 180, "constraint": 200}}):
            with self.subTest(report=str(fmax)[:55]):
                self.assertFalse(fpga_build._timing_target_met(fmax, 200))

    def test_resource_proof_requires_finite_integer_capacity_for_every_resource(self):
        self.assertTrue(fpga_build._utilization_fits({"a": {"used": 0, "available": 0},
                                                     "b": {"used": 14, "available": 14}}))
        for utilization in (None, {}, [], {"a": None}, {"a": {}},
                            {"a": {"used": 15, "available": 14}},
                            {"a": {"used": -1, "available": 14}},
                            {"a": {"used": True, "available": 14}},
                            {"a": {"used": 1.0, "available": 14}},
                            {"a": {"used": 1, "available": float("inf")}}):
            with self.subTest(report=str(utilization)[:55]):
                self.assertFalse(fpga_build._utilization_fits(utilization))


if __name__ == "__main__":
    unittest.main()
