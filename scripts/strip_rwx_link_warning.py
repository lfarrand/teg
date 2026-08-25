# PlatformIO post-build-env hook: teensy@5.2.0 adds --no-warn-rwx-segments.
# GCC 15's linker accepts it; older Teensy linkers (11.3) reject it. Stripping
# is harmless either way and keeps a downgrade of the toolchain buildable.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.link_flags import without_rwx_segment_warning

env.Replace(LINKFLAGS=without_rwx_segment_warning(list(env.get("LINKFLAGS", []))))  # noqa: F821
