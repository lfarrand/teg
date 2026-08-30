# Suggested commands

Host is Windows. `pio` and `python` work in PowerShell the same as Unix. CI YAML is bash on Ubuntu — do not paste those pipelines into PowerShell without translating.

## Firmware / tests

```
pio run -e teensy41
pio run -e teensy41 -t clean
pio test -e native --verbose
pio test -e native-sanitize --verbose
```

Sanitize on Windows: MSYS2 CLANG64 toolchain; leave `clang64/bin` on PATH for the ASan DLL. CI uses Ubuntu's compiler.

Flash: Teensy Loader on `.pio/build/teensy41/firmware.hex`. Eject MTP before upload — an open session can block auto-reboot. PID `0x04D5` when MTP serial is enabled (COM port may move).

Device monitor: `pio device monitor` (needs CDC; `USB_MTPDISK_SERIAL`).

## Scripts / policy

```
python -m unittest discover -s scripts/tests -p test_*.py -v
python scripts/verify_ci_policy.py
python scripts/check_teensy_size.py .pio/build/teensy41/...log
```

`python scripts/pio_package_updates.py` runs `pio pkg outdated` for every `platformio.ini` environment. Do not auto-apply skipped Teensy packages. Generated PRs reuse `deps/platformio-updates`.

## Forks

```
git submodule update --init --recursive
cmake -S lib/aWOT/test -B build/awot -DCMAKE_BUILD_TYPE=Release
cmake --build build/awot --parallel
ctest --test-dir build/awot --output-on-failure
```

Same pattern for `lib/eFlexPwm/test`. Change code in the submodule checkout, commit/push there, then update the parent gitlink.

## Git / Serena

`main` is protected (PR, linear history, required CI, no force-push). Feature-branch work is normal.

From the project root: `serena memories check` from the repo root after adding/renaming/deleting memories.

PowerShell has no `$(cat <<'EOF')` heredoc. Use `git commit -m "subject" -m "body"` or a here-string piped to `git commit -F -`.
