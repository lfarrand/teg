"""Host reference detector with independent raw/residual evidence and a latch.

Sample indices describe physical acquisition time, never Python execution
time. ``prototype=True`` permits unqualified replay but can never issue permit.
Nothing in this module drives TEG PWM, an interrupter, or actual FPGA pins.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum
import math
from typing import Iterable

import numpy as np

from .profile import DETECTION_RATE, FFT_LENGTH, Profile, SwitchingEdge
from .signal import (BandEvidence, FastBandEvidence, FastFilterBank, FrozenPredictor,
                     StreamingFIRDecimator, default_filters, fast_features,
                     features_from_psd, sample_vector, spectral_bin_indices,
                     spectral_psd)


class ProtectionState(str, Enum):
    INITIALISING = "INITIALISING"
    MONITORING = "MONITORING"
    SUSPECT = "SUSPECT"
    ARC_DETECTED = "ARC_DETECTED"
    PROTECTION_UNCERTAIN = "PROTECTION_UNCERTAIN"
    PROTECTION_UNAVAILABLE = "PROTECTION_UNAVAILABLE"


@dataclass(frozen=True)
class AcquisitionHealth:
    frame_valid: bool = True
    continuous: bool = True
    deadline_met: bool = True
    sensor_ok: bool = True
    gain_valid: bool = True
    pwm_reference_valid: bool = True
    diagnostics_passed: bool = True
    both_gains_clipped: bool = False
    raw_profile_valid: bool = True
    residual_profile_valid: bool = True

    def __post_init__(self) -> None:
        for name in self.__dataclass_fields__:
            if type(getattr(self, name)) is not bool:
                raise ValueError(f"health {name} requires a bool")


@dataclass(frozen=True)
class Decision:
    sample_index: int | float
    timestamp_us: float
    state: ProtectionState
    reason: str
    path: str


@dataclass(frozen=True)
class SpectralFrame:
    sample_index: int
    timestamp_us: float
    path: str
    bands: tuple[BandEvidence, ...]


@dataclass(frozen=True)
class FastBlock:
    sample_index: int
    timestamp_us: float
    path: str
    bands: tuple[FastBandEvidence, ...]


@dataclass(frozen=True)
class ProcessResult:
    state: ProtectionState
    shutdown_latched: bool
    permit: bool
    cancellation_active: bool
    decisions: tuple[Decision, ...]
    spectral_frames: tuple[SpectralFrame, ...]
    fast_blocks: tuple[FastBlock, ...]
    available_tiers: tuple[str, ...]
    prototype: bool
    arc_type: str = "unknown"
    # This is a software shutdown request, never proof of arc extinction.


@dataclass
class _Temporal:
    strong_streak: int = 0
    weak_samples: int = 0
    fast_streak: int = 0


class Detector:
    def __init__(self, profile: Profile, *, prototype: bool = False):
        if not isinstance(profile, Profile) or type(prototype) is not bool:
            raise ValueError("Detector requires a Profile and a bool prototype flag")
        self.profile = profile
        self.prototype = prototype
        self._reset_all()

    def _reset_all(self) -> None:
        self.state = ProtectionState.INITIALISING
        self.shutdown_latched = False
        self._cancel_active = False
        self._fast_active = False
        self._raw_available: bool | None = None
        self._residual_available = False
        self._suspect_since: int | None = None
        self._clear_frames = 0
        self._last_health = AcquisitionHealth()
        self._operating_mode_valid = False
        self._temporal = {"raw": _Temporal(), "residual": _Temporal()}
        self._next_index: int | None = None
        self._first_index: int | None = None
        self._reset_streams()
        self._qualification_fault_pending = False
        if not self.prototype and not (self.profile.spectral_qualified and self.profile.verified_arc_free):
            self.state = ProtectionState.PROTECTION_UNAVAILABLE
            self.shutdown_latched = True
            self._qualification_fault_pending = True

    def _reset_streams(self) -> None:
        first, second = default_filters()
        self._first_decimator = StreamingFIRDecimator(first, 5)
        self._decimators = {path: StreamingFIRDecimator(second, 8) for path in ("raw", "residual")}
        self._fast_banks = {path: FastFilterBank() for path in ("raw", "residual")}
        self._spectral_buffer = {path: np.empty(0) for path in ("raw", "residual")}
        self._spectral_indices = {path: np.empty(0, dtype=np.int64) for path in ("raw", "residual")}
        self._fast_buffer = {path: np.empty((0, 6)) for path in ("raw", "residual")}
        self._fast_indices = {path: np.empty(0, dtype=np.int64) for path in ("raw", "residual")}
        self._previous_psd = {"raw": None, "residual": None}
        self._spectral_ready = {"raw": False, "residual": False}
        self._predictor = FrozenPredictor(self.profile.templates, self.profile.edge_delay_samples)
        self._residual_valid_after: int | None = None
        self._residual_signal_valid_after: int | None = None
        self._fast_valid_after: int | None = None
        self._wrapper_first_valid_4m: int | None = None
        self._raw_spectral_valid_after: int | None = None
        self._raw_fast_valid_after: int | None = None
        self._20_pending_values = np.empty(0)
        self._20_pending_indices = np.empty(0, dtype=np.int64)
        self._20_pending_edges: list[SwitchingEdge] = []
        self._20_next_index: int | None = None

    def reset(self, *, output_inhibited: bool, diagnostics_passed: bool) -> bool:
        """Explicit caller request; latch remains held if inhibition/diagnostics fail."""
        if type(output_inhibited) is not bool or type(diagnostics_passed) is not bool:
            raise ValueError("reset arguments require bool values")
        if not output_inhibited or not diagnostics_passed:
            return False
        self._reset_all()
        return True

    def _latch(self, state: ProtectionState, reason: str, path: str, index: int,
               decisions: list[Decision]) -> None:
        if not self.shutdown_latched:
            self.shutdown_latched = True
            self.state = state
            decisions.append(Decision(index, index / DETECTION_RATE * 1e6, state, reason, path))

    def _timeout(self, index: int, decisions: list[Decision]) -> None:
        if self._suspect_since is not None and index - self._suspect_since >= 400_000:
            # A sample-clock timer expires exactly at the physical deadline,
            # including a deadline between completed FFT windows or chunks.
            deadline = self._suspect_since + 400_000
            self._latch(ProtectionState.PROTECTION_UNCERTAIN, "suspicion_timeout", "spectral", deadline, decisions)

    def _health_reason(self, health: AcquisitionHealth, start_index: int) -> str | None:
        if not health.frame_valid:
            return "invalid_adc_frame"
        if not health.continuous or (self._next_index is not None and start_index != self._next_index):
            return "acquisition_gap"
        if not health.deadline_met:
            return "processing_deadline"
        if not health.sensor_ok:
            return "sensor_failure"
        if not health.gain_valid:
            return "invalid_gain_selection"
        if health.both_gains_clipped:
            return "both_gains_clipped"
        if not health.diagnostics_passed:
            return "diagnostics_failed"
        return None

    def _mode_reason(self, operating_mode: str | None) -> str | None:
        """Bind all envelopes to an explicitly supplied, exact operating mode."""
        self._operating_mode_valid = (type(operating_mode) is str and
                                      operating_mode == self.profile.operating_mode)
        if self._operating_mode_valid:
            return None
        return "missing_operating_mode" if operating_mode is None else "operating_mode_mismatch"

    @staticmethod
    def _usable_count(envelope) -> int:
        full = spectral_bin_indices()
        usable = spectral_bin_indices(envelope.excluded_bins)
        return sum(len(usable[i]) >= math.ceil(len(full[i]) * 0.6) for i in range(4))

    def _clear_residual(self) -> None:
        self._decimators["residual"].reset()
        self._fast_banks["residual"].reset()
        self._spectral_buffer["residual"] = np.empty(0)
        self._spectral_indices["residual"] = np.empty(0, dtype=np.int64)
        self._fast_buffer["residual"] = np.empty((0, 6))
        self._fast_indices["residual"] = np.empty(0, dtype=np.int64)
        self._previous_psd["residual"] = None
        self._temporal["residual"] = _Temporal()
        self._spectral_ready["residual"] = False
        self._predictor.reset()

    def _clear_raw(self, start_index: int) -> None:
        """Discard evidence/filters when raw applicability changes.

        The unresolved global suspicion clock deliberately remains unchanged.
        A caller cannot dismiss old suspicion by withdrawing a profile.
        """
        self._decimators["raw"].reset()
        self._fast_banks["raw"].reset()
        self._spectral_buffer["raw"] = np.empty(0)
        self._spectral_indices["raw"] = np.empty(0, dtype=np.int64)
        self._fast_buffer["raw"] = np.empty((0, 6))
        self._fast_indices["raw"] = np.empty(0, dtype=np.int64)
        self._previous_psd["raw"] = None
        self._temporal["raw"] = _Temporal()
        self._spectral_ready["raw"] = False
        upstream_warmup = 26 if self._20_next_index is not None else 0
        self._raw_spectral_valid_after = start_index + upstream_warmup + 254
        self._raw_fast_valid_after = start_index + upstream_warmup + FastFilterBank.warmup_samples

    def _qualification_decision(self, index: int | float, decisions: list[Decision]) -> None:
        if self._qualification_fault_pending:
            decisions.append(Decision(index, index / DETECTION_RATE * 1e6,
                                      ProtectionState.PROTECTION_UNAVAILABLE, "unqualified_profile", "health"))
            self._qualification_fault_pending = False

    def _apply_applicability(self, index: int | float, health: AcquisitionHealth,
                             known_edges: bool, decisions: list[Decision]) -> None:
        """Apply control changes at their actual time, before sample buffering.

        ADC calls may not complete a /5 interval. Path resets and coverage loss
        must still persist so later samples cannot overwrite that history.
        """
        cancellation = bool(self.profile.templates and self.profile.verified_arc_free and
                            self._operating_mode_valid and health.pwm_reference_valid and
                            health.residual_profile_valid and known_edges)
        if cancellation != self._cancel_active:
            self._clear_residual()
            self._residual_signal_valid_after = index + 512 + math.ceil(self.profile.edge_delay_samples)
            self._residual_valid_after = self._residual_signal_valid_after + 254
        self._cancel_active = cancellation
        profile_available = self.prototype or (self.profile.spectral_qualified and self.profile.verified_arc_free)
        raw_available = (profile_available and self._operating_mode_valid and health.raw_profile_valid and
                         self._usable_count(self.profile.raw_envelope) >= 2)
        residual_available = (profile_available and cancellation and
                              self._usable_count(self.profile.residual_envelope) >= 2)
        if raw_available != self._raw_available:
            self._clear_raw(index)
        self._raw_available = raw_available
        self._residual_available = residual_available
        if not raw_available and not residual_available:
            self._latch(ProtectionState.PROTECTION_UNAVAILABLE, "no_qualified_spectral_bandwidth", "health", index, decisions)
        fast_enabled = (self.profile.fast_qualified and self.profile.impulse_tail_qualified and
                        self.profile.fast_raw is not None and health.pwm_reference_valid and
                        self._operating_mode_valid)
        if fast_enabled != self._fast_active:
            for path in ("raw", "residual"):
                self._fast_banks[path].reset()
                self._fast_buffer[path] = np.empty((0, 6))
                self._fast_indices[path] = np.empty(0, dtype=np.int64)
                self._temporal[path].fast_streak = 0
            self._fast_valid_after = index + FastFilterBank.warmup_samples
            if self._wrapper_first_valid_4m is not None:
                self._fast_valid_after = max(self._fast_valid_after, self._wrapper_first_valid_4m + FastFilterBank.warmup_samples)
        self._fast_active = fast_enabled

    def _spectral(self, path: str, values: np.ndarray, start_index: int,
                  valid_after: int) -> list[SpectralFrame]:
        down = self._decimators[path].process(values, start_index)
        valid = down.valid & (down.sample_indices >= valid_after)
        self._spectral_buffer[path] = np.concatenate((self._spectral_buffer[path], down.values[valid]))
        self._spectral_indices[path] = np.concatenate((self._spectral_indices[path], down.sample_indices[valid]))
        envelope = self.profile.raw_envelope if path == "raw" else self.profile.residual_envelope
        output = []
        consumed = 0
        while len(self._spectral_buffer[path]) - consumed >= FFT_LENGTH:
            frame = self._spectral_buffer[path][consumed:consumed + FFT_LENGTH]
            psd = spectral_psd(frame)
            bands = features_from_psd(psd, envelope, self._previous_psd[path])
            self._previous_psd[path] = psd
            # Availability follows the last actual 4 MSPS source sample. An
            # FFT does not need the seven not-yet-observed ticks until the next
            # decimated sample, even though consecutive windows are 8192 ticks.
            index = int(self._spectral_indices[path][consumed + FFT_LENGTH - 1]) + 1
            output.append(SpectralFrame(index, index / DETECTION_RATE * 1e6, path, bands))
            consumed += FFT_LENGTH
        self._spectral_buffer[path] = self._spectral_buffer[path][consumed:]
        self._spectral_indices[path] = self._spectral_indices[path][consumed:]
        return output

    def _fast(self, path: str, values: np.ndarray, start_index: int,
              valid_after: int) -> list[FastBlock]:
        filtered = self._fast_banks[path].process(values)
        indices = np.arange(start_index, start_index + len(values), dtype=np.int64)
        keep = indices >= valid_after
        self._fast_buffer[path] = np.concatenate((self._fast_buffer[path], filtered[keep]))
        self._fast_indices[path] = np.concatenate((self._fast_indices[path], indices[keep]))
        envelope = self.profile.fast_raw if path == "raw" else self.profile.fast_residual
        output = []
        consumed = 0
        while len(self._fast_buffer[path]) - consumed >= 256:
            block = self._fast_buffer[path][consumed:consumed + 256]
            bands = fast_features(block, envelope)
            index = int(self._fast_indices[path][consumed + 255]) + 1
            output.append(FastBlock(index, index / DETECTION_RATE * 1e6, path, bands))
            consumed += 256
        self._fast_buffer[path] = self._fast_buffer[path][consumed:]
        self._fast_indices[path] = self._fast_indices[path][consumed:]
        return output

    def _consume_spectral(self, frames: list[SpectralFrame], decisions: list[Decision]) -> None:
        # Raw/residual are paired only when their actual completed timestamps
        # agree. Either path's independent accumulator can trip protection.
        index = frames[0].sample_index
        weak_any = False
        for frame in frames:
            self._spectral_ready[frame.path] = True
            temporal = self._temporal[frame.path]
            weak = sum(band.weak for band in frame.bands) >= 2
            strong = sum(band.strong for band in frame.bands) >= 3
            temporal.strong_streak = temporal.strong_streak + 1 if strong else 0
            temporal.weak_samples = (temporal.weak_samples + 8192 if weak else
                                     max(0, temporal.weak_samples - 2048))
            weak_any |= weak
            if temporal.strong_streak >= 3:
                self._latch(ProtectionState.ARC_DETECTED, "spectral_strong", frame.path, index, decisions)
            if temporal.weak_samples >= 80_000:
                self._latch(ProtectionState.ARC_DETECTED, "spectral_accumulated", frame.path, index, decisions)
        if weak_any:
            self._clear_frames = 0
            if self._suspect_since is None:
                self._suspect_since = index
            if not self.shutdown_latched:
                self.state = ProtectionState.SUSPECT
        elif not any(temporal.weak_samples for temporal in self._temporal.values()):
            # Count complete raw windows once; staggered residual windows must
            # not shorten the specified two-clear-frame recovery interval.
            if any(frame.path == "raw" for frame in frames):
                self._clear_frames += 1
            if self._clear_frames >= 2:
                self._suspect_since = None
                if not self.shutdown_latched:
                    self.state = ProtectionState.MONITORING
        else:
            self._clear_frames = 0
        if self.state == ProtectionState.INITIALISING:
            self.state = ProtectionState.MONITORING
        self._timeout(index, decisions)

    def _consume_fast(self, block: FastBlock, decisions: list[Decision]) -> None:
        strong = [i for i, band in enumerate(block.bands) if band.strong]
        qualifies = len(strong) >= 4 and {i // 2 for i in strong} == {0, 1, 2}
        temporal = self._temporal[block.path]
        temporal.fast_streak = temporal.fast_streak + 1 if qualifies else 0
        if temporal.fast_streak >= 8:
            self._latch(ProtectionState.ARC_DETECTED, "fast_sustained", block.path, block.sample_index, decisions)
        self._timeout(block.sample_index, decisions)

    def _result(self, decisions=(), spectral=(), fast=(), tiers=None) -> ProcessResult:
        if tiers is None:
            tiers = []
            if self._raw_available:
                tiers.append("raw_spectral")
            if self._residual_available:
                tiers.append("residual_spectral")
            if self._fast_active and self._raw_available:
                tiers.append("raw_fast")
            if self._fast_active and self._residual_available and self.profile.fast_residual is not None:
                tiers.append("residual_fast")
        ready = ((self._raw_available and self._spectral_ready["raw"]) or
                 (self._residual_available and self._spectral_ready["residual"]))
        permit = (not self.prototype and self.profile.spectral_qualified and self.profile.verified_arc_free and
                  self._operating_mode_valid and ready and not self.shutdown_latched and
                  self.state in (ProtectionState.MONITORING, ProtectionState.SUSPECT) and
                  self._last_health.diagnostics_passed)
        return ProcessResult(self.state, self.shutdown_latched, permit, self._cancel_active,
                             tuple(decisions), tuple(spectral), tuple(fast), tuple(tiers), self.prototype)

    def process(self, samples_4m: Iterable[float], *, start_index: int,
                edges: Iterable[SwitchingEdge] = (), health: AcquisitionHealth | None = None,
                operating_mode: str | None = None) -> ProcessResult:
        """Process 4 MSPS samples under an explicitly supplied profile mode.

        A missing or different mode latches unavailable; the profile's own mode
        is never evidence of the currently observed operating conditions.
        """
        if type(start_index) is not int or start_index < 0:
            raise ValueError("start_index requires a nonnegative integer at 4 MSPS")
        health = AcquisitionHealth() if health is None else health
        if not isinstance(health, AcquisitionHealth):
            raise ValueError("health must be an AcquisitionHealth")
        values = sample_vector(samples_4m)
        edges = tuple(edges)
        if any(not isinstance(edge, SwitchingEdge) for edge in edges):
            raise ValueError("edges require SwitchingEdge objects")
        decisions: list[Decision] = []
        self._last_health = health
        self._qualification_decision(start_index, decisions)
        mode_reason = self._mode_reason(operating_mode)
        reason = self._health_reason(health, start_index) or mode_reason
        if reason:
            self._latch(ProtectionState.PROTECTION_UNAVAILABLE, reason, "health", start_index, decisions)
            self._reset_streams()
            self._cancel_active = False
            self._fast_active = False
            self._raw_available = None
            self._residual_available = False
            self._next_index = start_index + len(values)
            self._first_index = self._next_index
            self._temporal = {"raw": _Temporal(), "residual": _Temporal()}
            return self._result(decisions)
        known_edges = all(edge.channel in {template.channel for template in self.profile.templates} for edge in edges)
        self._apply_applicability(start_index, health, known_edges, decisions)
        raw_available = self._raw_available
        residual_available = self._residual_available
        cancellation = self._cancel_active
        if not len(values):
            return self._result(decisions)
        if self._first_index is None:
            self._first_index = start_index
        self._next_index = start_index + len(values)
        # All inputs remain visible in raw evidence. No residual veto exists.
        raw_valid_after = max(self._first_index, self._raw_spectral_valid_after)
        if self._wrapper_first_valid_4m is not None:
            raw_valid_after = max(raw_valid_after, self._wrapper_first_valid_4m + 254)
        spectral = self._spectral("raw", values, start_index, raw_valid_after)
        residual = None
        if cancellation:
            prediction = self._predictor.process(len(values), start_index, edges)
            residual = values - prediction
            spectral += self._spectral("residual", residual, start_index, self._residual_valid_after)
        fast_enabled = self._fast_active
        fast = []
        if fast_enabled:
            if self._wrapper_first_valid_4m is not None:
                # The /5 FIR may first become valid after a tiny initial ADC
                # chunk enabled the fast tier. Propagate this newly observed
                # settling bound on every call, not only on the enable edge.
                self._fast_valid_after = max(self._fast_valid_after,
                                             self._wrapper_first_valid_4m + FastFilterBank.warmup_samples)
            fast = self._fast("raw", values, start_index, max(self._fast_valid_after, self._raw_fast_valid_after))
            if residual is not None and self.profile.fast_residual is not None:
                fast += self._fast("residual", residual, start_index,
                                   max(self._fast_valid_after, self._residual_signal_valid_after + FastFilterBank.warmup_samples))
        events = [(frame.sample_index, 1, frame) for frame in spectral]
        events += [(block.sample_index, 0, block) for block in fast]
        events.sort(key=lambda event: (event[0], event[1], event[2].path))
        position = 0
        while position < len(events):
            index, kind, event = events[position]
            # Timer expiry precedes a later window that might clear evidence.
            self._timeout(index, decisions)
            if kind == 0:
                if ((event.path == "raw" and raw_available) or
                        (event.path == "residual" and residual_available)):
                    self._consume_fast(event, decisions)
                position += 1
            else:
                grouped = []
                while position < len(events) and events[position][:2] == (index, 1):
                    frame = events[position][2]
                    if ((frame.path == "raw" and raw_available) or
                            (frame.path == "residual" and residual_available)):
                        grouped.append(frame)
                    position += 1
                if grouped:
                    self._consume_spectral(grouped, decisions)
        self._timeout(self._next_index, decisions)
        return self._result(decisions, sorted(spectral, key=lambda frame: (frame.sample_index, frame.path)),
                            sorted(fast, key=lambda block: (block.sample_index, block.path)))

    def process20m(self, samples_20m: Iterable[float], *, start_index: int,
                   edges: Iterable[SwitchingEdge] = (), health: AcquisitionHealth | None = None,
                   operating_mode: str | None = None) -> ProcessResult:
        """ADC-rate wrapper; edge indices remain in physical 4 MSPS units.

        Complete five-ADC-sample intervals are held together so edge ownership
        remains invariant even when a caller partitions data inside a /5 block.
        ``operating_mode`` must explicitly match the profile, including chunks
        too small to produce a decimated output sample.
        Do not mix this wrapper and direct ``process`` on the same instance.
        """
        if type(start_index) is not int or start_index < 0:
            raise ValueError("ADC start_index requires a nonnegative integer")
        values = sample_vector(samples_20m)
        health = AcquisitionHealth() if health is None else health
        if not isinstance(health, AcquisitionHealth):
            raise ValueError("health must be an AcquisitionHealth")
        decisions: list[Decision] = []
        physical4 = start_index // 5 if start_index % 5 == 0 else start_index / 5
        self._last_health = health
        self._qualification_decision(physical4, decisions)
        if self._20_next_index is not None and self._20_next_index != start_index:
            health = AcquisitionHealth(**{**health.__dict__, "continuous": False})
        mode_reason = self._mode_reason(operating_mode)
        reason = self._health_reason(health, self._next_index if self._next_index is not None else start_index // 5) or mode_reason
        if reason:
            # A raw ADC fault can occur between 4 MSPS sample ticks. Keep its
            # actual 20 MSPS time, rather than a stale completed-/5 index.
            self._latch(ProtectionState.PROTECTION_UNAVAILABLE, reason, "health", physical4, decisions)
            self._reset_streams()
            self._cancel_active = False
            self._fast_active = False
            self._raw_available = None
            self._residual_available = False
            self._next_index = None
            self._first_index = None
            self._suspect_since = None
            self._temporal = {"raw": _Temporal(), "residual": _Temporal()}
            self._20_next_index = start_index + len(values)
            return self._result(decisions)
        incoming = tuple(edges)
        if any(not isinstance(edge, SwitchingEdge) or
               not start_index / 5 <= edge.sample_index < (start_index + len(values)) / 5 for edge in incoming):
            raise ValueError("ADC wrapper edges must be within the supplied physical sample interval")
        known_edges = all(edge.channel in {template.channel for template in self.profile.templates} for edge in incoming)
        self._apply_applicability(physical4, health, known_edges, decisions)
        down = self._first_decimator.process(values, start_index)
        if self._wrapper_first_valid_4m is None and down.valid.any():
            self._wrapper_first_valid_4m = int(down.sample_indices[down.valid][0]) // 5
        self._20_pending_values = np.concatenate((self._20_pending_values, down.values))
        self._20_pending_indices = np.concatenate((self._20_pending_indices, down.sample_indices // 5))
        self._20_pending_edges.extend(incoming)
        end20 = start_index + len(values)
        complete = self._20_pending_indices < end20 // 5
        count = int(complete.sum())
        self._20_next_index = end20
        if not count:
            return self._result(decisions)
        first4 = int(self._20_pending_indices[0])
        end4 = int(self._20_pending_indices[count - 1]) + 1
        ready_edges = tuple(edge for edge in self._20_pending_edges if first4 <= edge.sample_index < end4)
        self._20_pending_edges = [edge for edge in self._20_pending_edges if edge.sample_index >= end4]
        ready_values = self._20_pending_values[:count]
        self._20_pending_values = self._20_pending_values[count:]
        self._20_pending_indices = self._20_pending_indices[count:]
        result = self.process(ready_values, start_index=first4, edges=ready_edges, health=health, operating_mode=operating_mode)
        return replace(result, decisions=tuple(decisions) + result.decisions) if decisions else result
