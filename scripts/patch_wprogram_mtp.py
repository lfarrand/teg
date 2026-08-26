# PlatformIO pre-build hook: Teensyduino 1.62 WProgram.h includes MTP_Teensy.h
# before FS is complete. Copy the installed framework into .pio and strip the
# include there. Do not write the global ~/.platformio package.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.teensy_framework import ensure_framework_copy, framework_copy_dir

installed = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))  # noqa: F821
copy = framework_copy_dir(env["PROJECT_DIR"])  # noqa: F821
ensure_framework_copy(installed, copy)
print(f"patch_wprogram_mtp: patched WProgram.h in {copy}")
