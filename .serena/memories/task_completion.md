# Task completion

A coding task is not done until the matching local gates are green. There is no in-repo formatter or clang-tidy gate.

## Always (scripts or CI policy touched)

```
python -m unittest discover -s scripts/tests -p test_*.py -v
python scripts/verify_ci_policy.py
```

## Firmware / `src/` / `platformio.ini` / `web/`

```
pio test -e native --verbose
pio run -e teensy41
```

If the change can affect UB, parsers, or lifetime: also `pio test -e native-sanitize --verbose` (MSYS2 CLANG64 on Windows).

Coverage is CI-gated (`gcovr --filter src/ --fail-under-line 90 --fail-under-branch 60`). Locally, after `pio test -e native`, the same gcovr command if gcovr is installed. Native coverage ignores `src/*.cpp`.

Firmware size: CI runs `scripts/check_teensy_size.py` on the build log. If RAM1/flash/PSRAM moved, run it and do not “fix” a miss by only raising the floor.

Two clean hex files must compare equal in CI; a local `pio run -t clean` + rebuild is enough to catch an accidental non-reproducible stamp. `scripts/git_version.py` writes `src/version.h` (gitignored).

## Forks

If `lib/aWOT` or `lib/eFlexPwm` sources changed: their CMake/ctest suites (see `mem:suggested_commands`). Parent gitlink update is a separate commit after the submodule is pushed.

## Hardware-affecting PWM / MTP / ADC / trip

Host tests cannot close the task. Point at the relevant `docs/BENCH_CHECKS.md` items; do not claim bench-verified.

## Docs / comments / memories

Operator-doc, comment, and memory refreshes that only name already-landed contracts land via a feature-branch PR (e.g. #69), not a commit on `main`.

## Memories

After adding, renaming, or deleting Serena memories: `serena memories check` from the repo root.
