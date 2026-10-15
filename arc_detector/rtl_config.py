"""Freeze an existing passive integer envelope into a static RTL ROM package."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from .fast_oracle import Architecture, FastOracleEnvelope, FixedFastOracle
from .fixed_point import FixedDomain, coefficient_codes


def emit_rtl_config(directory, variant: Architecture, domain: FixedDomain,
                    envelope: FastOracleEnvelope):
    """Write new ROMs and manifest; refuse collisions or incompatible domains.

    ROMs contain only the unique half (including centre) of each symmetric
    FIR, as signed 24-bit two's-complement hexadecimal. Thresholds are unsigned
    64-bit mean powers. There is no online fitting and no hardware qualification.
    """
    oracle = FixedFastOracle(variant, domain, envelope)
    if domain.sample_bits != 24:
        raise ValueError("the current RTL port requires an explicitly declared 24-bit domain")
    contents = {}
    for name, coefficients in [("prefix.hex", variant.prefix_coefficients)] + [
            (f"bank{band}.hex", coefficients) for band, coefficients in enumerate(variant.bank_coefficients)]:
        codes = coefficient_codes(coefficients) if coefficients else (0,)
        if codes != codes[::-1]:
            raise ValueError("RTL symmetric MAC requires exactly symmetric quantized coefficients")
        unique = codes[:(len(codes) + 1) // 2]
        contents[name] = "".join(f"{code & 0xffffff:06x}\n" for code in unique)
    for name, thresholds in (("energy.hex", envelope.energy_q999), ("activity.hex", envelope.activity_floor)):
        if any(type(value) is not int or not 1 <= value < 1 << 64 for value in thresholds):
            raise ValueError("RTL threshold ROM requires positive unsigned 64-bit integers")
        contents[name] = "".join(f"{value:016x}\n" for value in thresholds)
    rtl = Path(__file__).parent / "rtl"
    manifest = {
        "schema_version": 1, "top": "arc_fast_raw_top", "synthesis_top": "arc_fast_synth_top",
        "rtl_sources": [str(rtl / name) for name in ("arc_serial_fir.sv", "arc_fast_raw_top.sv", "arc_fast_synth_top.sv")],
        "parameters": {"MULTIRATE": int(variant.decimation == 2)},
        "files": {"prefix": "prefix.hex", "banks": [f"bank{band}.hex" for band in range(6)],
                  "energy": "energy.hex", "activity": "activity.hex"},
        "sha256": {name: hashlib.sha256(content.encode()).hexdigest() for name, content in contents.items()},
        "architecture": variant.to_dict(), "domain": domain.to_dict(), "envelope": envelope.to_dict(),
        "arithmetic_certificate": oracle.arithmetic_certificate(),
        "clock_hz_assumption": 200_000_000, "source_period_cycles": 50,
        "start_phase": "arbitrary nonnegative int64 source index; /2 phase is absolute",
        "fir_pipeline": "issue/preadd, registered multiply, accumulation, ties-even rounding",
        "passive": True, "hardware_qualification": False, "permit": False,
        "device_fit_proven": False, "timing_closure_proven": False,
        "synthesis_wrapper_limitation": "internal source counter starts at zero and cannot detect external ADC gaps"}
    contents["manifest.json"] = json.dumps(manifest, indent=2, allow_nan=False) + "\n"
    destination = Path(directory)
    destination.mkdir(parents=True, exist_ok=True)
    if any((destination / name).exists() or (destination / name).is_symlink() for name in contents):
        raise FileExistsError("RTL configuration refuses to overwrite an existing ROM or manifest")
    created = []
    try:
        for name, content in contents.items():
            path = destination / name
            with path.open("x", encoding="utf-8", newline="\n") as output:
                created.append(path)
                output.write(content)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return manifest
