"""Passive integer fast-tier experiments; never a hardware protection permit.

Healthy thresholds are specific to the architecture and declared numeric domain.
Quantiles use exact integer interpolation and a conservative ceiling to code².
They are intentionally not interchangeable with floating Profile.fast_envelopes.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from functools import cached_property, lru_cache
import hashlib
import json

import numpy as np
from scipy.signal import firwin

from .fixed_point import FixedDomain, FixedFIRDecimator, coefficient_codes
from .profile import FAST_BANDS

SOURCE_RATE = 4_000_000
MAX_PROCESS_SAMPLES = 32768
MAX_CALIBRATION_SAMPLES = 2_000_000


@dataclass(frozen=True)
class Architecture:
    name: str
    prefix_coefficients: tuple[float, ...]
    bank_coefficients: tuple[tuple[float, ...], ...]
    decimation: int
    block_samples: int

    @property
    def bank_rate_hz(self):
        return SOURCE_RATE // self.decimation

    @property
    def fragment_samples(self):
        return self.block_samples // 8

    @cached_property
    def signature(self):
        codes = [coefficient_codes(self.prefix_coefficients)] if self.prefix_coefficients else []
        codes.extend(coefficient_codes(coefficients) for coefficients in self.bank_coefficients)
        specification = (self.name, self.decimation, self.block_samples, codes)
        return hashlib.sha256(json.dumps(specification, separators=(",", ":")).encode()).hexdigest()

    def to_dict(self):
        return {"name": self.name, "signature": self.signature, "source_rate_hz": SOURCE_RATE,
                "bank_rate_hz": self.bank_rate_hz, "decimation": self.decimation,
                "prefix_taps": len(self.prefix_coefficients),
                "bank_taps": [len(coefficients) for coefficients in self.bank_coefficients],
                "block_samples": self.block_samples, "fragment_samples": self.fragment_samples,
                "source_block_samples": self.block_samples * self.decimation,
                "block_duration_us": 64, "hardware_qualification": False}


@lru_cache(maxsize=2)
def architecture(name: str = "baseline_4m") -> Architecture:
    if name == "baseline_4m":
        decimation, bank_taps, prefix = 1, 257, ()
    elif name == "multirate_2m":
        decimation, bank_taps = 2, 129
        prefix = tuple(firwin(127, 850_000, fs=SOURCE_RATE, window=("kaiser", 8.6)))
    else:
        raise ValueError("architecture must be baseline_4m or multirate_2m")
    coefficients = tuple(tuple(firwin(bank_taps, band, pass_zero=False,
                                     fs=SOURCE_RATE // decimation, window=("kaiser", 8.6)))
                         for band in FAST_BANDS)
    return Architecture(name, prefix, coefficients, decimation, 256 // decimation)


def _canonical_architecture(value):
    if not isinstance(value, Architecture) or value != architecture(value.name):
        raise ValueError("an unchanged named architecture is required")
    return value


def _domain_binding(domain):
    if not isinstance(domain, FixedDomain):
        raise ValueError("an explicit FixedDomain is required")
    # Full scale and unit label are both binding; no amplitude inference occurs.
    return json.dumps(domain.to_dict(), sort_keys=True, separators=(",", ":"), allow_nan=False)


def _codes(values, bits: int, maximum: int):
    codes = (np.empty(0, dtype=np.int64) if isinstance(values, (list, tuple)) and not values
             else np.asarray(values))
    if codes.ndim != 1 or codes.dtype.kind not in "iu" or codes.size > maximum:
        raise ValueError("codes must be a bounded one-dimensional integer vector")
    if np.any(codes < -(1 << (bits - 1))) or np.any(codes > (1 << (bits - 1)) - 1):
        raise ValueError("input code exceeds the declared signed domain")
    return codes.astype(np.int64, copy=False)


def _start_index(value, length):
    if type(value) is not int or not 0 <= value <= (1 << 63) - 1 - length:
        raise ValueError("source sample interval exceeds the nonnegative int64 clock")


@dataclass(frozen=True)
class FastOracleEnvelope:
    architecture_signature: str
    domain_binding: str
    energy_q999: tuple[int, ...]
    activity_floor: tuple[int, ...]
    quantization_floor: int
    verified_healthy: bool
    provenance: str
    calibration_blocks: int

    def __post_init__(self):
        for name in ("energy_q999", "activity_floor"):
            values = tuple(getattr(self, name))
            if len(values) != 6 or any(type(value) is not int or value <= 0 for value in values):
                raise ValueError("healthy mean-power thresholds require six positive integers")
            object.__setattr__(self, name, values)
        if type(self.quantization_floor) is not int or self.quantization_floor < 1:
            raise ValueError("a positive integer code-squared quantization floor is required")
        if any(value < self.quantization_floor for value in self.energy_q999 + self.activity_floor):
            raise ValueError("thresholds cannot underflow the declared quantization floor")
        if self.verified_healthy is not True or not isinstance(self.provenance, str) or not self.provenance.strip():
            raise ValueError("immutable healthy envelopes require explicit verified provenance")
        if type(self.calibration_blocks) is not int or self.calibration_blocks <= 8:
            raise ValueError("healthy calibration requires more than eight valid blocks")
        if not isinstance(self.architecture_signature, str) or not self.architecture_signature:
            raise ValueError("envelope requires an architecture binding")
        if not isinstance(self.domain_binding, str) or not self.domain_binding:
            raise ValueError("envelope requires a numeric-domain binding")

    def to_dict(self):
        return {"schema_version": 1, **asdict(self), "domain": json.loads(self.domain_binding),
                "threshold_units": "mean power in declared domain codes squared",
                "quantile_rule": "exact q999 linear interpolation, ceiling after division by sample count",
                "hardware_qualification": False, "passive": True}


@dataclass(frozen=True)
class BlockEvidence:
    energy_sums: tuple[int, ...]
    fragment_sums: tuple[tuple[int, ...], ...]
    active_fragments: tuple[int, ...]
    strong_subbands: tuple[bool, ...]
    qualifying: bool


def evaluate_sums(energy_sums, fragment_sums, envelope: FastOracleEnvelope,
                  variant: Architecture) -> BlockEvidence:
    variant = _canonical_architecture(variant)
    if not isinstance(envelope, FastOracleEnvelope) or envelope.architecture_signature != variant.signature:
        raise ValueError("block envelope does not match its architecture")
    energy = tuple(energy_sums)
    fragments = tuple(tuple(row) for row in fragment_sums)
    if len(energy) != 6 or len(fragments) != 8 or any(len(row) != 6 for row in fragments):
        raise ValueError("six band energies and eight by six fragment energies are required")
    if any(type(value) is not int or value < 0 for value in energy + tuple(value for row in fragments for value in row)):
        raise ValueError("energy sums must be nonnegative exact Python integers")
    if any(sum(row[band] for row in fragments) != energy[band] for band in range(6)):
        raise ValueError("fragment energies must sum to each band energy")
    active = tuple(sum(row[band] > 8 * envelope.activity_floor[band] * variant.fragment_samples
                       for row in fragments) for band in range(6))
    strong = tuple(energy[band] > 8 * envelope.energy_q999[band] * variant.block_samples and active[band] >= 6
                   for band in range(6))
    qualifies = sum(strong) >= 4 and all(any(strong[2 * octave:2 * octave + 2]) for octave in range(3))
    return BlockEvidence(energy, fragments, active, strong, bool(qualifies))


@dataclass(frozen=True)
class FastOracleBlock:
    source_start_index: int
    source_end_index: int
    energy_sums: tuple[int, ...]
    fragment_sums: tuple[tuple[int, ...], ...]
    active_fragments: tuple[int, ...]
    strong_subbands: tuple[bool, ...]
    qualifying: bool
    streak: int


@dataclass(frozen=True)
class CandidateDecision:
    source_end_index: int
    timestamp_us: float
    reason: str = "passive_fast_eight_blocks"


@dataclass(frozen=True)
class FastOracleResult:
    blocks: tuple[FastOracleBlock, ...]
    decisions: tuple[CandidateDecision, ...]
    permit: bool = False
    hardware_qualification: bool = False
    passive: bool = True


class _Bank:
    """Causal fixed FIR bank with full cascade settling and source-span guarding."""
    def __init__(self, variant: Architecture, domain: FixedDomain):
        self.variant, self.domain = _canonical_architecture(variant), domain
        _domain_binding(domain)
        self.prefix = (FixedFIRDecimator(variant.prefix_coefficients, factor=2,
                                        input_bits=domain.sample_bits, accumulator_bits=64)
                       if variant.prefix_coefficients else None)
        bank_input_bits = self.prefix.certificate.output_bits if self.prefix else domain.sample_bits
        self.filters = tuple(FixedFIRDecimator(coefficients, input_bits=bank_input_bits, accumulator_bits=64)
                             for coefficients in variant.bank_coefficients)
        self.power_bounds = tuple((1 << (filtered.certificate.output_bits - 1)) ** 2 * variant.block_samples
                                  for filtered in self.filters)
        if any(bound > (1 << 63) - 1 for bound in self.power_bounds):
            raise ValueError("declared domain cannot certify signed-int64 block power sums")
        self.reset()

    def reset(self):
        if self.prefix:
            self.prefix.reset()
        for filtered in self.filters:
            filtered.reset()
        self._next_index = None
        self._prefix_valid_after = None
        self._buffer = np.empty((0, 6), dtype=np.int64)
        self._indices = np.empty(0, dtype=np.int64)
        self._pending = []

    def process(self, values, start_index):
        codes = _codes(values, self.domain.sample_bits, MAX_PROCESS_SAMPLES)
        _start_index(start_index, codes.size)
        if self._next_index is not None and start_index != self._next_index:
            raise ValueError("source stream is discontinuous; explicit reset is required")
        if not codes.size:
            return ()
        # The entire input has been validated before any filter state changes.
        end = start_index + codes.size
        bank_codes, bank_start = codes, start_index
        if self.prefix:
            prefixed = self.prefix.process(codes, start_index=start_index)
            bank_codes = prefixed.values
            if bank_codes.size:
                bank_start = int(prefixed.sample_indices[0]) // 2
                first_valid = np.flatnonzero(prefixed.valid)
                if self._prefix_valid_after is None and first_valid.size:
                    self._prefix_valid_after = int(prefixed.sample_indices[first_valid[0]]) // 2
        if bank_codes.size:
            outputs = [filtered.process(bank_codes, start_index=bank_start) for filtered in self.filters]
            indices = outputs[0].sample_indices
            valid = outputs[0].valid.copy()
            if self.prefix:
                if self._prefix_valid_after is None:
                    valid[:] = False
                else:
                    valid &= indices >= self._prefix_valid_after + len(self.variant.bank_coefficients[0]) - 1
            matrix = np.column_stack([output.values for output in outputs])[valid]
            indices = indices[valid]
            self._buffer = np.concatenate((self._buffer, matrix))
            self._indices = np.concatenate((self._indices, indices))
            size = self.variant.block_samples
            while self._buffer.shape[0] >= size:
                block = self._buffer[:size]
                bank_indices = self._indices[:size]
                fragments = []
                for fragment in block.reshape(8, self.variant.fragment_samples, 6):
                    fragments.append(tuple(sum(int(value) ** 2 for value in fragment[:, band]) for band in range(6)))
                fragment_sums = tuple(fragments)
                energies = tuple(sum(row[band] for row in fragment_sums) for band in range(6))
                source_start = int(bank_indices[0]) * self.variant.decimation
                source_end = (int(bank_indices[-1]) + 1) * self.variant.decimation
                self._pending.append((source_start, source_end, energies, fragment_sums))
                self._buffer = self._buffer[size:]
                self._indices = self._indices[size:]
        ready = tuple(block for block in self._pending if block[1] <= end)
        self._pending = [block for block in self._pending if block[1] > end]
        self._next_index = end
        return ready


class FixedFastOracle:
    def __init__(self, variant: Architecture, domain: FixedDomain, envelope: FastOracleEnvelope):
        self.architecture = _canonical_architecture(variant)
        self.domain = domain
        if not isinstance(envelope, FastOracleEnvelope) or envelope.architecture_signature != variant.signature or envelope.domain_binding != _domain_binding(domain):
            raise ValueError("healthy envelope must match the exact architecture and numeric domain")
        self.envelope = envelope
        self._bank = _Bank(variant, domain)
        self._streak = 0

    def reset(self):
        self._bank.reset()
        self._streak = 0

    def arithmetic_certificate(self):
        return {"prefix": self._bank.prefix.certificate.to_dict() if self._bank.prefix else None,
                "banks": [filtered.certificate.to_dict() for filtered in self._bank.filters],
                "maximum_block_energy_sums": list(self._bank.power_bounds),
                "power_sum_word_bits": 64, "hardware_qualification": False}

    def process(self, codes, *, start_index):
        blocks, decisions = [], []
        for first, end, energy, fragments in self._bank.process(codes, start_index):
            evidence = evaluate_sums(energy, fragments, self.envelope, self.architecture)
            self._streak = self._streak + 1 if evidence.qualifying else 0
            blocks.append(FastOracleBlock(first, end, energy, fragments, evidence.active_fragments,
                                          evidence.strong_subbands, evidence.qualifying, self._streak))
            if self._streak == 8:
                decisions.append(CandidateDecision(end, end / SOURCE_RATE * 1_000_000))
        return FastOracleResult(tuple(blocks), tuple(decisions))


def _ceil_quantile_mean(values, denominator):
    ordered = sorted(int(value) for value in values)
    if not ordered or denominator <= 0:
        raise ValueError("a nonempty integer quantile and positive divisor are required")
    position, fraction = divmod(999 * (len(ordered) - 1), 1000)
    next_position = min(position + 1, len(ordered) - 1)
    numerator = ordered[position] * (1000 - fraction) + ordered[next_position] * fraction
    divisor = 1000 * denominator
    return (numerator + divisor - 1) // divisor


def calibrate_fast(codes, *, architecture: Architecture, domain: FixedDomain,
                   verified_healthy: bool, provenance: str, start_index: int = 0,
                   quantization_floor: int = 1) -> FastOracleEnvelope:
    variant = _canonical_architecture(architecture)
    binding = _domain_binding(domain)
    if verified_healthy is not True or not isinstance(provenance, str) or not provenance.strip():
        raise ValueError("explicit verified healthy provenance is required before fitting")
    if type(quantization_floor) is not int or quantization_floor < 1:
        raise ValueError("a positive integer code-squared quantization floor is required")
    values = _codes(codes, domain.sample_bits, MAX_CALIBRATION_SAMPLES)
    _start_index(start_index, values.size)
    if not values.size:
        raise ValueError("calibration cannot use an empty recording")
    bank = _Bank(variant, domain)
    energy_samples = [[] for _ in range(6)]
    activity_samples = [[] for _ in range(6)]
    count = 0
    for offset in range(0, values.size, 8192):
        for _, _, energy, fragments in bank.process(values[offset:offset + 8192], start_index + offset):
            count += 1
            for band in range(6):
                energy_samples[band].append(energy[band])
                activity_samples[band].extend(row[band] for row in fragments)
    if count <= 8:
        raise ValueError("healthy calibration requires more than eight fully settled blocks")
    energy = tuple(max(quantization_floor, _ceil_quantile_mean(values, variant.block_samples)) for values in energy_samples)
    activity = tuple(max(quantization_floor, _ceil_quantile_mean(values, variant.fragment_samples)) for values in activity_samples)
    return FastOracleEnvelope(variant.signature, binding, energy, activity, quantization_floor,
                              True, provenance, count)
