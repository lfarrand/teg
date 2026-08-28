# Conventions

## Split host-testable math from target code

Put algorithm/state machines in headers (`*_math.h`, `modulation.h`, `pwm_timing.h`, parsers). Native Unity suites include those headers only. Do not add firmware `.cpp` to `native` — `test_build_src = no` is intentional; coverage is tested-header, not whole-firmware.

New host-testable unit: `test/test_<name>/test_main.cpp`, Unity, `setUp`/`tearDown`, `pio test -e native`.

Host suites: serde/ota/spectrum/thermal_math/waveform (80/80 last host run), not ISR/OUTEN.

`build_src_flags` (`-Wall -Wextra -Wdouble-promotion`) apply to `src/` only so library noise stays out.

## Memory placement

- Hot ISR / duty path: `FASTRUN` (ITCM), zero-wait DTCM for ISR state / sine LUT.
- Cold setup, status, MQTT/Influx: `FLASHMEM`.
- Large buffers: `EXTMEM` only after PSRAM is proven.
- Portable radix-2 FFT is the tested default. Do not enable `TEG_ENABLE_CMSIS_FFT` or global `-O3` (RAM1 floor). Spectrum default 1024 / JSON bins ≤128; capture clamp 32768. CMSIS UI picker and DTC checkbox are gone.

## Build-flag landmines

- `-DUSB_MTPDISK_SERIAL` bare. Never `USB_MTPDISK_SERIAL=1`.
- `-fno-strict-aliasing` is for the Teensy USB serial-number LTO type mismatch; keep it if LTO stays on.
- `-DDISABLE_FS_H_WARNING` + include order: SdFat vs framework `FS.h`.
- miniz: only inflate; keep `MINIZ_NO_ARCHIVE_APIS`, `MINIZ_NO_STDIO`, `MINIZ_NO_TIME`.
- Production `TEG_ENABLE_UNSAFE_LAB_OTA` stays undefined: `ota.h` stubs, empty `ota.cpp`/`flash_ota.cpp`, no `/api/ota` routes.

## Safety / API

Every `/api/*` needs `X-Auth-Pin`, same-subnet peer, valid Host; browser Origin must match. Rate-limit failures. Secrets never leave the device on export. `restoreSecrets` always restores the write PIN; MQTT/Influx secrets only when endpoint identity matches, otherwise clear and disable.

Preset/import must disable PWM IRQ via `pwmInterruptRequired()` (not only `spwmActive()`) before memcpy of `MainConfig`.

`configToJson` omits SyncPwm, CurrentLimit FilterCount/Period, and Tm2 cell PwmFrequency; reads and validate clamps stay.

Config is a complete versioned schema; reject partial/wrong-type/unsafe sections before touching hardware. Saves: generation + CRC, read-back, live/tmp/backup rotation.

Settings UI: `/api/status?lite=1` (no analogRead); presets/waveform GET on first panel open; export `/api/config?download=1`. `pico.min.css` is a ~2 KB token sheet at the same URL. OLED paints newest 5 EventLog lines; `logs[5]` deleted.

`serviceControlTasks()` from long HTTP handlers. It must not re-enter network/USB stacks.

MTP write opcodes stay refused at the dispatcher (`0x100B/C/D/F`, `0x1019/1A`, `0x9804`). Descriptor must not advertise refused ops. Adapter also refuses mutating FS calls. `MTP.begin()` must `MTP.loop()` while still inhibited; `mtpAllowsPwmRelease()` gates OUTEN. GetObjectHandles / Storage2Store store index must be `< get_FSCount()`.

## Libraries and pins

Do not restore framework `SD` / `USBHost_t36` to satisfy MTP callbacks — those files were deleted from vendored MTP; stores are `MTP_FSTYPE_UNKNOWN` via `src/sd_fs_adapter.h`.

QNEthernet: do not merge experimental IPv6/PTP branches (`docs/QNETHERNET_BRANCH_AUDIT.md`).

Keep registry `lib_deps` exact. Bump one library at a time and re-read bench checks.

`Feedback.LoopHz` default 250. `PowerMon.IntervalMs` default and validate floor 250. RAM Serial 30 s; Serial only if `Pwm.Verbose`. Keep `delay(1)` in the control loop (QNEthernet yield); do not replace it with bare `yield()`.

`include/defines.h`, `printDigits`, and `enableXbar` are deleted; do not restore them.

No clang-format / editorconfig in-repo; match surrounding file (2-space C++, existing comment density). Python scripts: stdlib, imports at top of module (SCons extra_scripts use `Import("env")` first, then stdlib — existing pattern).
