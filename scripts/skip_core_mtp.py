# PlatformIO post-build-env hook: compile against the project-local framework
# copy (patched WProgram.h) and replace cores/teensy4/MTP_*.cpp with the
# read-only + watchdog sources in scripts/mtp_core162.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.teensy_framework import (
    apply_framework_copy_paths,
    ensure_framework_copy,
    framework_copy_dir,
    remap_compile_path,
)
from scripts.teensy_mtp import project_mtp_include_dir

project_dir = env["PROJECT_DIR"]  # noqa: F821
installed = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))  # noqa: F821
copy = ensure_framework_copy(installed, framework_copy_dir(project_dir))
mtp_include = project_mtp_include_dir(project_dir)
env.Prepend(CPPPATH=[mtp_include])  # noqa: F821
apply_framework_copy_paths(env, installed, copy)  # noqa: F821


def remap_framework_node(node):
    try:
        path = node.srcnode().get_abspath()
    except Exception:
        return node
    replacement = remap_compile_path(str(path), project_dir, installed, copy)
    if replacement is None:
        return node
    return env.File(replacement)  # noqa: F821


env.AddBuildMiddleware(remap_framework_node)  # noqa: F821

try:
    Import("projenv")  # noqa: F821
except Exception:
    pass
else:
    projenv.Prepend(CPPPATH=[mtp_include])  # noqa: F821
    apply_framework_copy_paths(projenv, installed, copy)  # noqa: F821
