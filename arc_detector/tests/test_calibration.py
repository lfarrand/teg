import unittest
from unittest.mock import patch

import numpy as np

from arc_detector.calibration import calibrate, edge_impulses, fit_templates
from arc_detector.profile import SwitchingEdge
from arc_detector.signal import FrozenPredictor


class CalibrationTests(unittest.TestCase):
    def setUp(self):
        self.edges = tuple(SwitchingEdge(float(index), "pwm0", rising)
                           for index, rising in ((50, True), (850, False), (1650, True), (2450, False)))
        time = np.arange(512)
        rise = .5 * np.exp(-time / 20) * np.sin(time * .3)
        fall = -.3 * np.exp(-time / 25) * np.sin(time * .4)
        self.impulses = edge_impulses(4096, self.edges, ("pwm0",), 0, 12.6)
        self.samples = (np.convolve(self.impulses[0], rise)[:4096] +
                        np.convolve(self.impulses[1], fall)[:4096])

    def test_fractional_edges_and_independent_polarity(self):
        impulses = edge_impulses(100, (SwitchingEdge(1.25, "pwm0", True),
                                      SwitchingEdge(20, "pwm0", False)), ("pwm0",), 0, 12.6)
        self.assertAlmostEqual(impulses[0, 13], .15)
        self.assertAlmostEqual(impulses[0, 14], .85)
        self.assertAlmostEqual(impulses[1, 32], .4)
        self.assertAlmostEqual(impulses[1, 33], .6)
        with self.assertRaisesRegex(ValueError, "channel"):
            edge_impulses(100, self.edges, ("other",), 0, 12.6)

    def test_joint_fit_matches_predictor_including_fractional_delay(self):
        templates, predicted, stop, iterations = fit_templates(self.samples, self.edges)
        self.assertIn(stop, (1, 2, 4, 5))
        self.assertGreater(iterations, 0)
        self.assertEqual(len(templates[0].rising), 512)
        self.assertEqual(len(templates[0].falling), 512)
        self.assertLess(np.linalg.norm(predicted - self.samples) / np.linalg.norm(self.samples), 1e-5)
        predictor = FrozenPredictor(templates, 12.6)
        np.testing.assert_allclose(predictor.process(4096, 0, self.edges), predicted, atol=1e-10)

    def test_fit_requires_both_polarities_complete_responses_and_finite_input(self):
        for samples, edges in ((self.samples, self.edges[:1]),
                               (np.full(4096, np.nan), self.edges),
                               (np.ones(4095), self.edges),
                               (np.ones((4096, 1)), self.edges)):
            with self.subTest(shape=np.shape(samples), edges=len(edges)):
                with self.assertRaises(ValueError):
                    fit_templates(samples, edges)
        with self.assertRaisesRegex(ValueError, "delay"):
            fit_templates(self.samples, self.edges, edge_delay_samples=float("inf"))
        with self.assertRaisesRegex(ValueError, "converge"):
            fit_templates(self.samples, self.edges, max_iterations=1)

    def test_live_or_unverified_recording_cannot_train(self):
        arguments = dict(profile_id="test", node_id="node", operating_mode="mode",
                         verified_arc_free=False, provenance="external healthy lab declaration")
        with self.assertRaisesRegex(ValueError, "verified"):
            calibrate(self.samples, self.edges, **arguments)
        arguments.update(verified_arc_free=True, provenance="")
        with self.assertRaisesRegex(ValueError, "provenance"):
            calibrate(self.samples, self.edges, **arguments)
        arguments.update(provenance="lab declaration", operating_mode="")
        with self.assertRaisesRegex(ValueError, "mode"):
            calibrate(self.samples, self.edges, **arguments)

    def test_calibration_envelopes_are_frozen_and_never_hardware_qualified(self):
        from arc_detector.examples.synthetic import make_recording
        metadata, arrays = make_recording(count=100_000)
        edges = tuple(SwitchingEdge(**edge) for edge in metadata["edges"])
        result = calibrate(arrays["current_high"], edges, profile_id="synthetic",
                           node_id=metadata["node_id"], operating_mode=metadata["operating_mode"],
                           verified_arc_free=True, provenance=metadata["provenance"])
        self.assertTrue(result.profile.verified_arc_free)
        self.assertFalse(result.profile.spectral_qualified)
        self.assertFalse(result.profile.fast_qualified)
        self.assertFalse(result.profile.impulse_tail_qualified)
        self.assertGreaterEqual(result.spectral_frames, 8)
        self.assertEqual(len(result.profile.raw_envelope.psd_q95), 513)
        self.assertTrue(all(value > 0 for value in result.profile.residual_envelope.energy_q999))
        self.assertLess(result.fit_relative_rms, .12)
        from dataclasses import FrozenInstanceError
        with self.assertRaises(FrozenInstanceError):
            result.profile.spectral_qualified = True

    def test_insufficient_healthy_spectral_duration_rejects_profile(self):
        with self.assertRaisesRegex(ValueError, "eight"):
            calibrate(self.samples, self.edges, profile_id="test", node_id="node", operating_mode="mode",
                      verified_arc_free=True, provenance="lab declaration")

    def test_calibration_excludes_unmodelled_prefix_before_residual_filters_and_floor(self):
        from arc_detector.examples.synthetic import make_recording
        metadata, arrays = make_recording(count=100_000)
        edges = tuple(SwitchingEdge(**edge) for edge in metadata["edges"])
        samples = arrays["current_high"]
        contaminated = samples.copy()
        # Unknown pre-capture switching tail, including known edges early in
        # the fit interval: neither fitting nor envelope floors may learn it.
        contaminated[:525] += np.random.default_rng(912).normal(0, 1000, 525)
        args = dict(profile_id="prefix", node_id=metadata["node_id"],
                    operating_mode=metadata["operating_mode"],
                    verified_arc_free=True, provenance=metadata["provenance"])
        clean = calibrate(samples, edges, **args)
        dirty = calibrate(contaminated, edges, **args)
        self.assertGreater(max(dirty.profile.raw_envelope.energy_q999),
                           max(clean.profile.raw_envelope.energy_q999))
        np.testing.assert_allclose(dirty.profile.residual_envelope.energy_q999,
                                   clean.profile.residual_envelope.energy_q999, rtol=1e-10)
        np.testing.assert_allclose(dirty.profile.residual_envelope.psd_q95,
                                   clean.profile.residual_envelope.psd_q95, rtol=1e-10)
        np.testing.assert_allclose(dirty.profile.fast_residual.energy_q999,
                                   clean.profile.fast_residual.energy_q999, rtol=1e-10)
        np.testing.assert_allclose(dirty.profile.fast_residual.activity_floor,
                                   clean.profile.fast_residual.activity_floor, rtol=1e-10)
        self.assertEqual(dirty.profile.templates, clean.profile.templates)
        self.assertEqual(dirty.fit_relative_rms, clean.fit_relative_rms)

    def test_residual_calibration_requires_eight_frames_after_predictor_warmup(self):
        # Exactly eight raw frames; residual lacks eight after its extra tail.
        samples = np.zeros(65_790)
        with patch("arc_detector.calibration.fit_templates",
                   return_value=((), samples.copy(), 1, 1)):
            with self.assertRaisesRegex(ValueError, "eight"):
                calibrate(samples, (), profile_id="short", node_id="node",
                          operating_mode="mode", verified_arc_free=True,
                          provenance="independent healthy declaration")

    def test_fit_settling_samples_must_leave_valid_objective(self):
        for prefix in (-1, True, 4096):
            with self.subTest(prefix=prefix):
                with self.assertRaisesRegex(ValueError, "settling"):
                    fit_templates(self.samples, self.edges, settling_samples=prefix)


if __name__ == "__main__":
    unittest.main()
