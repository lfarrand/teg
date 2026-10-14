import dataclasses
import json
import unittest

from arc_detector.profile import EdgeTemplate, FastEnvelope, Profile, SpectralEnvelope, SwitchingEdge


def profile(**changes):
    defaults = dict(profile_id="fixture", node_id="node-1", operating_mode="bench",
                    raw_envelope=SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513),
                    residual_envelope=SpectralEnvelope((1e-7,) * 4, (1e-13,) * 513))
    defaults.update(changes)
    return Profile(**defaults)


class ProfileTests(unittest.TestCase):
    def test_json_round_trip_copies_all_nested_values(self):
        item = profile(templates=(EdgeTemplate("pwm-a", (0.1,) * 512, (-0.2,) * 512),),
                       fast_raw=FastEnvelope((1e-5,) * 6, (2e-5,) * 6))
        restored = Profile.from_dict(json.loads(json.dumps(item.to_dict())))
        self.assertEqual(item, restored)
        self.assertIsInstance(restored.templates, tuple)
        self.assertIsInstance(restored.raw_envelope.psd_q95, tuple)

    def test_mutable_inputs_are_copied_and_profile_is_frozen(self):
        energies = [1.0] * 4
        envelope = SpectralEnvelope(energies, [0.0] * 513)
        energies[0] = 99
        self.assertEqual(envelope.energy_q999[0], 1)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            envelope.energy_q999 = (2.0,) * 4

    def test_invalid_numeric_envelopes_rejected(self):
        for value in (0, -1, float("inf"), float("nan"), True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                SpectralEnvelope((value,) * 4, (1.0,) * 513)
        with self.assertRaises(ValueError):
            SpectralEnvelope((1.0,) * 4, (1.0,) * 512)
        with self.assertRaises(ValueError):
            FastEnvelope((1.0,) * 6, (-1.0,) * 6)

    def test_narrow_sorted_unique_characterised_notches_only(self):
        for bins in ((3, 2), (3, 3), (1, 2, 3, 4), (513,), (True,)):
            with self.subTest(bins=bins), self.assertRaises(ValueError):
                SpectralEnvelope((1.0,) * 4, (1.0,) * 513, bins)
        self.assertEqual(SpectralEnvelope((1.0,) * 4, (1.0,) * 513, (10, 11, 12)).excluded_bins, (10, 11, 12))

    def test_qualification_requires_verified_healthy_recording_and_fast_tail(self):
        with self.assertRaises(ValueError):
            profile(spectral_qualified=True)
        with self.assertRaises(ValueError):
            profile(verified_arc_free=True, fast_qualified=True, fast_raw=FastEnvelope((1.0,) * 6, (1.0,) * 6))
        item = profile(verified_arc_free=True, spectral_qualified=True)
        self.assertTrue(item.spectral_qualified)

    def test_template_identity_length_and_edges_validate(self):
        with self.assertRaises(ValueError):
            EdgeTemplate("pwm", (0.0,) * 511, (0.0,) * 512)
        edge = EdgeTemplate("pwm", (0.0,) * 512, (0.0,) * 512)
        with self.assertRaises(ValueError):
            profile(templates=(edge, edge))
        for value in (-1, float("nan"), True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                SwitchingEdge(value, "pwm", True)
        self.assertEqual(SwitchingEdge(12.25, "pwm", False).sample_index, 12.25)

    def test_unknown_schema_and_unexpected_fields_rejected(self):
        data = profile().to_dict()
        for changed in ({**data, "schema_version": 2}, {**data, "schema_version": True},
                        {**data, "schema_version": 1.0}, {key: value for key, value in data.items() if key != "templates"},
                        {**data, "mystery": 1},
                        {**data, "raw_envelope": None}, {**data, "verified_arc_free": "yes"}):
            with self.subTest(changed=changed.keys()), self.assertRaises(ValueError):
                Profile.from_dict(changed)


if __name__ == "__main__":
    unittest.main()
