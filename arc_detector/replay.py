"""Bounded recording readers and event-level reports, independent of detector labels."""
from __future__ import annotations

import csv
from dataclasses import asdict, dataclass, is_dataclass
import json
from pathlib import Path
import zipfile
from collections.abc import Iterator
from types import MappingProxyType
import numpy as np

from .detector import AcquisitionHealth, Detector
from .profile import Profile, SwitchingEdge

SAMPLE_RATE = 4_000_000
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_METADATA_BYTES = 8 * 1024 * 1024
MAX_SAMPLES = 4_000_000
COLUMNS = ("sample_index", "current_high", "current_low", "high_clipped", "low_clipped")
HEALTH_KEYS = ("frame_valid", "continuous", "deadline_met", "sensor_ok", "gain_valid",
               "pwm_reference_valid", "diagnostics_passed", "raw_profile_valid", "residual_profile_valid")


def _finite(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not np.isfinite(value):
        raise ValueError(f"{name} must be a finite number")
    return float(value)


def _integer(value, name: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return value


def load_json(path: str | Path, max_bytes: int = MAX_METADATA_BYTES):
    path = Path(path)
    if path.stat().st_size > max_bytes:
        raise ValueError("JSON exceeds bounded input size")
    def reject_constant(token):
        raise ValueError(f"non-finite JSON value: {token}")
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant,
                      object_pairs_hook=unique_pairs)


def write_json(path: str | Path, value) -> None:
    """CLI output is a user-requested artifact; refuses accidental replacement."""
    with Path(path).open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, indent=2, allow_nan=False)
        output.write("\n")


@dataclass(frozen=True)
class LabelledEvent:
    event_id: str
    kind: str
    onset_s: float
    end_s: float
    arc_type: str = "unknown"
    shutdown_command_s: float | None = None
    extinction_s: float | None = None
    terminal_zero_s: float | None = None

    def __post_init__(self):
        if not isinstance(self.event_id, str) or not self.event_id or len(self.event_id) > 512:
            raise ValueError("event identifier must be bounded and nonempty")
        if self.kind not in ("arc", "nuisance") or self.arc_type not in ("unknown", "series", "parallel"):
            raise ValueError("unsupported independent event label")
        onset, end = _finite(self.onset_s, "onset_s"), _finite(self.end_s, "end_s")
        if onset < 0 or end <= onset:
            raise ValueError("event requires a positive nonnegative interval")
        for name in ("shutdown_command_s", "extinction_s", "terminal_zero_s"):
            value = getattr(self, name)
            if value is not None and _finite(value, name) < onset:
                raise ValueError("measured event timestamp cannot precede onset")
        if self.extinction_s is not None and self.shutdown_command_s is not None and self.extinction_s < self.shutdown_command_s:
            raise ValueError("extinction cannot precede measured shutdown command")


