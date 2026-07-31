# Vendored MTP_Teensy — local patches

Upstream: <https://github.com/KurtE/MTP_Teensy> (MIT).
Pinned at commit `13ae26f39f6d9f3b690950dfb96ed0f79ee436d1` (2024-09-09, the
current `main`; upstream has no tags and `library.properties` has never moved
off 1.0.0, so the SHA is the only stable reference).

Vendored rather than used as a git dependency because it needs the local
patches below — the same reason `lib/aWOT` and `lib/eFlexPwm` are forks.

## Patch 1: the device is read-only

`MTP_class::loop()` refuses `DeleteObject` (`0x100B`), `SendObjectInfo` (`0x100C`),
`SendObject` (`0x100D`), `FormatStore` (`0x100F`), `moveObject` (`0x1019`),
`copyObject` (`0x101A`) and `SetObjectPropValue` (`0x9804`) with
`MTP_RESPONSE_OBJECT_WRITE_PROTECTED`, at the single point where a host
request is dispatched. `src/sd_fs_adapter.h` independently refuses every
mutating filesystem call, so the two layers agree.

**`0x9804` was missed by the first version of this patch and added 2026-07-31**, after
an adversarial review found it. It was the only surviving write path:
`setObjectPropValue()` calls `storage_.rename()` for
`MTP_PROPERTY_OBJECT_FILE_NAME`, so a host could rename `/settings.cfg` or a preset on
a device this file described as read-only. It was also still listed in the
supported-operations descriptor, so hosts were actively told rename would work — that
entry is now commented out as well, since advertising an operation that always answers
`OBJECT_WRITE_PROTECTED` only makes hosts offer a rename that cannot succeed.

If you add operations to the supported list, check them against this refusal list.
The two are not derived from one another, and the descriptor is the half a host reads.

This is a safety measure, not a preference. Those operations reach code that
walks the filesystem or copies bytes **without bound and under host control,
inside one service call**: the recursive `removeFile()`, the 4 KB-at-a-time
`CompleteCopyFile()`/`CopyByPathNames()` loops, and the recursive
`CopyFiles()`/`moveDir()` tree walks. On this board each is either an 8 s
watchdog reset of a running inverter, or — if a watchdog kick is added to a
loop whose termination the host controls — a hang the watchdog can no longer
rescue. Refusing them leaves exactly what the feature needs: browse and read.

It also means a host can never damage `/settings.cfg`, `/presets` or an
uploaded waveform. Writes go through the authenticated HTTP API.

## Patch 2: watchdog service

Upstream runs an entire MTP file transfer, directory scan or recursive delete
to completion inside a **single `MTP.loop()` call**, and never services a
watchdog. This firmware arms WDOG1 with an 8 s timeout fed once per superloop
pass, and on the i.MX RT1062 that timeout **cannot be widened or disabled
once enabled**. Unpatched, copying more than a few MB off the card — or the
first directory index walk on a card full of captures — is a guaranteed
hardware reset of a running inverter.

`src/mtp_wdog.h` (new file) provides `mtpKickWatchdog()`: the two register
writes of the documented WDOG1 service sequence, safe to call from anywhere.

Call sites added (each marked `LOCAL PATCH` in the source):

| File | Function | Loop patched |
| --- | --- | --- |
| `MTP_Teensy.cpp` | `GetObject` | per transmit chunk |
| `MTP_Teensy.cpp` | `GetPartialObject` | per transmit chunk |
| `MTP_Teensy.cpp` | `SendObject` | receive loop and abort-drain loop |
| `MTP_Storage.cpp` | `ScanDir` | per directory entry |
| `MTP_Storage.cpp` | `ScanAll` | per index entry |

`src/sd_fs_adapter.h` also kicks around the free-cluster scan behind
`usedSize()`, which walks the whole FAT.

**Deliberately NOT patched:** the sibling-relink walk in
`MTPStorage::DeleteObject` (and its twin in `MTPStorage::move`). Their only
exit is finding the target in the sibling chain, and a stale or damaged
index makes them spin forever — `ReadIndexRecord` synthesises `sibling = 0`
for index 0 and zeroes a record on a short read. Feeding the watchdog there
would convert a recoverable reset into a permanent hang with the PWM
free-running. Both are unreachable behind patch 1; the comment in the source
says so, to stop a future reader "fixing" the omission.

Nothing else is modified: no behaviour, API or protocol changes.

## Why two files were removed

`src/MTP_SD_Callbacks.cpp` and `src/MTP_USBFS_Callbacks.cpp` are deleted.
They are self-registering, optional media-detect helpers for the
`MTP_FSTYPE_SD` and `MTP_FSTYPE_USBFS` store types; nothing else in the
library references them, and this firmware registers its stores as
`MTP_FSTYPE_UNKNOWN` (see `src/sd_fs_adapter.h`), so they are dead code here.

They also cannot compile in this project: they pull in the Teensy framework's
`SD` library and `USBHost_t36`, and the framework's `SD.h` hard-errors against
upstream SdFat (`#error "Teensy's SD library uses a custom modified copy of
SdFat"`). Keeping them would mean abandoning `greiman/SdFat@^2.3.1` for the
framework's older bundled 2.1.2.

## Upstream issues to be aware of (not fixed here)

- **#41 "Active MTP seems to interfere with SD writing".** `MTP.begin()` arms a
  20 Hz IntervalTimer whose handler answers `GetStorageInfo` — which queries
  SdFat cluster counts **from interrupt context**. This firmware therefore
  calls `MTP.begin()` as the last statement of `setup()`, after the card is
  mounted and settings are loaded, so that window contains no other SD access.
- **#44** file timestamps are wrong on the host for even-numbered years after
  February (upstream leap-year bug in the FAT→MTP date conversion).
- `transmit_bulk()` discards the return of `usb_mtp_send(..., 50)`, which
  returns 0 on timeout — a stalled host can silently drop payload. Verify any
  copied capture you care about (upstream ships integrity-check examples).
- `formatStore()` re-arms the 20 Hz IntervalTimer *before* consulting the
  filesystem, so refusing `format()` at the adapter alone would not have
  prevented it. Patch 1 refuses the operation at the dispatcher instead, so
  `formatStore()` is never entered.
- Unconditional `PrintStream()` writes remain on some non-debug paths and
  default to `&Serial`, so library chatter interleaves with the firmware's
  own log during a session.
