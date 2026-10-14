import copy
import csv
import json
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import numpy as np

from arc_detector.__main__ import _write_calibration, main
from arc_detector.calibration import calibrate
from arc_detector.examples.synthetic import make_recording
from arc_detector.profile import Profile, SwitchingEdge
from arc_detector.replay import (COLUMNS, LabelledEvent, RecordingMetadata, event_report,
                                 load_json, read_chunks, replay, write_json)


class RecordingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name)
        self.metadata, self.arrays = make_recording(count=4096)
        self.meta = RecordingMetadata.from_dict(self.metadata)

    def save_npz(self, name="record.npz", arrays=None):
        target = self.base / name
        np.savez_compressed(target, **(self.arrays if arrays is None else arrays))
        return target

    def test_strict_metadata_and_event_boundaries(self):
        for key, value in (("sample_rate_hz", 500_000), ("start_index", True),
                           ("sample_count", 4_000_001), ("high_gain_scale", 0),
                           ("low_gain_scale", float("nan")), ("synthetic", 1),
                           ("operating_mode", ""), ("health", {})):
            invalid = copy.deepcopy(self.metadata)
            invalid[key] = value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    RecordingMetadata.from_dict(invalid)
        invalid = copy.deepcopy(self.metadata)
        del invalid["provenance"]
        with self.assertRaises(ValueError):
            RecordingMetadata.from_dict(invalid)
        invalid = copy.deepcopy(self.metadata)
        invalid["extra"] = 1
        with self.assertRaises(ValueError):
            RecordingMetadata.from_dict(invalid)
        invalid = copy.deepcopy(self.metadata)
        invalid["edges"][0]["sample_index"] = -1
        with self.assertRaises(ValueError):
            RecordingMetadata.from_dict(invalid)
        invalid = copy.deepcopy(self.metadata)
        invalid["events"] = [{"event_id": "arc", "kind": "arc", "arc_type": "parallel",
                              "onset_s": 0, "end_s": .001}]
        with self.assertRaisesRegex(ValueError, "train"):
            RecordingMetadata.from_dict(invalid)
        invalid["verified_arc_free"] = False
        invalid["events"][0]["extinction_s"] = -.1
        with self.assertRaisesRegex(ValueError, "precede"):
            RecordingMetadata.from_dict(invalid)
        invalid["events"][0]["extinction_s"] = .0003
        invalid["events"][0]["shutdown_command_s"] = .0004
        with self.assertRaisesRegex(ValueError, "command"):
            RecordingMetadata.from_dict(invalid)

    def test_duplicate_nonfinite_json_and_output_overwrite_are_rejected(self):
        for contents in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}'):
            path = self.base / "input.json"
            path.write_text(contents)
            with self.assertRaises(ValueError):
                load_json(path)
        target = self.base / "new.json"
        write_json(target, {"valid": True})
        with self.assertRaises(FileExistsError):
            write_json(target, {"replacement": True})
        self.assertEqual(load_json(target), {"valid": True})

    def test_npz_gain_handover_and_chunk_continuity(self):
        arrays = {key: array.copy() for key, array in self.arrays.items()}
        arrays["current_high"][100] = 999
        arrays["current_low"][100] = 2
        arrays["high_clipped"][100] = 1
        arrays["high_clipped"][300] = 1
        arrays["low_clipped"][300] = 1
        chunks = list(read_chunks(self.save_npz(arrays=arrays), self.meta, 256))
        self.assertEqual(chunks[0].current[100], 2)
        self.assertFalse(chunks[0].both_gains_clipped)
        unusable = [chunk for chunk in chunks if chunk.both_gains_clipped]
        self.assertEqual([(chunk.start_index, len(chunk.current)) for chunk in unusable], [(300, 1)])
        self.assertEqual(sum(len(chunk.current) for chunk in chunks), 4096)
        self.assertTrue(all(left.start_index + len(left.current) == right.start_index
                            for left, right in zip(chunks, chunks[1:])))

    def test_npz_rejects_gaps_nonfinite_arrays_wrong_shapes_and_unknown_arrays(self):
        for mutator in (lambda arrays: arrays["sample_index"].__setitem__(100, 99),
                        lambda arrays: arrays["current_low"].__setitem__(100, np.inf),
                        lambda arrays: arrays.update(current_low=np.ones(10)),
                        lambda arrays: arrays.update(secret=np.ones(10)),
                        lambda arrays: arrays["high_clipped"].__setitem__(100, 2)):
            arrays = {key: array.copy() for key, array in self.arrays.items()}
            mutator(arrays)
            with self.assertRaises(ValueError):
                list(read_chunks(self.save_npz(arrays=arrays), self.meta, 256))
        with self.assertRaisesRegex(ValueError, "chunk"):
            list(read_chunks(self.save_npz(), self.meta, 1))
        with self.assertRaisesRegex(ValueError, "chunk"):
            list(read_chunks(self.save_npz(), self.meta, 256.5))
        from dataclasses import replace
        arrays = {key: array.copy() for key, array in self.arrays.items()}
        arrays["current_high"][0] = 1e308
        with self.assertRaisesRegex(ValueError, "overflow"):
            list(read_chunks(self.save_npz(arrays=arrays), replace(self.meta, high_gain_scale=1e308)))

    def test_npz_rejects_oversized_claimed_array_before_numpy_allocation(self):
        target = self.base / "oversized.npz"
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for column in COLUMNS:
                buffer = io.BytesIO()
                if column == "current_high":
                    np.lib.format.write_array_header_1_0(buffer, {"descr": "<f8", "fortran_order": False,
                                                                "shape": (1_000_000_000,)})
                else:
                    np.save(buffer, self.arrays[column], allow_pickle=False)
                archive.writestr(column + ".npy", buffer.getvalue())
        with patch("arc_detector.replay.np.load", side_effect=AssertionError("untrusted array allocation")):
            with self.assertRaisesRegex(ValueError, "header"):
                list(read_chunks(target, self.meta))

    def test_csv_streams_exact_schema_and_rejects_truncation(self):
        path = self.base / "record.csv"
        with path.open("w", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(COLUMNS)
            writer.writerows(zip(*(self.arrays[column] for column in COLUMNS)))
        chunks = list(read_chunks(path, self.meta, 1000))
        self.assertEqual([len(chunk.current) for chunk in chunks], [1000, 1000, 1000, 1000, 96])
        np.testing.assert_allclose(np.concatenate([chunk.current for chunk in chunks]), self.arrays["current_high"])
        path.write_text(",".join(COLUMNS) + "\n0,1,1,0,0\n")
        with self.assertRaisesRegex(ValueError, "count"):
            list(read_chunks(path, self.meta))
        path.write_text("unexpected\n1\n")
        with self.assertRaisesRegex(ValueError, "header"):
            list(read_chunks(path, self.meta))

    def test_event_metrics_do_not_equate_terminal_zero_and_extinction(self):
        events = (LabelledEvent("parallel", "arc", .01, .1, "parallel", .02, .06, .021),
                  LabelledEvent("missed", "arc", .2, .3, "series"),
                  LabelledEvent("radio", "nuisance", .4, .5))
        report = event_report(events, [{"state": "ARC_DETECTED", "timestamp_s": .015},
                                       {"state": "ARC_DETECTED", "timestamp_s": .45},
                                       {"state": "ARC_DETECTED", "timestamp_s": .6}], synthetic=False)
        arc = report["arc_events"][0]
        self.assertAlmostEqual(arc["decision_latency_s"], .005)
        self.assertAlmostEqual(arc["terminal_zero_latency_s"], .011)
        self.assertAlmostEqual(arc["measured_extinction_latency_s"], .05)
        self.assertAlmostEqual(arc["measured_command_to_extinction_s"], .04)
        self.assertEqual(report["missed_arc_events"], 1)
        self.assertEqual(report["nuisance_shutdowns"], 1)
        self.assertEqual(report["unlabelled_shutdowns"], 1)
        self.assertEqual(arc["detector_arc_type"], "unknown")
        uncertainty = event_report(events[:1], [{"state": "PROTECTION_UNCERTAIN", "timestamp_s": .04}], synthetic=False)
        self.assertFalse(uncertainty["arc_events"][0]["detected"])
        self.assertTrue(uncertainty["arc_events"][0]["protective_shutdown"])
        self.assertEqual(uncertainty["missed_arc_events"], 1)
        boundary = event_report(events[:1], [{"state": "ARC_DETECTED", "timestamp_s": .1}], synthetic=False)
        self.assertEqual(boundary["missed_arc_events"], 1)

    def test_health_shutdown_is_protection_without_an_arc_classification(self):
        health_stop = {"state": "PROTECTION_UNAVAILABLE", "timestamp_s": .1,
                       "reason": "both_gains_clipped", "path": "health"}
        report = event_report((LabelledEvent("arc", "arc", 0, 1),), [health_stop], synthetic=False)
        event = report["arc_events"][0]
        self.assertTrue(event["protective_shutdown"])
        self.assertAlmostEqual(event["shutdown_request_latency_s"], .1)
        self.assertFalse(event["detected"])
        self.assertIsNone(event["decision_latency_s"])
        self.assertEqual(event["shutdown_state"], "PROTECTION_UNAVAILABLE")
        self.assertEqual(event["shutdown_reason"], "both_gains_clipped")
        self.assertEqual(report["missed_arc_events"], 1)
        self.assertEqual(report["health_shutdowns"], 1)
        self.assertEqual(report["nuisance_shutdowns"], 0)
        nuisance = event_report((LabelledEvent("benign", "nuisance", 0, 1),), [health_stop], synthetic=False)
        self.assertEqual(nuisance["nuisance_shutdowns"], 1)
        self.assertEqual(nuisance["nuisance_events"][0]["shutdown_decisions"], 1)
        self.assertEqual(nuisance["unlabelled_shutdowns"], 0)
        healthy = event_report((), [health_stop], synthetic=False, verified_arc_free=True)
        self.assertEqual(healthy["nuisance_shutdowns"], 1)
        unlabelled = event_report((), [health_stop], synthetic=False)
        self.assertEqual(unlabelled["unlabelled_shutdowns"], 1)
        self.assertEqual(unlabelled["nuisance_shutdowns"], 0)


class PipelineReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        metadata, arrays = make_recording(count=120_000)
        cls.calibration = calibrate(arrays["current_high"], tuple(SwitchingEdge(**edge) for edge in metadata["edges"]),
                                    profile_id="test", node_id=metadata["node_id"],
                                    operating_mode=metadata["operating_mode"], verified_arc_free=True,
                                    provenance=metadata["provenance"])
        cls.profile = cls.calibration.profile
        cls.healthy_metadata = RecordingMetadata.from_dict(metadata)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name)

    def save(self, metadata, arrays):
        np.savez_compressed(self.base / "record.npz", **arrays)
        write_json(self.base / "record.json", metadata)
        write_json(self.base / "profile.json", self.profile.to_dict())
        return self.base / "record.npz", RecordingMetadata.from_dict(metadata)

    def test_correlated_and_startup_arcs_survive_cancellation(self):
        for kwargs in ({"arc_onset_s": .01}, {"startup_arc": True}):
            with self.subTest(kwargs=kwargs), tempfile.TemporaryDirectory() as directory:
                metadata, arrays = make_recording(count=160_000, seed=913, **kwargs)
                path = Path(directory) / "record.npz"
                np.savez_compressed(path, **arrays)
                report = replay(path, RecordingMetadata.from_dict(metadata), self.profile,
                                prototype=True, chunk_samples=8192)
                self.assertEqual(report["missed_arc_events"], 0)
                self.assertTrue(report["shutdown_latched"])
                self.assertFalse(report["permit"])
                self.assertTrue(report["synthetic"])
                self.assertFalse(report["hardware_qualification"])
                self.assertLess(report["decision_latency_s"]["maximum"], .02)

    def test_labels_cannot_change_detector_decisions_and_chunks_do_not_change_time(self):
        metadata, arrays = make_recording(count=160_000, seed=913, arc_onset_s=.01)
        path, recording = self.save(metadata, arrays)
        report = replay(path, recording, self.profile, prototype=True, chunk_samples=8192)
        metadata["events"] = []
        unlabelled = replay(path, RecordingMetadata.from_dict(metadata), self.profile,
                            prototype=True, chunk_samples=7777)
        self.assertEqual(report["decisions"], unlabelled["decisions"])
        self.assertEqual(report["processed_samples"], 160_000)
        self.assertEqual(report["spectral_frames"], unlabelled["spectral_frames"])
        self.assertEqual(unlabelled["nuisance_shutdowns"], 0)
        self.assertEqual(unlabelled["known_benign_observation_s"], 0)

    def test_clipping_trip_uses_physical_sample_time_independent_of_chunk_size(self):
        metadata, arrays = make_recording(count=120_000)
        clipped_index = 73_421
        arrays["high_clipped"][clipped_index:clipped_index + 17] = 1
        arrays["low_clipped"][clipped_index:clipped_index + 17] = 1
        path, recording = self.save(metadata, arrays)
        reports = [replay(path, recording, self.profile, prototype=True, chunk_samples=size)
                   for size in (4096, 131072)]
        for report in reports:
            self.assertEqual(report["processed_samples"], 120_000)
            self.assertEqual(report["final_state"], "PROTECTION_UNAVAILABLE")
            self.assertEqual(report["health_shutdowns"], 1)
            self.assertEqual(len(report["decisions"]), 1)
            decision = report["decisions"][0]
            self.assertEqual(decision["reason"], "both_gains_clipped")
            self.assertEqual(decision["sample_index"], clipped_index)
            self.assertAlmostEqual(decision["timestamp_s"], clipped_index / 4_000_000)
        self.assertEqual(reports[0]["decisions"], reports[1]["decisions"])
        self.assertEqual(reports[0]["tier_coverage_transitions"], reports[1]["tier_coverage_transitions"])
        self.assertEqual(reports[0]["tier_coverage_transitions"], [
            {"sample_index": 0, "timestamp_s": 0.0,
             "available_tiers": ["raw_spectral", "residual_spectral"]},
            {"sample_index": clipped_index, "timestamp_s": clipped_index / 4_000_000,
             "available_tiers": []},
            {"sample_index": clipped_index + 17, "timestamp_s": (clipped_index + 17) / 4_000_000,
             "available_tiers": ["raw_spectral", "residual_spectral"]}])

    def test_mid_chunk_clipping_cannot_preempt_an_earlier_arc(self):
        metadata, arrays = make_recording(count=160_000, seed=913, arc_onset_s=.001)
        arrays["high_clipped"][100_003] = 1
        arrays["low_clipped"][100_003] = 1
        path, recording = self.save(metadata, arrays)
        reports = [replay(path, recording, self.profile, prototype=True, chunk_samples=size)
                   for size in (4096, 131072)]
        for report in reports:
            self.assertEqual(report["missed_arc_events"], 0)
            self.assertEqual(report["final_state"], "ARC_DETECTED")
            self.assertLess(report["decisions"][0]["sample_index"], 100_003)
        self.assertEqual(reports[0]["decisions"], reports[1]["decisions"])

    def test_default_cli_never_claims_unqualified_protection_and_prototype_is_explicit(self):
        metadata, arrays = make_recording(count=160_000, seed=913, arc_onset_s=.01)
        path, recording = self.save(metadata, arrays)
        report = replay(path, recording, self.profile)
        self.assertEqual(report["final_state"], "PROTECTION_UNAVAILABLE")
        self.assertFalse(report["permit"])
        args = ["replay", "--recording", str(path), "--metadata", str(self.base / "record.json"),
                "--profile", str(self.base / "profile.json"), "--output", str(self.base / "report.json"), "--prototype"]
        self.assertEqual(main(args), 0)
        self.assertEqual(load_json(self.base / "report.json")["missed_arc_events"], 0)
        self.assertEqual(main(args), 2)
        invalid = copy.deepcopy(metadata)
        invalid["node_id"] = "other-node"
        with self.assertRaisesRegex(ValueError, "node"):
            replay(path, RecordingMetadata.from_dict(invalid), self.profile, prototype=True)
        from dataclasses import replace
        forged = replace(self.profile, spectral_qualified=True)
        with self.assertRaisesRegex(ValueError, "synthetic"):
            replay(path, recording, forged, prototype=True)
        self.assertEqual(main(["calibrate", "--recording", str(path), "--metadata", str(self.base / "record.json"),
                               "--output", str(self.base / "new-profile.json"), "--profile-id", "bad", "--verified-healthy"]), 2)

    def test_calibration_pair_collision_preserves_existing_file_without_partial_profile(self):
        for colliding_suffix in ("", ".calibration.json"):
            with self.subTest(colliding_suffix=colliding_suffix), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "profile.json"
                collision = Path(str(output) + colliding_suffix)
                collision.write_text("existing operator artifact", encoding="utf-8")
                with self.assertRaises(FileExistsError):
                    _write_calibration(output, self.calibration, self.healthy_metadata)
                self.assertEqual(collision.read_text(), "existing operator artifact")
                self.assertEqual({path.name for path in Path(directory).iterdir()}, {collision.name})

    def test_calibration_pair_second_publish_failure_rolls_back_both_artifacts(self):
        output = self.base / "profile.json"
        actual_link = os.link
        calls = []
        def fail_second_publish(source, target):
            calls.append(Path(target).name)
            if len(calls) == 2:
                raise PermissionError("simulated second publication failure")
            actual_link(source, target)
        with patch("arc_detector.__main__.os.link", side_effect=fail_second_publish):
            with self.assertRaisesRegex(PermissionError, "second publication"):
                _write_calibration(output, self.calibration, self.healthy_metadata)
        self.assertEqual(calls, ["profile.json.calibration.json", "profile.json"])
        self.assertEqual(list(self.base.iterdir()), [])

    def test_calibration_pair_raced_collision_retains_new_operator_file(self):
        output = self.base / "profile.json"
        actual_link = os.link
        def create_collision(source, target):
            if Path(target) == output:
                output.write_text("concurrent operator artifact", encoding="utf-8")
            actual_link(source, target)
        with patch("arc_detector.__main__.os.link", side_effect=create_collision):
            with self.assertRaises(FileExistsError):
                _write_calibration(output, self.calibration, self.healthy_metadata)
        self.assertEqual(output.read_text(), "concurrent operator artifact")
        self.assertEqual({path.name for path in self.base.iterdir()}, {"profile.json"})

    def test_calibration_pair_success_has_complete_profile_and_provenance(self):
        output = self.base / "profile.json"
        _write_calibration(output, self.calibration, self.healthy_metadata)
        self.assertEqual(Profile.from_dict(load_json(output)), self.profile)
        provenance = load_json(str(output) + ".calibration.json")
        self.assertEqual(provenance["provenance"], self.healthy_metadata.provenance)
        self.assertFalse(provenance["hardware_qualification"])
        self.assertEqual({path.name for path in self.base.iterdir()},
                         {"profile.json", "profile.json.calibration.json"})

    def test_tier_telemetry_distinguishes_unavailable_robust_only_and_full(self):
        metadata, arrays = make_recording(count=120_000)
        path, recording = self.save(metadata, arrays)
        unavailable = replay(path, recording, self.profile)
        self.assertEqual(unavailable["available_tiers"], [])
        self.assertEqual(unavailable["tier_coverage_transitions"],
                         [{"sample_index": 0, "timestamp_s": 0.0, "available_tiers": []}])
        robust = replay(path, recording, self.profile, prototype=True)
        self.assertEqual(robust["available_tiers"], ["raw_spectral", "residual_spectral"])
        from dataclasses import replace
        # Qualification flags are a software coverage fixture, not bench evidence.
        qualified_fixture = replace(self.profile, spectral_qualified=True, fast_qualified=True,
                                    impulse_tail_qualified=True)
        metadata["synthetic"] = False
        full = replay(path, RecordingMetadata.from_dict(metadata), qualified_fixture)
        expected = ["raw_spectral", "residual_spectral", "raw_fast", "residual_fast"]
        self.assertEqual(full["available_tiers"], expected)
        self.assertEqual(full["tier_coverage_transitions"],
                         [{"sample_index": 0, "timestamp_s": 0.0, "available_tiers": expected}])
        self.assertFalse(full["hardware_qualification"])


if __name__ == "__main__":
    unittest.main()
