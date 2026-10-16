import unittest

import numpy as np

from arc_detector.detector import (AcquisitionHealth, Detector, FastBlock,
                                  ProtectionState, SpectralFrame)
from arc_detector.profile import EdgeTemplate, FastEnvelope, Profile, SpectralEnvelope, SwitchingEdge
from arc_detector.signal import BandEvidence, FastBandEvidence, FrozenPredictor


def profile(**changes):
    defaults = dict(profile_id="fixture", node_id="n1", operating_mode="bench",
                    raw_envelope=SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513),
                    residual_envelope=SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513))
    defaults.update(changes)
    return Profile(**defaults)


def frame(index, path="raw", *, weak_bands=0, strong_bands=0):
    bands = tuple(BandEvidence(1.0, 1.0, 1.0, 0, 30, 30, True,
                              band < max(weak_bands, strong_bands), band < strong_bands)
                  for band in range(4))
    return SpectralFrame(index, index / 4, path, bands)


class DetectorTests(unittest.TestCase):
    def test_unqualified_mode_rejects_permit_and_prototype_never_issues_permit(self):
        default = Detector(profile())
        self.assertEqual(default.state, ProtectionState.PROTECTION_UNAVAILABLE)
        self.assertTrue(default.shutdown_latched)
        failed = default.process(np.zeros(12000), start_index=0, operating_mode="bench")
        self.assertEqual(failed.decisions[0].reason, "unqualified_profile")
        self.assertFalse(failed.permit)
        self.assertTrue(default.reset(output_inhibited=True, diagnostics_passed=True))
        self.assertTrue(default.shutdown_latched)
        detector = Detector(profile(), prototype=True)
        result = detector.process(np.zeros(20000), start_index=0, operating_mode="bench")
        self.assertEqual(result.state, ProtectionState.MONITORING)
        self.assertFalse(result.permit)
        self.assertFalse(result.shutdown_latched)
        self.assertTrue(result.prototype)

    def test_qualified_suspect_keeps_permit_until_a_latched_decision(self):
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True))
        decisions = []
        detector.process(np.zeros(9000), start_index=0, operating_mode="bench")
        detector._consume_spectral([frame(16384, weak_bands=2)], decisions)
        self.assertEqual(detector.state, ProtectionState.SUSPECT)
        self.assertTrue(detector._result().permit)
        detector._timeout(416384, decisions)
        self.assertFalse(detector._result().permit)

    def test_qualified_profile_requires_explicit_exact_operating_mode(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        item = profile(verified_arc_free=True, spectral_qualified=True, templates=(template,),
                       fast_qualified=True, impulse_tail_qualified=True,
                       fast_raw=FastEnvelope((1e-8,) * 6, (1e-8,) * 6))
        for mode in (None, "other", "bench ", 1):
            with self.subTest(mode=mode):
                detector = Detector(item)
                arguments = {} if mode is None else {"operating_mode": mode}
                result = detector.process(np.zeros(20000), start_index=0, **arguments)
                self.assertEqual(result.state, ProtectionState.PROTECTION_UNAVAILABLE)
                self.assertTrue(result.shutdown_latched)
                self.assertFalse(result.permit)
                self.assertFalse(result.cancellation_active)
                self.assertFalse(result.available_tiers)
                self.assertEqual(result.decisions[0].reason,
                                 "missing_operating_mode" if mode is None else "operating_mode_mismatch")

    def test_mode_loss_revokes_existing_permit_and_requires_explicit_reset(self):
        item = profile(verified_arc_free=True, spectral_qualified=True)
        for mode in (None, "unsupported"):
            with self.subTest(mode=mode):
                detector = Detector(item)
                ready = detector.process(np.zeros(20000), start_index=0, operating_mode="bench")
                self.assertTrue(ready.permit)
                # Mode knowledge is required even when a caller supplies no
                # further current samples; an old permit must not survive.
                arguments = {} if mode is None else {"operating_mode": mode}
                unavailable = detector.process((), start_index=20000, **arguments)
                self.assertFalse(unavailable.permit)
                self.assertTrue(unavailable.shutdown_latched)
                self.assertEqual(unavailable.decisions[0].sample_index, 20000)
                resumed = detector.process(np.zeros(20000), start_index=20000, operating_mode="bench")
                self.assertTrue(resumed.spectral_frames)
                self.assertFalse(resumed.permit)
                self.assertTrue(resumed.shutdown_latched)
                self.assertTrue(detector.reset(output_inhibited=True, diagnostics_passed=True))
                self.assertFalse(detector._result().permit)
                reset_ready = detector.process(np.zeros(20000), start_index=40000, operating_mode="bench")
                self.assertTrue(reset_ready.permit)

    def test_adc_wrapper_checks_mode_before_first_complete_decimation_interval(self):
        item = profile(verified_arc_free=True, spectral_qualified=True)
        for arguments in ({}, {"operating_mode": "unsupported"}):
            with self.subTest(arguments=arguments):
                detector = Detector(item)
                first = detector.process20m(np.zeros(1), start_index=0, **arguments)
                self.assertEqual(first.state, ProtectionState.PROTECTION_UNAVAILABLE)
                self.assertTrue(first.shutdown_latched)
                self.assertFalse(first.permit)
                self.assertEqual(first.decisions[0].sample_index, 0)
                detector = Detector(item)
                ready = detector.process20m(np.zeros(60000), start_index=0, operating_mode="bench")
                self.assertTrue(ready.permit)
                lost = detector.process20m(np.zeros(1), start_index=60000, **arguments)
                self.assertFalse(lost.permit)
                self.assertTrue(lost.shutdown_latched)
                self.assertEqual(lost.decisions[0].sample_index, 12000)
                self.assertEqual(lost.decisions[0].timestamp_us, 3000)

    def test_prototype_cannot_treat_unknown_mode_as_matching_envelopes(self):
        detector = Detector(profile(), prototype=True)
        result = detector.process(np.zeros(20000), start_index=0, operating_mode="other")
        self.assertEqual(result.state, ProtectionState.PROTECTION_UNAVAILABLE)
        self.assertTrue(result.shutdown_latched)
        self.assertFalse(result.available_tiers)

    def test_fresh_residual_path_cannot_inherit_raw_frame_readiness(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True, templates=(template,)))
        raw_only = AcquisitionHealth(residual_profile_valid=False)
        raw = detector.process(np.zeros(20000), start_index=0, health=raw_only, operating_mode="bench")
        self.assertTrue(raw.permit)
        residual_only = AcquisitionHealth(raw_profile_valid=False)
        warming = detector.process(np.zeros(1000), start_index=20000, health=residual_only, operating_mode="bench")
        self.assertFalse(warming.permit)
        self.assertFalse(warming.shutdown_latched)
        self.assertFalse(detector._spectral_ready["raw"])
        self.assertFalse(detector._spectral_ready["residual"])
        ready = detector.process(np.zeros(9000), start_index=21000, health=residual_only, operating_mode="bench")
        self.assertTrue(any(frame.path == "residual" for frame in ready.spectral_frames))
        self.assertTrue(ready.permit)

    def test_fresh_raw_path_cannot_inherit_residual_frame_readiness(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True, templates=(template,)))
        residual_only = AcquisitionHealth(raw_profile_valid=False)
        residual = detector.process(np.zeros(20000), start_index=0, health=residual_only, operating_mode="bench")
        self.assertTrue(residual.permit)
        raw_only = AcquisitionHealth(residual_profile_valid=False)
        warming = detector.process(np.zeros(1), start_index=20000, health=raw_only, operating_mode="bench")
        self.assertFalse(warming.permit)
        self.assertFalse(warming.shutdown_latched)
        ready = detector.process(np.zeros(9000), start_index=20001, health=raw_only, operating_mode="bench")
        self.assertTrue(any(frame.path == "raw" for frame in ready.spectral_frames))
        self.assertTrue(ready.permit)

    def test_raw_readiness_survives_only_cancellation_changes(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True, templates=(template,)))
        ready = detector.process(np.zeros(20000), start_index=0,
                                 health=AcquisitionHealth(pwm_reference_valid=False), operating_mode="bench")
        self.assertTrue(ready.permit)
        enabled = detector.process(np.zeros(1), start_index=20000, operating_mode="bench")
        self.assertTrue(enabled.permit)
        self.assertTrue(detector._spectral_ready["raw"])
        self.assertFalse(detector._spectral_ready["residual"])
        disabled = detector.process(np.zeros(1), start_index=20001,
                                    health=AcquisitionHealth(pwm_reference_valid=False), operating_mode="bench")
        self.assertTrue(disabled.permit)

    def test_one_adc_sample_coverage_loss_latches_before_partial_interval_is_completed(self):
        for prefix in (0, 100000):
            with self.subTest(prefix=prefix):
                detector = Detector(profile(verified_arc_free=True, spectral_qualified=True))
                if prefix:
                    ready = detector.process20m(np.zeros(prefix), start_index=0, operating_mode="bench")
                    self.assertTrue(ready.permit)
                failed = detector.process20m(np.zeros(1), start_index=prefix,
                                             health=AcquisitionHealth(raw_profile_valid=False), operating_mode="bench")
                self.assertTrue(failed.shutdown_latched)
                self.assertFalse(failed.permit)
                self.assertEqual(failed.decisions[0].reason, "no_qualified_spectral_bandwidth")
                self.assertEqual(failed.decisions[0].sample_index, prefix / 5)
                completed = detector.process20m(np.zeros(4), start_index=prefix + 1, operating_mode="bench")
                self.assertTrue(completed.shutdown_latched)
                self.assertFalse(completed.permit)
                self.assertFalse(completed.decisions)

    def test_adc_reference_reset_persists_across_incomplete_decimation_interval(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True, templates=(template,)))
        ready = detector.process20m(np.zeros(100000), start_index=0, operating_mode="bench")
        self.assertTrue(ready.permit)
        self.assertTrue(detector._spectral_ready["residual"])
        no_reference = detector.process20m(np.zeros(1), start_index=100000,
                                           health=AcquisitionHealth(pwm_reference_valid=False), operating_mode="bench")
        self.assertTrue(no_reference.permit)
        self.assertFalse(detector._spectral_ready["residual"])
        restored = detector.process20m(np.zeros(4), start_index=100001, operating_mode="bench")
        self.assertTrue(restored.permit)
        self.assertFalse(detector._spectral_ready["residual"])
        fresh_residual_only = detector.process20m(np.zeros(1), start_index=100005,
                                                  health=AcquisitionHealth(raw_profile_valid=False), operating_mode="bench")
        self.assertFalse(fresh_residual_only.permit)
        self.assertFalse(fresh_residual_only.shutdown_latched)

    def test_initial_empty_calls_emit_pending_unqualified_decision_once(self):
        for method in ("process", "process20m"):
            with self.subTest(method=method):
                detector = Detector(profile())
                call = getattr(detector, method)
                first = call((), start_index=0, operating_mode="bench")
                self.assertEqual(len(first.decisions), 1)
                self.assertEqual(first.decisions[0].reason, "unqualified_profile")
                second = call((), start_index=0, operating_mode="bench")
                self.assertFalse(second.decisions)

    def test_partial_adc_calls_report_current_available_tiers(self):
        detector = Detector(profile(verified_arc_free=True, spectral_qualified=True))
        ready = detector.process20m(np.zeros(60000), start_index=0, operating_mode="bench")
        self.assertTrue(ready.permit)
        partial = detector.process20m(np.zeros(1), start_index=60000, operating_mode="bench")
        self.assertTrue(partial.permit)
        self.assertEqual(partial.available_tiers, ("raw_spectral",))
        failed = detector.process20m(np.zeros(1), start_index=60001,
                                     health=AcquisitionHealth(raw_profile_valid=False), operating_mode="bench")
        self.assertFalse(failed.permit)
        self.assertFalse(failed.available_tiers)

    def test_synthetic_startup_arc_detects_actual_samples_under_ten_ms(self):
        result = Detector(profile(), prototype=True).process(np.random.default_rng(10).normal(size=40000), start_index=0, operating_mode="bench")
        self.assertEqual(result.state, ProtectionState.ARC_DETECTED)
        self.assertEqual(result.decisions[0].reason, "spectral_strong")
        self.assertLess(result.decisions[0].timestamp_us, 10000)
        self.assertEqual(result.arc_type, "unknown")

    def test_raw_path_cannot_be_vetoed_by_cancellation_matching_signal(self):
        kernel = tuple(np.random.default_rng(27).normal(size=512))
        template = EdgeTemplate("pwm", kernel, tuple(-np.asarray(kernel)))
        item = profile(templates=(template,), verified_arc_free=True)
        edges = tuple(SwitchingEdge(i, "pwm", bool((i // 1000) % 2)) for i in range(0, 40000, 1000))
        raw = FrozenPredictor((template,)).process(40000, 0, edges)
        result = Detector(item, prototype=True).process(raw, start_index=0, edges=edges, operating_mode="bench")
        self.assertTrue(result.cancellation_active)
        self.assertEqual(result.decisions[0].path, "raw")
        residual = [f for f in result.spectral_frames if f.path == "residual"]
        self.assertTrue(residual)
        self.assertTrue(all(not any(band.weak for band in f.bands) for f in residual))

    def test_cancellation_requires_verified_arc_free_matching_profile_and_reference(self):
        template = EdgeTemplate("pwm", (1.0,) * 512, (0.0,) * 512)
        for item, health, mode in ((profile(templates=(template,)), AcquisitionHealth(), "bench"),
                                   (profile(templates=(template,), verified_arc_free=True), AcquisitionHealth(pwm_reference_valid=False), "bench"),
                                   (profile(templates=(template,), verified_arc_free=True), AcquisitionHealth(), "other")):
            with self.subTest(mode=mode, health=health):
                result = Detector(item, prototype=True).process(np.zeros(12000), start_index=0, health=health, operating_mode=mode)
                self.assertFalse(result.cancellation_active)
                if mode == "bench":
                    self.assertIn("raw_spectral", result.available_tiers)
                else:
                    self.assertTrue(result.shutdown_latched)
                    self.assertEqual(result.state, ProtectionState.PROTECTION_UNAVAILABLE)
                    self.assertNotIn("raw_spectral", result.available_tiers)

    def test_strong_requires_three_bands_and_three_consecutive_complete_frames(self):
        detector = Detector(profile(), prototype=True)
        decisions = []
        for index in (8192, 16384):
            detector._consume_spectral([frame(index, strong_bands=3)], decisions)
        self.assertFalse(detector.shutdown_latched)
        detector._consume_spectral([frame(24576, strong_bands=2)], decisions)
        for index in (32768, 40960):
            detector._consume_spectral([frame(index, strong_bands=3)], decisions)
        self.assertFalse(detector.shutdown_latched)
        detector._consume_spectral([frame(49152, strong_bands=3)], decisions)
        self.assertEqual(decisions[0].reason, "spectral_strong")

    def test_weak_accumulates_physical_time_with_one_quarter_decay(self):
        detector = Detector(profile(), prototype=True)
        decisions = []
        detector._consume_spectral([frame(8192, weak_bands=2)], decisions)
        detector._consume_spectral([frame(16384)], decisions)
        self.assertEqual(detector._temporal["raw"].weak_samples, 6144)
        for step in range(3, 12):
            detector._consume_spectral([frame(step * 8192, weak_bands=2)], decisions)
        self.assertFalse(detector.shutdown_latched)
        detector._consume_spectral([frame(12 * 8192, weak_bands=2)], decisions)
        self.assertEqual(decisions[0].reason, "spectral_accumulated")

    def test_suspect_only_clears_after_zero_and_two_clear_raw_frames(self):
        detector = Detector(profile(), prototype=True)
        decisions = []
        detector._consume_spectral([frame(8192, weak_bands=2)], decisions)
        self.assertEqual(detector.state, ProtectionState.SUSPECT)
        for step in range(2, 5):
            detector._consume_spectral([frame(step * 8192)], decisions)
            self.assertEqual(detector.state, ProtectionState.SUSPECT)
        detector._consume_spectral([frame(5 * 8192)], decisions)
        self.assertEqual(detector.state, ProtectionState.SUSPECT)
        detector._consume_spectral([frame(6 * 8192)], decisions)
        self.assertEqual(detector.state, ProtectionState.MONITORING)

    def test_persistent_intermittent_suspicion_times_out_at_wall_clock(self):
        detector = Detector(profile(), prototype=True)
        decisions = []
        for step in range(1, 53):
            detector._consume_spectral([frame(step * 8192, weak_bands=2 if step % 5 == 1 else 0)], decisions)
        self.assertEqual(decisions[0].state, ProtectionState.PROTECTION_UNCERTAIN)
        self.assertEqual(decisions[0].reason, "suspicion_timeout")
        self.assertGreaterEqual(decisions[0].sample_index - 8192, 400000)

    def test_suspicion_timeout_before_at_and_after_exact_sample_deadline(self):
        for delta, expected in ((399999, False), (400000, True), (500000, True)):
            with self.subTest(delta=delta):
                detector = Detector(profile(), prototype=True)
                decisions = []
                detector._consume_spectral([frame(8192, weak_bands=2)], decisions)
                detector._timeout(8192 + delta, decisions)
                self.assertEqual(detector.shutdown_latched, expected)
                if expected:
                    self.assertEqual(decisions[0].sample_index, 408192)

    def test_health_failure_partitions_latch_and_need_inhibited_diagnostic_reset(self):
        flags = ("frame_valid", "continuous", "deadline_met", "sensor_ok", "gain_valid", "diagnostics_passed")
        for flag in flags:
            with self.subTest(flag=flag):
                detector = Detector(profile(), prototype=True)
                result = detector.process(np.zeros(10), start_index=0, health=AcquisitionHealth(**{flag: False}), operating_mode="bench")
                self.assertEqual(result.state, ProtectionState.PROTECTION_UNAVAILABLE)
                self.assertFalse(detector.reset(output_inhibited=False, diagnostics_passed=True))
                self.assertFalse(detector.reset(output_inhibited=True, diagnostics_passed=False))
                self.assertTrue(detector.shutdown_latched)
                self.assertTrue(detector.reset(output_inhibited=True, diagnostics_passed=True))
                self.assertEqual(detector.state, ProtectionState.INITIALISING)
        detector = Detector(profile(), prototype=True)
        result = detector.process(np.zeros(10), start_index=0, health=AcquisitionHealth(both_gains_clipped=True), operating_mode="bench")
        self.assertEqual(result.decisions[0].reason, "both_gains_clipped")

    def test_gap_and_loss_of_all_qualified_bandwidth_latch(self):
        detector = Detector(profile(), prototype=True)
        detector.process(np.zeros(100), start_index=0, operating_mode="bench")
        result = detector.process(np.zeros(100), start_index=101, operating_mode="bench")
        self.assertEqual(result.decisions[0].reason, "acquisition_gap")
        detector = Detector(profile(), prototype=True)
        result = detector.process(np.zeros(100), start_index=0, health=AcquisitionHealth(raw_profile_valid=False), operating_mode="bench")
        self.assertEqual(result.decisions[0].reason, "no_qualified_spectral_bandwidth")

    def test_raw_profile_reentry_discards_old_qualified_streak_and_filter_history(self):
        template = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        item = profile(verified_arc_free=True, spectral_qualified=True, templates=(template,),
                       residual_envelope=SpectralEnvelope((1e12,) * 4, (1e10,) * 513))
        detector = Detector(item, prototype=True)
        first = detector.process(np.random.default_rng(71).normal(size=16633), start_index=0, operating_mode="bench")
        self.assertFalse(first.shutdown_latched)
        self.assertEqual(detector._temporal["raw"].strong_streak, 2)
        since = detector._suspect_since
        middle = detector.process(np.zeros(32000), start_index=16633,
                                  health=AcquisitionHealth(raw_profile_valid=False), operating_mode="bench")
        self.assertFalse(middle.shutdown_latched)
        self.assertIn("residual_spectral", middle.available_tiers)
        self.assertNotIn("raw_spectral", middle.available_tiers)
        self.assertEqual(detector._temporal["raw"].strong_streak, 0)
        self.assertEqual(detector._suspect_since, since)
        after = detector.process(np.random.default_rng(81).normal(size=9000), start_index=48633, operating_mode="bench")
        self.assertFalse(after.shutdown_latched)
        self.assertEqual(detector._temporal["raw"].strong_streak, 1)
        raw_frames = [f for f in after.spectral_frames if f.path == "raw"]
        self.assertEqual(len(raw_frames), 1)
        self.assertGreaterEqual(raw_frames[0].sample_index - 8185, 48633 + 254)

    def test_fast_requires_four_subbands_all_octaves_and_eight_blocks(self):
        detector = Detector(profile(), prototype=True)
        decisions = []
        def block(index, strong):
            return FastBlock(index, index / 4, "raw", tuple(FastBandEvidence(1, 1, i in strong) for i in range(6)))
        for step in range(1, 20):
            detector._consume_fast(block(step * 256, {0, 1, 2, 3}), decisions)
        self.assertFalse(detector.shutdown_latched)
        for step in range(20, 27):
            detector._consume_fast(block(step * 256, {0, 2, 4, 5}), decisions)
        self.assertFalse(detector.shutdown_latched)
        detector._consume_fast(block(27 * 256, {0, 2, 4, 5}), decisions)
        self.assertEqual(decisions[0].reason, "fast_sustained")

    def test_qualified_fast_actual_signal_detects_and_loss_reports_reduced_coverage(self):
        item = profile(verified_arc_free=True, spectral_qualified=True, fast_qualified=True,
                       impulse_tail_qualified=True, fast_raw=FastEnvelope((1e-8,) * 6, (1e-8,) * 6))
        result = Detector(item, prototype=True).process(np.random.default_rng(19).normal(size=12000), start_index=0, operating_mode="bench")
        self.assertEqual(result.decisions[0].reason, "fast_sustained")
        self.assertLess(result.decisions[0].timestamp_us, 2000)
        degraded = Detector(item, prototype=True).process(np.zeros(12000), start_index=0,
                                                         health=AcquisitionHealth(pwm_reference_valid=False), operating_mode="bench")
        self.assertFalse(degraded.shutdown_latched)
        self.assertIn("raw_spectral", degraded.available_tiers)
        self.assertNotIn("raw_fast", degraded.available_tiers)

    def test_one_impulse_cannot_pass_sustained_fast_guard(self):
        item = profile(verified_arc_free=True, spectral_qualified=True, fast_qualified=True,
                       impulse_tail_qualified=True, fast_raw=FastEnvelope((1e-8,) * 6, (1e-8,) * 6))
        samples = np.zeros(6000)
        samples[600] = 1000
        result = Detector(item, prototype=True).process(samples, start_index=0, operating_mode="bench")
        self.assertFalse(result.shutdown_latched)

    def test_detection_and_sample_evidence_continue_after_trip(self):
        detector = Detector(profile(), prototype=True)
        first = detector.process(np.random.default_rng(11).normal(size=30000), start_index=0, operating_mode="bench")
        self.assertTrue(first.shutdown_latched)
        after = detector.process(np.zeros(24000), start_index=30000, operating_mode="bench")
        self.assertTrue(after.spectral_frames)
        self.assertEqual(after.state, ProtectionState.ARC_DETECTED)
        self.assertFalse(after.permit)
        self.assertFalse(after.decisions)

    def test_complete_processing_chunk_invariance_and_chronological_first_decision(self):
        rng = np.random.default_rng(23)
        samples = np.concatenate((np.zeros(15000), rng.normal(size=50000)))
        whole = Detector(profile(), prototype=True).process(samples, start_index=0, operating_mode="bench")
        detector = Detector(profile(), prototype=True)
        decisions, frames = [], []
        offset = 0
        while offset < len(samples):
            count = min(int(rng.integers(1, 3500)), len(samples) - offset)
            part = detector.process(samples[offset:offset + count], start_index=offset, operating_mode="bench")
            decisions.extend(part.decisions)
            frames.extend(part.spectral_frames)
            offset += count
        self.assertEqual(tuple(decisions), whole.decisions)
        self.assertEqual([frame.sample_index for frame in frames], [frame.sample_index for frame in whole.spectral_frames])
        np.testing.assert_allclose([[b.energy for b in f.bands] for f in frames],
                                   [[b.energy for b in f.bands] for f in whole.spectral_frames], atol=1e-14)

    def test_twenty_msps_wrapper_chunk_invariant_even_inside_decimation_intervals(self):
        rng = np.random.default_rng(59)
        samples = rng.normal(size=210000)
        whole = Detector(profile(), prototype=True).process20m(samples, start_index=0, operating_mode="bench")
        detector = Detector(profile(), prototype=True)
        decisions, frames = [], []
        offset = 0
        while offset < len(samples):
            count = min(int(rng.integers(1, 1300)), len(samples) - offset)
            part = detector.process20m(samples[offset:offset + count], start_index=offset, operating_mode="bench")
            decisions.extend(part.decisions)
            frames.extend(part.spectral_frames)
            offset += count
        self.assertEqual(tuple(decisions), whole.decisions)
        self.assertEqual([f.sample_index for f in frames], [f.sample_index for f in whole.spectral_frames])
        np.testing.assert_allclose([[b.energy for b in f.bands] for f in frames],
                                   [[b.energy for b in f.bands] for f in whole.spectral_frames], atol=1e-13)

    def test_twenty_msps_cascaded_settling_excludes_incomplete_fir_history(self):
        samples = np.full(60000, 1.0)
        whole = Detector(profile(), prototype=True).process20m(samples, start_index=0, operating_mode="bench")
        first_frame_source = whole.spectral_frames[0].sample_index - 8185
        self.assertGreaterEqual(first_frame_source, 280)
        self.assertEqual(first_frame_source, 280)
        detector = Detector(profile(), prototype=True)
        first = detector.process20m(samples[:126], start_index=0, operating_mode="bench")
        second = detector.process20m(samples[126:130], start_index=126, operating_mode="bench")
        third = detector.process20m(samples[130:], start_index=130, operating_mode="bench")
        self.assertFalse(first.spectral_frames)
        self.assertFalse(second.spectral_frames)
        self.assertEqual(third.spectral_frames[0].sample_index, whole.spectral_frames[0].sample_index)

    def test_twenty_msps_health_failure_uses_exact_adc_fault_time_and_drops_pending_data(self):
        detector = Detector(profile(), prototype=True)
        detector.process20m(np.zeros(30000), start_index=0, operating_mode="bench")
        result = detector.process20m(np.ones(1), start_index=30000,
                                     health=AcquisitionHealth(both_gains_clipped=True), operating_mode="bench")
        self.assertEqual(result.decisions[0].reason, "both_gains_clipped")
        self.assertEqual(result.decisions[0].sample_index, 6000)
        self.assertEqual(result.decisions[0].timestamp_us, 1500)
        # Recovery keeps the latch, starts fresh FIR history, and cannot bridge
        # the invalid raw sample with old wrapper values or switching edges.
        after = detector.process20m(np.zeros(60000), start_index=30001, operating_mode="bench")
        self.assertTrue(after.shutdown_latched)
        self.assertTrue(after.spectral_frames)
        self.assertGreaterEqual(after.spectral_frames[0].sample_index - 8185, 6280)
        detector = Detector(profile(), prototype=True)
        detector.process20m(np.zeros(2), start_index=0, operating_mode="bench")
        result = detector.process20m(np.zeros(1), start_index=2, health=AcquisitionHealth(frame_valid=False), operating_mode="bench")
        self.assertEqual(result.decisions[0].sample_index, 0.4)
        self.assertAlmostEqual(result.decisions[0].timestamp_us, 0.1)

    def test_no_spectral_frame_or_decision_can_use_unobserved_future_samples(self):
        detector = Detector(profile(), prototype=True)
        result = detector.process(np.random.default_rng(29).normal(size=8441), start_index=0, operating_mode="bench")
        self.assertEqual(len(result.spectral_frames), 1)
        self.assertEqual(result.spectral_frames[0].sample_index, 8441)
        for index in (f.sample_index for f in result.spectral_frames):
            self.assertLessEqual(index, 8441)
        for index in (d.sample_index for d in result.decisions):
            self.assertLessEqual(index, 8441)

    def test_fast_adc_wrapper_settling_parity_with_tiny_initial_chunks(self):
        item = profile(verified_arc_free=True, spectral_qualified=True, fast_qualified=True,
                       impulse_tail_qualified=True, fast_raw=FastEnvelope((1e-8,) * 6, (1e-8,) * 6))
        samples = np.zeros(10000)
        whole = Detector(item, prototype=True).process20m(samples, start_index=0, operating_mode="bench")
        self.assertEqual(whole.fast_blocks[0].sample_index, 538)
        for first_count in (5, 100, 125, 129, 130):
            with self.subTest(first_count=first_count):
                detector = Detector(item, prototype=True)
                before = detector.process20m(samples[:first_count], start_index=0, operating_mode="bench")
                after = detector.process20m(samples[first_count:], start_index=first_count, operating_mode="bench")
                partitioned = before.fast_blocks + after.fast_blocks
                self.assertEqual([b.sample_index for b in partitioned], [b.sample_index for b in whole.fast_blocks])
                for block in before.fast_blocks:
                    self.assertLessEqual(block.sample_index * 5, first_count)
                for block in after.fast_blocks:
                    self.assertLessEqual(block.sample_index * 5, len(samples))


if __name__ == "__main__":
    unittest.main()
