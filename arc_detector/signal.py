"""Streaming sample-domain DSP shared by the detector and offline calibration.

This floating-point implementation is a replay oracle, not FPGA RTL or a
real-time deadline claim. FIR coefficients are quantised to signed Q1.23;
fixed-point accumulator sizing remains an FPGA implementation obligation.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import math
from typing import Iterable
from types import MappingProxyType

import numpy as np
from scipy import signal as scipy_signal

from .profile import (ACQUISITION_RATE, DETECTION_RATE, FAST_BANDS, FFT_LENGTH,
                      SPECTRAL_BANDS, SPECTRAL_RATE, EdgeTemplate, FastEnvelope,
                      SpectralEnvelope, SwitchingEdge)


def sample_vector(samples: Iterable[float]) -> np.ndarray:
    values = np.asarray(samples, dtype=np.float64)
    if values.ndim != 1 or not np.isfinite(values).all():
        raise ValueError("samples require a finite one-dimensional vector")
    return values


def _index(start_index: int) -> None:
    if type(start_index) is not int or start_index < 0:
        raise ValueError("start_index requires a nonnegative integer")


def quantise_coefficients(values: Iterable[float], fractional_bits: int = 23) -> np.ndarray:
    values = sample_vector(values)
    if type(fractional_bits) is not int or not 1 <= fractional_bits <= 30:
        raise ValueError("fractional_bits must be from one through 30")
    scale = 2 ** fractional_bits
    if np.any(values < -1) or np.any(values >= 1):
        raise ValueError("FIR coefficients exceed signed Q1 range")
    result = np.rint(values * scale) / scale
    result.setflags(write=False)
    return result


@lru_cache(maxsize=1)
def default_filters() -> tuple[np.ndarray, np.ndarray]:
    """127-tap 20→4 MSPS and 255-tap 4→0.5 MSPS antialias filters."""
    first = scipy_signal.firwin(127, 1_350_000, fs=ACQUISITION_RATE, window=("kaiser", 8.6))
    second = scipy_signal.firwin(255, 175_000, fs=DETECTION_RATE, window=("kaiser", 8.6))
    return quantise_coefficients(first), quantise_coefficients(second)


def _response(coefficients: np.ndarray, rate: int, passband: tuple[int, int], stop_start: int) -> dict[str, float]:
    frequencies, response = scipy_signal.freqz(coefficients, worN=131072, fs=rate)
    magnitude = np.abs(response)
    pb = magnitude[(frequencies >= passband[0]) & (frequencies <= passband[1])]
    stop = magnitude[frequencies >= stop_start]
    return {"passband_min_db": float(20 * np.log10(max(pb.min(), np.finfo(float).tiny))),
            "passband_max_db": float(20 * np.log10(max(pb.max(), np.finfo(float).tiny))),
            "worst_stopband_db": float(20 * np.log10(max(stop.max(), np.finfo(float).tiny))),
            "group_delay_us": (len(coefficients) - 1) / (2 * rate) * 1e6,
            "tap_count": len(coefficients), "coefficient_fractional_bits": 23}


def filter_report() -> dict[str, object]:
    """Measured dense-grid responses of the actual quantised coefficient set."""
    first, second = default_filters()
    stage1 = _response(first, ACQUISITION_RATE, (10_000, 700_000), 2_000_000)
    stage2 = _response(second, DETECTION_RATE, (10_000, 100_000), 250_000)
    return {"stage_20m_to_4m": stage1, "stage_4m_to_500k": stage2,
            "combined_group_delay_us": stage1["group_delay_us"] + stage2["group_delay_us"],
            "arithmetic": "Q1.23 coefficients, float64 replay accumulators; not a fixed-point overflow proof"}


@dataclass(frozen=True)
class DecimatedSamples:
    values: np.ndarray
    sample_indices: np.ndarray
    valid: np.ndarray


class StreamingFIRDecimator:
    """Causal FIR with absolute decimation phase and chunk-invariant state."""

    def __init__(self, coefficients: Iterable[float], factor: int):
        coefficients = sample_vector(coefficients).copy()
        if not len(coefficients) or type(factor) is not int or factor < 1:
            raise ValueError("nonempty coefficients and a positive integer factor are required")
        self.coefficients = coefficients
        self.coefficients.setflags(write=False)
        self.factor = factor
        self.reset()

    def reset(self) -> None:
        self._history = np.zeros(len(self.coefficients) - 1)
        self._next_index: int | None = None
        self._first_index: int | None = None

    def process(self, samples: Iterable[float], start_index: int) -> DecimatedSamples:
        _index(start_index)
        values = sample_vector(samples)
        if self._next_index is not None and start_index != self._next_index:
            raise ValueError("FIR input is discontinuous")
        if self._first_index is None:
            self._first_index = start_index
        if not len(values):
            return DecimatedSamples(np.empty(0), np.empty(0, dtype=np.int64), np.empty(0, dtype=bool))
        offsets = np.arange((-start_index) % self.factor, len(values), self.factor, dtype=np.int64)
        indices = offsets + start_index
        # upfirdn uses a polyphase kernel and calculates only the retained
        # output phase. Prepend raw history and at most factor-1 alignment zeros
        # so the selected phase is tied to the absolute acquisition clock.
        history_length = len(self._history)
        padding = (start_index - history_length) % self.factor
        complete = np.concatenate((np.zeros(padding), self._history, values))
        filtered = scipy_signal.upfirdn(self.coefficients, complete, down=self.factor)
        first = (padding + history_length + ((-start_index) % self.factor)) // self.factor
        selected = filtered[first:first + len(offsets)]
        if history_length:
            self._history = complete[-history_length:].copy()
        valid = indices >= self._first_index + len(self.coefficients) - 1
        self._next_index = start_index + len(values)
        return DecimatedSamples(selected, indices, valid)


class FrozenPredictor:
    """Sum immutable edge responses with fractional acquisition-delay alignment."""

    def __init__(self, templates: Iterable[EdgeTemplate], edge_delay_samples: float = 12.6):
        templates = tuple(templates)
        if not math.isfinite(edge_delay_samples) or edge_delay_samples < 0:
            raise ValueError("edge delay must be finite and nonnegative")
        if len({x.channel for x in templates}) != len(templates):
            raise ValueError("duplicate edge-template channels")
        kernels = {x.channel: (np.asarray(x.rising), np.asarray(x.falling)) for x in templates}
        for pair in kernels.values():
            for kernel in pair:
                kernel.setflags(write=False)
        self.templates = MappingProxyType(kernels)
        self.delay = edge_delay_samples
        self.reset()

    def reset(self) -> None:
        self._next_index: int | None = None
        self._tail = np.zeros(0)

    def process(self, length: int, start_index: int, edges: Iterable[SwitchingEdge]) -> np.ndarray:
        _index(start_index)
        if type(length) is not int or length < 0:
            raise ValueError("length must be nonnegative")
        if self._next_index is not None and start_index != self._next_index:
            raise ValueError("prediction input is discontinuous")
        edges = tuple(edges)
        if any(not isinstance(edge, SwitchingEdge) for edge in edges):
            raise ValueError("edges require SwitchingEdge objects")
        if any(edges[i].sample_index > edges[i + 1].sample_index for i in range(len(edges) - 1)):
            raise ValueError("edges must be chronological")
        if any(not start_index <= edge.sample_index < start_index + length for edge in edges):
            raise ValueError("each edge must be in this sample interval")
        if any(edge.channel not in self.templates for edge in edges):
            raise ValueError("edge has no frozen template")
        total = np.zeros(max(length + 514 + math.ceil(self.delay), len(self._tail)))
        total[:len(self._tail)] += self._tail
        for edge in edges:
            kernel = self.templates[edge.channel][0 if edge.rising else 1]
            relative = edge.sample_index + self.delay - start_index
            offset = math.floor(relative)
            fraction = relative - offset
            total[offset:offset + len(kernel)] += (1 - fraction) * kernel
            total[offset + 1:offset + len(kernel) + 1] += fraction * kernel
        self._tail = total[length:]
        # Bound memory independent of run time and partitioning.
        last = np.flatnonzero(self._tail)
        self._tail = self._tail[:int(last[-1]) + 1] if len(last) else np.zeros(0)
        self._next_index = start_index + length
        return total[:length]


def spectral_bin_indices(excluded_bins: Iterable[int] = ()) -> tuple[np.ndarray, ...]:
    frequencies = np.fft.rfftfreq(FFT_LENGTH, 1 / SPECTRAL_RATE)
    excluded = set(excluded_bins)
    return tuple(np.asarray([i for i, frequency in enumerate(frequencies)
                             if low <= frequency < high and i not in excluded], dtype=np.int64)
                 for low, high in SPECTRAL_BANDS)


def spectral_psd(frame: Iterable[float]) -> np.ndarray:
    frame = sample_vector(frame)
    if len(frame) != FFT_LENGTH:
        raise ValueError("spectral frames require exactly 1024 samples")
    window = np.hanning(FFT_LENGTH)
    centred = frame - frame.mean()
    transform = np.fft.rfft(centred * window)
    psd = np.abs(transform) ** 2 / (SPECTRAL_RATE * np.dot(window, window))
    psd[1:-1] *= 2
    return psd


@dataclass(frozen=True)
class BandEvidence:
    energy: float
    occupancy: float
    flatness: float
    relative_change: float
    usable_bins: int
    total_bins: int
    available: bool
    weak: bool
    strong: bool


def spectral_features(frame: Iterable[float], envelope: SpectralEnvelope,
                      previous_psd: np.ndarray | None = None) -> tuple[BandEvidence, ...]:
    return features_from_psd(spectral_psd(frame), envelope, previous_psd)


def features_from_psd(psd: np.ndarray, envelope: SpectralEnvelope,
                      previous_psd: np.ndarray | None = None) -> tuple[BandEvidence, ...]:
    psd = sample_vector(psd)
    if len(psd) != 513 or np.any(psd < 0):
        raise ValueError("PSD requires 513 nonnegative values")
    if previous_psd is not None:
        previous_psd = sample_vector(previous_psd)
        if len(previous_psd) != 513 or np.any(previous_psd < 0):
            raise ValueError("previous PSD requires 513 nonnegative values")
    full_bins = spectral_bin_indices()
    bands = spectral_bin_indices(envelope.excluded_bins)
    floor = np.asarray(envelope.psd_q95)
    result = []
    for band, indices in enumerate(bands):
        values = psd[indices]
        available = len(indices) >= math.ceil(0.6 * len(full_bins[band]))
        energy = float(values.sum() * SPECTRAL_RATE / FFT_LENGTH)
        occupancy = float(np.mean(values > floor[indices])) if len(values) else 0.0
        mean = float(values.mean()) if len(values) else 0.0
        flatness = float(np.exp(np.log(np.maximum(values, np.finfo(float).tiny)).mean()) / mean) if mean > 0 else 0.0
        change = 0.0
        if previous_psd is not None and len(indices):
            before = previous_psd[indices]
            change = float(np.abs(values - before).sum() / max(float(before.sum()), np.finfo(float).tiny))
        result.append(BandEvidence(energy, occupancy, flatness, change, len(indices), len(full_bins[band]), available,
                                   available and energy > 2 * envelope.energy_q999[band] and occupancy >= 0.20,
                                   available and energy > 8 * envelope.energy_q999[band] and occupancy >= 0.35))
    return tuple(result)


class FastFilterBank:
    """Six causal, quantised 257-tap bandpass FIRs; measured tail is 256 samples."""

    warmup_samples = 256

    def __init__(self):
        self.coefficients = tuple(quantise_coefficients(scipy_signal.firwin(
            257, band, pass_zero=False, fs=DETECTION_RATE, window=("kaiser", 8.6))) for band in FAST_BANDS)
        self.reset()

    def reset(self) -> None:
        self._zi = [np.zeros(self.warmup_samples) for _ in FAST_BANDS]

    def process(self, samples: Iterable[float]) -> np.ndarray:
        values = sample_vector(samples)
        if not len(values):
            return np.empty((0, 6))
        outputs = []
        for band, coefficients in enumerate(self.coefficients):
            output, self._zi[band] = scipy_signal.lfilter(coefficients, (1.0,), values, zi=self._zi[band])
            outputs.append(output)
        return np.column_stack(outputs)


@dataclass(frozen=True)
class FastBandEvidence:
    energy: float
    active_fraction: float
    strong: bool


def fast_features(block: np.ndarray, envelope: FastEnvelope) -> tuple[FastBandEvidence, ...]:
    block = np.asarray(block, dtype=float)
    if block.shape != (256, 6) or not np.isfinite(block).all():
        raise ValueError("fast blocks require 256 by six finite filtered samples")
    power = block ** 2
    energy = power.mean(axis=0)
    fragments = power.reshape(8, 32, 6).mean(axis=1)
    active = (fragments > 8 * np.asarray(envelope.activity_floor)).mean(axis=0)
    return tuple(FastBandEvidence(float(energy[i]), float(active[i]),
                                 bool(energy[i] > 8 * envelope.energy_q999[i] and active[i] >= 0.75))
                 for i in range(6))
