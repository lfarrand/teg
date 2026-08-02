#!/usr/bin/env python3
"""Parse Teensy size output and enforce explicit memory-headroom budgets."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


@dataclass(frozen=True)
class TeensySize:
    flash_code: int
    flash_data: int
    flash_headers: int
    flash_files_free: int
    ram1_variables: int
    ram1_code: int
    ram1_padding: int
    ram1_local_free: int
    ram2_variables: int
    ram2_heap_free: int
    extram_variables: int


PATTERNS = {
    "flash": re.compile(
        r"FLASH:\s+code:(\d+),\s+data:(\d+),\s+headers:(\d+)\s+"
        r"free for files:(\d+)"
    ),
    "ram1": re.compile(
        r"RAM1:\s+variables:(\d+),\s+code:(\d+),\s+padding:(\d+)\s+"
        r"free for local variables:(\d+)"
    ),
    "ram2": re.compile(r"RAM2:\s+variables:(\d+)\s+free for malloc/new:(\d+)"),
    "extram": re.compile(r"EXTRAM:\s+variables:(\d+)"),
}


def parse_teensy_size(text: str) -> TeensySize:
    clean = ANSI_ESCAPE.sub("", text)
    matches = {name: pattern.search(clean) for name, pattern in PATTERNS.items()}
    missing = [name for name, match in matches.items() if match is None]
    if missing:
        raise ValueError("missing Teensy size section(s): " + ", ".join(missing))

    flash = tuple(int(value) for value in matches["flash"].groups())
    ram1 = tuple(int(value) for value in matches["ram1"].groups())
    ram2 = tuple(int(value) for value in matches["ram2"].groups())
    extram = int(matches["extram"].group(1))
    return TeensySize(*flash, *ram1, *ram2, extram)


def evaluate_headroom(
    size: TeensySize,
    *,
    min_flash_files_free: int,
    min_ram1_local_free: int,
    min_ram2_heap_free: int,
    extram_capacity: int,
    min_extram_free: int,
) -> tuple[dict[str, int], list[str]]:
    if size.extram_variables > extram_capacity:
        extram_free = -1
    else:
        extram_free = extram_capacity - size.extram_variables

    measured = {
        "flash_files_free": size.flash_files_free,
        "ram1_local_free": size.ram1_local_free,
        "ram2_heap_free": size.ram2_heap_free,
        "extram_free": extram_free,
    }
    limits = {
        "flash_files_free": min_flash_files_free,
        "ram1_local_free": min_ram1_local_free,
        "ram2_heap_free": min_ram2_heap_free,
        "extram_free": min_extram_free,
    }
    failures = [
        f"{name} is {measured[name]} bytes; minimum is {minimum} bytes"
        for name, minimum in limits.items()
        if measured[name] < minimum
    ]
    margins = {name: measured[name] - minimum for name, minimum in limits.items()}
    return margins, failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="PlatformIO build log containing teensy_size output")
    parser.add_argument("--json-out", type=Path, required=True)
    parser.add_argument("--min-flash-files-free", type=int, required=True)
    parser.add_argument("--min-ram1-local-free", type=int, required=True)
    parser.add_argument("--min-ram2-heap-free", type=int, required=True)
    parser.add_argument("--extram-capacity", type=int, required=True)
    parser.add_argument("--min-extram-free", type=int, required=True)
    args = parser.parse_args(argv)

    try:
        size = parse_teensy_size(args.log.read_text(encoding="utf-8", errors="replace"))
        margins, failures = evaluate_headroom(
            size,
            min_flash_files_free=args.min_flash_files_free,
            min_ram1_local_free=args.min_ram1_local_free,
            min_ram2_heap_free=args.min_ram2_heap_free,
            extram_capacity=args.extram_capacity,
            min_extram_free=args.min_extram_free,
        )
    except (OSError, ValueError) as exc:
        print(f"size gate: {exc}", file=sys.stderr)
        return 2

    report = {
        "schema": 1,
        "status": "fail" if failures else "pass",
        "measurements": asdict(size),
        "extram_capacity": args.extram_capacity,
        "headroom_margins": margins,
        "failures": failures,
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(json.dumps(report, indent=2, sort_keys=True))
    for failure in failures:
        print(f"size gate: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
