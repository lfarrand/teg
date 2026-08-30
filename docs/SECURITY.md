# Security posture

Updated 2026-08-30 after the six-lane adversarial release review, its
hardening pass, and the landed 28 August slices. This firmware remains a
**bench instrument for a trusted, isolated LAN**. It is not suitable for
direct Internet exposure, an untrusted shared network, unattended generation,
or a commercial product.

The security controls below reduce accidental and LAN-reachable failure modes;
they do not turn HTTP bearer authentication into a secure remote-management
plane. The hardware and power-stage gates remain in
[BENCH_CHECKS.md](BENCH_CHECKS.md), and the original findings plus their
remediation status are recorded in
[REVIEW_2026-08-01.md](REVIEW_2026-08-01.md).

## Current threat model

**Assumed:** trusted operators, an isolated IPv4 subnet, physical access only by
the owner, no hostile device on the Ethernet path, and no fleet deployment.

**Not defended against:** a network observer who can read or replay cleartext
HTTP/MQTT/InfluxDB traffic, a malicious device already on the local subnet,
physical access to USB/SWD/the Program button/removable SD, theft of the shared
PIN, or compromise of the build/signing environment.

## Management API controls

All `/api/*` methods, including diagnostic GETs, now pass through one policy:

- a non-empty `X-Auth-Pin` must match in constant time; an empty or
  non-persisted first-boot PIN fails closed;
- the peer must be on the device's own IPv4 subnet;
- `Host` must be the device IP, its per-device `teg-<serial>` hostname, or the
  corresponding `.local` name;
- when a browser sends `Origin`, it must be exactly `http://` plus that `Host`;
- five failed PIN attempts from one address in 60 seconds block it for 60
  seconds; the first failure and block event are logged;
- config, log, crash, capture, spectrum, preset and OTA endpoints receive the
  same policy; only the static UI assets remain public;
- secret fields are redacted from JSON responses and exports. Empty secret
  fields in a POST use `preserveSecrets`: the value already stored on the
  device is kept.
- import and preset load use `restoreSecrets` (a different contract): the
  write PIN is always restored; MQTT/Influx secrets restore only when the
  endpoint identity matches, otherwise they are cleared and that integration
  is disabled. Do not merge these two helpers.

The PIN is generated from the hardware entropy source on first boot, displayed
locally, and must be durably written before PWM output release. If its first save
fails, the in-RAM PIN can authenticate a repair attempt but the provisioning
interlock stays asserted until the complete document passes read-back verification. The
configuration parser requires the full versioned schema and validates pin
ownership, switching limits, safety sections, and metering calibration before
anything is applied.

These controls block casual cross-site requests and several DNS-rebinding forms,
but there is still **no TLS**. The PIN and every credential travel in cleartext.
Use a physically/logically isolated VLAN with no routed access. If remote access
is unavoidable, terminate authenticated TLS at a maintained reverse proxy and
restrict the device-facing leg to that proxy; do not port-forward TCP/80.

## Parser and denial-of-service hardening

The local aWOT fork is a separate git submodule and carries regression-tested
changes that:

- reject negative, overflowing, and malformed `Content-Length` values;
- bound duplicate/maximum-length registered headers without writing past their
  buffers;
- enforce both no-progress and absolute header/response deadlines;
- retry legal short writes, but terminate a stalled peer rather than spinning;
- service only non-network control work and the watchdog while a request or
  response is waiting;
- cap pending silent TCP clients and reap them after a deadline.

Large waveform and OTA bodies use declared-length checks plus elapsed-time
deadlines. Waveform ingest writes a staging file and atomically rotates
live/backup names only after complete parsing; live stepped or streamed playback
cannot share the store with an upload. Raw captures are chunked while control
tasks continue to run.

No timeout proves hard real-time behavior. A response may still occupy the
single HTTP service path for up to its absolute budget, and QNEthernet socket
shutdown has an upstream bounded wait. This is why network isolation and the
hardware trip path are still required.

## OTA policy

OTA is **compiled out of production builds**. Production `/api/ota*` routes are
unregistered and return HTTP 404. Lab upload/commit exist only when a
developer deliberately builds with `TEG_ENABLE_UNSAFE_LAB_OTA`.

The lab updater stages into unused program flash, validates Intel-HEX bounds,
the FlexSPI/IVT/boot-data structure, board/project markers and a whole-image
CRC, then performs read-back verification. Those checks prove integrity and
compatibility only. They do **not** authenticate the publisher, prevent rollback,
or provide A/B recovery. A power loss after the live image starts being erased
can require the physical Program button and Teensy Loader.

