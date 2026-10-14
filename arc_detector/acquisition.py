"""ADS9219 capture decoding and calibrated dual-gain sample selection.

This module consumes already frame-aligned words. It does not configure an ADC,
recover an LVDS clock, or certify sensor health. See FPGA_INTERFACE.md.
"""

from dataclasses import dataclass
from enum import Enum
import math

import numpy as np


class Randomization(str, Enum):
    NONE = "none"
    PRBS = "prbs"
    LSB = "lsb"


@dataclass(frozen=True)
class DecodedWords:
    counts: np.ndarray
    user_bits: np.ndarray


def decode_words(words, *, mode=Randomization.PRBS, expected_user_bits=None):
    """Decode unsigned 24-bit words into signed, two's-complement 18-bit codes.

    PRBS mode: bits 23:6 contain XOR-randomized data, bits 5:1 user bits,
    bit 0 the XOR reference. LSB mode leaves conversion bit 0 unchanged and
    XORs the other 17 data bits with it; bits 5:0 are user bits.
    """
    mode = Randomization(mode)
    values = np.asarray(words)
    if values.ndim != 1 or values.dtype.kind not in "iu":
        raise ValueError("frames must be a one-dimensional integer array")
    if np.any(values < 0) or np.any(values > 0xFFFFFF):
        raise ValueError("frame outside the unsigned 24-bit range")
    values = values.astype(np.uint32)
    data = values >> 6
    if mode == Randomization.PRBS:
        data = data ^ np.where(values & 1, 0x3FFFF, 0).astype(np.uint32)
        user = (values >> 1) & 0x1F
        user_width = 5
    else:
        user = values & 0x3F
        user_width = 6
        if mode == Randomization.LSB:
            data = data ^ np.where(data & 1, 0x3FFFE, 0).astype(np.uint32)
    if expected_user_bits is not None:
        if (isinstance(expected_user_bits, bool)
                or not isinstance(expected_user_bits, int)
                or not 0 <= expected_user_bits < (1 << user_width)):
            raise ValueError("user-bit expectation does not fit the selected mode")
        if np.any(user != expected_user_bits):
            raise ValueError("ADC appended user bits differ from the capture expectation")
    signed = data.astype(np.int32)
    signed = np.where(signed & 0x20000, signed - 0x40000, signed).astype(np.int32)
    return DecodedWords(signed, user.astype(np.uint8))


@dataclass(frozen=True)
class GainCalibration:
    high_gain: float = 16.0
    low_gain: float = 1.0
    high_offset: float = 0.0
    low_offset: float = 0.0
    rail_margin_counts: int = 8

    def __post_init__(self):
        values = (self.high_gain, self.low_gain, self.high_offset, self.low_offset)
        if not all(math.isfinite(v) for v in values):
            raise ValueError("gain calibration must be finite")
        if self.high_gain <= 0 or self.low_gain <= 0:
            raise ValueError("gains must be positive")
        if (isinstance(self.rail_margin_counts, bool)
                or not isinstance(self.rail_margin_counts, int)
                or not 0 <= self.rail_margin_counts < 65536):
            raise ValueError("invalid ADC rail margin")


@dataclass(frozen=True)
class GainSelection:
    samples: np.ndarray
    used_low: np.ndarray
    unusable: np.ndarray

    @property
    def both_gains_clipped(self):
        return bool(np.any(self.unusable))


def select_gain(high, low, calibration=GainCalibration()):
    """Select each usable sample in common input-referred ADC-code units.

    A sample for which both channels clip is flagged and set to zero only as a
    numerical placeholder. The caller MUST latch acquisition unavailability;
    that zero is never evidence that the circuit is healthy.
    """
    high = np.asarray(high)
    low = np.asarray(low)
    if (high.ndim != 1 or low.ndim != 1 or high.shape != low.shape
            or high.dtype.kind not in "iu" or low.dtype.kind not in "iu"):
        raise ValueError("gain channels must be equally sized signed-code arrays")
    if any(np.any((x < -131072) | (x > 131071)) for x in (high, low)):
        raise ValueError("gain input is not an 18-bit ADC code")
    margin = calibration.rail_margin_counts
    high_clip = (high <= -131072 + margin) | (high >= 131071 - margin)
    low_clip = (low <= -131072 + margin) | (low >= 131071 - margin)
    unusable = high_clip & low_clip
    used_low = high_clip & ~low_clip
    h = (high.astype(np.float64) - calibration.high_offset) / calibration.high_gain
    l = (low.astype(np.float64) - calibration.low_offset) / calibration.low_gain
    samples = np.where(used_low, l, h)
    samples[unusable] = 0.0
    return GainSelection(samples, used_low, unusable)


