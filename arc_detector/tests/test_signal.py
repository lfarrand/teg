import unittest

import numpy as np

from arc_detector.profile import EdgeTemplate, FastEnvelope, SpectralEnvelope, SwitchingEdge
from arc_detector.signal import (FastFilterBank, FrozenPredictor, StreamingFIRDecimator,
                                 default_filters, fast_features, filter_report,
                                 quantise_coefficients, spectral_bin_indices,
                                 spectral_features, spectral_psd)


class SignalTests(unittest.TestCase):
    def test_quantised_filters_meet_measured_passband_and_antialias_targets(self):
        report = filter_report()
        self.assertAlmostEqual(report["combined_group_delay_us"], 34.9, places=12)
        for key, taps in (("stage_20m_to_4m", 127), ("stage_4m_to_500k", 255)):
            stage = report[key]
            self.assertEqual(stage["tap_count"], taps)
            self.assertGreater(stage["passband_min_db"], -0.01)
            self.assertLess(stage["passband_max_db"], 0.01)
            self.assertLess(stage["worst_stopband_db"], -80)
        for coefficients in default_filters():
            np.testing.assert_array_equal(coefficients * 2 ** 23, np.rint(coefficients * 2 ** 23))

    def test_fir_decimation_partition_invariance_nonzero_absolute_phase(self):
        rng = np.random.default_rng(101)
        samples = rng.normal(size=15073)
        coefficients = default_filters()[0]
        whole = StreamingFIRDecimator(coefficients, 5).process(samples, 19)
        stream = StreamingFIRDecimator(coefficients, 5)
        values, indices, valid = [], [], []
        offset = 0
        while offset < len(samples):
            length = min(int(rng.integers(1, 401)), len(samples) - offset)
            part = stream.process(samples[offset:offset + length], 19 + offset)
            values.extend(part.values)
            indices.extend(part.sample_indices)
            valid.extend(part.valid)
            offset += length
        np.testing.assert_allclose(values, whole.values, rtol=0, atol=1e-14)
        np.testing.assert_array_equal(indices, whole.sample_indices)
        np.testing.assert_array_equal(valid, whole.valid)
        self.assertTrue(all(index % 5 == 0 for index in indices))

    def test_fir_discontinuity_rejected_and_startup_not_valid(self):
        decimator = StreamingFIRDecimator(default_filters()[0], 5)
        first = decimator.process(np.zeros(126), 0)
        self.assertFalse(first.valid.any())
        with self.assertRaises(ValueError):
            decimator.process(np.zeros(10), 127)
        good = decimator.process(np.zeros(10), 126)
        self.assertTrue(good.valid.any())

    def test_signed_templates_fractional_delay_overlap_and_chunk_invariance(self):
        rising = np.zeros(512)
        rising[0], rising[5] = 2, 1
        falling = -rising
        template = EdgeTemplate("pwm", tuple(rising), tuple(falling))
        edges = (SwitchingEdge(0, "pwm", True), SwitchingEdge(5, "pwm", False),
                 SwitchingEdge(50.25, "pwm", True))
        complete = FrozenPredictor((template,)).process(700, 0, edges)
        self.assertAlmostEqual(complete[12], 0.8)
        self.assertAlmostEqual(complete[13], 1.2)
        pieces = []
        predictor = FrozenPredictor((template,))
        for start, stop in ((0, 9), (9, 50), (50, 71), (71, 700)):
            pieces.extend(predictor.process(stop - start, start,
                                           tuple(edge for edge in edges if start <= edge.sample_index < stop)))
        np.testing.assert_allclose(pieces, complete, atol=1e-15)
        self.assertFalse(np.any(complete[580:]))

    def test_unknown_reference_channel_rejected(self):
        predictor = FrozenPredictor((EdgeTemplate("known", (0.0,) * 512, (0.0,) * 512),))
        with self.assertRaises(ValueError):
            predictor.process(100, 0, (SwitchingEdge(0, "missing", True),))

    def test_hann_psd_scaling_and_narrow_tone_occupancy(self):
        x = 2 * np.cos(2 * np.pi * 64 * np.arange(1024) / 1024)
        psd = spectral_psd(x)
        self.assertAlmostEqual(float(psd.sum() * 500_000 / 1024), 2.0, places=7)
        envelope = SpectralEnvelope((1e-6,) * 4, (1e-9,) * 513)
        bands = spectral_features(x, envelope)
        self.assertLess(bands[1].occupancy, 0.2)
        self.assertFalse(any(band.weak for band in bands))

    def test_broadband_features_require_both_energy_and_occupancy(self):
        rng = np.random.default_rng(91)
        x = rng.normal(size=1024)
        low_floor = SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513)
        self.assertTrue(all(band.strong for band in spectral_features(x, low_floor)))
        high_occupancy_floor = SpectralEnvelope((1e-7,) * 4, (1.0,) * 513)
        self.assertFalse(any(band.weak for band in spectral_features(x, high_occupancy_floor)))

    def test_exclusions_never_qualify_less_than_sixty_percent_band(self):
        first = spectral_bin_indices()[0]
        excluded = tuple(int(i) for j, i in enumerate(first) if j % 4 < 2)
        envelope = SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513, excluded)
        band = spectral_features(np.random.default_rng(72).normal(size=1024), envelope)[0]
        self.assertFalse(band.available)
        self.assertFalse(band.strong)

    def test_fast_bank_chunk_invariance_and_short_activity_rejected(self):
        samples = np.random.default_rng(3).normal(size=2309)
        whole = FastFilterBank().process(samples)
        bank = FastFilterBank()
        pieces = np.vstack((bank.process(samples[:1]), bank.process(samples[1:900]), bank.process(samples[900:])))
        np.testing.assert_allclose(whole, pieces, atol=1e-14)
        block = np.zeros((256, 6))
        block[:32] = 1
        envelope = FastEnvelope((1e-6,) * 6, (1e-6,) * 6)
        features = fast_features(block, envelope)
        self.assertTrue(all(band.energy > 8e-6 for band in features))
        self.assertTrue(all(not band.strong for band in features))

    def test_invalid_samples_and_q_range_rejected(self):
        with self.assertRaises(ValueError):
            spectral_psd([float("nan")] * 1024)
        with self.assertRaises(ValueError):
            quantise_coefficients([1.5])
        with self.assertRaises(ValueError):
            fast_features(np.zeros((1, 6)), FastEnvelope((1.0,) * 6, (1.0,) * 6))

    def test_q1_23_positive_rounding_cannot_overflow_the_signed_integer(self):
        scale = 2 ** 23
        maximum = (scale - 1) / scale
        quantised = quantise_coefficients([maximum, maximum + .25 / scale])
        np.testing.assert_array_equal(quantised, [maximum, maximum])
        self.assertFalse(quantised.flags.writeable)
        for coefficient in (1 - 2 ** -25, maximum + .5 / scale,
                            np.nextafter(1.0, 0.0)):
            with self.subTest(coefficient=coefficient):
                with self.assertRaisesRegex(ValueError, "round outside signed Q1"):
                    quantise_coefficients([coefficient])

    def test_q1_23_lower_boundary_remains_representable(self):
        scale = 2 ** 23
        quantised = quantise_coefficients([-1.0, -1.0 + .25 / scale,
                                           np.nextafter(-1.0, 0.0), -1.0 + 1.0 / scale])
        np.testing.assert_array_equal(quantised * scale,
                                      [-scale, -scale, -scale, -scale + 1])
        for coefficient in (np.nextafter(-1.0, -np.inf), -1.0 - 2 ** -25):
            with self.subTest(coefficient=coefficient):
                with self.assertRaisesRegex(ValueError, "exceed signed Q1"):
                    quantise_coefficients([coefficient])


if __name__ == "__main__":
    unittest.main()
