# PlatformIO post-build-env hook: WProgram.h includes cores/teensy4/MTP_Teensy.h,
# so the KurtE library cannot share the compile. Compile the 1.62 sources with
# our read-only + watchdog patches instead of the unpatched core .cpp files.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.teensy_mtp import (
    is_framework_core_mtp_source,
    patched_core_mtp_source,
    project_mtp_include_dir,
)

mtp_include = project_mtp_include_dir(env["PROJECT_DIR"])  # noqa: F821
env.Prepend(CPPPATH=[mtp_include])  # noqa: F821


def remap_core_mtp(node):
    try:
        path = node.srcnode().get_abspath()
    except Exception:
        return node
    if not is_framework_core_mtp_source(str(path)):
        return node
    replacement = patched_core_mtp_source(env["PROJECT_DIR"], Path(path).name)  # noqa: F821
    return env.File(replacement)  # noqa: F821


env.AddBuildMiddleware(remap_core_mtp)  # noqa: F821

try:
    Import("projenv")  # noqa: F821
except Exception:
    pass
else:
    projenv.Prepend(CPPPATH=[mtp_include])  # noqa: F821
