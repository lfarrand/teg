"""Real HDL verification and mapped synthesis; never an electrical permit.

Run with ``python -m arc_detector.fpga_build`` and a fresh output directory.
The native OSS CAD Suite is explicit, versioned and outside the source tree.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np

from .fast_oracle import FixedFastOracle, architecture, calibrate_fast
from .fixed_point import FixedDomain, FixedFIRDecimator
from .latency_study import _json_safe

ROOT = Path(__file__).resolve().parent
SCENARIOS = ("broadband", "quiet", "impulse", "reset", "reset_busy", "busy_fault", "gap_fault")
SUITE_RELEASE = "2026-10-14"
WINDOWS_ARCHIVE_SHA256 = "580a1bbe938f4af861ff49e42944dc00933d61d087693e43c316023ea07f6e1e"


def _publish(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(_json_safe(value), stream, indent=2, allow_nan=False)
        stream.write("\n")


def _tools(directory):
    root = Path(directory).resolve(strict=True)
    suffix = ".exe" if os.name == "nt" else ""
    result = {name: root / "bin" / (name + suffix)
              for name in ("yosys", "iverilog", "vvp", "nextpnr-nexus")}
    if any(not value.is_file() for value in result.values()):
        raise ValueError("toolchain must contain yosys, iverilog, vvp and nextpnr-nexus in bin")
    environment = dict(os.environ)
    environment["PATH"] = os.pathsep.join((str(root / "bin"), str(root / "lib"), environment.get("PATH", "")))
    return root, result, environment


def _run(command, directory, environment, log_name, timeout=120):
    """Preserve the tool's real status and complete output in an exclusive log."""
    before = time.perf_counter()
    with (Path(directory) / log_name).open("x", encoding="utf-8", newline="\n") as stream:
        try:
            result = subprocess.run([str(value) for value in command], cwd=directory,
                                    env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            return {"command": [str(value) for value in command], "exit_code": result.returncode,
                    "elapsed_s": time.perf_counter() - before, "log": log_name, "timed_out": False}
        except subprocess.TimeoutExpired:
            return {"command": [str(value) for value in command], "exit_code": None,
                    "elapsed_s": time.perf_counter() - before, "log": log_name, "timed_out": True}


def tool_manifest(directory):
    root, tools, environment = _tools(directory)
    versions = {}
    for name, arguments in (("yosys", ["-V"]), ("iverilog", ["-V"]), ("nextpnr-nexus", ["--version"])):
        result = subprocess.run([str(tools[name]), *arguments], env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=20, check=False,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        lines = result.stdout.strip().splitlines()
        if result.returncode or not lines:
            raise RuntimeError(f"{name} version probe failed: {result.returncode}")
        versions[name] = lines[0]
    return {"toolchain_root": str(root), "versions": versions,
            "reference_distribution": {"release": SUITE_RELEASE, "windows_archive_sha256": WINDOWS_ARCHIVE_SHA256},
            "binary_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in tools.items()}}


def _fixtures(scenario, sample_count):
    if scenario not in SCENARIOS or type(sample_count) is not int or not 4096 <= sample_count <= 32768:
        raise ValueError("known scenario and 4096..32768 samples are required")
    healthy = np.zeros(8192, dtype=np.int64)
    values = np.zeros(sample_count, dtype=np.int64)
    random = np.random.default_rng(581)
    if scenario in ("broadband", "reset", "reset_busy"):
        values[1003:] = random.integers(-1_000_000, 1_000_001, sample_count - 1003, dtype=np.int64)
    if scenario == "reset":
        values[sample_count // 2:] = 0
    if scenario == "reset_busy":
        values[0] = (1 << 23) - 1
    if scenario == "impulse":
        values[1003] = (1 << 23) - 1
        values[1004] = -(1 << 23)
    return healthy, values


def _expected_filtered(values, start, variant, domain):
    bank_values, bank_start = values, start
    valid_after = None
    if variant.prefix_coefficients:
        prefix = FixedFIRDecimator(variant.prefix_coefficients, factor=2, input_bits=24)
        block = prefix.process(values, start_index=start)
        bank_values = block.values
        bank_start = int(block.sample_indices[0]) // 2
        valid_positions = np.flatnonzero(block.valid)
        valid_after = int(block.sample_indices[valid_positions[0]]) // 2 if valid_positions.size else 2 ** 63
        input_bits = prefix.certificate.output_bits
    else:
        input_bits = domain.sample_bits
    filters = [FixedFIRDecimator(coefficients, input_bits=input_bits) for coefficients in variant.bank_coefficients]
    blocks = [fir.process(bank_values, start_index=bank_start) for fir in filters]
    valid = blocks[0].valid.copy()
    if valid_after is not None:
        valid &= blocks[0].sample_indices >= valid_after + len(variant.bank_coefficients[0]) - 1
    return [(int(blocks[0].sample_indices[position]) * variant.decimation,
             tuple(int(block.values[position]) for block in blocks)) for position in np.flatnonzero(valid)]


def _parse_trace(path):
    trace = {"filtered": [], "blocks": [], "candidates": [], "resets": []}
    done = []
    for line in Path(path).read_text(encoding="utf-8", errors="strict").splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] not in ("F", "B", "C", "DONE", "R"):
            continue  # Complete raw tool output is retained; counts below refuse missing data.
        try:
            numbers = tuple(int(word) for word in words[1:])
        except ValueError as error:
            raise ValueError("HDL trace contains an unknown/noninteger observation") from error
        expected = {"F": 8, "B": 17, "C": 2, "DONE": 3, "R": 2}[words[0]]
        if len(numbers) != expected:
            raise ValueError("HDL trace observation has the wrong field count")
        if words[0] == "F":
            trace["filtered"].append((numbers[0], numbers[1], tuple(numbers[2:])))
        elif words[0] == "B":
            trace["blocks"].append(numbers)
        elif words[0] == "C":
            trace["candidates"].append(numbers)
        elif words[0] == "R":
            trace["resets"].append(numbers)
        else:
            done.append(numbers)
    if len(done) != 1:
        raise ValueError("HDL run did not produce exactly one final observation")
    trace["done"] = done[0]
    return trace


def verify(toolchain, directory, *, variant_name="baseline_4m", scenario="broadband", sample_count=8192, start_index=0):
    from .rtl_config import emit_rtl_config
    root, tools, environment = _tools(toolchain)
    if type(start_index) is not int or not 0 <= start_index <= (2 ** 63 - 1) - sample_count:
        raise ValueError("source clock must fit the declared int64 domain")
    directory = Path(directory).resolve()
    if directory.exists():
        raise FileExistsError("verification directory must be new")
    variant, domain = architecture(variant_name), FixedDomain(16, "synthetic-current-unit", 24)
    healthy, values = _fixtures(scenario, sample_count)
    envelope = calibrate_fast(healthy, architecture=variant, domain=domain, verified_healthy=True,
                              provenance="Constructed numerical zero-current fixture, not verified physical healthy operation")
    directory.parent.mkdir(parents=True, exist_ok=True)
    manifest = emit_rtl_config(directory, variant, domain, envelope)
    with (directory / "input.hex").open("x", encoding="ascii", newline="\n") as output:
        for offset, value in enumerate(values):
            output.write(f"{start_index + offset:016x}{int(value) & 0xffffff:06x}\n")
    reset_at = 1 if scenario == "reset_busy" else (sample_count // 2 if scenario == "reset" else -1)
    flags = {"MULTIRATE": int(bool(variant.prefix_coefficients)), "SAMPLES": sample_count,
             "INJECT_BUSY": int(scenario == "busy_fault"), "INJECT_GAP": int(scenario == "gap_fault"),
             "RESET_AT": reset_at, "RESET_BUSY": int(scenario == "reset_busy")}
    command = [tools["iverilog"], "-g2012", "-s", "arc_fast_raw_tb", "-o", "simulation.vvp"]
    for name, value in flags.items():
        command.extend(("-P", f"arc_fast_raw_tb.{name}={value}"))
    command.extend(manifest["rtl_sources"])
    command.append(str(ROOT / "hdl_tb" / "arc_fast_raw_tb.sv"))
    compile_result = _run(command, directory, environment, "compile.log")
    if compile_result["exit_code"] != 0:
        _publish(directory / "verification.json", {"passed": False, "compile": compile_result})
        raise RuntimeError(f"HDL compile failed; see {directory / 'compile.log'}")
    simulation = _run([tools["vvp"], "simulation.vvp"], directory, environment, "simulation.log")
    if simulation["exit_code"] != 0:
        _publish(directory / "verification.json", {"passed": False, "compile": compile_result, "simulation": simulation})
        raise RuntimeError(f"HDL simulation failed; see {directory / 'simulation.log'}")
    trace = _parse_trace(directory / "simulation.log")
    fault_expected = scenario in ("busy_fault", "gap_fault")
    if bool(trace["done"][1]) != fault_expected:
        raise AssertionError("protocol fault result differs from the directed case")
    expected_blocks, expected_decisions, expected_filtered = [], [], []
    if not fault_expected:
        for left, right in ((0, reset_at), (reset_at, len(values))) if reset_at >= 0 else ((0, len(values)),):
            part = values[left:right]
            oracle = FixedFastOracle(variant, domain, envelope)
            expected = oracle.process(part, start_index=start_index + left)
            expected_blocks.extend(expected.blocks)
            expected_decisions.extend(expected.decisions)
            expected_filtered.extend(_expected_filtered(part, start_index + left, variant, domain))
        observed_filtered = [(index, samples) for _, index, samples in trace["filtered"]]
        if observed_filtered != expected_filtered:
            mismatch = next((position for position, pair in enumerate(zip(observed_filtered, expected_filtered))
                             if pair[0] != pair[1]), min(len(observed_filtered), len(expected_filtered)))
            raise AssertionError(f"HDL filtered codes/history differ at {mismatch}; see simulation.log")
        blocks = [(row[1], row[2], bool(row[3]), row[4], tuple(row[5:11]), tuple(row[11:17])) for row in trace["blocks"]]
        wanted = [(block.source_end_index, sum(int(flag) << band for band, flag in enumerate(block.strong_subbands)),
                   block.qualifying, block.streak, block.energy_sums, block.active_fragments) for block in expected_blocks]
        if blocks != wanted:
            mismatch = next((position for position, pair in enumerate(zip(blocks, wanted)) if pair[0] != pair[1]),
                            min(len(blocks), len(wanted)))
            raise AssertionError(f"HDL block energy/activity/decision evidence differs at {mismatch}")
        if [index for _, index in trace["candidates"]] != [item.source_end_index for item in expected_decisions]:
            raise AssertionError("HDL candidate sample clock differs from frozen integer oracle")
        expected_latch = bool(expected_decisions) if reset_at < 0 else any(item.source_end_index >= start_index + reset_at for item in expected_decisions)
        if bool(trace["done"][2]) != expected_latch:
            raise AssertionError("HDL candidate latch/reset differs")
    elif trace["candidates"] or trace["done"][2]:
        raise AssertionError("protocol fault must inhibit candidates")
    report = {"schema_version": 1, "passed": True, "kind": "actual_hdl_integer_oracle_parity",
              "architecture": variant.to_dict(), "scenario": scenario, "sample_count": sample_count,
              "start_index": start_index, "filtered_outputs_compared": len(expected_filtered),
              "blocks_compared": len(expected_blocks), "candidate_count": len(trace["candidates"]),
              "candidates": [{"cycle": cycle, "source_end_index": index} for cycle, index in trace["candidates"]],
              "clock_hz_assumption": 200_000_000, "source_period_cycles": 50,
              "protocol_fault": bool(trace["done"][1]), "reset_count": len(trace["resets"]),
              "candidate_latched": bool(trace["done"][2]),
              "compile": compile_result, "simulation": simulation, "tools": tool_manifest(root),
              "testbench_sha256": hashlib.sha256((ROOT / "hdl_tb" / "arc_fast_raw_tb.sv").read_bytes()).hexdigest(),
              "rtl_sha256": {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in manifest["rtl_sources"]},
              "config_sha256": {file: hashlib.sha256((directory / file).read_bytes()).hexdigest()
                                for file in ("prefix.hex", *(f"bank{band}.hex" for band in range(6)),
                                             "energy.hex", "activity.hex", "manifest.json")},
              "passive": True, "permit": False, "hardware_qualification": False,
              "physical_latency_measured": False}
    _publish(directory / "verification.json", report)
    return report


def _yosys_quote(path):
    value = str(Path(path).resolve()).replace("\\", "/")
    if any(character in value for character in ('"', '\n', '\r', ';')):
        raise ValueError("unsupported control syntax in synthesis source path")
    return '"' + value + '"'


def _timing_target_met(fmax, frequency_mhz):
    """A missing, malformed or differently constrained report cannot pass."""
    if not isinstance(fmax, dict) or not fmax:
        return False
    for item in fmax.values():
        if not isinstance(item, dict):
            return False
        achieved, constraint = item.get("achieved"), item.get("constraint")
        if (type(achieved) not in (int, float) or type(constraint) not in (int, float)
                or not math.isfinite(achieved) or not math.isfinite(constraint)
                or not math.isclose(constraint, frequency_mhz, rel_tol=1e-6)
                or achieved < frequency_mhz):
            return False
    return True


def _utilization_fits(utilization):
    if not isinstance(utilization, dict) or not utilization:
        return False
    for item in utilization.values():
        if not isinstance(item, dict):
            return False
        used, available = item.get("used"), item.get("available")
        if type(used) is not int or type(available) is not int or not 0 <= used <= available:
            return False
    return True


def synthesize(toolchain, verified_directory, directory, *, family="lfd2nx", route=False, research_pdc=False,
               device="LFD2NX-40-9BG256C", frequency_mhz=200, timeout=1800):
    root, tools, environment = _tools(toolchain)
    if family not in ("lfd2nx", "xcup") or type(route) is not bool or type(research_pdc) is not bool:
        raise ValueError("only explicit lfd2nx/xcup mappings are supported")
    if route != research_pdc:
        raise ValueError("routing requires explicit --research-pdc acknowledgement; constraints are not a board pinout")
    if type(frequency_mhz) not in (int, float) or not 1 <= frequency_mhz <= 1000:
        raise ValueError("frequency must be finite and within 1..1000 MHz")
    if type(timeout) is not int or not 1 <= timeout <= 7200:
        raise ValueError("timeout must be bounded")
    if route and (family != "lfd2nx" or device != "LFD2NX-40-9BG256C"):
        raise ValueError("only the explicitly inventoried LFD2NX-40-9BG256C routing target is supported")
    source, directory = Path(verified_directory).resolve(strict=True), Path(directory).resolve()
    verification = json.loads((source / "verification.json").read_text(encoding="utf-8"))
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    if verification.get("passed") is not True or verification.get("protocol_fault") is not False:
        raise ValueError("synthesis requires a successful nonfault actual HDL verification")
    if type(manifest["parameters"]["MULTIRATE"]) is not int or manifest["parameters"]["MULTIRATE"] not in (0, 1):
        raise ValueError("invalid architecture parameter")
    for path in manifest["rtl_sources"]:
        if verification.get("rtl_sha256", {}).get(path) != hashlib.sha256(Path(path).read_bytes()).hexdigest():
            raise ValueError("RTL changed after verification; verify the current sources first")
    for file in ("prefix.hex", *(f"bank{band}.hex" for band in range(6)), "energy.hex", "activity.hex", "manifest.json"):
        if verification.get("config_sha256", {}).get(file) != hashlib.sha256((source / file).read_bytes()).hexdigest():
            raise ValueError("configuration changed after verification")
    if directory.exists():
        raise FileExistsError("synthesis directory must be new")
    directory.mkdir(parents=True)
    for file in ("prefix.hex", *(f"bank{band}.hex" for band in range(6)), "energy.hex", "activity.hex"):
        shutil.copyfile(source / file, directory / file)
    commands = ["read_verilog -sv " + " ".join(_yosys_quote(path) for path in manifest["rtl_sources"]),
                f"chparam -set MULTIRATE {manifest['parameters']['MULTIRATE']} arc_fast_synth_top"]
    if family == "lfd2nx":
        commands.append("synth_lattice -family lfd2nx -top arc_fast_synth_top -json mapped.json")
    else:
        commands.append("synth_xilinx -family xcup -flatten -top arc_fast_synth_top")
        commands.append("write_json mapped.json")
    commands.extend(("check -assert", "tee -o stat.json stat -json"))
    (directory / "synthesis.ys").write_text("\n".join(commands) + "\n", encoding="utf-8")
    synthesis = _run([tools["yosys"], "-Q", "-T", "-s", "synthesis.ys"], directory, environment, "synthesis.log", timeout)
    report = {"schema_version": 1, "kind": "actual_mapped_hdl_synthesis", "family": family,
              "architecture": manifest["architecture"], "tools": tool_manifest(root),
              "synthesis": synthesis, "source_verification": str(source / "verification.json"),
              "synthesis_succeeded": synthesis["exit_code"] == 0,
              "place_and_route_requested": route, "place_and_route_succeeded": False,
              "device_fit_proven": False, "timing_target_mhz": frequency_mhz,
              "timing_target_met": False, "board_pinout_constrained": False,
              "physical_latency_measured": False, "hardware_qualification": False, "permit": False}
    if synthesis["exit_code"] == 0:
        mapped = json.loads((directory / "mapped.json").read_text(encoding="utf-8"))
        top = mapped["modules"]["arc_fast_synth_top"]
        counts = {}
        for cell in top["cells"].values():
            counts[cell["type"]] = counts.get(cell["type"], 0) + 1
        report["top_mapped_cell_counts"] = counts
        report["netlist_sha256"] = hashlib.sha256((directory / "mapped.json").read_bytes()).hexdigest()
        if route:
            from .fpga_constraints import emit_research_pdc
            # Explicit bonded pads remove unconstrained-I/O ambiguity, but these
            # assumed-voltage research constraints are NOT a physical board pinout.
            report["synthetic_package_constraints"] = emit_research_pdc(
                directory / "mapped.json", directory / "research-only.pdc")
            (directory / "timing.sdc").write_text(
                f"create_clock -name detector_clk -period {1000 / frequency_mhz:.9f} [get_ports clk]\n", encoding="ascii")
            routing = _run([tools["nextpnr-nexus"], "--device", device, "--json", "mapped.json",
                            "--freq", str(frequency_mhz), "--sdc", "timing.sdc", "--pdc", "research-only.pdc", "--seed", "1",
                            "--timing-allow-fail", "--report", "route.json", "--detailed-timing-report", "--fasm", "route.fasm"],
                           directory, environment, "route.log", timeout)
            report["routing"] = routing
            report["timing_failure_allowed_for_measurement"] = True
            report["device"] = device
            report["place_and_route_succeeded"] = routing["exit_code"] == 0
            if (directory / "route.json").is_file():
                route_report = json.loads((directory / "route.json").read_text(encoding="utf-8"))
                report["route_report"] = route_report
                fasm = directory / "route.fasm"
                report["device_fit_proven"] = (routing["exit_code"] == 0 and fasm.is_file()
                                               and fasm.stat().st_size > 0
                                               and _utilization_fits(route_report.get("utilization")))
                fmax = route_report.get("fmax", {})
                report["timing_target_met"] = report["device_fit_proven"] and _timing_target_met(fmax, frequency_mhz)
    _publish(directory / "synthesis-report.json", report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    for operation in ("verify", "synth"):
        command = subparsers.add_parser(operation)
        command.add_argument("--toolchain", required=True)
        command.add_argument("--output-dir", required=True)
        if operation == "verify":
            command.add_argument("--architecture", choices=("baseline_4m", "multirate_2m"), default="baseline_4m")
            command.add_argument("--scenario", choices=SCENARIOS, default="broadband")
            command.add_argument("--samples", type=int, default=8192)
            command.add_argument("--start-index", type=int, default=0)
        else:
            command.add_argument("--verified-dir", required=True)
            command.add_argument("--family", choices=("lfd2nx", "xcup"), default="lfd2nx")
            command.add_argument("--route", action="store_true")
            command.add_argument("--research-pdc", action="store_true",
                                 help="acknowledge synthetic package pads, never a board pinout or programming permission")
            command.add_argument("--frequency-mhz", type=float, default=200)
            command.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args(argv)
    try:
        if args.operation == "verify":
            verify(args.toolchain, args.output_dir, variant_name=args.architecture, scenario=args.scenario,
                   sample_count=args.samples, start_index=args.start_index)
            filename = "verification.json"
        else:
            result = synthesize(args.toolchain, args.verified_dir, args.output_dir, family=args.family,
                                route=args.route, research_pdc=args.research_pdc,
                                frequency_mhz=args.frequency_mhz, timeout=args.timeout)
            filename = "synthesis-report.json"
            if not result["synthesis_succeeded"] or (args.route and not result["timing_target_met"]):
                print(str((Path(args.output_dir) / filename).resolve()))
                return 1
    except (ValueError, TypeError, OSError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        parser.exit(2, f"arc FPGA build: {error}\n")
    print(str((Path(args.output_dir) / filename).resolve()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