@dataclass(frozen=True)
class RecordingMetadata:
    start_index: int
    sample_count: int
    node_id: str
    operating_mode: str
    high_gain_scale: float
    low_gain_scale: float
    health: dict
    edges: tuple[SwitchingEdge, ...]
    events: tuple[LabelledEvent, ...]
    synthetic: bool
    verified_arc_free: bool
    provenance: str

    @classmethod
    def from_dict(cls, value):
        required = {"schema_version", "sample_rate_hz", "start_index", "sample_count", "node_id",
                    "operating_mode", "high_gain_scale", "low_gain_scale", "health", "edges", "events",
                    "synthetic", "verified_arc_free", "provenance"}
        if not isinstance(value, dict) or set(value) != required:
            raise ValueError("recording metadata has missing or unknown fields")
        if type(value["schema_version"]) is not int or value["schema_version"] != 1:
            raise ValueError("unsupported recording schema")
        if type(value["sample_rate_hz"]) is not int or value["sample_rate_hz"] != SAMPLE_RATE:
            raise ValueError("recording must use the normalized 4 MSPS processing rate")
        start = _integer(value["start_index"], "start_index")
        count = _integer(value["sample_count"], "sample_count", 1)
        if count > MAX_SAMPLES:
            raise ValueError("recording exceeds maximum bounded sample count")
        for key in ("node_id", "operating_mode", "provenance"):
            if not isinstance(value[key], str) or not value[key].strip() or len(value[key]) > 512:
                raise ValueError(f"{key} requires a bounded nonempty string")
        scales = [_finite(value[key], key) for key in ("high_gain_scale", "low_gain_scale")]
        if any(scale <= 0 for scale in scales):
            raise ValueError("gain scales must be positive current-units per recorded unit")
        health = value["health"]
        if not isinstance(health, dict) or set(health) != set(HEALTH_KEYS) or any(type(x) is not bool for x in health.values()):
            raise ValueError("recording requires explicit Boolean acquisition health fields")
        if any(type(value[key]) is not bool for key in ("synthetic", "verified_arc_free")):
            raise ValueError("synthetic and verified_arc_free require Boolean values")
        edges = []
        if not isinstance(value["edges"], list) or len(value["edges"]) > 100_000:
            raise ValueError("edges must be a bounded list")
        previous = float("-inf")
        for edge in value["edges"]:
            if not isinstance(edge, dict) or set(edge) != {"sample_index", "channel", "rising"}:
                raise ValueError("invalid switching edge schema")
            index = _finite(edge["sample_index"], "edge sample_index")
            if index < start or index >= start + count or index < previous:
                raise ValueError("switching edges must be in recording bounds and chronological")
            if not isinstance(edge["channel"], str) or not edge["channel"] or len(edge["channel"]) > 64 or type(edge["rising"]) is not bool:
                raise ValueError("invalid switching channel or polarity")
            edges.append(SwitchingEdge(index, edge["channel"], edge["rising"]))
            previous = index
        labels = []
        ids = set()
        if not isinstance(value["events"], list) or len(value["events"]) > 1000:
            raise ValueError("events must be a bounded list")
        required_event = {"event_id", "kind", "onset_s", "end_s", "arc_type"}
        optional_event = {"shutdown_command_s", "extinction_s", "terminal_zero_s"}
        for event in value["events"]:
            if not isinstance(event, dict) or not required_event <= set(event) or not set(event) <= required_event | optional_event:
                raise ValueError("invalid independent event schema")
            if not isinstance(event["event_id"], str) or not event["event_id"] or event["event_id"] in ids:
                raise ValueError("event IDs must be unique nonempty strings")
            ids.add(event["event_id"])
            if event["kind"] not in ("arc", "nuisance") or event["arc_type"] not in ("unknown", "series", "parallel"):
                raise ValueError("unsupported event label")
            onset, end = (_finite(event[key], key) for key in ("onset_s", "end_s"))
            if onset < start / SAMPLE_RATE or end > (start + count) / SAMPLE_RATE or end <= onset:
                raise ValueError("event must have positive duration within recording")
            times = {key: _finite(event[key], key) if event.get(key) is not None else None for key in optional_event}
            if any(timestamp is not None and timestamp < onset for timestamp in times.values()):
                raise ValueError("command/extinction/terminal-zero cannot precede event onset")
            if times["shutdown_command_s"] is not None and times["extinction_s"] is not None and times["extinction_s"] < times["shutdown_command_s"]:
                raise ValueError("extinction cannot precede the measured shutdown command")
            labels.append(LabelledEvent(event["event_id"], event["kind"], onset, end, event["arc_type"], **times))
        chronological_labels = sorted(labels, key=lambda event: event.onset_s)
        if any(right.onset_s < left.end_s for left, right in zip(chronological_labels, chronological_labels[1:])):
            raise ValueError("independent event intervals cannot overlap")
        if value["verified_arc_free"] and any(event.kind == "arc" for event in labels):
            raise ValueError("an independently labelled arc recording cannot train a healthy profile")
        return cls(start, count, value["node_id"], value["operating_mode"], *scales, MappingProxyType(dict(health)),
                   tuple(edges), tuple(labels), value["synthetic"], value["verified_arc_free"], value["provenance"])


