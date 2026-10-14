"""Passive raw-fast integer/golden-vector study, never a protection controller.

Run with ``python -m arc_detector.latency_study``. Sample-clock evidence,
host execution time and an ideal arithmetic workload are separate quantities.
No output of this module connects to TEG, grants permit or qualifies hardware.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, is_dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import sys
import tempfile
from time import perf_counter_ns

import numpy as np
import scipy

from .fast_oracle import FixedFastOracle, architecture, calibrate_fast
from .fixed_point import FixedDomain, FixedFIRDecimator, coefficient_codes
from .profile import DETECTION_RATE
from .replay import HEALTH_KEYS, LabelledEvent, RecordingMetadata, load_json, read_chunks
from .signal import default_filters

MAX_STUDY_SAMPLES = 2_000_000
VARIANTS = ("baseline_4m", "multirate_2m")


def _integer(value, name, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer within {minimum}..{maximum}")
    return value


def _json_safe(value):
    """Do not silently narrow FPGA integers in JavaScript JSON consumers."""
    if is_dataclass(value):
        return _json_safe(asdict(value))
    if isinstance(value, np.ndarray):
        return _json_safe(value.tolist())
    if isinstance(value, np.generic):
        return _json_safe(value.item())
    if isinstance(value, dict):
        return {key: _json_safe(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [_json_safe(item) for item in value]
    if type(value) is int and abs(value) >= 2 ** 53:
        return str(value)
    return value


def write_artifact(path, value):
    """Stage finite JSON and exclusively publish one complete artifact."""
    target = Path(path)
    if target.exists() or target.is_symlink():
        raise FileExistsError(f"output already exists: {target}")
    content = json.dumps(_json_safe(value), indent=2, allow_nan=False) + "\n"
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         prefix=".arc-latency-", suffix=".tmp",
                                         dir=target.parent, delete=False) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(content)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.link(temporary_path, target)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def workload_report(clock_hz=200_000_000, wide_product_lanes=14):
    """Ideal products/s, not native-DSP packing, MAC count or a timing report."""
    _integer(clock_hz, "clock_hz", 1, 2_000_000_000)
    _integer(wide_product_lanes, "wide_product_lanes", 1, 100_000)
    first, second = default_filters()
    common = [
        {"stage": "shared_20m_to_4m", "paths": 1, "input_rate_hz": 20_000_000,
         "factor": 5, "taps": len(first)},
        {"stage": "raw_and_residual_4m_to_500k", "paths": 2,
         "input_rate_hz": DETECTION_RATE, "factor": 8, "taps": len(second)},
    ]
    reports = {}
    for name in VARIANTS:
        variant = architecture(name)
        # These are the proposed, exactly symmetric quantized FIR recipes.
        prefix_taps = len(variant.prefix_coefficients)
        bank_taps, bank_rate = len(variant.bank_coefficients[0]), variant.bank_rate_hz
        if any(len(coefficients) != bank_taps for coefficients in variant.bank_coefficients):
            raise ValueError("workload grouping requires equal bank tap counts")
        recipes = [first, second, *variant.bank_coefficients]
        if variant.prefix_coefficients:
            recipes.append(variant.prefix_coefficients)
        for coefficients in recipes:
            integers = coefficient_codes(coefficients)
            if integers != integers[::-1]:
                raise ValueError("symmetric workload projection requires exactly symmetric integer coefficients")
        stages = [dict(row) for row in common]
        if prefix_taps:
            stages.append({"stage": "raw_and_residual_fast_prefix", "paths": 2,
                           "input_rate_hz": DETECTION_RATE, "factor": 2, "taps": prefix_taps})
        stages.append({"stage": "raw_and_residual_six_fast_bands", "paths": 2 * len(variant.bank_coefficients),
                       "input_rate_hz": bank_rate, "factor": 1, "taps": bank_taps})
        for row in stages:
            output_rate = row["input_rate_hz"] // row["factor"]
            row["unfolded_products_per_second"] = row["paths"] * output_rate * row["taps"]
            row["symmetric_products_per_second"] = row["paths"] * output_rate * ((row["taps"] + 1) // 2)
        folded = sum(row["symmetric_products_per_second"] for row in stages)
        ideal_capacity = clock_hz * wide_product_lanes
        prefix_delay = (prefix_taps - 1) / (2 * DETECTION_RATE) if prefix_taps else 0
        fast_delay = (len(first) - 1) / 40_000_000 + prefix_delay + (bank_taps - 1) / (2 * bank_rate)
        reports[name] = {"architecture": variant.to_dict(), "stages": stages,
                         "total_symmetric_products_per_second": folded,
                         "total_unfolded_products_per_second": sum(row["unfolded_products_per_second"] for row in stages),
                         "ideal_minimum_lane_clock_hz": folded / wide_product_lanes,
                         "ideal_capacity_products_per_second": ideal_capacity,
                         "ideal_arithmetic_margin_products_per_second": ideal_capacity - folded,
                         "raw_fast_filter_group_delay_us": fast_delay * 1e6,
                         "nominal_eight_block_evidence_us": 512.0,
                         "nominal_phase_plus_evidence_plus_group_delay_us": 64 + 512 + fast_delay * 1e6}
    return {"scope": "analytical full-width FIR products only; dual paths, spectral decimators included",
            "clock_hz_assumption": clock_hz, "wide_product_lanes_assumption": wide_product_lanes,
            "device_fit_proven": False, "timing_closure_proven": False,
            "latency_note": "Group delay is not complete impulse settling or a detection upper bound; use trace observations for candidate latency.",
            "excludes": ["ADC/gain arithmetic", "prediction", "FFT", "features", "control", "RAM conflicts",
                         "DSP decomposition/packing", "pipeline stalls", "analogue delay", "shutdown/extinction"],
            "variants": reports}


def candidate_metrics(events, decisions, *, start_index, sample_count, verified_arc_free=False):
    """Associate labels after processing, never feed them into the oracle."""
    if type(verified_arc_free) is not bool:
        raise ValueError("verified_arc_free must be Boolean")
    _integer(sample_count, "sample_count", 1, MAX_STUDY_SAMPLES)
    _integer(start_index, "start_index", 0, (2 ** 63 - 1) - sample_count)
    start_s, end_s = start_index / DETECTION_RATE, (start_index + sample_count) / DETECTION_RATE
    events = tuple(events)
    if any(not isinstance(event, LabelledEvent) for event in events):
        raise ValueError("events require independently supplied LabelledEvent values")
    if any(event.onset_s < start_s or event.end_s > end_s for event in events):
        raise ValueError("event labels are outside the evaluated capture")
    if len({event.event_id for event in events}) != len(events):
        raise ValueError("event identifiers must be unique")
    if verified_arc_free and any(event.kind == "arc" for event in events):
        raise ValueError("verified-arc-free evaluation contradicts an arc label")
    indices = [_integer(decision.source_end_index, "candidate source end", start_index,
                        start_index + sample_count) for decision in decisions]
    if indices != sorted(set(indices)):
        raise ValueError("candidate source clock must be strictly increasing")
    times = [index / DETECTION_RATE for index in indices]
    arcs = [event for event in events if event.kind == "arc"]
    results = []
    for event in arcs:
        found = next((time for time in times if event.onset_s <= time < event.end_s), None)
        results.append({"event_id": event.event_id, "candidate_observed": found is not None,
                        "onset_s": event.onset_s, "candidate_time_s": found,
                        "candidate_latency_s": None if found is None else found - event.onset_s,
                        "labelled_arc_type": event.arc_type, "inferred_arc_type": "unknown"})
    benign = [(start_s, end_s)] if verified_arc_free else [
        (event.onset_s, event.end_s) for event in events if event.kind == "nuisance"]
    # Exposure is interval union, not double-counted overlapping labels.
    merged = []
    for left, right in sorted(benign):
        if merged and left <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], right)
        else:
            merged.append([left, right])
    for event in arcs:
        remaining = []
        for left, right in merged:
            if event.end_s <= left or event.onset_s >= right:
                remaining.append([left, right])
            else:
                if left < event.onset_s:
                    remaining.append([left, event.onset_s])
                if event.end_s < right:
                    remaining.append([event.end_s, right])
        merged = remaining
    in_arc = lambda time: any(event.onset_s <= time < event.end_s for event in arcs)
    in_benign = lambda time: any(left <= time < right for left, right in merged)
    return {"labelled_arc_events": len(arcs), "missed_labelled_arc_events": sum(not row["candidate_observed"] for row in results),
            "event_candidates": results, "labelled_benign_exposure_s": sum(right - left for left, right in merged),
            "candidates_in_labelled_benign": sum(in_benign(time) and not in_arc(time) for time in times),
            "unlabelled_candidates": sum(not in_benign(time) and not in_arc(time) for time in times),
            "candidate_is_shutdown": False, "extinction_established": False}


def compare_codes(healthy_codes, evaluation_codes, *, domain, verified_healthy, provenance,
                  healthy_start=0, evaluation_start=0, events=(), synthetic=False,
                  evaluation_verified_arc_free=False, chunk_samples=8192):
    """Compare frozen, separately fitted variant envelopes on identical codes."""
    if not isinstance(domain, FixedDomain) or domain.sample_bits > 24:
        raise ValueError("the bounded latency study requires a declared domain of at most 24 bits")
    if verified_healthy is not True or not isinstance(provenance, str) or not provenance.strip():
        raise ValueError("explicit verified healthy provenance is required")
    if type(synthetic) is not bool:
        raise ValueError("synthetic must be Boolean")
    _integer(chunk_samples, "chunk_samples", 1, 32768)
    events = tuple(events)
    for codes in (healthy_codes, evaluation_codes):
        values = np.asarray(codes)
        if values.ndim != 1 or not 1 <= values.size <= MAX_STUDY_SAMPLES:
            raise ValueError("study captures must be nonempty bounded vectors")
        # Exercise the domain's exact integer validation before any fit/run.
        domain.decode(values)
    healthy_codes, evaluation_codes = np.asarray(healthy_codes), np.asarray(evaluation_codes)
    _integer(healthy_start, "healthy_start", 0, (2 ** 63 - 1) - len(healthy_codes))
    _integer(evaluation_start, "evaluation_start", 0, (2 ** 63 - 1) - len(evaluation_codes))
    # Validate labels before spending time fitting, but still outside detection.
    candidate_metrics(events, (), start_index=evaluation_start, sample_count=len(evaluation_codes),
                      verified_arc_free=evaluation_verified_arc_free)
    variants = {}
    for name in VARIANTS:
        variant = architecture(name)
        before_fit = perf_counter_ns()
        envelope = calibrate_fast(healthy_codes, architecture=variant, domain=domain,
                                  verified_healthy=True, provenance=provenance, start_index=healthy_start)
        calibration_ns = perf_counter_ns() - before_fit
        oracle = FixedFastOracle(variant, domain, envelope)
        decisions, timings = [], []
        block_count = 0
        for offset in range(0, len(evaluation_codes), chunk_samples):
            chunk = evaluation_codes[offset:offset + chunk_samples]
            before = perf_counter_ns()
            result = oracle.process(chunk, start_index=evaluation_start + offset)
            timings.append(perf_counter_ns() - before)
            block_count += len(result.blocks)
            decisions.extend(result.decisions)
            if result.permit or result.hardware_qualification or not result.passive:
                raise RuntimeError("passive oracle violated the study claim boundary")
        milliseconds = np.asarray(timings, dtype=np.float64) / 1e6
        total_ns = sum(timings)
        variants[name] = {"architecture": variant.to_dict(), "envelope": envelope.to_dict(),
                          "arithmetic_certificate": oracle.arithmetic_certificate(),
                          "valid_blocks": block_count, "candidate_decisions": [_json_safe(value) for value in decisions],
                          "metrics": candidate_metrics(events, decisions, start_index=evaluation_start,
                                                       sample_count=len(evaluation_codes), verified_arc_free=evaluation_verified_arc_free),
                          "host_runtime": {"calibration_ms": calibration_ns / 1e6,
                                           "processing_total_ms": total_ns / 1e6,
                                           "processing_samples_per_second": len(evaluation_codes) * 1e9 / max(total_ns, 1),
                                           "chunk_ms_p50": float(np.quantile(milliseconds, .5)),
                                           "chunk_ms_p95": float(np.quantile(milliseconds, .95)),
                                           "chunk_ms_p99": float(np.quantile(milliseconds, .99)),
                                           "chunk_ms_max": float(milliseconds.max()),
                                           "excludes": ["capture", "file I/O", "encoding", "telemetry", "shutdown"]}}
    return {"schema_version": 1, "kind": "passive_raw_fast_latency_study", "passive": True,
            "synthetic": synthetic, "hardware_qualification": False, "permit": False,
            "market_superiority_established": False, "hardware_latency_measured": False,
            "extinction_measured": False, "cancellation_ported": False, "spectral_fft_ported": False,
            "wide_integer_encoding": "decimal string when absolute value is at least 2**53",
            "domain": domain.to_dict(), "healthy_provenance": provenance,
            "healthy_start_index": healthy_start, "healthy_sample_count": len(healthy_codes),
            "evaluation_start_index": evaluation_start, "evaluation_sample_count": len(evaluation_codes),
            "processing_chunk_samples": chunk_samples,
            "environment": {"system": platform.system(), "machine": platform.machine(),
                            "python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__},
            "workload": workload_report(), "variants": variants}


def _recording_codes(path, metadata, domain, chunk_samples):
    if (metadata.sample_count > MAX_STUDY_SAMPLES or set(metadata.health) != set(HEALTH_KEYS)
            or not all(type(value) is bool and value for value in metadata.health.values())):
        raise ValueError("study requires bounded captures with all declared acquisition/reference/profile health valid")
    codes = []
    # File import has a256-sample minimum; numerical partition proofs may use
    # one-sample processing calls independently of the import batch size.
    for chunk in read_chunks(path, metadata, max(256, chunk_samples)):
        if chunk.both_gains_clipped:
            raise ValueError("a doubly clipped capture cannot provide latency or calibration evidence")
        codes.append(domain.encode(chunk.current))
    return np.concatenate(codes)


def compare_recordings(healthy_path, healthy_metadata, evaluation_path, evaluation_metadata, *,
                       domain, verified_healthy, chunk_samples=8192):
    if verified_healthy is not True or not healthy_metadata.verified_arc_free:
        raise ValueError("calibration capture requires both independent arc-free metadata and explicit attestation")
    if any(event.kind == "arc" for event in healthy_metadata.events):
        raise ValueError("an arc-labelled recording cannot train healthy envelopes")
    if (healthy_metadata.node_id, healthy_metadata.operating_mode) != (evaluation_metadata.node_id, evaluation_metadata.operating_mode):
        raise ValueError("healthy/evaluation node and operating mode must match")
    if Path(healthy_path).resolve() == Path(evaluation_path).resolve() or os.path.samefile(healthy_path, evaluation_path):
        raise ValueError("use separate healthy and evaluation recordings")
    _integer(chunk_samples, "chunk_samples", 1, 32768)
    before = perf_counter_ns()
    healthy = _recording_codes(healthy_path, healthy_metadata, domain, chunk_samples)
    evaluation = _recording_codes(evaluation_path, evaluation_metadata, domain, chunk_samples)
    import_ms = (perf_counter_ns() - before) / 1e6
    result = compare_codes(healthy, evaluation, domain=domain, verified_healthy=True,
                           provenance=healthy_metadata.provenance, healthy_start=healthy_metadata.start_index,
                           evaluation_start=evaluation_metadata.start_index, events=evaluation_metadata.events,
                           synthetic=healthy_metadata.synthetic or evaluation_metadata.synthetic,
                           evaluation_verified_arc_free=evaluation_metadata.verified_arc_free, chunk_samples=chunk_samples)
    result["node_id"], result["operating_mode"] = evaluation_metadata.node_id, evaluation_metadata.operating_mode
    result["evaluation_provenance"] = evaluation_metadata.provenance
    result["host_import_encode_ms"] = import_ms
    return result


def golden_vectors(domain, *, sample_count=2048, start_index=0, seed=903):
    """Elementary FIR-port vectors, not complete ADC/cancellation/protection RTL parity."""
    if not isinstance(domain, FixedDomain) or domain.sample_bits > 24:
        raise ValueError("golden vectors require an explicit domain of at most 24 bits")
    _integer(sample_count, "sample_count", 256, 4096)
    _integer(start_index, "start_index", 0, 2 ** 31 - sample_count)
    _integer(seed, "seed", 0, 2 ** 32 - 1)
    peak = max(1, 2 ** (domain.sample_bits - 3))
    inputs = np.random.default_rng(seed).integers(-peak, peak, sample_count, dtype=np.int64)
    first, second = default_filters()
    components = [("selected_20m_to_4m", first, 5, 20_000_000, domain.sample_bits),
                  ("raw_4m_to_500k", second, 8, DETECTION_RATE, domain.sample_bits)]
    for name in VARIANTS:
        variant = architecture(name)
        # Export recipes from the same architecture definitions as the oracle.
        if variant.prefix_coefficients:
            components.append((name + "_prefix", variant.prefix_coefficients, 2, DETECTION_RATE, domain.sample_bits))
        bank_input_bits = (FixedFIRDecimator(variant.prefix_coefficients, input_bits=domain.sample_bits).certificate.output_bits
                           if variant.prefix_coefficients else domain.sample_bits)
        components.extend((name + "_band_" + str(index), coefficients, 1, variant.bank_rate_hz, bank_input_bits)
                          for index, coefficients in enumerate(variant.bank_coefficients))
    streams = {}
    for name, coefficients, factor, rate, input_bits in components:
        engine = FixedFIRDecimator(coefficients, factor=factor, input_bits=input_bits)
        block = engine.process(inputs, start_index=start_index)
        streams[name] = {"input_rate_hz": rate, "decimation_factor": factor,
                         "certificate": engine.certificate.to_dict(),
                         "coefficient_codes": list(engine.coefficient_codes),
                         "output_codes": block.values.tolist(), "output_indices": block.sample_indices.tolist(),
                         "output_valid": block.valid.tolist()}
    return {"schema_version": 1, "kind": "elementary_fixed_fir_golden_vectors", "passive": True,
            "hardware_qualification": False, "permit": False, "complete_detector_parity": False,
            "coefficient_fractional_bits": 23, "rounding": "nearest, ties to even after exact accumulation",
            "scope_note": "Independent FIR port fixtures; identical supplied codes are not a cascaded ADC-to-detector capture. Widened ports retain the same physical LSB.",
            "wide_integer_encoding": "decimal string when absolute value is at least 2**53",
            "domain": domain.to_dict(), "seed": seed, "start_index": start_index,
            "input_codes": inputs.tolist(),
            "input_sha256_le_i64": hashlib.sha256(inputs.astype("<i8").tobytes()).hexdigest(),
            "streams": streams, "workload": workload_report()}


def _demo(domain, chunk_samples):
    rng = np.random.default_rng(2903)
    count = 90_000
    time = np.arange(count) / DETECTION_RATE
    switching = .02 * np.sin(2 * np.pi * 120_000 * time) + .02 * np.sin(2 * np.pi * 250_000 * time)
    healthy = switching[:60_000] + rng.normal(0, .004, 60_000)
    evaluation = switching + rng.normal(0, .004, count)
    onset = 16_003
    evaluation[onset:] += rng.normal(0, .6, count - onset)
    events = (LabelledEvent("constructed-broadband-signature", "arc", onset / DETECTION_RATE, count / DETECTION_RATE),)
    return compare_codes(domain.encode(healthy), domain.encode(evaluation), domain=domain,
                          verified_healthy=True, provenance="Deterministic numerical healthy fixture; not electrical bench evidence.",
                          events=events, synthetic=True, chunk_samples=chunk_samples)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    for name in ("compare", "demo", "vectors"):
        command = commands.add_parser(name)
        command.add_argument("--output", required=True)
        command.add_argument("--full-scale", type=float, required=True)
        command.add_argument("--unit-label", required=True)
        command.add_argument("--sample-bits", type=int, default=24)
        if name == "vectors":
            command.add_argument("--samples", type=int, default=2048)
            command.add_argument("--start-index", type=int, default=0)
        else:
            command.add_argument("--chunk-samples", type=int, default=8192)
        if name == "compare":
            command.add_argument("--healthy-recording", required=True)
            command.add_argument("--healthy-metadata", required=True)
            command.add_argument("--evaluation-recording", required=True)
            command.add_argument("--evaluation-metadata", required=True)
            command.add_argument("--verified-healthy", action="store_true")
    args = parser.parse_args(argv)
    try:
        if Path(args.output).exists() or Path(args.output).is_symlink():
            raise FileExistsError("output already exists")
        domain = FixedDomain(full_scale=args.full_scale, unit_label=args.unit_label, sample_bits=args.sample_bits)
        if args.operation == "vectors":
            report = golden_vectors(domain, sample_count=args.samples, start_index=args.start_index)
        elif args.operation == "demo":
            report = _demo(domain, args.chunk_samples)
        else:
            if not all((args.healthy_recording, args.healthy_metadata, args.evaluation_recording, args.evaluation_metadata)):
                raise ValueError("compare requires separate healthy/evaluation recordings and their metadata")
            healthy_meta = RecordingMetadata.from_dict(load_json(args.healthy_metadata))
            evaluation_meta = RecordingMetadata.from_dict(load_json(args.evaluation_metadata))
            report = compare_recordings(args.healthy_recording, healthy_meta, args.evaluation_recording,
                                        evaluation_meta, domain=domain, verified_healthy=args.verified_healthy,
                                        chunk_samples=args.chunk_samples)
        write_artifact(args.output, report)
    except (ValueError, TypeError, OSError, OverflowError) as error:
        parser.exit(2, f"arc latency study: {error}\n")
    print(str(Path(args.output).resolve()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
