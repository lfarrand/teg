"""Project-local copy of framework-arduinoteensy so WProgram.h is not patched in-place."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from scripts.teensy_mtp import is_framework_core_mtp_source, patched_core_mtp_source
from scripts.wprogram_mtp import without_wprogram_mtp

COPY_DIRNAME = "framework-arduinoteensy-teg"
STAMP_NAME = ".teg-cow.json"
WPROGRAM = Path("cores") / "teensy4" / "WProgram.h"


def framework_copy_dir(project_dir: str | Path) -> Path:
    return Path(project_dir) / ".pio" / COPY_DIRNAME


def framework_identity(src: Path) -> str:
    package = src / "package.json"
    if package.is_file():
        data = json.loads(package.read_text(encoding="utf-8"))
        name = data.get("name") or src.name
        version = data.get("version") or ""
        return f"{name}@{version}"
    raise ValueError(f"{src} has no package.json")


def rewrite_abs_path(value: str, installed: Path, copy: Path) -> str:
    try:
        relative = Path(value).resolve().relative_to(installed.resolve())
    except ValueError:
        return value
    return str((copy.resolve() / relative))


def rewrite_compiler_flags(flags: list, installed: Path, copy: Path) -> list:
    rewritten = []
    for flag in flags:
        if not isinstance(flag, str):
            rewritten.append(flag)
            continue
        if flag.startswith("-I") and len(flag) > 2:
            rewritten.append("-I" + rewrite_abs_path(flag[2:], installed, copy))
            continue
        rewritten.append(rewrite_abs_path(flag, installed, copy))
    return rewritten


def rewrite_path_list(values: list, installed: Path, copy: Path) -> list:
    return [rewrite_abs_path(str(value), installed, copy) for value in values]


def remap_compile_path(path: str, project_dir: str, installed: Path, copy: Path) -> str | None:
    if is_framework_core_mtp_source(path):
        return patched_core_mtp_source(project_dir, Path(path).name)
    rewritten = rewrite_abs_path(path, installed, copy)
    if rewritten != path:
        return rewritten
    return None


def apply_framework_copy_paths(env, installed: Path, copy: Path) -> None:
    if "CPPPATH" in env:
        env.Replace(CPPPATH=rewrite_path_list(list(env["CPPPATH"]), installed, copy))
    for key in ("CCFLAGS", "CFLAGS", "CXXFLAGS", "ASFLAGS"):
        if key in env:
            env.Replace(**{key: rewrite_compiler_flags(list(env[key]), installed, copy)})


def ensure_framework_copy(installed: Path, copy: Path) -> Path:
    installed = installed.resolve()
    copy = copy.resolve()
    if installed == copy:
        raise ValueError("framework copy path must differ from the installed package")
    wprogram = installed / WPROGRAM
    if not wprogram.is_file():
        raise ValueError(f"missing {wprogram}")
    identity = framework_identity(installed)
    stamp = copy / STAMP_NAME
    if _stamp_matches(stamp, installed, identity) and (copy / WPROGRAM).is_file():
        _patch_copy_wprogram(copy)
        return copy
    if copy.exists():
        shutil.rmtree(copy)
    shutil.copytree(installed, copy, ignore=shutil.ignore_patterns(STAMP_NAME))
    _patch_copy_wprogram(copy)
    stamp.write_text(
        json.dumps({"source": str(installed), "identity": identity}, indent=2) + "\n",
        encoding="utf-8",
    )
    return copy


def _stamp_matches(stamp: Path, installed: Path, identity: str) -> bool:
    if not stamp.is_file():
        return False
    try:
        data = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, TypeError):
        return False
    return data.get("source") == str(installed) and data.get("identity") == identity


def _patch_copy_wprogram(copy: Path) -> None:
    wprogram = copy / WPROGRAM
    original = wprogram.read_text(encoding="utf-8")
    patched = without_wprogram_mtp(original)
    if patched != original:
        wprogram.write_text(patched, encoding="utf-8")