@dataclass(frozen=True)
class SampleChunk:
    start_index: int
    current: np.ndarray
    both_gains_clipped: bool


def _select_gain(high, low, high_clipped, low_clipped, metadata, start_index):
    high, low = np.asarray(high, dtype=float), np.asarray(low, dtype=float)
    hc, lc = np.asarray(high_clipped), np.asarray(low_clipped)
    if high.ndim != 1 or any(array.shape != high.shape for array in (low, hc, lc)):
        raise ValueError("gain arrays have incompatible dimensions")
    if not np.isfinite(high).all() or not np.isfinite(low).all():
        raise ValueError("non-finite acquisition sample")
    if not np.isin(hc, [0, 1]).all() or not np.isin(lc, [0, 1]).all():
        raise ValueError("gain clipping flags must be 0/1")
    with np.errstate(over="ignore", invalid="ignore"):
        current = np.where(hc.astype(bool), low * metadata.low_gain_scale, high * metadata.high_gain_scale)
    if not np.isfinite(current).all():
        raise ValueError("gain normalization overflowed current units")
    clipped = hc.astype(bool) & lc.astype(bool)
    # A health fault belongs to the actual unusable sample, not the beginning
    # of the storage chunk that happens to contain it. Split into constant
    # health runs so an earlier arc decision remains chronologically first.
    boundaries = np.concatenate(([0], np.flatnonzero(clipped[1:] != clipped[:-1]) + 1, [current.size]))
    for left, right in zip(boundaries[:-1], boundaries[1:]):
        if right > left:
            yield SampleChunk(start_index + int(left), current[left:right], bool(clipped[left]))


def read_chunks(path: str | Path, metadata: RecordingMetadata, chunk_samples: int = 32768) -> Iterator[SampleChunk]:
    path = Path(path)
    if type(chunk_samples) is not int or not 256 <= chunk_samples <= 262144:
        raise ValueError("chunk size must be 256..262144 samples")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("recording exceeds bounded file size")
    if path.suffix.lower() == ".csv":
        with path.open(newline="", encoding="utf-8") as source:
            reader = csv.DictReader(source)
            if tuple(reader.fieldnames or ()) != COLUMNS:
                raise ValueError("CSV header must exactly match the normalized dual-gain schema")
            rows, read_count = [], 0
            for row in reader:
                if len(row) != len(COLUMNS) or None in row or any(value is None for value in row.values()):
                    raise ValueError("malformed CSV row")
                try:
                    index = int(row["sample_index"])
                    values = [float(row[key]) for key in COLUMNS[1:]]
                except (ValueError, TypeError) as error:
                    raise ValueError("invalid numeric CSV sample") from error
                if index != metadata.start_index + read_count or read_count >= metadata.sample_count:
                    raise ValueError("acquisition gap, duplicate or excess sample index")
                rows.append(values)
                read_count += 1
                if len(rows) == chunk_samples:
                    array = np.asarray(rows)
                    yield from _select_gain(*array.T, metadata, index + 1 - len(rows))
                    rows = []
            if read_count != metadata.sample_count:
                raise ValueError("sample count disagrees with recording metadata")
            if rows:
                yield from _select_gain(*np.asarray(rows).T, metadata, metadata.start_index + read_count - len(rows))
    elif path.suffix.lower() == ".npz":
        with zipfile.ZipFile(path) as archive:
            members = archive.infolist()
            if {entry.filename for entry in members} != {column + ".npy" for column in COLUMNS} or len(members) != len(COLUMNS):
                raise ValueError("NPZ must contain only the five numeric acquisition arrays")
            if sum(entry.file_size for entry in members) > MAX_FILE_BYTES:
                raise ValueError("NPZ uncompressed data exceeds bounded memory")
            # Inspect headers before np.load can allocate arrays from attacker-
            # supplied shapes, even when the compressed payload itself is tiny.
            for entry in members:
                with archive.open(entry) as member:
                    version = np.lib.format.read_magic(member)
                    if version == (1, 0):
                        shape, _, dtype = np.lib.format.read_array_header_1_0(member)
                    elif version == (2, 0):
                        shape, _, dtype = np.lib.format.read_array_header_2_0(member)
                    else:
                        raise ValueError("unsupported NPZ member format")
                    if shape != (metadata.sample_count,) or dtype.kind not in "fibu":
                        raise ValueError("NPZ member header requests invalid shape or type")
                    if entry.filename == "sample_index.npy" and dtype.kind not in "iu":
                        raise ValueError("NPZ sample indices must use an integer dtype")
        with np.load(path, allow_pickle=False) as source:
            arrays = [source[column] for column in COLUMNS]
            for array in arrays:
                if array.shape != (metadata.sample_count,) or array.dtype.kind not in "fibu":
                    raise ValueError("NPZ arrays must be bounded one-dimensional numeric samples")
            indices = arrays[0]
            for offset in range(0, metadata.sample_count, chunk_samples):
                end = min(offset + chunk_samples, metadata.sample_count)
                if not np.array_equal(indices[offset:end], np.arange(metadata.start_index + offset, metadata.start_index + end)):
                    raise ValueError("acquisition gap, duplicate or invalid sample index")
                yield from _select_gain(*(array[offset:end] for array in arrays[1:]), metadata, metadata.start_index + offset)
    else:
        raise ValueError("recording file must be CSV or NPZ")


