# Tech stack

## Languages

- Firmware: C++17 GNU dialect (`-std=gnu++17`), Arduino/Teensy core, O2 + LTO. Do not switch the release to global O3 (measured ~65 KiB extra ITCM, unsafe RAM1 stack). Do not enable `TEG_ENABLE_CMSIS_FFT`; portable radix-2 is the default. Spectrum default 1024 / JSON bins ≤128; capture clamp 32768. CMSIS UI picker is gone. Production `TEG_ENABLE_UNSAFE_LAB_OTA` stays undefined.
- Web UI: static HTML/CSS/JS, no bundler; embedded via pre-script.
- Tooling: Python 3 (CI pins 3.12.13); script tests are stdlib `unittest`.

## Build

PlatformIO. Envs in `platformio.ini`:

- `teensy41` — default firmware. `platform`, `platform_packages`, and `lib_deps` are **exact pins**.
- `native` — Unity + gcov; `test_build_src = no`. Host suites cover serde/ota/spectrum/thermal_math/waveform (80/80 last host run), not ISR/OUTEN.
- `native-sanitize` — ASan/UBSan. On Windows use MSYS2 CLANG64 and keep `clang64/bin` on PATH so the ASan DLL loads.

Load-bearing Teensy pins (read the `platformio.ini` comment before changing):

- Current pins: Teensyduino 1.62 (`framework-arduinoteensy@1.162.0`) + GCC 15.2.1 (`toolchain-gccarmnoneeabi-teensy@1.150201.0`) + `tool-teensy@1.162.0` on `teensy@5.2.0`.
- 1.60+ compiles `cores/teensy4/MTP_*.cpp` and `WProgram.h` includes those headers. The 1.62 API dropped `useFileSystemIndexFileStore()` / made `addFilesystem()` return bool. `scripts/skip_core_mtp.py` compiles `scripts/mtp_core162/*.cpp` (1.62 + read-only/watchdog patches) instead of the unpatched core objects. `lib/MTP_Teensy/src` is `mtp_wdog.h` only.
- Framework and toolchain must move together; mixing 1.162.0 with GCC 11.3 or 1.159.0 with GCC 15 fails.
- `teensy@5.2.0` builder adds `--no-warn-rwx-segments`; GCC 15 `ld` accepts it.
- `scripts/patch_wprogram_mtp.py` copies `framework-arduinoteensy` into `.pio/framework-arduinoteensy-teg` and strips `#include "MTP_Teensy.h"` from that copy's `WProgram.h` (1.62 FS.h include cycle). The global PlatformIO package is not written. `skip_core_mtp.py` remaps compile nodes and include paths onto the copy, then still compiles `scripts/mtp_core162` MTP sources.
- `SKIP_AUTO_UPDATE` still includes `teensy`, `framework-arduinoteensy`, `toolchain-gccarmnoneeabi-teensy`, `tool-teensy`.

Also pinned: `tool-scons`, registry `lib_deps` (no caret ranges), `native@1.2.1`.

`lib_ignore`: `Ethernet`, `VirtualWire`, `SD` (framework SD `#error`s against `greiman/SdFat`), `USBHost_t36`.

USB composite **must** be `-DUSB_MTPDISK_SERIAL` with no `=1`. The builder token-matches to drop `-DUSB_SERIAL`; `=1` defines both and MTP silently becomes CDC-only. PID becomes `0x04D5`.

CI Python: `requirements-ci.txt` (`platformio`, `gcovr`). Workflows install from that file.

## Libraries

QNEthernet is a registry dep (not a gitlink). IPv6/PTP/PHY branches are not wholesale-safe: `docs/QNETHERNET_BRANCH_AUDIT.md`.

aWOT fork: plain `Client*`, bounded writes, no QNEthernet dep. eFlexPwm fork: 16-bit duty; keep `EFLEXPWM_ENABLE_LOGGING` off.

## CI / supply chain

Six required jobs (also branch-protection on `main`): firmware (two hex `cmp` + size gate), native+coverage+ASan, forked-library tests, microbenchmarks, parser fuzz, secrets/SBOM/OSV. Details and limits: `docs/CI_SECURITY.md`.

Size floors (headroom, not exact image): flash files ≥7 MiB; RAM1 local ≥80 KiB; RAM2 heap ≥256 KiB; PSRAM free ≥768 KiB of 8 MiB. Script: `scripts/check_teensy_size.py`.

Dependabot: `github-actions`, `gitsubmodule`, pip group `ci-python`. No PlatformIO ecosystem — Monday `pio-updates.yml` instead. That job opens `deps/platformio-updates` with `GITHUB_TOKEN` by default, then `scripts/start_updater_ci.py` dispatches `ci.yml` and waits for a run on the head SHA. Optional repo secret `PIO_UPDATES_TOKEN` (PAT/App, contents + pull-requests write) makes a normal `pull_request` event and skips dispatch. Firmware cache keys hash the MTP/framework overlay scripts.

OSV lock: `scripts/osv-dependencies.json` maps registry/vendored sources to reviewed commits. The CI scan uses `--no-resolve` so `requirements-ci.txt` is checked as the two declared pins only. SBOM: `scripts/generate_sbom.py` (deterministic, no wall-clock).
