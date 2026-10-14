"""Immutable, externally calibrated profiles for the host arc-detector reference.

No fitting or adaptive updates occur in the detector. Qualification fields are
claims made by the profile issuer; this module cannot establish bench evidence.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import math
from typing import Any, Mapping

SPECTRAL_RATE = 500_000
DETECTION_RATE = 4_000_000
ACQUISITION_RATE = 20_000_000
FFT_LENGTH = 1024
TEMPLATE_LENGTH = 512
SPECTRAL_BANDS = ((10_000, 25_000), (25_000, 40_000),
                  (40_000, 65_000), (65_000, 100_000))
FAST_BANDS = ((80_000, 80_000 * math.sqrt(2)),
              (80_000 * math.sqrt(2), 160_000),
              (160_000, 160_000 * math.sqrt(2)),
              (160_000 * math.sqrt(2), 320_000),
              (320_000, 320_000 * math.sqrt(2)),
              (320_000 * math.sqrt(2), 640_000))


def _numbers(values: Any, size: int, name: str, *, positive: bool = False) -> tuple[float, ...]:
    """Copy numeric sequences; reject booleans, NaNs, negatives and wrong lengths."""
    try:
        result = tuple(values)
    except TypeError as exc:
        raise ValueError(f"{name} must be a numeric sequence") from exc
    if len(result) != size:
        raise ValueError(f"{name} requires {size} values")
    if any(isinstance(x, bool) or not isinstance(x, (int, float)) for x in result):
        raise ValueError(f"{name} requires real numbers")
    result = tuple(float(x) for x in result)
    if any(not math.isfinite(x) or x < 0 or (positive and x == 0) for x in result):
        raise ValueError(f"{name} requires finite {'positive' if positive else 'nonnegative'} values")
    return result


def _boolean(value: Any, name: str) -> None:
    if type(value) is not bool:
        raise ValueError(f"{name} must be a bool")


@dataclass(frozen=True)
class SpectralEnvelope:
    """Band energy in current-unit squared; PSD in current-unit squared/Hz."""

    energy_q999: tuple[float, ...]
    psd_q95: tuple[float, ...]
    excluded_bins: tuple[int, ...] = ()

    def __post_init__(self) -> None:
        object.__setattr__(self, "energy_q999", _numbers(self.energy_q999, 4, "energy_q999", positive=True))
        object.__setattr__(self, "psd_q95", _numbers(self.psd_q95, 513, "psd_q95"))
        bins = tuple(self.excluded_bins)
        if any(type(x) is not int or not 0 <= x <= 512 for x in bins):
            raise ValueError("excluded_bins requires integers from 0 through 512")
        if tuple(sorted(set(bins))) != bins:
            raise ValueError("excluded_bins must be unique and sorted")
        # At 500 kSPS/1024 this limits an individual notch to 1.465 kHz.
        run = 0
        previous = -2
        for index in bins:
            run = run + 1 if index == previous + 1 else 1
            if run > 3:
                raise ValueError("a characterised switching-line notch may span at most three bins")
            previous = index
        object.__setattr__(self, "excluded_bins", bins)


@dataclass(frozen=True)
class FastEnvelope:
    """Mean-square envelopes for whole blocks and 32-sample activity fragments."""

    energy_q999: tuple[float, ...]
    activity_floor: tuple[float, ...]

    def __post_init__(self) -> None:
        object.__setattr__(self, "energy_q999", _numbers(self.energy_q999, 6, "fast energy_q999", positive=True))
        object.__setattr__(self, "activity_floor", _numbers(self.activity_floor, 6, "activity_floor"))


@dataclass(frozen=True)
class EdgeTemplate:
    channel: str
    rising: tuple[float, ...]
    falling: tuple[float, ...]

    def __post_init__(self) -> None:
        if not isinstance(self.channel, str) or not self.channel.strip():
            raise ValueError("edge template requires a channel identifier")
        for name in ("rising", "falling"):
            values = tuple(getattr(self, name))
            if len(values) != TEMPLATE_LENGTH:
                raise ValueError(f"{name} template requires {TEMPLATE_LENGTH} coefficients")
            if any(isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) for x in values):
                raise ValueError(f"{name} coefficients must be finite real numbers")
            object.__setattr__(self, name, tuple(float(x) for x in values))


@dataclass(frozen=True)
class Profile:
    profile_id: str
    node_id: str
    operating_mode: str
    raw_envelope: SpectralEnvelope
    residual_envelope: SpectralEnvelope
    fast_raw: FastEnvelope | None = None
    fast_residual: FastEnvelope | None = None
    templates: tuple[EdgeTemplate, ...] = ()
    verified_arc_free: bool = False
    spectral_qualified: bool = False
    fast_qualified: bool = False
    impulse_tail_qualified: bool = False
    edge_delay_samples: float = 12.6

    def __post_init__(self) -> None:
        for name in ("profile_id", "node_id", "operating_mode"):
            if not isinstance(getattr(self, name), str) or not getattr(self, name).strip():
                raise ValueError(f"{name} requires a nonempty identifier")
        if not isinstance(self.raw_envelope, SpectralEnvelope) or not isinstance(self.residual_envelope, SpectralEnvelope):
            raise ValueError("raw and residual require separate SpectralEnvelope objects")
        for name in ("fast_raw", "fast_residual"):
            if getattr(self, name) is not None and not isinstance(getattr(self, name), FastEnvelope):
                raise ValueError(f"{name} requires a FastEnvelope")
        templates = tuple(self.templates)
        if any(not isinstance(x, EdgeTemplate) for x in templates):
            raise ValueError("templates requires EdgeTemplate objects")
        if len({x.channel for x in templates}) != len(templates):
            raise ValueError("duplicate switching channels")
        object.__setattr__(self, "templates", templates)
        for name in ("verified_arc_free", "spectral_qualified", "fast_qualified", "impulse_tail_qualified"):
            _boolean(getattr(self, name), name)
        if isinstance(self.edge_delay_samples, bool) or not isinstance(self.edge_delay_samples, (int, float)):
            raise ValueError("edge_delay_samples must be a finite nonnegative number")
        if not math.isfinite(self.edge_delay_samples) or not 0 <= self.edge_delay_samples <= 512:
            raise ValueError("edge_delay_samples must be between zero and 512")
        if (self.spectral_qualified or self.fast_qualified) and not self.verified_arc_free:
            raise ValueError("qualified profiles require independently verified arc-free calibration")
        if self.fast_qualified and (self.fast_raw is None or not self.impulse_tail_qualified):
            raise ValueError("fast qualification requires raw envelopes and impulse-tail evidence")

    def to_dict(self) -> dict[str, Any]:
        """Versioned JSON-compatible copy; caller controls persistence."""
        return {"schema_version": 1, **asdict(self)}

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "Profile":
        if not isinstance(value, Mapping):
            raise ValueError("profile must be an object")
        data = dict(value)
        version = data.pop("schema_version", None)
        if type(version) is not int or version != 1:
            raise ValueError("unsupported arc profile schema_version")
        allowed = set(cls.__dataclass_fields__)
        if set(data) != allowed:
            raise ValueError(f"profile fields differ: missing={sorted(allowed - set(data))}, unknown={sorted(set(data) - allowed)}")
        try:
            for name in ("raw_envelope", "residual_envelope"):
                data[name] = SpectralEnvelope(**data[name])
            for name in ("fast_raw", "fast_residual"):
                if data.get(name) is not None:
                    data[name] = FastEnvelope(**data[name])
            data["templates"] = tuple(EdgeTemplate(**x) for x in data.get("templates", ()))
            return cls(**data)
        except (KeyError, TypeError) as exc:
            raise ValueError(f"malformed profile: {exc}") from exc


@dataclass(frozen=True)
class SwitchingEdge:
    """Physical edge time in 4 MSPS sample units, before acquisition FIR delay."""

    sample_index: float
    channel: str
    rising: bool

    def __post_init__(self) -> None:
        if isinstance(self.sample_index, bool) or not isinstance(self.sample_index, (int, float)):
            raise ValueError("edge sample_index must be numeric")
        if not math.isfinite(self.sample_index) or self.sample_index < 0:
            raise ValueError("edge sample_index must be finite and nonnegative")
        if not isinstance(self.channel, str) or not self.channel.strip():
            raise ValueError("edge channel must be nonempty")
        _boolean(self.rising, "rising")
