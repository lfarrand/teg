"""Compile patched Teensyduino 1.62 MTP instead of the unpatched core copy."""

from pathlib import Path

CORE_MTP_SOURCES = frozenset({"MTP_Teensy.cpp", "MTP_Storage.cpp"})


def is_framework_core_mtp_source(path: str) -> bool:
    normalized = path.replace("\\", "/")
    name = normalized.rsplit("/", 1)[-1]
    if name not in CORE_MTP_SOURCES:
        return False
    return "framework-arduinoteensy" in normalized and "/cores/" in normalized


def patched_core_mtp_source(project_dir: str, filename: str) -> str:
    return str(Path(project_dir) / "scripts" / "mtp_core162" / filename)


def project_mtp_include_dir(project_dir: str) -> str:
    return str(Path(project_dir) / "lib" / "MTP_Teensy" / "src")
