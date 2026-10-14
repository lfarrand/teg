"""Deterministic dual-gain recordings with switching-correlated injected noise."""
from __future__ import annotations

import numpy as np
from scipy.signal import fftconvolve

from ..calibration import edge_impulses
from ..profile import SwitchingEdge
from ..replay import HEALTH_KEYS, SAMPLE_RATE


def make_recording(*, count: int = 300_000, seed: int = 741,
                   arc_onset_s: float | None = None, startup_arc: bool = False):
    rng = np.random.default_rng(seed)
    edges = []
    position = 40
    while position < count - 520:
        edges.extend((SwitchingEdge(float(position), "pwm0", True),
                      SwitchingEdge(float(position + 35), "pwm0", False)))
        position += int(rng.integers(77, 98))
    time = np.arange(512) / SAMPLE_RATE
    rise = .35 * np.exp(-np.arange(512) / 70) * np.sin(2 * np.pi * 170_000 * time)
    fall = -.25 * np.exp(-np.arange(512) / 55) * np.sin(2 * np.pi * 220_000 * time)
    impulses = edge_impulses(count, tuple(edges), ("pwm0",), 0, 12.6)
    current = (fftconvolve(impulses[0], rise)[:count] +
               fftconvolve(impulses[1], fall)[:count] + rng.normal(0, .004, count))
    events = []
    if arc_onset_s is not None or startup_arc:
        onset = 0.0 if startup_arc else float(arc_onset_s)
        start = int(onset * SAMPLE_RATE)
        if not 0 <= start < count:
            raise ValueError("synthetic arc onset outside recording")
        # PWM-gated broadband disturbance specifically tests the risk that an arc
        # correlated with switching timing could disappear in cancellation.
        disturbance = rng.normal(0, .7, count)
        gate = np.zeros(count)
        for edge in edges:
            index = int(edge.sample_index)
            gate[index:min(index + 48, count)] = 1
        current[start:] += disturbance[start:] * (.5 + .5 * gate[start:])
        events.append({"event_id": "synthetic-arc", "kind": "arc", "onset_s": onset,
                       "end_s": count / SAMPLE_RATE, "arc_type": "unknown"})
    metadata = {"schema_version": 1, "sample_rate_hz": SAMPLE_RATE, "start_index": 0,
                "sample_count": count, "node_id": "synthetic-node", "operating_mode": "synthetic-jittered-pwm",
                "high_gain_scale": 1.0, "low_gain_scale": 1.0,
                "health": {key: True for key in HEALTH_KEYS},
                "edges": [{"sample_index": edge.sample_index, "channel": edge.channel,
                           "rising": edge.rising} for edge in edges], "events": events,
                "synthetic": True, "verified_arc_free": not events,
                "provenance": "Deterministic numerical injection, not measured healthy or real-arc hardware evidence."}
    arrays = {"sample_index": np.arange(count, dtype=np.int64),
              "current_high": current, "current_low": current.copy(),
              "high_clipped": np.zeros(count, dtype=np.uint8),
              "low_clipped": np.zeros(count, dtype=np.uint8)}
    return metadata, arrays
