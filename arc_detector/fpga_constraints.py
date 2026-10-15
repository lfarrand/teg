"""Explicit synthetic package constraints for resource/timing research only.

These are bonded package pads, NOT a board pinout. Never use this helper's
output to program hardware or infer ADC, TEG, interrupter or power compatibility.
The factual pad selection is frozen from the credited Oxide package database;
it needs no account, network download, vendor licence or runtime pin guessing.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


PART = "LFD2NX-40-9BG256C"
SOURCE_URL = ("https://raw.githubusercontent.com/gatecat/prjoxide-db/"
              "8d8174509356fa5bde12db614c230f7c425f9d2b/LIFCL/LFD2NX-40/iodb.json")
SOURCE_SHA256 = "a0f83c62022c3361b60a2373afa1052543e3ab2f7456de144609c6ffd5f52496"
# CABGA256 index 0 of the source pins arrays. F6 is PCLKT7_2/EIO. Remaining
# pads are other bonded EIOs followed by ordinary SIOs with no special function.
# Configuration/JTAG pads and unbonded '-' entries are deliberately excluded.
_EIO_PINS = (
    "F6", "D4", "D5", "D6", "E6", "E4", "E5", "C2", "D2", "E3", "D3",
    "C1", "B1", "D1", "E1", "F2", "E2", "G6", "F4", "G4", "G3", "G2",
    "H6", "H5", "H4", "H3", "H1", "H2", "J6", "J5", "J1", "J2", "K1",
    "K2", "K4", "K3", "K6", "K5", "L1", "L2", "M1", "M2", "F1", "G1",
    "L4", "L5", "M3", "L3", "N2", "N1", "C4", "C5", "A4", "B4", "B5",
    "A5", "A6", "B6", "B7", "A7", "C8", "C7", "K11", "K12", "J11",
    "J12", "J16", "J15", "H13", "H12", "H11", "H10", "H15", "H14",
    "H16", "B2", "B3", "A2", "G15", "A3", "E13", "F10", "E12", "E9", "F9",
)
_SIO_PINS = ("R2", "T3", "T4", "T5", "R6", "P5", "R5", "T6", "R7", "L8")
_PADS = tuple((pin, "LVCMOS18") for pin in _EIO_PINS) + tuple(
    (pin, "LVCMOS18H") for pin in _SIO_PINS)
_PORT_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_$]*\Z")
_MAX_NETLIST_BYTES = 512 * 1024 * 1024


def _is_top(value):
    return ((type(value) is int and value == 1) or
            (isinstance(value, str) and re.fullmatch(r"0*1", value) is not None))


def _port_names(design):
    if not isinstance(design, dict) or not isinstance(design.get("modules"), dict):
        raise ValueError("mapped JSON requires modules")
    tops = [(name, module) for name, module in design["modules"].items()
            if isinstance(module, dict) and isinstance(module.get("attributes"), dict)
            and _is_top(module["attributes"].get("top"))]
    if len(tops) != 1:
        raise ValueError("mapped JSON requires exactly one explicitly marked top")
    name, top = tops[0]
    ports = top.get("ports")
    if not isinstance(ports, dict) or not ports:
        raise ValueError("top requires ports")
    expanded = []
    for port, specification in sorted(ports.items()):
        if not isinstance(port, str) or _PORT_NAME.fullmatch(port) is None:
            raise ValueError("unsupported port name; PDC interpolation is refused")
        if not isinstance(specification, dict):
            raise ValueError("malformed port specification")
        bits = specification.get("bits")
        offset, upto = specification.get("offset", 0), specification.get("upto", 0)
        if (not isinstance(bits, list) or not bits or len(bits) > len(_PADS)
                or type(offset) is not int or not 0 <= offset <= 2 ** 31 - 1
                or type(upto) is not int or upto not in (0, 1)
                or specification.get("direction") not in ("input", "output", "inout")):
            raise ValueError("invalid port width, index or direction")
        if any(not ((type(bit) is int and bit >= 0) or
                    (isinstance(bit, str) and bit in ("0", "1", "x", "z"))) for bit in bits):
            raise ValueError("invalid mapped port bit")
        if port == "clk":
            if len(bits) != 1 or offset or upto or specification["direction"] != "input":
                raise ValueError("clk must be a scalar input")
            continue
        for position in range(len(bits)):
            index = offset + (len(bits) - 1 - position if upto else position)
            expanded.append(port if len(bits) == 1 and offset == 0 and not upto else f"{port}[{index}]")
    if "clk" not in ports:
        raise ValueError("a scalar input clk is required for the F6 research clock")
    names = ["clk", *expanded]
    if len(names) > len(_PADS) or len(names) != len(set(names)):
        raise ValueError("research package selection supports at most 95 unique port bits")
    return name, names


def _publish_pair(items):
    """Stage complete files, publish exclusive links and roll back our own links.

    Publication is not a transaction for observers; a reader must require both
    files and verify the PDC hash. Neither existing artifact is overwritten.
    """
    stages, published = [], []
    try:
        for destination, content in items:
            if os.path.lexists(destination):
                raise FileExistsError(destination)
            with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".research-pdc-", delete=False) as stream:
                stage = Path(stream.name)
                stages.append((stage, destination))
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
        for stage, destination in stages:
            os.link(stage, destination)
            published.append((stage, destination))
    except BaseException:
        for stage, destination in reversed(published):
            # Do not remove a file another actor replaced after publication.
            if destination.exists() and os.path.samefile(stage, destination):
                destination.unlink()
        raise
    finally:
        for stage, _ in stages:
            stage.unlink(missing_ok=True)


def emit_research_pdc(mapped_json, destination):
    """Publish deterministic PDC + provenance for this exact synthetic package.

    destination must be a new .pdc file in an existing directory. The sidecar is
    <stem>.provenance.json. No bitstream is created, no tool is run, no physical
    hardware is accessed, and no board/electrical compatibility is established.
    """
    mapped_json, destination = Path(mapped_json), Path(destination)
    if destination.suffix.lower() != ".pdc":
        raise ValueError("research constraints require a .pdc destination")
    if mapped_json.stat().st_size > _MAX_NETLIST_BYTES:
        raise ValueError("mapped netlist exceeds the 512 MiB research bound")
    raw = mapped_json.read_bytes()
    if len(raw) > _MAX_NETLIST_BYTES:
        raise ValueError("mapped netlist exceeds the 512 MiB research bound")
    top, names = _port_names(json.loads(raw))
    bindings = [{"port": port, "pin": pin, "io_type": mode}
                for port, (pin, mode) in zip(names, _PADS)]
    lines = ["# SYNTHETIC RESEARCH ONLY - NOT A BOARD PINOUT - NOT FOR PROGRAMMING HARDWARE.",
             "# No ADC, TEG, interrupter, isolation, power or electrical compatibility is qualified.",
             f"# {PART}; clock F6=PCLKT7_2; all bank supplies ASSUMED 1.8 V.",
             "# EIO=LVCMOS18; high-performance SIO=LVCMOS18H.",
             f"# Pin database: {SOURCE_URL}", f"# Pin database SHA256: {SOURCE_SHA256}"]
    for binding in bindings:
        port, pin, mode = binding["port"], binding["pin"], binding["io_type"]
        lines.extend((f"ldc_set_location -site {{{pin}}} [get_ports {{{port}}}]",
                      f"ldc_set_port -iobuf {{IO_TYPE={mode}}} [get_ports {{{port}}}]"))
    pdc = ("\n".join(lines) + "\n").encode("utf-8")
    provenance_path = destination.with_suffix(".provenance.json")
    provenance = {"schema_version": 1, "kind": "synthetic_package_constraints_not_board_pinout",
                  "hardware_programming_authorized": False, "device_fit_proven": False,
                  "timing_closure_proven": False, "part": PART, "package": "CABGA256", "top": top,
                  "bank_supply_assumption_v": 1.8, "clock_pin": "F6", "clock_function": "PCLKT7_2",
                  "pin_database_url": SOURCE_URL, "pin_database_sha256": SOURCE_SHA256,
                  "netlist_sha256": hashlib.sha256(raw).hexdigest(), "pdc_file": destination.name,
                  "pdc_sha256": hashlib.sha256(pdc).hexdigest(), "bindings": bindings}
    sidecar = (json.dumps(provenance, indent=2, allow_nan=False) + "\n").encode("utf-8")
    _publish_pair(((destination, pdc), (provenance_path, sidecar)))
    return {"pdc": str(destination), "provenance": str(provenance_path), "part": PART,
            "port_count": len(bindings), "hardware_programming_authorized": False}