Therefore the lab flag must never be used on an unattended or remotely managed
unit. A deployable updater needs an immutable verifier, an embedded public key,
signed images, anti-rollback state, and recoverable A/B storage. ROM HAB secure
boot is not compatible with the stock Teensy 4.1 HalfKay recovery model; a
closed, signed-boot product requires a custom RT1062 design.

## Secrets and local storage

InfluxDB, MQTT and API credentials are stored in plaintext JSON on the removable
SD card. Configuration persistence now uses generation numbers, CRC-validated
documents, read-back verification, and live/temporary/backup recovery, but it
does not encrypt data at rest.

Missing/invalid storage is fail-dark rather than a silent fallback: compiled
defaults select zero duty with modulation/asymmetric operation disabled, and a
separate provisioning interlock prevents OUTEN reconnection until a complete
configuration plus PIN has been durably promoted.

USB MTP is read-only, runs only while every PWM output is inhibited, and hides
the settings and preset trees. `MTP.begin()` must `MTP.loop()` while still
inhibited; `mtpAllowsPwmRelease()` holds OUTEN until that first loop. The USB
composite is always present (`-DUSB_MTPDISK_SERIAL`, PID `0x04D5`);
`Mtp.Enabled` only starts the service. Framework patches stay on the
copy-on-write tree `.pio/framework-arduinoteensy-teg`, not the global
PlatformIO package. This prevents ordinary host browsing from exposing those
credentials, but it is not a defence against someone removing the card or
using SWD/serial access. Do not reuse any device credential elsewhere.

## Time and logs

NTP replies are associated with a fresh request token and checked for source
address/port, server mode, stratum, leap state, packet length and epoch bounds.
This prevents stale or unrelated UDP packets from silently setting the RTC.
NTP itself remains unauthenticated, and the in-RAM event ring is not tamper-proof
or non-volatile evidence. Treat timestamps and logs as operational telemetry.

## Network identity and outbound services

QNEthernet obtains the factory-assigned Teensy MAC rather than using a shared
constant. DHCP and mDNS use a per-device `teg-<serial>` name. NTP, MQTT and
InfluxDB accept literal IP addresses without DNS; hostname resolution is only
attempted while outputs are inhibited so a resolver stall cannot freeze active
supervision. MQTT and InfluxDB are plaintext protocols in this build.

## Supply chain and licensing

`lib/aWOT` and `lib/eFlexPwm` are independent git submodules. Their changes must
be reviewed, tested, committed and pushed in their own repositories before the
parent gitlink is updated. `lib/miniz` and `lib/MTP_Teensy` are vendored in the
parent. Platform, framework, compiler, registry libraries, CI runner family, Python,
PlatformIO, gcovr and GitHub actions are pinned; CI also performs a second clean
firmware build and compares the resulting image. CI additionally enforces tested-
source line/branch coverage and memory headroom, runs short ASan/UBSan parser fuzz
campaigns, retains host benchmark JSON, scans the checked-out tree with a checksum-
verified Gitleaks binary, emits a deterministic CycloneDX inventory, and scans
exact upstream C/C++ source commits with checksum-verified OSV-Scanner. A known-
vulnerable sentinel must be detected before the real scan is trusted. These are
regression controls, not target-hardware or power-stage validation; the exact gates
and private-repository platform limitations are in
[CI_SECURITY.md](CI_SECURITY.md).

The current-tree secret scan deliberately does not inspect Git history, where an
old InfluxDB token is known to have existed. That token still must be revoked and
rotated. CodeQL is not enabled because this private repository does not currently
have GitHub Code Security; committing a workflow that permanently fails entitlement
checks would weaken, not improve, the signal from CI.

QNEthernet is AGPL-3.0-or-later. Shipping a linked firmware image can require
offering its complete corresponding source. Obtain a commercial licence, replace
the network stack, or intentionally comply with the AGPL before commercial use.
This is a licensing constraint, not a runtime security control.

## Residual release gates

Before this can move beyond an isolated bench:

1. Complete the disconnected-power-stage checklist, including boot/restart,
   complementary dead time, every fault polarity, ACMP/XBAR latency, ADC overrun,
   PSRAM failure, I2C recovery, network stalls and MTP maintenance gating.
2. Add signed, anti-rollback, recoverable updates or keep OTA permanently absent.
3. Replace cleartext bearer management with an authenticated encrypted channel.
4. Move or encrypt secrets and create a tamper-evident non-volatile audit trail if
   logs are intended as evidence.
5. For a product, separate the safety controller from the network/filesystem MCU
   and design a hardware inhibit that software cannot override.
