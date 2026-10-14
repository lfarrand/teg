from dataclasses import FrozenInstanceError, replace
import unittest

import numpy as np
from scipy.signal import lfilter

from arc_detector.fast_oracle import (FastOracleEnvelope, FixedFastOracle, _ceil_quantile_mean,
                                     architecture, calibrate_fast, evaluate_sums)
from arc_detector.fixed_point import FixedDomain, coefficient_codes


class FastOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.domain = FixedDomain(full_scale=1.0, unit_label="numerical fixture", sample_bits=24)
        cls.envelopes = {name: calibrate_fast(np.zeros(4096, dtype=np.int64),
                                             architecture=architecture(name), domain=cls.domain,
                                             verified_healthy=True, provenance="Known zero numerical fixture")
                         for name in ("baseline_4m", "multirate_2m")}

    def oracle(self, name="baseline_4m", domain=None, envelope=None):
        return FixedFastOracle(architecture(name), self.domain if domain is None else domain,
                               self.envelopes[name] if envelope is None else envelope)

    def sums(self, name, active_bands=(0, 1, 2, 4), active_fragments=8, fragment_increment=1):
        variant = architecture(name)
        boundary = 8 * variant.fragment_samples
        fragments = tuple(tuple(boundary + fragment_increment if band in active_bands and fragment < active_fragments else 0
                                for band in range(6)) for fragment in range(8))
        return tuple(sum(row[band] for row in fragments) for band in range(6)), fragments

    def test_named_variants_bind_exact_taps_rates_and_source_duration(self):
        for name, prefix, taps, decimation, samples in (("baseline_4m", 0, 257, 1, 256),
                                                       ("multirate_2m", 127, 129, 2, 128)):
            variant = architecture(name)
            metadata = variant.to_dict()
            self.assertEqual(metadata["prefix_taps"], prefix)
            self.assertEqual(metadata["bank_taps"], [taps] * 6)
            self.assertEqual(metadata["bank_rate_hz"], 4_000_000 // decimation)
            self.assertEqual(variant.block_samples, samples)
            self.assertEqual(metadata["source_block_samples"], 256)
            self.assertFalse(metadata["hardware_qualification"])
        with self.assertRaises(ValueError):
            architecture("unknown")

    def test_exact_quantile_ceiling_never_uses_float_or_underflows(self):
        self.assertEqual(_ceil_quantile_mean([0, 1000], 256), 4)
        enormous = 1 << 2000
        self.assertEqual(_ceil_quantile_mean([enormous, enormous], 256), enormous // 256)
        self.assertEqual(_ceil_quantile_mean([0, 0], 256), 0)
        for envelope in self.envelopes.values():
            self.assertEqual(envelope.energy_q999, (1,) * 6)
            self.assertEqual(envelope.activity_floor, (1,) * 6)
            self.assertGreater(envelope.calibration_blocks, 8)
            self.assertFalse(envelope.to_dict()["hardware_qualification"])
        envelope = self.envelopes["baseline_4m"]
        with self.assertRaises(FrozenInstanceError):
            envelope.verified_healthy = False
        with self.assertRaisesRegex(ValueError, "underflow"):
            replace(envelope, quantization_floor=2)
        with self.assertRaisesRegex(ValueError, "positive"):
            replace(envelope, energy_q999=(0,) * 6)

    def test_calibration_requires_explicit_attestation_complete_settling_and_more_than_eight_blocks(self):
        arguments = dict(architecture=architecture(), domain=self.domain,
                         verified_healthy=False, provenance="numerical fixture")
        with self.assertRaisesRegex(ValueError, "verified"):
            calibrate_fast(np.zeros(4096, dtype=np.int64), **arguments)
        arguments.update(verified_healthy=True, provenance="")
        with self.assertRaisesRegex(ValueError, "provenance"):
            calibrate_fast(np.zeros(4096, dtype=np.int64), **arguments)
        arguments.update(provenance="numerical fixture", quantization_floor=0)
        with self.assertRaisesRegex(ValueError, "floor"):
            calibrate_fast(np.zeros(4096, dtype=np.int64), **arguments)
        arguments.pop("quantization_floor")
        for name, eight_end in (("baseline_4m", 2304), ("multirate_2m", 2430)):
            arguments["architecture"] = architecture(name)
            with self.assertRaisesRegex(ValueError, "eight"):
                calibrate_fast(np.zeros(eight_end, dtype=np.int64), **arguments)
            envelope = calibrate_fast(np.zeros(eight_end + 256, dtype=np.int64), **arguments)
            self.assertEqual(envelope.calibration_blocks, 9)

    def test_frozen_thresholds_cannot_cross_variant_or_numeric_domain(self):
        with self.assertRaisesRegex(ValueError, "match"):
            self.oracle("multirate_2m", envelope=self.envelopes["baseline_4m"])
        for domain in (FixedDomain(2.0, "numerical fixture", 24),
                       FixedDomain(1.0, "different unit", 24),
                       FixedDomain(1.0, "numerical fixture", 23)):
            with self.subTest(domain=domain), self.assertRaisesRegex(ValueError, "match"):
                self.oracle(domain=domain)
        with self.assertRaises(ValueError):
            self.oracle(envelope={"energy_q999": [1] * 6})

    def test_calibration_rejects_modified_same_name_architecture(self):
        healthy = np.zeros(4096, dtype=np.int64)
        for name in ("baseline_4m", "multirate_2m"):
            with self.subTest(architecture=name):
                altered = replace(architecture(name), block_samples=architecture(name).block_samples // 2)
                with self.assertRaisesRegex(ValueError, "unchanged named architecture"):
                    calibrate_fast(healthy, architecture=altered, domain=self.domain,
                                   verified_healthy=True, provenance="Known zero numerical fixture")

    def test_integer_energy_and_fragment_comparisons_are_strict(self):
        for name in self.envelopes:
            variant, envelope = architecture(name), self.envelopes[name]
            at_energy = tuple(8 * variant.block_samples for _ in range(6))
            at_fragment = tuple(tuple(8 * variant.fragment_samples for _ in range(6)) for _ in range(8))
            exact = evaluate_sums(at_energy, at_fragment, envelope, variant)
            self.assertFalse(any(exact.strong_subbands))
            self.assertEqual(exact.active_fragments, (0,) * 6)
            above_energy, above_fragment = self.sums(name, active_bands=range(6))
            above = evaluate_sums(above_energy, above_fragment, envelope, variant)
            self.assertTrue(all(above.strong_subbands))
            self.assertTrue(above.qualifying)
            self.assertEqual(above.active_fragments, (8,) * 6)
            inconsistent = list(above_energy)
            inconsistent[0] += 1
            with self.assertRaisesRegex(ValueError, "sum"):
                evaluate_sums(inconsistent, above_fragment, envelope, variant)

    def test_six_of_eight_activity_four_subbands_and_all_three_octaves_required(self):
        name, variant, envelope = "baseline_4m", architecture(), self.envelopes["baseline_4m"]
        # Larger active fragments preserve energy excess while isolating activity count.
        for active_count, expected in ((5, False), (6, True)):
            energy, fragments = self.sums(name, active_fragments=active_count,
                                          fragment_increment=8 * variant.fragment_samples)
            evidence = evaluate_sums(energy, fragments, envelope, variant)
            self.assertEqual(evidence.active_fragments[0], active_count)
            self.assertEqual(evidence.qualifying, expected)
        for bands, expected in (((0, 2, 4), False), ((0, 1, 2, 3), False), ((0, 1, 2, 4), True)):
            energy, fragments = self.sums(name, active_bands=bands)
            self.assertEqual(evaluate_sums(energy, fragments, envelope, variant).qualifying, expected)

    def test_threshold_arithmetic_above_int64_remains_exact_python_integer(self):
        variant = architecture()
        enormous = 1 << 80
        envelope = replace(self.envelopes["baseline_4m"], energy_q999=(enormous,) * 6,
                           activity_floor=(enormous,) * 6)
        boundary = 8 * enormous * variant.fragment_samples
        fragments = tuple(tuple(boundary + 1 for _ in range(6)) for _ in range(8))
        energy = tuple(8 * (boundary + 1) for _ in range(6))
        evidence = evaluate_sums(energy, fragments, envelope, variant)
        self.assertTrue(evidence.qualifying)
        self.assertEqual(evidence.energy_sums, energy)

    def test_domain_words_are_bounded_and_power_sums_are_certified(self):
        with self.assertRaisesRegex(ValueError, "sample_bits"):
            FixedDomain(1.0, "numerical fixture", 31)
        certificate = self.oracle().arithmetic_certificate()
        self.assertTrue(all(value <= (1 << 63) - 1 for value in certificate["maximum_block_energy_sums"]))
        self.assertFalse(certificate["hardware_qualification"])

    def test_real_integer_noise_requires_eight_blocks_and_is_always_passive(self):
        codes = np.random.default_rng(551).integers(-1024, 1025, 4096, dtype=np.int64)
        for name, first_start, first_end in (("baseline_4m", 256, 2304), ("multirate_2m", 382, 2430)):
            result = self.oracle(name).process(codes, start_index=0)
            self.assertEqual(result.blocks[0].source_start_index, first_start)
            self.assertTrue(all(block.qualifying for block in result.blocks[:8]))
            self.assertEqual([block.streak for block in result.blocks[:8]], list(range(1, 9)))
            self.assertEqual(result.decisions[0].source_end_index, first_end)
            self.assertAlmostEqual(result.decisions[0].timestamp_us, first_end / 4)
            self.assertFalse(result.permit)
            self.assertFalse(result.hardware_qualification)
            self.assertTrue(result.passive)

    def test_streaming_partition_and_nonzero_phase_are_exact(self):
        codes = np.random.default_rng(123).integers(-1024, 1025, 4096, dtype=np.int64)
        for name in self.envelopes:
            for start in (0, 1, 7):
                with self.subTest(name=name, start=start):
                    expected = self.oracle(name).process(codes, start_index=start)
                    streamed = self.oracle(name)
                    blocks, decisions, offset = [], [], 0
                    for length in (1, 3, 7, 127, 503, 997, 2458):
                        result = streamed.process(codes[offset:offset + length], start_index=start + offset)
                        offset += length
                        blocks.extend(result.blocks)
                        decisions.extend(result.decisions)
                        self.assertTrue(all(block.source_end_index <= start + offset for block in result.blocks))
                        self.assertTrue(all(decision.source_end_index <= start + offset for decision in result.decisions))
                    self.assertEqual(tuple(blocks), expected.blocks)
                    self.assertEqual(tuple(decisions), expected.decisions)

    def test_multirate_waits_for_complete_unsampled_source_end(self):
        oracle = self.oracle("multirate_2m")
        codes = np.random.default_rng(441).integers(-1024, 1025, 2430, dtype=np.int64)
        before = oracle.process(codes[:2429], start_index=0)
        self.assertEqual(len(before.blocks), 7)
        self.assertEqual(before.decisions, ())
        after = oracle.process(codes[2429:], start_index=2429)
        self.assertEqual(len(after.blocks), 1)
        self.assertEqual(after.blocks[0].source_end_index, 2430)
        self.assertEqual(after.decisions[0].source_end_index, 2430)

    def test_baseline_seven_blocks_are_insufficient_and_eighth_is_exact(self):
        oracle = self.oracle()
        codes = np.random.default_rng(551).integers(-1024, 1025, 2304, dtype=np.int64)
        before = oracle.process(codes[:2048], start_index=0)
        self.assertEqual(len(before.blocks), 7)
        self.assertEqual(before.blocks[-1].streak, 7)
        self.assertEqual(before.decisions, ())
        after = oracle.process(codes[2048:], start_index=2048)
        self.assertEqual(after.blocks[0].streak, 8)
        self.assertEqual(after.decisions[0].source_end_index, 2304)

    def test_single_impulse_and_interrupted_noise_cannot_inherit_a_streak(self):
        impulse = np.zeros(8192, dtype=np.int64)
        impulse[1000] = 1 << 22
        rng = np.random.default_rng(722)
        interrupted = rng.integers(-1024, 1025, 8192, dtype=np.int64)
        interrupted[1536:4096] = 0
        for name in self.envelopes:
            self.assertEqual(self.oracle(name).process(impulse, start_index=0).decisions, ())
            result = self.oracle(name).process(interrupted, start_index=0)
            self.assertTrue(result.decisions)
            self.assertTrue(all(decision.source_end_index > 4096 for decision in result.decisions))
            resumed = next(block for block in result.blocks if block.source_start_index >= 4096 and block.qualifying)
            self.assertEqual(resumed.streak, 1)
            quiet = [block for block in result.blocks if 2048 <= block.source_start_index < 3840]
            self.assertTrue(quiet)
            self.assertTrue(all(not block.qualifying and block.streak == 0 for block in quiet))

    def test_invalid_whole_chunk_preserves_state_and_reset_discards_history(self):
        codes = np.random.default_rng(512).integers(-1024, 1025, 4096, dtype=np.int64)
        for name in self.envelopes:
            expected = self.oracle(name).process(codes, start_index=0)
            oracle = self.oracle(name)
            bad = codes.copy()
            bad[-1] = 1 << 23
            with self.assertRaisesRegex(ValueError, "domain"):
                oracle.process(bad, start_index=0)
            actual = oracle.process(codes, start_index=0)
            self.assertEqual(actual, expected)
            self.assertEqual(oracle.process([], start_index=4096).blocks, ())
            with self.assertRaisesRegex(ValueError, "discontinuous"):
                oracle.process(codes[:1], start_index=4097)
            oracle.reset()
            self.assertEqual(oracle.process(codes, start_index=0), expected)
        with self.assertRaises(ValueError):
            self.oracle().process(np.zeros(32769, dtype=np.int64), start_index=0)
        with self.assertRaises(ValueError):
            self.oracle().process(np.zeros(1), start_index=0)

    def test_integer_energy_agrees_with_float_with_derived_rounding_bound(self):
        codes = np.random.default_rng(147).integers(-1024, 1025, 4096, dtype=np.int64)
        for name in self.envelopes:
            variant = architecture(name)
            signal = codes.astype(np.float64)
            if variant.prefix_coefficients:
                prefix = np.asarray(coefficient_codes(variant.prefix_coefficients)) / 2 ** 23
                signal = lfilter(prefix, [1.0], signal)[::2]
            matrix = np.column_stack([lfilter(np.asarray(coefficient_codes(coefficients)) / 2 ** 23,
                                             [1.0], signal) for coefficients in variant.bank_coefficients])
            result = self.oracle(name).process(codes, start_index=0)
            for block in result.blocks:
                first = block.source_start_index // variant.decimation
                floating = matrix[first:first + variant.block_samples]
                for band in range(6):
                    error = .5000001
                    if variant.prefix_coefficients:
                        l1 = sum(abs(value) for value in coefficient_codes(variant.bank_coefficients[band])) / 2 ** 23
                        error += .5 * l1
                    bound = float(np.sum(2 * np.abs(floating[:, band]) * error + error ** 2))
                    self.assertLessEqual(abs(block.energy_sums[band] - float(np.sum(floating[:, band] ** 2))), bound)


if __name__ == "__main__":
    unittest.main()
