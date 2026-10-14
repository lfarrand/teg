"""Offline, explicitly verified healthy calibration; never trains a live detector."""
from __future__ import annotations

from dataclasses import dataclass
import math
import numpy as np
from scipy.signal import fftconvolve
from scipy.sparse.linalg import LinearOperator, lsqr

from .profile import EdgeTemplate, FastEnvelope, Profile, SpectralEnvelope, SwitchingEdge
from .signal import (FastFilterBank, StreamingFIRDecimator, default_filters,
                     spectral_bin_indices, spectral_psd)

RATE = 4_000_000
TAPS = 512
MAX_CALIBRATION_SAMPLES = 2_000_000


@dataclass(frozen=True)
class CalibrationResult:
    profile: Profile
    fit_relative_rms: float
    spectral_frames: int
    fast_blocks: int
    solver_stop: int
    solver_iterations: int


def edge_impulses(count: int, edges: tuple[SwitchingEdge, ...], channels: tuple[str, ...],
                  start_index: int, edge_delay_samples: float) -> np.ndarray:
    """Fractional edge alignment shared with the reference predictor convention."""
    if type(count) is not int or not 1 <= count <= MAX_CALIBRATION_SAMPLES:
        raise ValueError("impulse sample count is outside the bounded calibration budget")
    if type(start_index) is not int or start_index < 0:
        raise ValueError("start index must be a nonnegative integer")
    if isinstance(edge_delay_samples, bool) or not isinstance(edge_delay_samples, (int, float)) or not np.isfinite(edge_delay_samples) or not 0 <= edge_delay_samples <= 512:
        raise ValueError("edge delay must be finite and within 0..512 samples")
    if not 1 <= len(channels) <= 8 or len(set(channels)) != len(channels):
        raise ValueError("impulse channels must be unique and bounded")
    impulses = np.zeros((2 * len(channels), count), dtype=np.float64)
    lookup = {channel: 2 * i for i, channel in enumerate(channels)}
    for edge in edges:
        if edge.channel not in lookup:
            raise ValueError("edge channel has no calibrated template")
        location = float(edge.sample_index) + edge_delay_samples - start_index
        whole = int(np.floor(location))
        fraction = location - whole
        row = lookup[edge.channel] + (0 if edge.rising else 1)
        if 0 <= whole < count:
            impulses[row, whole] += 1.0 - fraction
        if 0 <= whole + 1 < count:
            impulses[row, whole + 1] += fraction
    return impulses


def fit_templates(samples: np.ndarray, edges: tuple[SwitchingEdge, ...], *,
                  start_index: int = 0, edge_delay_samples: float = 12.6,
                  max_iterations: int = 160, settling_samples: int = 0
                  ) -> tuple[tuple[EdgeTemplate, ...], np.ndarray, int, int]:
    """Joint bounded-memory convolution least squares for all edge polarities.

    No dense N-by-(channels*1024) design matrix is constructed. Some repetitive
    schedules are rank deficient; the result is a minimum-norm prediction model,
    not an identified physical impulse response. Held-out recordings must validate it.
    Exclude any unmodelled pre-capture response from the fit objective by setting
    settling_samples; predictions retain the original full-stream alignment.
    """
    x = np.asarray(samples, dtype=np.float64)
    if x.ndim != 1 or not 4096 <= x.size <= MAX_CALIBRATION_SAMPLES or not np.isfinite(x).all():
        raise ValueError("healthy calibration requires 4096..2000000 finite samples")
    if type(max_iterations) is not int or not 1 <= max_iterations <= 1000:
        raise ValueError("solver iterations must be bounded within 1..1000")
    if type(settling_samples) is not int or not 0 <= settling_samples < x.size:
        raise ValueError("settling samples must leave a nonempty fit interval")
    if isinstance(edge_delay_samples, bool) or not isinstance(edge_delay_samples, (int, float)) or not np.isfinite(edge_delay_samples) or not 0 <= edge_delay_samples <= 512:
        raise ValueError("edge delay must be finite and within 0..512 samples")
    channels = tuple(sorted({edge.channel for edge in edges}))
    if not 1 <= len(channels) <= 8:
        raise ValueError("calibration requires 1..8 known switching channels")
    for channel in channels:
        for rising in (False, True):
            relevant = [edge for edge in edges if edge.channel == channel and edge.rising == rising
                        and start_index <= edge.sample_index < start_index + x.size - TAPS - edge_delay_samples]
            if len(relevant) < 2:
                raise ValueError("each switching channel requires two complete rising and falling responses")
    impulses = edge_impulses(x.size, edges, channels, start_index, edge_delay_samples)
    variables = impulses.shape[0] * TAPS

    def multiply(coefficients):
        kernels = np.asarray(coefficients).reshape(-1, TAPS)
        result = np.zeros(x.size)
        for sequence, kernel in zip(impulses, kernels):
            result += fftconvolve(sequence, kernel, mode="full")[:x.size]
        return result

    def transpose(values):
        result = []
        for sequence in impulses:
            correlation = fftconvolve(values, sequence[::-1], mode="full")
            result.append(correlation[x.size - 1:x.size - 1 + TAPS])
        return np.concatenate(result)

    def settled_multiply(coefficients):
        return multiply(coefficients)[settling_samples:]

    def settled_transpose(values):
        return transpose(np.pad(values, (settling_samples, 0)))

    matrix = LinearOperator((x.size - settling_samples, variables),
                            matvec=settled_multiply, rmatvec=settled_transpose,
                            dtype=np.float64)
    fit = lsqr(matrix, x[settling_samples:], damp=1e-6, atol=1e-7, btol=1e-7,
               iter_lim=max_iterations)
    if fit[1] not in (1, 2, 4, 5):
        raise ValueError(f"template solver did not converge (stop={fit[1]})")
    kernels = fit[0].reshape(-1, TAPS)
    templates = tuple(EdgeTemplate(channel=channel,
                                   rising=tuple(kernels[2 * i]),
                                   falling=tuple(kernels[2 * i + 1]))
                      for i, channel in enumerate(channels))
    return templates, multiply(fit[0]), int(fit[1]), int(fit[2])


