"""Offline calibration and replay CLI. No GPIO, PWM or shutdown hardware is driven."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import zipfile
import numpy as np

from .calibration import MAX_CALIBRATION_SAMPLES, calibrate
from .profile import Profile
from .replay import RecordingMetadata, load_json, read_chunks, replay, write_json


def _calibration_from_recording(recording, metadata, profile_id):
    if not metadata.verified_arc_free:
        raise ValueError("recording provenance does not mark this recording verified arc-free")
    if not all(metadata.health.values()):
        raise ValueError("healthy calibration requires every acquisition diagnostic to pass")
    if metadata.sample_count > MAX_CALIBRATION_SAMPLES:
        raise ValueError("calibration exceeds bounded sample budget")
    chunks = list(read_chunks(recording, metadata))
    if any(chunk.both_gains_clipped for chunk in chunks):
        raise ValueError("clipped healthy acquisition cannot train a profile")
    return calibrate(np.concatenate([chunk.current for chunk in chunks]), metadata.edges,
                     profile_id=profile_id, node_id=metadata.node_id,
                     operating_mode=metadata.operating_mode, verified_arc_free=True,
                     provenance=metadata.provenance, start_index=metadata.start_index)


def _write_calibration(output, result, metadata):
    write_json(output, result.profile.to_dict())
    write_json(str(output) + ".calibration.json", {
        "schema_version": 1, "profile_id": result.profile.profile_id,
        "provenance": metadata.provenance, "synthetic": metadata.synthetic,
        "verified_arc_free_assertion": metadata.verified_arc_free,
        "hardware_qualification": False, "spectral_frames": result.spectral_frames,
        "fast_blocks": result.fast_blocks, "fit_relative_rms": result.fit_relative_rms,
        "solver_stop": result.solver_stop, "solver_iterations": result.solver_iterations,
        "note": "Offline fit on this recording; qualify on independent held-out hardware events before deployment."})


def _save_npz(path, arrays):
    with Path(path).open("xb") as output:
        np.savez_compressed(output, **arrays)


def _demo(output_dir):
    from .examples.synthetic import make_recording
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    healthy_json, healthy_arrays = make_recording()
    metadata = RecordingMetadata.from_dict(healthy_json)
    healthy_file = directory / "healthy.npz"
    _save_npz(healthy_file, healthy_arrays)
    write_json(directory / "healthy.json", healthy_json)
    result = _calibration_from_recording(healthy_file, metadata, "synthetic-profile")
    profile_file = directory / "profile.json"
    _write_calibration(profile_file, result, metadata)
    for name, kwargs in (("arc", {"arc_onset_s": .025}), ("startup-arc", {"startup_arc": True})):
        recording_json, arrays = make_recording(seed=913, **kwargs)
        recording_file = directory / (name + ".npz")
        _save_npz(recording_file, arrays)
        write_json(directory / (name + ".json"), recording_json)
        report = replay(recording_file, RecordingMetadata.from_dict(recording_json),
                        result.profile, prototype=True)
        write_json(directory / (name + "-report.json"), report)
    print(f"{directory}: deterministic synthetic healthy calibration and two arc replay reports; no hardware qualification.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("calibrate", "replay"):
        command = commands.add_parser(name)
        command.add_argument("--recording", required=True, help="bounded normalized dual-gain CSV or NPZ")
        command.add_argument("--metadata", required=True, help="schema-1 JSON with independent event labels")
        command.add_argument("--output", required=True, help="new output JSON path; existing files are refused")
        if name == "calibrate":
            command.add_argument("--profile-id", required=True)
            command.add_argument("--verified-healthy", action="store_true", required=True,
                                 help="explicit operator assertion; metadata must also be verified_arc_free")
        else:
            command.add_argument("--profile", required=True)
            command.add_argument("--prototype", action="store_true", help="allow research decisions using unqualified profiles; permit stays false")
            command.add_argument("--chunk-samples", type=int, default=32768)
    demo = commands.add_parser("demo")
    demo.add_argument("--output-dir", required=True, help="new directory for deterministic synthetic artifacts")
    args = parser.parse_args(argv)
    try:
        if args.command == "demo":
            _demo(args.output_dir)
        else:
            metadata = RecordingMetadata.from_dict(load_json(args.metadata))
            if args.command == "calibrate":
                result = _calibration_from_recording(args.recording, metadata, args.profile_id)
                _write_calibration(args.output, result, metadata)
                print(f"{args.output}: immutable experimental switching templates and healthy envelopes; unqualified for hardware.")
            else:
                profile = Profile.from_dict(load_json(args.profile))
                report = replay(args.recording, metadata, profile, prototype=args.prototype,
                                chunk_samples=args.chunk_samples)
                write_json(args.output, report)
                print(f"{args.output}: event-level arc, nuisance and separate interruption metrics; offline replay only.")
    except (ValueError, OSError, TypeError, KeyError, EOFError, zipfile.BadZipFile) as error:
        print(f"arc_detector: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