class CaptureContinuity:
    """Validate sequential raw ADC blocks before decimation/cancellation."""

    def __init__(self):
        self.next_sample = None
        self.unavailable = False

    def observe(self, start_index, sample_count, *, frame_valid=True, clock_valid=True):
        if (isinstance(start_index, bool) or not isinstance(start_index, int)
                or start_index < 0 or isinstance(sample_count, bool)
                or not isinstance(sample_count, int) or sample_count <= 0):
            self.unavailable = True
            return False
        if (not frame_valid or not clock_valid
                or (self.next_sample is not None and start_index != self.next_sample)):
            self.unavailable = True
        self.next_sample = start_index + sample_count
        return not self.unavailable

    def reset(self, *, output_inhibited, diagnostics_ok):
        if not output_inhibited or not diagnostics_ok:
            return False
        self.next_sample = None
        self.unavailable = False
        return True


class CaptureProcessor:
    """Connect aligned dual ADC words to the detector's actual 20 MSPS path.

    The detector owns filtering, time continuity and shutdown latching. This
    adapter never treats a decoding exception or doubly clipped sample as quiet
    data. Bad hardware words generate a health decision, not an arc label.
    """

    def __init__(self, detector, *, calibration=GainCalibration(), mode=Randomization.PRBS):
        self.detector = detector
        self.calibration = calibration
        self.mode = Randomization(mode)

    def process(self, high_words, low_words, *, start_index, edges=(), health=None,
                operating_mode=None):
        from dataclasses import replace
        from .detector import AcquisitionHealth

        health = AcquisitionHealth() if health is None else health
        if not isinstance(health, AcquisitionHealth):
            raise ValueError("health must be an AcquisitionHealth")
        if type(start_index) is not int or start_index < 0:
            raise ValueError("raw capture start requires a nonnegative integer")
        try:
            high = decode_words(high_words, mode=self.mode).counts
            low = decode_words(low_words, mode=self.mode).counts
            chosen = select_gain(high, low, self.calibration)
            if not chosen.samples.size or not np.all(np.isfinite(chosen.samples)):
                raise ValueError("empty or nonfinite raw capture")
        except (ValueError, TypeError, OverflowError):
            bad = AcquisitionHealth(**{**health.__dict__, "frame_valid": False})
            return self.detector.process20m((), start_index=start_index,
                                           health=bad, operating_mode=operating_mode)
        # Split only at acquisition-health transitions. Otherwise a late clipped
        # sample in a large host chunk would be reported at the chunk's start,
        # preempting genuine earlier decisions and fabricating latency.
        edges = tuple(edges)
        clipped = chosen.unusable
        boundaries = np.concatenate(([0], np.flatnonzero(clipped[1:] != clipped[:-1]) + 1,
                                     [len(clipped)]))
        results = []
        for first, end in zip(boundaries[:-1], boundaries[1:]):
            first, end = int(first), int(end)
            selected_health = AcquisitionHealth(**{
                **health.__dict__,
                "both_gains_clipped": health.both_gains_clipped or bool(clipped[first]),
            })
            raw_first, raw_end = start_index + first, start_index + end
            segment_edges = tuple(e for e in edges if raw_first / 5 <= e.sample_index < raw_end / 5)
            results.append(self.detector.process20m(
                chosen.samples[first:end], start_index=raw_first, edges=segment_edges,
                health=selected_health, operating_mode=operating_mode))
        return replace(results[-1],
                       decisions=tuple(d for result in results for d in result.decisions),
                       spectral_frames=tuple(f for result in results for f in result.spectral_frames),
                       fast_blocks=tuple(b for result in results for b in result.fast_blocks))
