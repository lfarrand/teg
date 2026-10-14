import dataclasses
from fractions import Fraction
import math
import unittest

import numpy as np

from arc_detector.fixed_point import (COEFFICIENT_FRACTION_BITS, MAX_BLOCK_SAMPLES,
                                      MAX_SAMPLE_INDEX, MAX_TAPS, FixedDomain,
                                      FixedFIRDecimator, coefficient_codes,
                                      round_shift_even)
from arc_detector.signal import default_filters, FastFilterBank


def scalar_codes(coefficients):
    """Independent standard-library quantizer, never production helper code."""
    return tuple(round(Fraction(float(value)) * (1 << 23)) for value in coefficients)


def scalar_convolution(samples, coefficients, factor=1, start_index=0):
    taps = scalar_codes(coefficients)
    values, indices, valid = [], [], []
    for offset in range(len(samples)):
        index = start_index + offset
        if index % factor:
            continue
        total = sum(int(samples[offset - lag]) * coefficient
                    for lag, coefficient in enumerate(taps) if offset >= lag)
        values.append(round(Fraction(total, 1 << 23)))
        indices.append(index)
        valid.append(offset >= len(taps) - 1)
    return values, indices, valid


class FixedDomainTests(unittest.TestCase):
    def test_explicit_units_and_frozen_serializable_domain(self):
        domain = FixedDomain(131072, "low-gain-equivalent ADC count")
        self.assertEqual(domain.units_per_code, 1 / 64)
        self.assertEqual(FixedDomain(**domain.to_dict()), domain)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            domain.full_scale = 1.0

    def test_exact_nonbinary_scale_rounding_and_signed_ties(self):
        domain = FixedDomain(0.1, "A", sample_bits=8)
        samples = np.array([-0.093, -0.0013, 0, 0.0067, 0.098])
        expected = [round(Fraction(float(value)) / Fraction(domain.full_scale) * 128) for value in samples]
        np.testing.assert_array_equal(domain.encode(samples), expected)
        binary = FixedDomain(1.0, "A", sample_bits=4)
        np.testing.assert_array_equal(binary.encode(np.array([-5, -3, -1, 1, 3, 5]) / 16), [-2, -2, 0, 0, 2, 2])

    def test_signed_endpoints_and_rounded_endpoint_rejected(self):
        domain = FixedDomain(1.0, "A", sample_bits=4)
        np.testing.assert_array_equal(domain.encode([-1.0, 0.875]), [-8, 7])
        for value in (1.0, math.nextafter(-1.0, -math.inf), 0.9375):
            with self.subTest(value=value), self.assertRaises(ValueError):
                domain.encode([value])

    def test_widened_decode_keeps_original_physical_lsb(self):
        domain = FixedDomain(0.1, "A", sample_bits=8)
        codes = np.array([-400, 0, 513], dtype=np.int64)
        with self.assertRaises(ValueError):
            domain.decode(codes)
        expected = [float(Fraction(int(code)) * Fraction(domain.full_scale) / 128) for code in codes]
        np.testing.assert_array_equal(domain.decode(codes, word_bits=12), expected)
        self.assertEqual(domain.encode([]).dtype, np.int64)
        self.assertEqual(domain.decode([]).dtype, np.float64)

    def test_invalid_domains_and_numeric_inputs_rejected(self):
        for scale in (True, 0, -1, float("nan"), float("inf"), 1 << 2000,
                      math.nextafter(0.0, 1.0)):
            with self.subTest(scale=scale), self.assertRaises(ValueError):
                FixedDomain(scale, "A")
        for bits in (1, 25, True, 8.0):
            with self.subTest(bits=bits), self.assertRaises(ValueError):
                FixedDomain(1.0, "A", bits)
        with self.assertRaises(ValueError):
            FixedDomain(1.0, "  ")
        domain = FixedDomain(1.0, "A")
        for values in ([float("nan")], [float("inf")], [True, 0.5], [1j], [[0.5]], ["0.5"]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                domain.encode(values)
        for codes in ([0.0], [True], np.array([2 ** 63], dtype=np.uint64)):
            with self.subTest(codes=codes), self.assertRaises(ValueError):
                domain.decode(codes)
        with self.assertRaises(ValueError):
            FixedDomain(1e308, "A", sample_bits=2).decode([4], word_bits=4)


class IntegerMathTests(unittest.TestCase):
    def test_round_shift_signed_ties_matches_fraction_oracle(self):
        for shift in range(9):
            for value in range(-257, 258):
                self.assertEqual(round_shift_even(value, shift), round(Fraction(value, 1 << shift)))
        self.assertEqual(round_shift_even(-((1 << 100) + 1), 99), -2)
        self.assertEqual(round_shift_even(1, 1 << 100), 0)
        for value, shift in ((True, 1), (1.0, 1), (1, -1), (1, True)):
            with self.subTest(value=value, shift=shift), self.assertRaises(ValueError):
                round_shift_even(value, shift)

    def test_coefficient_exact_quantization_and_endpoint_policy(self):
        values = [-1, -3 / (1 << 24), -1 / (1 << 24), 0, 1 / (1 << 24), 3 / (1 << 24), 0.75]
        self.assertEqual(coefficient_codes(values), scalar_codes(values))
        for coefficients in ([], [1.0], [math.nextafter(1.0, 0.0)], [True], [float("nan")], [1j]):
            with self.subTest(coefficients=coefficients), self.assertRaises(ValueError):
                coefficient_codes(coefficients)


class FixedFIRTests(unittest.TestCase):
    def test_symmetry_pairing_and_nonsymmetric_fallback_match_scalar_convolution(self):
        samples = np.random.default_rng(81).integers(-32768, 32768, size=311, dtype=np.int64)
        filters = ((0.5,), (-0.125, 0.375, -0.125), (0.125, -0.5, -0.5, 0.125),
                   (0.5, 0.25, -0.75))
        for coefficients in filters:
            with self.subTest(coefficients=coefficients):
                fir = FixedFIRDecimator(coefficients, input_bits=16)
                actual = fir.process(samples, 19)
                expected = scalar_convolution(samples, coefficients, start_index=19)
                np.testing.assert_array_equal(actual.values, expected[0])
                np.testing.assert_array_equal(actual.sample_indices, expected[1])
                np.testing.assert_array_equal(actual.valid, expected[2])
                symmetric = coefficients == coefficients[::-1]
                self.assertEqual(fir.certificate.symmetric, symmetric)
                self.assertEqual(fir.certificate.multiplications_per_output,
                                 (len(coefficients) + 1) // 2 if symmetric else len(coefficients))
                self.assertEqual(fir.certificate.preadd_bits,
                                 17 if symmetric and len(coefficients) > 1 else 16)

    def test_default_filters_and_adversarial_signed_extrema_use_proven_word_lengths(self):
        configurations = ((default_filters()[0], 24, 48, 25),
                          (default_filters()[1], 25, 49, 26),
                          (default_filters()[1], 33, 57, 34),
                          (FastFilterBank().coefficients[-1], 25, 49, 26))
        for coefficients, input_bits, accumulator_bits, output_bits in configurations:
            with self.subTest(input_bits=input_bits, taps=len(coefficients)):
                fir = FixedFIRDecimator(coefficients, input_bits=input_bits, accumulator_bits=accumulator_bits)
                certificate = fir.certificate
                self.assertEqual(certificate.minimum_accumulator_bits, accumulator_bits)
                self.assertEqual(certificate.output_bits, output_bits)
                self.assertEqual(certificate.l1_codes, sum(abs(code) for code in scalar_codes(coefficients)))
                maximum = (1 << (input_bits - 1)) - 1
                minimum = -(1 << (input_bits - 1))
                samples = np.array([minimum if code < 0 else maximum
                                    for code in scalar_codes(coefficients)[::-1]], dtype=np.int64)
                actual = fir.process(samples, 0)
                np.testing.assert_array_equal(actual.values, scalar_convolution(samples, coefficients)[0])
                self.assertLessEqual(int(abs(actual.values[-1])), certificate.maximum_abs_output)
                with self.assertRaises(ValueError):
                    FixedFIRDecimator(coefficients, input_bits=input_bits, accumulator_bits=accumulator_bits - 1)
                with self.assertRaises(ValueError):
                    FixedFIRDecimator(coefficients, input_bits=input_bits, output_bits=output_bits - 1)
                with self.assertRaises(dataclasses.FrozenInstanceError):
                    certificate.output_bits = 2
                self.assertEqual(certificate.to_dict()["input_bits"], input_bits)

    def test_int64_boundary_arithmetic_does_not_wrap(self):
        fir = FixedFIRDecimator((1 / (1 << 23),), input_bits=63, accumulator_bits=64)
        samples = np.array([-(1 << 62), (1 << 62) - 1], dtype=np.int64)
        actual = fir.process(samples, MAX_SAMPLE_INDEX - 2)
        np.testing.assert_array_equal(actual.values, scalar_convolution(samples, (1 / (1 << 23),))[0])
        np.testing.assert_array_equal(actual.sample_indices, [MAX_SAMPLE_INDEX - 2, MAX_SAMPLE_INDEX - 1])
        with self.assertRaises(ValueError):
            FixedFIRDecimator((1.0 - 1 / (1 << 23),), input_bits=63)

    def test_absolute_phase_and_random_chunk_partition_invariance(self):
        rng = np.random.default_rng(29)
        samples = rng.integers(-(1 << 23), 1 << 23, size=7003, dtype=np.int64)
        for coefficients, factor in ((default_filters()[0], 5), (default_filters()[1], 8)):
            with self.subTest(factor=factor):
                complete = FixedFIRDecimator(coefficients, factor).process(samples, 19)
                expected = scalar_convolution(samples, coefficients, factor, 19)
                np.testing.assert_array_equal(complete.values, expected[0])
                streamed = FixedFIRDecimator(coefficients, factor)
                values, indices, valid = [], [], []
                offset = 0
                while offset < len(samples):
                    count = min(int(rng.integers(1, 281)), len(samples) - offset)
                    part = streamed.process(samples[offset:offset + count], 19 + offset)
                    values.extend(part.values)
                    indices.extend(part.sample_indices)
                    valid.extend(part.valid)
                    offset += count
                np.testing.assert_array_equal(values, complete.values)
                np.testing.assert_array_equal(indices, complete.sample_indices)
                np.testing.assert_array_equal(valid, complete.valid)

    def test_complete_fir_history_empty_reset_and_no_output_intervals(self):
        fir = FixedFIRDecimator((0.25, 0.5, 0.25), factor=5, input_bits=8)
        empty = fir.process([], MAX_SAMPLE_INDEX)
        self.assertFalse(len(empty.values))
        self.assertEqual(empty.values.dtype, np.int64)
        first = fir.process([7], 1)
        self.assertFalse(len(first.values))
        second = fir.process([8, 9, 10, 11], 2)
        self.assertEqual(second.sample_indices.tolist(), [5])
        self.assertEqual(second.valid.tolist(), [True])
        self.assertFalse(len(fir.process([], 6).values))
        fir.reset()
        fresh = fir.process([7], 5)
        self.assertEqual(fresh.valid.tolist(), [False])

    def test_rejected_full_blocks_leave_state_and_clock_unchanged(self):
        coefficients = (0.125, 0.75, 0.125)
        fir = FixedFIRDecimator(coefficients, input_bits=8)
        baseline = FixedFIRDecimator(coefficients, input_bits=8)
        fir.process([1, 2, 3], 0)
        baseline.process([1, 2, 3], 0)
        bad_calls = (([4, 128], 3), ([4, -129], 3), ([4.0], 3), ([True, 4], 3),
                     (np.array([4, 2 ** 63], dtype=np.uint64), 3), ([[4]], 3),
                     ([4], -1), ([4], 4), ([4], True), ([4], MAX_SAMPLE_INDEX),
                     (np.zeros(MAX_BLOCK_SAMPLES + 1, dtype=np.int64), 3))
        for samples, start in bad_calls:
            with self.subTest(start=start, dtype=type(samples)), self.assertRaises(ValueError):
                fir.process(samples, start)
        actual = fir.process([4, 5, 6], 3)
        expected = baseline.process([4, 5, 6], 3)
        np.testing.assert_array_equal(actual.values, expected.values)
        np.testing.assert_array_equal(actual.sample_indices, expected.sample_indices)
        np.testing.assert_array_equal(actual.valid, expected.valid)

    def test_format_and_coefficient_limits_rejected(self):
        cases = ({"factor": 0}, {"factor": True}, {"input_bits": 1}, {"input_bits": 64},
                 {"accumulator_bits": 65}, {"output_bits": 64})
        for keywords in cases:
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                FixedFIRDecimator((0.5,), **keywords)
        with self.assertRaises(ValueError):
            FixedFIRDecimator([0.0] * (MAX_TAPS + 1))
        for samples in (np.empty(0, dtype=float), np.empty(0, dtype=bool)):
            with self.subTest(dtype=samples.dtype), self.assertRaises(ValueError):
                FixedFIRDecimator((0.5,)).process(samples, 0)

    def test_declared_lsb_error_bound_against_floating_convolution(self):
        domain = FixedDomain(16.0, "A")
        samples = np.random.default_rng(72).uniform(-12, 12, size=3000)
        coefficients = default_filters()[0]
        fir = FixedFIRDecimator(coefficients, factor=5)
        block = fir.process(domain.encode(samples), 0)
        actual = domain.decode(block.values, word_bits=fir.certificate.output_bits)
        expected = np.convolve(samples, coefficients)[:len(samples)][block.sample_indices]
        # Quantizing input contributes L1 * half an LSB. One final exact MAC
        # rounding contributes another half LSB; coefficients match exactly.
        error_bound = domain.units_per_code * (float(np.abs(coefficients).sum()) + 1) / 2
        self.assertLessEqual(float(np.max(np.abs(actual - expected))), error_bound + 1e-12)


if __name__ == "__main__":
    unittest.main()
