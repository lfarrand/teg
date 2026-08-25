# PlatformIO pre-build hook: Teensyduino 1.62 WProgram.h includes MTP_Teensy.h
# before FS is complete. FS.h includes Arduino.h, so any TU that enters through
# FS.h sees an incomplete FS class inside the core MTP headers.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.wprogram_mtp import without_wprogram_mtp

framework = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))  # noqa: F821
wprogram = framework / "cores" / "teensy4" / "WProgram.h"
original = wprogram.read_text(encoding="utf-8")
patched = without_wprogram_mtp(original)
if patched != original:
    wprogram.write_text(patched, encoding="utf-8")
    print("patch_wprogram_mtp: removed MTP include from WProgram.h")
