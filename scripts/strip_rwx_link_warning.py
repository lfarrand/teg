# PlatformIO post-build-env hook: teensy@5.2.0 adds --no-warn-rwx-segments.
# The pinned toolchain-gccarmnoneeabi-teensy 1.110301.0 linker rejects it.

Import("env")  # noqa: F821

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env["PROJECT_DIR"])))  # noqa: F821
from scripts.link_flags import without_rwx_segment_warning

env.Replace(LINKFLAGS=without_rwx_segment_warning(list(env.get("LINKFLAGS", []))))  # noqa: F821