def _envelopes(samples: np.ndarray, start_index: int, excluded_bins: tuple[int, ...]):
    _, second_fir = default_filters()
    decimated = StreamingFIRDecimator(second_fir, 8).process(samples, start_index)
    valid = np.asarray(decimated.values)[np.asarray(decimated.valid, dtype=bool)]
    frame_count = valid.size // 1024
    if frame_count < 8:
        raise ValueError("at least eight valid healthy spectral frames are required")
    powers = np.array([spectral_psd(frame) for frame in valid[:frame_count * 1024].reshape(-1, 1024)])
    bins = spectral_bin_indices(excluded_bins)
    energies = np.column_stack([powers[:, indices].sum(axis=1) * (500_000 / 1024)
                                for indices in bins])
    # A zero training floor must not turn floating point roundoff into an arc.
    floor = max(float(np.mean(samples * samples)) * 1e-12, 1e-24)
    spectral = SpectralEnvelope(energy_q999=tuple(np.maximum(np.quantile(energies, .999, axis=0), floor)),
                                psd_q95=tuple(np.maximum(np.quantile(powers, .95, axis=0), floor / 500_000)),
                                excluded_bins=excluded_bins)
    bank = FastFilterBank()
    filtered = bank.process(samples)[bank.warmup_samples:]
    blocks = filtered.shape[0] // 256
    block_samples = filtered[:blocks * 256].reshape(blocks, 256, 6)
    energy = np.mean(block_samples * block_samples, axis=1)
    fragments = block_samples.reshape(blocks, 8, 32, 6)
    activity = np.mean(fragments * fragments, axis=2).reshape(-1, 6)
    fast = FastEnvelope(energy_q999=tuple(np.maximum(np.quantile(energy, .999, axis=0), floor)),
                        activity_floor=tuple(np.maximum(np.quantile(activity, .999, axis=0), floor)))
    return spectral, fast, frame_count, blocks


def calibrate(samples: np.ndarray, edges: tuple[SwitchingEdge, ...], *,
              profile_id: str, node_id: str, operating_mode: str,
              verified_arc_free: bool, provenance: str, start_index: int = 0,
              excluded_bins: tuple[int, ...] = (), edge_delay_samples: float = 12.6) -> CalibrationResult:
    """Create an immutable experimental profile, never a hardware qualification."""
    if verified_arc_free is not True or not isinstance(provenance, str) or not provenance.strip():
        raise ValueError("calibration requires explicit verified-arc-free provenance")
    if not all(isinstance(value, str) and value.strip() for value in (profile_id, node_id, operating_mode)):
        raise ValueError("profile, node and known operating mode must be named")
    x = np.asarray(samples, dtype=np.float64)
    if (isinstance(edge_delay_samples, bool) or
            not isinstance(edge_delay_samples, (int, float)) or
            not np.isfinite(edge_delay_samples) or not 0 <= edge_delay_samples <= 512):
        raise ValueError("edge delay must be finite and within 0..512 samples")
    # Metadata cannot carry edges before start_index. Their unknown responses
    # must not bias either the fit or the residual thresholds. Start the FIRs
    # only after the complete predictor tail, then apply their own settling.
    predictor_settling = TAPS + math.ceil(edge_delay_samples)
    templates, prediction, stop, iterations = fit_templates(
        x, tuple(edges), start_index=start_index, edge_delay_samples=edge_delay_samples,
        settling_samples=predictor_settling)
    raw, fast_raw, frames, blocks = _envelopes(x, start_index, excluded_bins)
    settled_residual = (x - prediction)[predictor_settling:]
    residual, fast_residual, _, _ = _envelopes(
        settled_residual, start_index + predictor_settling, excluded_bins)
    profile = Profile(profile_id=profile_id, node_id=node_id, operating_mode=operating_mode,
                      raw_envelope=raw, residual_envelope=residual,
                      fast_raw=fast_raw, fast_residual=fast_residual, templates=templates,
                      verified_arc_free=True, spectral_qualified=False, fast_qualified=False,
                      impulse_tail_qualified=False, edge_delay_samples=edge_delay_samples)
    relative = float(np.sqrt(np.mean(settled_residual ** 2)) /
                     max(np.sqrt(np.mean(x[predictor_settling:] ** 2)), 1e-24))
    return CalibrationResult(profile, relative, frames, blocks, stop, iterations)
