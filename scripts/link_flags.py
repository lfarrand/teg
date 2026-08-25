"""Helpers for Teensy platform LINKFLAGS that the pinned toolchain cannot accept."""


def without_rwx_segment_warning(flags: list) -> list:
    cleaned = []
    for flag in flags:
        if not isinstance(flag, str):
            cleaned.append(flag)
            continue
        parts = [part for part in flag.split(",") if part != "--no-warn-rwx-segments"]
        if parts:
            cleaned.append(",".join(parts))
    return cleaned