def _plain(value):
    if is_dataclass(value):
        return {key: _plain(item) for key, item in asdict(value).items()}
    if isinstance(value, dict):
        return {key: _plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_plain(item) for item in value]
    if hasattr(value, "value"):
        return value.value
    if isinstance(value, np.generic):
        return value.item()
    return value


def event_report(events: tuple[LabelledEvent, ...], decisions: list[dict], *, synthetic: bool,
                 verified_arc_free: bool = False):
    """Associate decision time with independent intervals, never with frame truth."""
    trips = [decision for decision in decisions if decision["state"] in
             ("ARC_DETECTED", "PROTECTION_UNCERTAIN", "PROTECTION_UNAVAILABLE")]
    arc_events, nuisance_events = [], []
    assigned, nuisance_indices = set(), set()
    for event in events:
        matches = [(index, decision) for index, decision in enumerate(trips)
                   if event.onset_s <= decision["timestamp_s"] < event.end_s]
        if event.kind == "arc":
            match = next(((index, decision) for index, decision in matches if index not in assigned), None)
            if match is not None:
                assigned.add(match[0])
            decision_time = match[1]["timestamp_s"] if match is not None else None
            detected = match is not None and match[1]["state"] == "ARC_DETECTED"
            arc_events.append({"event_id": event.event_id, "independent_arc_type": event.arc_type,
                               "detector_arc_type": "unknown", "detected": detected,
                               "protective_shutdown": match is not None,
                               "shutdown_state": match[1]["state"] if match is not None else None,
                               "shutdown_reason": match[1].get("reason") if match is not None else None,
                               "decision_latency_s": decision_time - event.onset_s if detected else None,
                               "shutdown_request_latency_s": decision_time - event.onset_s if match is not None else None,
                               "measured_command_latency_s": event.shutdown_command_s - event.onset_s if event.shutdown_command_s is not None else None,
                               "measured_extinction_latency_s": event.extinction_s - event.onset_s if event.extinction_s is not None else None,
                               "measured_command_to_extinction_s": event.extinction_s - event.shutdown_command_s if event.extinction_s is not None and event.shutdown_command_s is not None else None,
                               "terminal_zero_latency_s": event.terminal_zero_s - event.onset_s if event.terminal_zero_s is not None else None})
        else:
            nuisance_indices.update(index for index, _ in matches)
            nuisance_events.append({"event_id": event.event_id, "shutdown_decisions": len(matches)})
    if verified_arc_free:
        nuisance_indices.update(range(len(trips)))
    latencies = [event["decision_latency_s"] for event in arc_events if event["detected"]]
    return {"synthetic": synthetic, "hardware_qualification": False,
            "arc_events": arc_events, "nuisance_events": nuisance_events,
            "missed_arc_events": sum(not event["detected"] for event in arc_events),
            "nuisance_shutdowns": len(nuisance_indices - assigned),
            "unlabelled_shutdowns": len(set(range(len(trips))) - assigned - nuisance_indices),
            "health_shutdowns": sum(decision["state"] == "PROTECTION_UNAVAILABLE" for decision in decisions),
            "decision_latency_s": {"count": len(latencies), "median": float(np.median(latencies)) if latencies else None,
                                   "p95": float(np.quantile(latencies, .95)) if latencies else None,
                                   "maximum": max(latencies) if latencies else None},
            "metric_note": "Detector decisions, measured shutdown command, independently observed arc extinction and terminal current zero are separate. One latched episode per recording; later arcs need separately reset recordings."}


