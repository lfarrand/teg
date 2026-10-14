from contextlib import redirect_stdout
from dataclasses import replace
from fractions import Fraction
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from arc_detector.examples.synthetic import make_recording
from arc_detector.fast_oracle import CandidateDecision
from arc_detector.fixed_point import FixedDomain
from arc_detector.latency_study import (
    candidate_metrics, compare_codes, compare_recordings, golden_vectors, main,
    workload_report, write_artifact,
)
from arc_detector.replay import LabelledEvent, RecordingMetadata


class WorkloadAndMetricsTests(unittest.TestCase):
    def test_workload_counts_width_lanes_and_delay_without_claiming_fit(self):
        report = workload_report()
        baseline = report["variants"]["baseline_4m"]
        candidate = report["variants"]["multirate_2m"]
        self.assertEqual(baseline["total_symmetric_products_per_second"], 6_576_000_000)
        self.assertEqual(candidate["total_symmetric_products_per_second"], 2_200_000_000)
        self.assertEqual(baseline["total_unfolded_products_per_second"], 13_099_000_000)
        self.assertAlmostEqual(baseline["ideal_minimum_lane_clock_hz"], 6_576_000_000 / 14)
        self.assertEqual(baseline["ideal_capacity_products_per_second"], 2_800_000_000)
        self.assertLess(baseline["ideal_arithmetic_margin_products_per_second"], 0)
        self.assertGreater(candidate["ideal_arithmetic_margin_products_per_second"], 0)
        self.assertAlmostEqual(baseline["raw_fast_filter_group_delay_us"], 35.15)
        self.assertAlmostEqual(candidate["raw_fast_filter_group_delay_us"], 50.9)
        self.assertEqual(candidate["nominal_eight_block_evidence_us"], 512.0)
        self.assertFalse(report["device_fit_proven"])
        self.assertFalse(report["timing_closure_proven"])
        self.assertIn("RAM conflicts", report["excludes"])
        self.assertIn("not complete impulse settling", report["latency_note"])

    def test_workload_rejects_bad_assumptions_and_asymmetric_coefficients(self):
        for arguments in ((0, 14), (True, 14), (200_000_000, 0), (200_000_000, 1.5)):
            with self.subTest(case=str(arguments)):
                with self.assertRaises(ValueError):
                    workload_report(*arguments)
        with patch("arc_detector.latency_study.default_filters",
                   return_value=(np.array([.25, .125]), np.array([.25, .5, .25]))):
            with self.assertRaisesRegex(ValueError, "symmetric"):
                workload_report()

    def test_candidate_metrics_keep_half_open_events_and_extinction_separate(self):
        events = (LabelledEvent("arc", "arc", .001, .002, "parallel",
                                shutdown_command_s=.0018, terminal_zero_s=.0019),)
        decisions = tuple(CandidateDecision(index, index / 4) for index in (3999, 4000, 8000))
        metrics = candidate_metrics(events, decisions, start_index=0, sample_count=12_000)
        self.assertEqual(metrics["labelled_arc_events"], 1)
        self.assertEqual(metrics["missed_labelled_arc_events"], 0)
        self.assertEqual(metrics["event_candidates"][0]["candidate_latency_s"], 0.0)
        self.assertEqual(metrics["event_candidates"][0]["inferred_arc_type"], "unknown")
        self.assertEqual(metrics["unlabelled_candidates"], 2)
        self.assertFalse(metrics["candidate_is_shutdown"])
        self.assertFalse(metrics["extinction_established"])

    def test_candidate_metrics_preserve_adjacent_ticks_beyond_binary64_precision(self):
        onset = 2 ** 32
        boundary = onset * 4_000_000
        event = LabelledEvent("wide-clock", "arc", onset, onset + .000008)
        # Binary64 absolute seconds merge these two distinct physical ticks.
        decisions = (CandidateDecision(boundary - 1, 0), CandidateDecision(boundary + 1, 0))
        report = candidate_metrics((event,), decisions, start_index=boundary - 20, sample_count=100)
        found = report["event_candidates"][0]
        self.assertEqual(found["candidate_latency_s"], 1 / 4_000_000)
        self.assertEqual(found["candidate_source_end_index"], boundary + 1)
        self.assertEqual(found["candidate_latency_sample_ratio"], {"numerator": 1, "denominator": 1})
        self.assertEqual(report["unlabelled_candidates"], 1)
        self.assertEqual(report["missed_labelled_arc_events"], 0)
        # A candidate just before end must not be rounded into the excluded end.
        end = onset + .25
        near_end = boundary + 1_000_000 - 1
        tail = candidate_metrics((LabelledEvent("tail", "arc", onset, end),),
                                 (CandidateDecision(near_end, 0),), start_index=boundary,
                                 sample_count=1_000_000)
        self.assertEqual(tail["event_candidates"][0]["candidate_source_end_index"], near_end)
        self.assertEqual(tail["event_candidates"][0]["candidate_latency_s"], .25 - 1 / 4_000_000)

    def test_benign_exposure_unions_intervals_and_removes_arc_overlap(self):
        events = (LabelledEvent("noise1", "nuisance", 0, .003),
                  LabelledEvent("noise2", "nuisance", .002, .004),
                  LabelledEvent("arc", "arc", .001, .002))
        decisions = (CandidateDecision(2000, 500), CandidateDecision(6000, 1500), CandidateDecision(12000, 3000))
        metrics = candidate_metrics(events, decisions, start_index=0, sample_count=20_000)
        self.assertAlmostEqual(metrics["labelled_benign_exposure_s"], .003)
        self.assertEqual(metrics["candidates_in_labelled_benign"], 2)
        self.assertEqual(metrics["unlabelled_candidates"], 0)
        self.assertEqual(metrics["missed_labelled_arc_events"], 0)

    def test_metrics_reject_bad_labels_clocks_and_arc_free_contradiction(self):
        event = LabelledEvent("arc", "arc", .00025, .00075)
        for events, decisions, free in (((event,), (), True), ((event, event), (), False),
                                        ((LabelledEvent("late", "arc", 0, .01),), (), False),
                                        ((), (CandidateDecision(-1, 0),), False),
                                        ((), (CandidateDecision(5000, 0),), False),
                                        ((), (CandidateDecision(100, 0), CandidateDecision(100, 0)), False)):
            with self.subTest(case=len(events), decisions=len(decisions), free=free):
                with self.assertRaises(ValueError):
                    candidate_metrics(events, decisions, start_index=0, sample_count=4000,
                                      verified_arc_free=free)


