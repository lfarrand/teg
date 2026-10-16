"""Bounded, bit-exact offline arithmetic for FPGA design comparisons.

This module has no detector health, qualification, permit or TEG output path.
Integers represent a declared physical LSB; FIR output rounding preserves that
LSB. Invalid inputs and unsafe formats raise before streaming state changes.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from fractions import Fraction
import math
from numbers import Integral, Real

import numpy as np

COEFFICIENT_BITS = 24
COEFFICIENT_FRACTION_BITS = 23
MAX_BLOCK_SAMPLES = 1_000_000
MAX_TAPS = 4096
MAX_SAMPLE_INDEX = (1 << 63) - 1


def _integer(value, name: str) -> int:
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, Integral):
        raise ValueError(f"{name} requires an integer")
    return int(value)


def _word_bits(value, name: str, maximum: int = 63) -> int:
    value = _integer(value, name)
    if not 2 <= value <= maximum:
        raise ValueError(f"{name} must be from two through {maximum}")
    return value


def _array(values, limit: int, name: str) -> np.ndarray:
    if not isinstance(values, (np.ndarray, list, tuple)):
        raise ValueError(f"{name} requires an array, list or tuple")
    if isinstance(values, np.ndarray):
        if values.ndim != 1 or len(values) > limit:
            raise ValueError(f"{name} requires a bounded one-dimensional vector")
    else:
        if len(values) > limit:
            raise ValueError(f"{name} exceeds its {limit}-element limit")
        if any(isinstance(value, (bool, np.bool_)) for value in values):
            raise ValueError(f"{name} must not contain booleans")
    try:
        result = np.asarray(values)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} requires a numeric vector") from exc
    if result.ndim != 1:
        raise ValueError(f"{name} requires a one-dimensional vector")
    return result


def _real_array(values, limit: int, name: str) -> np.ndarray:
    result = _array(values, limit, name)
    if result.dtype.kind not in "iuf" or (result.dtype.kind == "f" and result.dtype.itemsize > 8):
        raise ValueError(f"{name} requires real integer or IEEE floating-point values")
    if not np.isfinite(result).all():
        raise ValueError(f"{name} requires finite values")
    return result


def _code_array(values, bits: int, name: str = "codes") -> np.ndarray:
    result = _array(values, MAX_BLOCK_SAMPLES, name)
    # Empty Python sequences have a floating dtype but contain no invalid code.
    if not len(result):
        if isinstance(values, np.ndarray) and result.dtype.kind not in "iu":
            raise ValueError(f"{name} requires an integer array dtype")
        return np.empty(0, dtype=np.int64)
    if result.dtype.kind not in "iu":
        raise ValueError(f"{name} requires integer values")
    low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    if ((result.dtype.kind == "i" and np.any(result < low)) or np.any(result > high)):
        raise ValueError(f"{name} exceeds its signed {bits}-bit range")
    # Validate unsigned and wider values before conversion; never wrap uint64.
    return result.astype(np.int64, copy=True)


def _ratio(value) -> tuple[int, int]:
    if isinstance(value, Integral):
        return int(value), 1
    return float(value).as_integer_ratio()


def _round_ratio_even(numerator: int, denominator: int) -> int:
    quotient, remainder = divmod(abs(numerator), denominator)
    if remainder * 2 > denominator or (remainder * 2 == denominator and quotient & 1):
        quotient += 1
    return -quotient if numerator < 0 else quotient


def round_shift_even(integer, shift) -> int:
    """Exact signed division by 2**shift, rounding nearest with even ties."""
    value = _integer(integer, "integer")
    shift = _integer(shift, "shift")
    if shift < 0:
        raise ValueError("shift must be nonnegative")
    if shift == 0:
        return value
    if shift > abs(value).bit_length():
        return 0
    return _round_ratio_even(value, 1 << shift)


@dataclass(frozen=True)
class FixedDomain:
    """An explicitly chosen physical full scale; never inferred from samples.

    Encode accepts [-full_scale, full_scale), then rejects any rounding that
    leaves the signed code range. Decode uses sample_bits by default; specify
    word_bits for widened FIR outputs. A representable positive float64 LSB is
    required, and a non-finite widened physical decode is rejected.
    """

    full_scale: float
    unit_label: str
    sample_bits: int = 24

    def __post_init__(self) -> None:
        if isinstance(self.full_scale, (bool, np.bool_)) or not isinstance(self.full_scale, Real):
            raise ValueError("full_scale requires a positive finite real value")
        try:
            full_scale = float(self.full_scale)
        except OverflowError as exc:
            raise ValueError("full_scale exceeds float64") from exc
        if not math.isfinite(full_scale) or full_scale <= 0:
            raise ValueError("full_scale requires a positive finite value")
        bits = _word_bits(self.sample_bits, "sample_bits", 24)
        if not isinstance(self.unit_label, str) or not self.unit_label.strip():
            raise ValueError("unit_label requires an explicit nonempty identifier")
        if full_scale / (1 << (bits - 1)) == 0:
            raise ValueError("domain LSB underflows float64")
        object.__setattr__(self, "full_scale", full_scale)
        object.__setattr__(self, "sample_bits", bits)

    @property
    def units_per_code(self) -> float:
        return self.full_scale / (1 << (self.sample_bits - 1))

    def encode(self, values) -> np.ndarray:
        values = _real_array(values, MAX_BLOCK_SAMPLES, "physical samples")
        scale_num, scale_den = self.full_scale.as_integer_ratio()
        half_range = 1 << (self.sample_bits - 1)
        result = np.empty(len(values), dtype=np.int64)
        for index, value in enumerate(values):
            numerator, denominator = _ratio(value)
            numerator *= scale_den * half_range
            denominator *= scale_num
            if numerator < -half_range * denominator or numerator >= half_range * denominator:
                raise ValueError("physical sample lies outside the declared full-scale domain")
            code = _round_ratio_even(numerator, denominator)
            if not -half_range <= code < half_range:
                raise ValueError("physical sample rounds outside the declared signed code range")
            result[index] = code
        return result

    def decode(self, codes, word_bits=None) -> np.ndarray:
        bits = self.sample_bits if word_bits is None else _word_bits(word_bits, "word_bits")
        codes = _code_array(codes, bits)
        scale_num, scale_den = self.full_scale.as_integer_ratio()
        denominator = scale_den * (1 << (self.sample_bits - 1))
        result = np.empty(len(codes), dtype=np.float64)
        for index, code in enumerate(codes):
            try:
                value = float(Fraction(int(code) * scale_num, denominator))
            except OverflowError as exc:
                raise ValueError("decoded physical value exceeds float64") from exc
            if not math.isfinite(value):
                raise ValueError("decoded physical value must remain finite")
            result[index] = value
        return result

    def to_dict(self) -> dict[str, object]:
        return {"full_scale": self.full_scale, "unit_label": self.unit_label, "sample_bits": self.sample_bits}


def coefficient_codes(coefficients) -> tuple[int, ...]:
    """Quantize signed Q1.23 coefficients exactly; reject rather than clip."""
    values = _real_array(coefficients, MAX_TAPS, "coefficients")
    if not len(values):
        raise ValueError("coefficients must not be empty")
    scale = 1 << COEFFICIENT_FRACTION_BITS
    result = []
    for value in values:
        numerator, denominator = _ratio(value)
        if numerator < -denominator or numerator >= denominator:
            raise ValueError("coefficient lies outside signed Q1.23")
        code = _round_ratio_even(numerator * scale, denominator)
        if not -scale <= code < scale:
            raise ValueError("coefficient rounds outside signed Q1.23")
        result.append(code)
    return tuple(result)


@dataclass(frozen=True)
class FIRCertificate:
    input_bits: int
    output_bits: int
    accumulator_bits: int
    minimum_accumulator_bits: int
    minimum_output_bits: int
    l1_codes: int
    maximum_abs_accumulator: int
    maximum_abs_output: int
    taps: int
    symmetric: bool
    preadd_bits: int
    multiplications_per_output: int

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class FixedBlock:
    values: np.ndarray
    sample_indices: np.ndarray
    valid: np.ndarray


class FixedFIRDecimator:
    """Causal integer FIR, symmetric paired products and absolute /N phase.

    The conservative certificate assumes every input word can take its largest
    absolute value. Accumulation is exact signed int64; the proof is established
    with Python integers before NumPy arithmetic. One final Q1.23 rounding
    preserves the input physical LSB. No overflow or saturation is permissible.
    """

    def __init__(self, coefficients, factor=1, input_bits=24, accumulator_bits=64, output_bits=None):
        codes = coefficient_codes(coefficients)
        factor = _integer(factor, "factor")
        if not 1 <= factor <= MAX_BLOCK_SAMPLES:
            raise ValueError("factor requires a bounded positive integer")
        input_bits = _word_bits(input_bits, "input_bits")
        accumulator_bits = _word_bits(accumulator_bits, "accumulator_bits", 64)
        l1_codes = sum(abs(code) for code in codes)
        accumulator_bound = (1 << (input_bits - 1)) * l1_codes
        minimum_accumulator_bits = max(2, accumulator_bound.bit_length() + 1)
        if accumulator_bits < minimum_accumulator_bits:
            raise ValueError(f"unsafe accumulator: requires {minimum_accumulator_bits} signed bits")
        output_bound = round_shift_even(accumulator_bound, COEFFICIENT_FRACTION_BITS)
        minimum_output_bits = max(2, output_bound.bit_length() + 1)
        output_bits = minimum_output_bits if output_bits is None else _word_bits(output_bits, "output_bits")
        if not minimum_output_bits <= output_bits <= 63:
            raise ValueError(f"output requires at least {minimum_output_bits} and at most 63 signed bits")
        symmetric = codes == codes[::-1]
        self._factor = factor
        self._codes = codes
        self._certificate = FIRCertificate(input_bits, output_bits, accumulator_bits,
                                           minimum_accumulator_bits, minimum_output_bits,
                                           l1_codes, accumulator_bound, output_bound, len(codes),
                                           symmetric, input_bits + 1 if symmetric and len(codes) > 1 else input_bits,
                                           (len(codes) + 1) // 2 if symmetric else len(codes))
        self.reset()

    @property
    def factor(self) -> int:
        return self._factor

    @property
    def certificate(self) -> FIRCertificate:
        return self._certificate

    @property
    def coefficient_codes(self) -> tuple[int, ...]:
        return self._codes

    def reset(self) -> None:
        self._history = np.zeros(len(self._codes) - 1, dtype=np.int64)
        self._next_index: int | None = None
        self._first_index: int | None = None

    def process(self, codes, start_index) -> FixedBlock:
        start_index = _integer(start_index, "start_index")
        if not 0 <= start_index <= MAX_SAMPLE_INDEX:
            raise ValueError("start_index exceeds the supported nonnegative int64 clock")
        values = _code_array(codes, self._certificate.input_bits)
        if start_index + len(values) > MAX_SAMPLE_INDEX:
            raise ValueError("sample interval exceeds the int64 clock")
        if self._next_index is not None and start_index != self._next_index:
            raise ValueError("FIR input is discontinuous")
        if not len(values):
            return FixedBlock(np.empty(0, dtype=np.int64), np.empty(0, dtype=np.int64), np.empty(0, dtype=bool))
        first_index = start_index if self._first_index is None else self._first_index
        complete = np.concatenate((self._history, values))
        windows = np.lib.stride_tricks.sliding_window_view(complete, len(self._codes))
        first_offset = (-start_index) % self._factor
        selected = windows[first_offset::self._factor]
        accumulator = np.zeros(len(selected), dtype=np.int64)
        if self._certificate.symmetric:
            pairs = len(self._codes) // 2
            for index in range(pairs):
                accumulator += (selected[:, index] + selected[:, -index - 1]) * self._codes[index]
            if len(self._codes) % 2:
                accumulator += selected[:, pairs] * self._codes[pairs]
        else:
            for index, coefficient in enumerate(self._codes):
                accumulator += selected[:, -index - 1] * coefficient
        magnitude = np.abs(accumulator)
        quotient = magnitude >> COEFFICIENT_FRACTION_BITS
        remainder = magnitude & ((1 << COEFFICIENT_FRACTION_BITS) - 1)
        half = 1 << (COEFFICIENT_FRACTION_BITS - 1)
        quotient += (remainder > half) | ((remainder == half) & ((quotient & 1) != 0))
        output = np.where(accumulator < 0, -quotient, quotient)
        offsets = np.arange(first_offset, len(values), self._factor, dtype=np.int64)
        indices = offsets + start_index
        valid = indices - first_index >= len(self._codes) - 1
        # Commit only after every input, operation and output has been handled.
        self._history = complete[-(len(self._codes) - 1):].copy() if len(self._codes) > 1 else np.empty(0, dtype=np.int64)
        self._first_index = first_index
        self._next_index = start_index + len(values)
        return FixedBlock(output, indices, valid)
