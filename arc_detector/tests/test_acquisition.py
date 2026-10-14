import unittest
import numpy as np

from arc_detector.acquisition import (
    CaptureContinuity, CaptureProcessor, GainCalibration, Randomization, decode_words, select_gain,
)


class AcquisitionTests(unittest.TestCase):
    def test_fixed_plain_vectors_preserve_sign_and_user_bits(self):
        # Literal wire words independently specified, not an encoder roundtrip.
        result = decode_words(np.array([0x00002A, 0xFFFFEA, 0x80002A,
                                        0x7FFFEA], dtype=np.uint32),
                              mode=Randomization.NONE, expected_user_bits=42)
        np.testing.assert_array_equal(result.counts, [0, -1, -131072, 131071])
        np.testing.assert_array_equal(result.user_bits, [42, 42, 42, 42])

    def test_fixed_prbs_vectors_use_appended_reference_not_conversion_lsb(self):
        result = decode_words(np.array([0xFFFFEB, 0x00002B, 0x7FFFEB,
                                        0x80002B, 0x00006A], dtype=np.uint32),
                              mode=Randomization.PRBS, expected_user_bits=21)
        np.testing.assert_array_equal(result.counts, [0, -1, -131072, 131071, 1])

    def test_fixed_lsb_vectors_leave_conversion_lsb_unchanged(self):
        result = decode_words(np.array([0xFFFFED, 0x00006D, 0x80002D,
                                        0x80006D], dtype=np.uint32),
                              mode=Randomization.LSB, expected_user_bits=45)
        np.testing.assert_array_equal(result.counts, [1, -1, -131072, 131071])

    def test_bad_frame_ranges_modes_and_markers_are_rejected(self):
        for values in ([0x1000000], [-1], [1.0], [[1]]):
            with self.subTest(case=str(values)):
                with self.assertRaises(ValueError):
                    decode_words(values)
        with self.assertRaises(ValueError):
            decode_words([0], mode="automatic")
        with self.assertRaises(ValueError):
            decode_words([0], expected_user_bits=32)
        with self.assertRaises(ValueError):
            decode_words([0], expected_user_bits=1)

    def test_gain_handover_preserves_input_units_and_flags_double_clipping(self):
        selected = select_gain(np.array([1600, 131071, -131072, 131071]),
                               np.array([100, 8200, -8200, 131071]))
        np.testing.assert_allclose(selected.samples, [100, 8200, -8200, 0])
        np.testing.assert_array_equal(selected.used_low, [False, True, True, False])
        np.testing.assert_array_equal(selected.unusable, [False, False, False, True])
        self.assertTrue(selected.both_gains_clipped)

    def test_gain_selection_is_chunk_invariant_and_uses_calibrated_offsets(self):
        cal = GainCalibration(16, 1, 16, 1)
        h, l = np.array([1616, 131071, 3216]), np.array([101, 8301, 201])
        whole = select_gain(h, l, cal)
        pieces = [select_gain(h[:1], l[:1], cal), select_gain(h[1:], l[1:], cal)]
        np.testing.assert_array_equal(whole.samples, np.concatenate([p.samples for p in pieces]))
        np.testing.assert_allclose(whole.samples, [100, 8300, 200])

    def test_gain_calibration_rejects_invalid_and_nonfinite_inputs(self):
        for kwargs in ({"high_gain": 0}, {"low_gain": -1}, {"high_offset": float("nan")},
                       {"rail_margin_counts": 65536}, {"rail_margin_counts": True}):
            with self.subTest(case=next(iter(kwargs))):
                with self.assertRaises(ValueError):
                    GainCalibration(**kwargs)
        with self.assertRaises(ValueError):
            select_gain([131072], [1])
        with self.assertRaises(ValueError):
            select_gain([1, 2], [1])

    def test_sequence_gap_replay_clock_loss_and_reset_latch(self):
        for case in ("gap", "duplicate", "clock", "framing"):
            with self.subTest(case=case):
                continuity = CaptureContinuity()
                self.assertTrue(continuity.observe(10, 20))
                self.assertFalse(continuity.observe(31 if case == "gap" else 10 if case == "duplicate" else 30,
                                                    20, clock_valid=case != "clock",
                                                    frame_valid=case != "framing"))
                self.assertFalse(continuity.observe(continuity.next_sample, 20))
                self.assertFalse(continuity.reset(output_inhibited=False, diagnostics_ok=True))
                self.assertFalse(continuity.reset(output_inhibited=True, diagnostics_ok=False))
                self.assertTrue(continuity.reset(output_inhibited=True, diagnostics_ok=True))
                self.assertTrue(continuity.observe(0, 1))

    def test_sequence_accepts_large_counters_and_rejects_empty_or_invalid_blocks(self):
        continuity = CaptureContinuity()
        self.assertTrue(continuity.observe(2**40, 100))
        self.assertTrue(continuity.observe(2**40 + 100, 1))
        for start, count in ((-1, 1), (0, 0), (True, 1), (0, True)):
            with self.subTest(case=f"start_{start}_count_{count}"):
                self.assertFalse(CaptureContinuity().observe(start, count))

    def test_adc_decode_to_real_decimator_and_detector_composition(self):
        from arc_detector.detector import Detector, ProtectionState
        from arc_detector.profile import Profile, SpectralEnvelope

        envelope = SpectralEnvelope((1.0,) * 4, (1.0,) * 513)
        profile = Profile("capture", "string-1", "fixed", envelope, envelope)
        adapter = CaptureProcessor(Detector(profile, prototype=True), mode=Randomization.NONE)
        count = 100_000
        # Fixed words encode high=16 and low=1, both input-referred value=1.
        result = adapter.process(np.full(count, 0x400, dtype=np.uint32),
                                 np.full(count, 0x40, dtype=np.uint32), start_index=0,
                                 operating_mode="fixed")
        self.assertEqual(result.state, ProtectionState.MONITORING)
        self.assertFalse(result.permit)
        self.assertFalse(result.shutdown_latched)
        self.assertGreater(len(result.spectral_frames), 0)
        self.assertAlmostEqual(result.spectral_frames[0].bands[0].energy, 0.0, places=10)

    def test_adc_decode_failure_and_double_clipping_latch_unavailability(self):
        from arc_detector.detector import Detector, ProtectionState
        from arc_detector.profile import Profile, SpectralEnvelope

        envelope = SpectralEnvelope((1.0,) * 4, (1.0,) * 513)
        profile = Profile("capture", "string-1", "fixed", envelope, envelope)
        for case, high, low in (("bad-frame", [0x1000000], [0]),
                                ("double-clip", [0x7FFFC0], [0x7FFFC0])):
            with self.subTest(case=case):
                adapter = CaptureProcessor(Detector(profile, prototype=True), mode=Randomization.NONE)
                result = adapter.process(high, low, start_index=0, operating_mode="fixed")
                self.assertEqual(result.state, ProtectionState.PROTECTION_UNAVAILABLE)
                self.assertTrue(result.shutdown_latched)
                self.assertFalse(result.permit)
                self.assertEqual(result.decisions[0].path, "health")

    def test_middle_clip_reports_physical_time_independent_of_host_chunk(self):
        from arc_detector.detector import Detector
        from arc_detector.profile import Profile, SpectralEnvelope

        envelope = SpectralEnvelope((1.0,) * 4, (1.0,) * 513)
        profile = Profile("capture", "string-1", "fixed", envelope, envelope)
        words = np.zeros(40_000, dtype=np.uint32)
        words[30_000] = 0x7FFFC0
        outcomes = []
        for chunk_size in (40_000, 1_000):
            adapter = CaptureProcessor(Detector(profile, prototype=True), mode=Randomization.NONE)
            decisions = []
            for start in range(0, len(words), chunk_size):
                decisions.extend(adapter.process(words[start:start + chunk_size],
                                                 words[start:start + chunk_size],
                                                 start_index=start, operating_mode="fixed").decisions)
            outcomes.append([(d.reason, d.sample_index, d.timestamp_us) for d in decisions])
        self.assertEqual(outcomes[0], outcomes[1])
        self.assertEqual(outcomes[0], [("both_gains_clipped", 6000, 1500.0)])

    def test_late_invalid_word_preserves_prefix_time_and_consumed_continuity(self):
        from arc_detector.detector import Detector
        from arc_detector.profile import Profile, SpectralEnvelope

        envelope = SpectralEnvelope((1.0,) * 4, (1.0,) * 513)
        profile = Profile("capture", "string-1", "fixed", envelope, envelope)
        for bad_word, bad_index in ((-1, 30_000), (0x1000000, 30_000),
                                    (-1, 30_003), (0x1000000, 30_003)):
            outcomes = []
            for chunk_size in (40_000, 1_000):
                with self.subTest(bad_word=bad_word, bad_index=bad_index, chunk_size=chunk_size):
                    words = np.zeros(40_000, dtype=np.int64)
                    words[bad_index] = bad_word
                    adapter = CaptureProcessor(Detector(profile, prototype=True),
                                               mode=Randomization.NONE)
                    decisions = []
                    for start in range(0, len(words), chunk_size):
                        result = adapter.process(words[start:start + chunk_size],
                                                 np.zeros(min(chunk_size, len(words) - start), dtype=np.int64),
                                                 start_index=start, operating_mode="fixed")
                        decisions.extend(result.decisions)
                    self.assertEqual(adapter.detector._20_next_index, 40_000)
                    # Continue sensing after the invalid sample and the latch.
                    after = adapter.process(np.zeros(100_000, dtype=np.int64),
                                            np.zeros(100_000, dtype=np.int64),
                                            start_index=40_000, operating_mode="fixed")
                    self.assertGreater(len(after.spectral_frames), 0)
                    self.assertFalse(after.permit)
                    outcomes.append([(d.reason, d.sample_index, d.timestamp_us) for d in decisions])
            self.assertEqual(outcomes[0], outcomes[1])
            self.assertEqual(outcomes[0], [("invalid_adc_frame", bad_index / 5,
                                           bad_index / 20_000_000 * 1e6)])

    def test_late_invalid_word_cannot_preempt_an_earlier_arc(self):
        from arc_detector.detector import Detector, ProtectionState
        from arc_detector.profile import Profile, SpectralEnvelope

        envelope = SpectralEnvelope((1e-6,) * 4, (1e-12,) * 513)
        profile = Profile("capture", "string-1", "fixed", envelope, envelope,
                          verified_arc_free=True, spectral_qualified=True)
        counts = np.random.default_rng(322).normal(0, 100, 200_000).round().astype(np.int64)
        high = (((counts * 16) & 0x3FFFF) << 6).astype(np.int64)
        low = ((counts & 0x3FFFF) << 6).astype(np.int64)
        low[150_000] = 0x1000000
        outcomes = []
        for chunk_size in (200_000, 1_000):
            adapter = CaptureProcessor(Detector(profile), mode=Randomization.NONE)
            decisions = []
            for start in range(0, len(high), chunk_size):
                result = adapter.process(high[start:start + chunk_size], low[start:start + chunk_size],
                                         start_index=start, operating_mode="fixed")
                decisions.extend(result.decisions)
            self.assertEqual(result.state, ProtectionState.ARC_DETECTED)
            self.assertEqual(decisions[0].state, ProtectionState.ARC_DETECTED)
            self.assertLess(decisions[0].timestamp_us, 150_000 / 20_000_000 * 1e6)
            outcomes.append([(d.reason, d.sample_index, d.timestamp_us) for d in decisions])
        self.assertEqual(outcomes[0], outcomes[1])


if __name__ == "__main__":
    unittest.main()
