# MTP_Teensy — local patches

Teensyduino 1.60+ compiles `cores/teensy4/MTP_*.cpp` and `WProgram.h` includes
the core headers. That API is not the KurtE snapshot this tree used to vendor:
`addFilesystem()` returns `bool`, and `useFileSystemIndexFileStore()` is gone.

The firmware therefore uses the 1.62 core headers (via `Arduino.h`) and compiles
**patched copies** of the 1.62 sources from `scripts/mtp_core162/` instead of the
unpatched core `.cpp` files. `scripts/skip_core_mtp.py` remaps those compile
nodes. `lib/MTP_Teensy/src` now holds only `mtp_wdog.h`.

Do not put the old KurtE headers back on the include path — they redefine
`MTP_class` after `WProgram.h` has already included the core header.

`scripts/patch_wprogram_mtp.py` copies the installed `framework-arduinoteensy`
package into `.pio/framework-arduinoteensy-teg` and removes
`#include "MTP_Teensy.h"` from **that copy's** `cores/teensy4/WProgram.h`.
1.62 added that include, which makes `FS.h` → `Arduino.h` → `WProgram.h` → MTP
→ `FS.h` re-enter `FS.h` while `class FS` is still incomplete. Firmware
includes `MTP_Teensy.h` after `Arduino.h`. The global PlatformIO package is
left untouched. `scripts/skip_core_mtp.py` points the compile at the copy.

## Patch 1: the device is read-only

`MTP_class::loop()` refuses `DeleteObject` (`0x100B`), `SendObjectInfo` (`0x100C`),
`SendObject` (`0x100D`), `FormatStore` (`0x100F`), `moveObject` (`0x1019`),
`copyObject` (`0x101A`) and `SetObjectPropValue` (`0x9804`) with
`MTP_RESPONSE_OBJECT_WRITE_PROTECTED`, at the single point where a host
request is dispatched. Those opcodes are also omitted from the GetDeviceInfo
supported-operations list, and `GetStorageInfo` reports AccessCapability
`0x0001` (read-only without object deletion). `src/sd_fs_adapter.h` independently
refuses every mutating filesystem call, so the three layers agree.

**`0x9804` was missed by the first version of this patch and added 2026-07-31**,
after an adversarial review found it. It was the only surviving write path:
`setObjectPropValue()` calls `storage_.rename()` for
`MTP_PROPERTY_OBJECT_FILE_NAME`.

If you add operations to the supported list, check them against this refusal list.

This is a safety measure, not a preference. Those operations reach code that
walks the filesystem or copies bytes **without bound and under host control,
inside one service call**. On this board each is either an 8 s watchdog reset
of a running inverter, or — if a watchdog kick is added to a loop whose
termination the host controls — a hang the watchdog can no longer rescue.
Refusing them leaves browse and read. Writes go through the authenticated HTTP API.

## Patch 2: watchdog service

Upstream runs an entire MTP file transfer or directory scan to completion inside
a **single `MTP.loop()` call**, and never services a watchdog. This firmware
arms WDOG1 with an 8 s timeout that **cannot be widened once enabled**.

`src/mtp_wdog.h` (path: `lib/MTP_Teensy/src/mtp_wdog.h`) provides
`mtpKickWatchdog()`.

Call sites (each marked `LOCAL PATCH`):

| File | Function | Loop patched |
| --- | --- | --- |
| `scripts/mtp_core162/MTP_Teensy.cpp` | `GetObject` (both T4 buffer variants) | per transmit chunk |
| `scripts/mtp_core162/MTP_Teensy.cpp` | `GetPartialObject` | per transmit chunk |
| `scripts/mtp_core162/MTP_Teensy.cpp` | `SendObject` | receive loop and abort-drain loop |
| `scripts/mtp_core162/MTP_Storage.cpp` | `ScanDir` | per directory entry |
| `scripts/mtp_core162/MTP_Storage.cpp` | `ScanAll` | per index entry |

`src/sd_fs_adapter.h` also kicks around the free-cluster scan behind `usedSize()`.

**Deliberately NOT patched:** sibling-relink walks in delete/move. Unreachable
behind patch 1; feeding the watchdog there would convert a reset into a hang.

## Patch 3: store index bounds (P1-19)

`mtpStoreInRange(store, get_FSCount())` after `Storage2Store` before `isMediaPresent` / store indexing; OOB returns `MTP_RESPONSE_INVALID_STORAGE_ID`.

## Why the KurtE callback files stay gone

`MTP_SD_Callbacks.cpp` and `MTP_USBFS_Callbacks.cpp` are still absent. They
need the framework `SD` library and `USBHost_t36`. Stores are registered as
plain `FS` objects (`src/sd_fs_adapter.h`).

## Upstream issues to be aware of (not fixed here)

- **KurtE #41:** `MTP.begin()` arms a 20 Hz IntervalTimer that answers
  `GetStorageInfo` from interrupt context. Call `MTP.begin()` last in `setup()`.
- File timestamps / leap-year bugs may still exist in the 1.62 core copy.
- `formatStore()` is refused at the dispatcher, so it is never entered.