def replay(path: str | Path, metadata: RecordingMetadata, profile: Profile, *,
           prototype: bool = False, chunk_samples: int = 32768):
    if profile.node_id != metadata.node_id:
        raise ValueError("recording node differs from profile node")
    if metadata.synthetic and (profile.spectral_qualified or profile.fast_qualified or profile.impulse_tail_qualified):
        raise ValueError("synthetic recordings cannot use hardware-qualified profiles")
    detector = Detector(profile, prototype=prototype)
    decisions = []
    edge_position = 0
    total_samples = 0
    spectral_frames = fast_blocks = 0
    tier_coverage_transitions = []
    previous_tiers = None
    last = None
    for chunk in read_chunks(path, metadata, chunk_samples):
        limit = chunk.start_index + chunk.current.size
        chunk_edges = []
        while edge_position < len(metadata.edges) and metadata.edges[edge_position].sample_index < limit:
            chunk_edges.append(metadata.edges[edge_position])
            edge_position += 1
        health = AcquisitionHealth(**metadata.health, both_gains_clipped=chunk.both_gains_clipped)
        # Labels and onset/extinction timestamps are deliberately absent here.
        last = detector.process(chunk.current, start_index=chunk.start_index,
                                edges=tuple(chunk_edges), health=health,
                                operating_mode=metadata.operating_mode)
        tiers = tuple(last.available_tiers)
        if tiers != previous_tiers:
            tier_coverage_transitions.append({"sample_index": chunk.start_index,
                                              "timestamp_s": chunk.start_index / SAMPLE_RATE,
                                              "available_tiers": list(tiers)})
            previous_tiers = tiers
        for decision in last.decisions:
            plain = _plain(decision)
            plain["timestamp_s"] = plain["timestamp_us"] / 1_000_000
            decisions.append(plain)
        spectral_frames += len(last.spectral_frames)
        fast_blocks += len(last.fast_blocks)
        total_samples += chunk.current.size
    report = event_report(metadata.events, decisions, synthetic=metadata.synthetic,
                          verified_arc_free=metadata.verified_arc_free)
    report.update({"schema_version": 1, "node_id": metadata.node_id, "profile_id": profile.profile_id,
                   "prototype": prototype, "sample_rate_hz": SAMPLE_RATE, "processed_samples": total_samples,
                   "spectral_frames": spectral_frames, "fast_blocks": fast_blocks,
                   "final_state": _plain(last.state), "shutdown_latched": last.shutdown_latched,
                   "permit": last.permit, "cancellation_active": last.cancellation_active,
                   "available_tiers": list(last.available_tiers),
                   "tier_coverage_transitions": tier_coverage_transitions,
                   "decisions": decisions})
    report["known_benign_observation_s"] = (metadata.sample_count / SAMPLE_RATE if metadata.verified_arc_free
                                            else sum(event.end_s - event.onset_s for event in metadata.events if event.kind == "nuisance"))
    return report