class StudyAndGoldenTests(unittest.TestCase):
    def setUp(self):
        self.domain = FixedDomain(full_scale=16.0, unit_label="synthetic-current-unit", sample_bits=16)
        self.healthy = np.zeros(4096, dtype=np.int64)
        self.evaluation = np.random.default_rng(121).integers(-1000, 1000, 6000, dtype=np.int64)

    def test_labels_only_score_and_partitions_cannot_change_candidates(self):
        arc = LabelledEvent("arc", "arc", 37 / 4_000_000, (37 + 6000) / 4_000_000)
        nuisance = LabelledEvent("noise", "nuisance", arc.onset_s, arc.end_s)
        first = compare_codes(self.healthy, self.evaluation, domain=self.domain,
                              verified_healthy=True, provenance="independent numerical healthy fixture",
                              healthy_start=5, evaluation_start=37, events=(arc,), synthetic=True,
                              chunk_samples=1024)
        second = compare_codes(self.healthy, self.evaluation, domain=self.domain,
                               verified_healthy=True, provenance="independent numerical healthy fixture",
                               healthy_start=5, evaluation_start=37, events=(item for item in (nuisance,)),
                               synthetic=True, chunk_samples=37)
        self.assertFalse(first["permit"])
        self.assertFalse(first["hardware_qualification"])
        self.assertFalse(first["market_superiority_established"])
        self.assertFalse(first["cancellation_ported"])
        self.assertFalse(first["spectral_fft_ported"])
        for name in first["variants"]:
            actual = first["variants"][name]
            self.assertEqual(actual["candidate_decisions"], second["variants"][name]["candidate_decisions"])
            self.assertEqual(actual["metrics"]["missed_labelled_arc_events"], 0)
            latency = actual["metrics"]["event_candidates"][0]["candidate_latency_s"]
            self.assertGreaterEqual(latency, .000512)
            self.assertLess(latency, .002)
            self.assertGreater(second["variants"][name]["metrics"]["candidates_in_labelled_benign"], 0)
            runtime = actual["host_runtime"]
            self.assertLessEqual(runtime["chunk_ms_p50"], runtime["chunk_ms_p95"])
            self.assertLessEqual(runtime["chunk_ms_p95"], runtime["chunk_ms_p99"])
            self.assertLessEqual(runtime["chunk_ms_p99"], runtime["chunk_ms_max"])
            self.assertIn("encoding", runtime["excludes"])

    def test_study_requires_attestation_explicit_units_and_bounded_inputs(self):
        arguments = dict(domain=self.domain, verified_healthy=True, provenance="healthy")
        for change in ({"verified_healthy": False}, {"provenance": " "},
                       {"chunk_samples": 0}, {"chunk_samples": 32769}, {"synthetic": 1},
                       {"healthy_start": -1}, {"evaluation_start": True}, {"domain": None}):
            with self.subTest(case=next(iter(change))):
                with self.assertRaises(ValueError):
                    compare_codes(self.healthy, self.evaluation, **{**arguments, **change})
        for values in (np.array([], dtype=np.int64), np.array([.5]), np.array([32768], dtype=np.int64)):
            with self.subTest(shape=values.shape):
                with self.assertRaises(ValueError):
                    compare_codes(self.healthy, values, **arguments)

    def test_study_accepts_two_million_samples_and_validates_final_chunk_before_fit(self):
        # Exercise both complete captures through actual calibration/detection,
        # beyond the domain utility's one-million-sample per-call limit.
        values = np.zeros(2_000_000, dtype=np.int64)
        arguments = dict(domain=self.domain, verified_healthy=True,
                         provenance="independent numerical full-limit fixture")
        report = compare_codes(values, values, **arguments)
        self.assertEqual(report["healthy_sample_count"], 2_000_000)
        self.assertEqual(report["evaluation_sample_count"], 2_000_000)
        self.assertFalse(report["permit"])
        self.assertFalse(report["hardware_qualification"])
        for result in report["variants"].values():
            self.assertGreater(result["envelope"]["calibration_blocks"], 7000)
            self.assertGreater(result["valid_blocks"], 7000)
            self.assertEqual(result["candidate_decisions"], [])
        values[-1] = 32768  # First invalid code in this signed 16-bit domain.
        with patch("arc_detector.latency_study.calibrate_fast") as fit:
            for healthy, evaluation in ((values, self.evaluation), (self.healthy, values),
                                        (self.healthy, np.zeros(2_000_001, dtype=np.int64))):
                with self.subTest(healthy=len(healthy), evaluation=len(evaluation)):
                    with self.assertRaises(ValueError):
                        compare_codes(healthy, evaluation, **arguments)
            fit.assert_not_called()

    def test_golden_ports_match_independent_integer_convolution_and_phase(self):
        report = golden_vectors(self.domain, sample_count=512, start_index=5, seed=43)
        self.assertEqual(len(report["streams"]), 15)
        self.assertFalse(report["complete_detector_parity"])
        self.assertFalse(report["permit"])
        self.assertFalse(report["hardware_qualification"])
        codes = report["input_codes"]
        for stream in report["streams"].values():
            coefficients = stream["coefficient_codes"]
            self.assertEqual(coefficients, coefficients[::-1])
            self.assertTrue(all(index % stream["decimation_factor"] == 0 for index in stream["output_indices"]))
            self.assertTrue(all(5 <= index < 517 for index in stream["output_indices"]))
            positions = (0, len(stream["output_indices"]) // 2, len(stream["output_indices"]) - 1)
            for position in positions:
                offset = stream["output_indices"][position] - 5
                accumulator = sum(int(coefficient) * int(codes[offset - tap])
                                  for tap, coefficient in enumerate(coefficients) if 0 <= offset - tap < len(codes))
                expected = round(Fraction(accumulator, 2 ** 23))
                self.assertEqual(stream["output_codes"][position], expected)
                self.assertEqual(stream["output_valid"][position], offset >= len(coefficients) - 1)
        widened = report["streams"]["multirate_2m_band_0"]["certificate"]["input_bits"]
        prefix_out = report["streams"]["multirate_2m_prefix"]["certificate"]["output_bits"]
        self.assertEqual(widened, prefix_out)

    def test_golden_generation_is_reproducible_and_rejects_bad_parameters(self):
        first = golden_vectors(self.domain, sample_count=256, seed=90)
        second = golden_vectors(self.domain, sample_count=256, seed=90)
        self.assertEqual(first, second)
        self.assertNotEqual(first["input_sha256_le_i64"], golden_vectors(self.domain, sample_count=256, seed=91)["input_sha256_le_i64"])
        for change in ({"sample_count": 255}, {"sample_count": 4097}, {"start_index": -1},
                       {"start_index": 2 ** 31}, {"seed": True}, {"seed": -1}):
            with self.subTest(case=next(iter(change))):
                with self.assertRaises(ValueError):
                    golden_vectors(self.domain, **change)

    def test_recording_compare_keeps_one_sample_processing_separate_from_import(self):
        with tempfile.TemporaryDirectory() as directory:
            healthy_meta, healthy_arrays = make_recording(count=4096)
            eval_meta, eval_arrays = make_recording(count=512, seed=742)
            healthy_path, eval_path = Path(directory) / "healthy.npz", Path(directory) / "eval.npz"
            np.savez(healthy_path, **healthy_arrays)
            np.savez(eval_path, **eval_arrays)
            result = compare_recordings(healthy_path, RecordingMetadata.from_dict(healthy_meta),
                                        eval_path, RecordingMetadata.from_dict(eval_meta), domain=self.domain,
                                        verified_healthy=True, chunk_samples=1)
            self.assertEqual(result["processing_chunk_samples"], 1)
            self.assertEqual(result["evaluation_sample_count"], 512)
            self.assertFalse(result["permit"])
            for row in result["variants"].values():
                self.assertEqual(row["candidate_decisions"], [])

    def test_recording_compare_rejects_different_node_in_same_operating_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            metadata, arrays = make_recording(count=4096)
            healthy = RecordingMetadata.from_dict(metadata)
            healthy_path, evaluation_path = Path(directory) / "healthy.npz", Path(directory) / "eval.npz"
            np.savez(healthy_path, **arrays)
            np.savez(evaluation_path, **arrays)
            result = compare_recordings(healthy_path, healthy, evaluation_path, healthy,
                                        domain=self.domain, verified_healthy=True)
            self.assertEqual(result["node_id"], healthy.node_id)
            different_node = replace(healthy, node_id=healthy.node_id + "-other")
            with self.assertRaisesRegex(ValueError, "node"):
                compare_recordings(healthy_path, healthy, evaluation_path, different_node,
                                   domain=self.domain, verified_healthy=True)

    def test_recording_compare_rejects_training_arc_failed_health_and_same_file(self):
        with tempfile.TemporaryDirectory() as directory:
            healthy_meta, arrays = make_recording(count=4096)
            hp, ep = Path(directory) / "healthy.npz", Path(directory) / "eval.npz"
            np.savez(hp, **arrays)
            np.savez(ep, **arrays)
            healthy = RecordingMetadata.from_dict(healthy_meta)
            def run(training=healthy, evaluation=healthy, attested=True, evaluation_path=ep):
                return compare_recordings(hp, training, evaluation_path, evaluation,
                                           domain=self.domain, verified_healthy=attested)
            for case, invoke in (
                ("missing-attestation", lambda: run(attested=False)),
                ("not-healthy", lambda: run(training=replace(healthy, verified_arc_free=False))),
                ("arc-training", lambda: run(training=replace(healthy, events=(LabelledEvent("arc", "arc", 0, .001),)))),
                ("different-mode", lambda: run(evaluation=replace(healthy, operating_mode="other"))),
                ("same-file", lambda: run(evaluation_path=hp)),
                ("failed-sensor", lambda: run(training=replace(healthy, health={**healthy.health, "sensor_ok": False}))),
                ("missing-health", lambda: run(training=replace(healthy, health={}))),
                ("nonbool-health", lambda: run(training=replace(healthy, health={**healthy.health, "sensor_ok": 1}))),
            ):
                with self.subTest(case=case):
                    with self.assertRaises(ValueError):
                        invoke()
            arrays["high_clipped"][3000] = 1
            arrays["low_clipped"][3000] = 1
            np.savez(hp, **arrays)
            with self.assertRaisesRegex(ValueError, "clipped"):
                run()


class ArtifactAndCliTests(unittest.TestCase):
    def test_atomic_artifact_publication_preserves_large_integers_and_collisions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            write_artifact(path, {"wide": 2 ** 63 + 1, "flags": np.array([False, True]), "count": 3})
            result = json.loads(path.read_text())
            self.assertEqual(result["wide"], "9223372036854775809")
            self.assertEqual(result["flags"], [False, True])
            self.assertEqual(result["count"], 3)
            prior = path.read_bytes()
            with self.assertRaises(FileExistsError):
                write_artifact(path, {"replace": True})
            self.assertEqual(path.read_bytes(), prior)
            self.assertEqual([item.name for item in Path(directory).iterdir()], ["result.json"])

    def test_nonfinite_json_and_publish_failure_leave_no_partial_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            with self.assertRaises(ValueError):
                write_artifact(path, {"bad": float("nan")})
            self.assertEqual(list(Path(directory).iterdir()), [])
            with patch("arc_detector.latency_study.os.link", side_effect=PermissionError("publication denied")):
                with self.assertRaises(PermissionError):
                    write_artifact(path, {"value": 7})
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_cli_exports_parseable_vectors_and_requires_declared_scale(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "vectors.json"
            with redirect_stdout(StringIO()):
                self.assertEqual(main(["vectors", "--output", str(path), "--full-scale", "16",
                                       "--unit-label", "fixture-units", "--samples", "256"]), 0)
            report = json.loads(path.read_text())
            self.assertEqual(report["kind"], "elementary_fixed_fir_golden_vectors")
            self.assertEqual(report["domain"]["unit_label"], "fixture-units")
            self.assertEqual(len(report["input_codes"]), 256)
            self.assertFalse(report["permit"])
            with patch("sys.stderr", StringIO()):
                with self.assertRaises(SystemExit) as overwrite:
                    main(["vectors", "--output", str(path), "--full-scale", "16", "--unit-label", "fixture-units"])
                self.assertEqual(overwrite.exception.code, 2)
                with self.assertRaises(SystemExit) as omitted:
                    main(["vectors", "--output", str(Path(directory) / "other.json")])
                self.assertEqual(omitted.exception.code, 2)

    def test_cli_demo_reports_synthetic_candidates_not_hardware_superiority(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "demo.json"
            with redirect_stdout(StringIO()):
                self.assertEqual(main(["demo", "--output", str(path), "--full-scale", "16",
                                       "--unit-label", "synthetic-current-unit"]), 0)
            report = json.loads(path.read_text())
            self.assertTrue(report["synthetic"])
            self.assertTrue(report["passive"])
            for flag in ("permit", "hardware_qualification", "market_superiority_established",
                         "hardware_latency_measured", "extinction_measured"):
                self.assertFalse(report[flag])
            for row in report["variants"].values():
                self.assertEqual(row["metrics"]["labelled_arc_events"], 1)
                self.assertEqual(row["metrics"]["missed_labelled_arc_events"], 0)
                observed = row["metrics"]["event_candidates"][0]
                self.assertGreaterEqual(observed["candidate_latency_s"], .000512)
                self.assertLess(observed["candidate_latency_s"], .002)


if __name__ == "__main__":
    unittest.main()
