# QNEthernet branch audit (2026-08-02)

This project currently consumes released QNEthernet through PlatformIO. The
developer's experimental branches were reviewed in the fork at
`D:\git\QNEthernet` without changing that checkout. The reviewed baseline was
`origin/master` at `8c054180f8182c6fe615cd1c1fc938cdca580035`.

## Decision

Keep `master` as the production baseline. No public experimental branch is fit
for a wholesale merge. All build results below are compile-time evidence only;
none of the PHY, timestamp, or PTP paths has been validated on a Teensy 4.1 with
network test equipment.

| Branch | Divergence from `master` at review | Decision |
|---|---:|---|
| `ieee1588-2` | 116 behind, 58 ahead | Do not merge. Useful API prototype, but the confirmed defects below are release blockers. Reimplement from current `master` with tests. |
| `tdr` | 116 behind, 7 ahead | Selectively port only if cable diagnostics are needed. Reimplement the final MMD/TDR accessors and add a bounded completion timeout; do not merge the stale branch. |
| `no-driver-constants` | 58 behind, 6 ahead | Do not merge unless a runtime-MTU external driver is required. It changes compile-time API properties and conflicts with current `master`. |
| `tlsclient` | 58 behind, 153 ahead | Do not merge. The branch does not build in the default Teensy environment and its no-CA path disables peer verification. Design TLS afresh around current ALTCP support. |
| `hook-raw-frames` | 39 behind, 7 ahead | No port needed: the useful receive hook is already represented by the later raw-frame path on `master`. |
| `restart-auto-neg` | 510 behind, 1 ahead | No port needed: `git cherry` shows the patch is already equivalent to code on `master`, with later refinements. |
| older lwIP, PHY-reset, `altcp`, `c++11`, TeensyMM and workflow experiments | substantially stale | Do not merge; use current `master` and make narrowly scoped changes there. |

`master`, `ieee1588-2`, `tdr`, and `no-driver-constants` compiled for the
Teensy 4.1 during the audit. `tlsclient` did not compile because its Mbed TLS
dependency/configuration was absent. Compilation does not validate ENET register
sequencing or PHY behaviour.

## IEEE 1588 / PTP blockers

The `ieee1588-2` branch exposes timer adjustment plus RX/TX hardware timestamps,
but it should not be used until all of these are corrected and regression-tested:

1. The TX descriptor's Ready/ownership bit is set before its enhanced timestamp
   flag. The i.MX RT1060 ENET descriptor contract prohibits software changes after
   ownership is handed to hardware. Populate every descriptor field, issue the
   appropriate memory barrier, and set Ready last.
2. `writeTimer()` accepts negative seconds/nanoseconds, nanoseconds at or above
   one billion, and seconds wider than its 32-bit software counter. The casts
   silently wrap or truncate invalid input.
3. `writeTimer()` does not clear/reconcile an already-pending periodic timer event;
   that stale event can increment a newly written time by one second.
4. `adjustFreq(INT_MIN)` negates `INT_MIN`, which is signed-overflow undefined
   behaviour. Requests with magnitude above the 25 MHz timer clock also calculate
   `ATCOR=0`, disabling correction while returning success.
5. `readTimer()` can add a second when the timer wraps after capture but before the
   event-register test. Its wrap reconciliation must describe the captured instant,
   not the later status-read instant.
6. If TX timestamp-available and one-second timer events are pending together, the
   ISR assigns the new seconds value to a frame timestamped just before the wrap.
   Simply reversing ISR order breaks the corresponding just-after-wrap case; the
   captured nanoseconds must be compared with a coherently captured current timer.
7. RX timestamp reconstruction represents at most one elapsed wrap. Either enforce
   and test a sub-second drain-latency contract or retain enough epoch information
   to handle longer backlog.
8. Public timer calls unconditionally disable and then enable interrupts instead of
   restoring the caller's PRIMASK state. Use QNEthernet's existing save/restore HAL
   primitives so nested critical sections remain critical.

The relevant hardware rules are in i.MX RT1060 Reference Manual Rev. 4,
ENET sections 41.3.10.5, 41.3.14.2, and 41.5.1.89 through 41.5.1.93. The Rev. 11
errata review found no published ENET/1588 erratum that removes these software
obligations.

## IPv6

Current `master` contains lwIP's IPv6 implementation and can be configured with
`LWIP_IPV6`, but it defaults off and the public Arduino-compatible surface remains
largely IPv4-oriented. None of the public fork branches is a complete, current
IPv6 productisation branch. Treat IPv6 as a new feature tranche: define address,
DNS, listener, multicast, DHCPv6/SLAAC, UI/config, and dual-stack tests explicitly
instead of merging an old lwIP snapshot.

## Energy-Efficient Ethernet (IEEE 802.3az)

No public branch contains the developer's EEE additions. The Teensy 4.1's
DP83825I PHY supports 802.3az and TI documents a legacy-MAC mode, so support is
technically possible without an ENET MAC LPI signal. It is not just a register
toggle: software must advertise EEE, enter LPI only after TX is genuinely idle,
wake the PHY before queueing traffic, and honour the PHY's normal inter-frame
timing before transmission.

For this inverter controller, EEE is low priority until bench measurements show a
meaningful system-level saving. Link wake latency and a new TX state machine add
reliability risk to OTA, telemetry, and fault reporting. If implemented, begin on
current `master`, keep it opt-in, add timeout/fail-open recovery, and test under
continuous, bursty, and link-flap traffic.

Primary PHY references: TI's
[DP83825I datasheet](https://www.ti.com/lit/ds/symlink/dp83825i.pdf) and
[DP83825I Energy-Efficient Ethernet application note](https://www.ti.com/lit/an/snla328/snla328.pdf).

## Recommended order

1. Stay on released QNEthernet for the present firmware.
2. If useful, selectively implement TDR on current `master`, with bounded MDIO/TDR
   waits and a non-destructive diagnostics API.
3. Build a fresh IEEE 1588 branch from current `master`; fix descriptor ownership,
   time-domain validation, wrap reconstruction, and interrupt restoration before
   adding PTP servo logic.
4. Productise IPv6 as a tested dual-stack API change rather than a lwIP-only switch.
5. Consider EEE only after measuring its benefit and defining deterministic wake
   behaviour for safety-critical traffic.
