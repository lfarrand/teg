# Product readiness: what this firmware would have to become

Written 2026-07-28, after a research pass on using this codebase as the engine
behind commercial products — in particular solar PV equipment combining arc-fault
detection and interruption, module optimisation, AC generation, MPPT and
monitoring.

The companion hardware analysis lives with the switch board
(`PRODUCT_ROADMAP_PV_ISOLATOR`, `APPLICATION_LANDSCAPE_FAST_SWITCH`). This file
covers only the **firmware** side and the conclusions that change what should be
built here.

> **Status update, 2026-08-01:** the six-lane release review in
> [REVIEW_2026-08-01.md](REVIEW_2026-08-01.md) remains **NO-SHIP** for current
> inverter use until hardware verification. The hardening branch implements the
> fail-dark boot, active-level checks, canonical/representable timing, atomic apply,
> strict schema/pin validation, recoverable persistence, fail-closed PSRAM and bounded
> network/USB/storage paths. Those fixes make a safer bench controller; they do not
> supply functional-safety independence, secure transport or product certification.

## 1. The four firmware facts that constrain the product

### 1.1 The capture path cannot see an arc

Capture is driven from the modulation ISR: **one sample per carrier cycle**. At a
20 kHz carrier that is 20 kSPS — Nyquist 10 kHz — against an arc signature that
lives in the 1–100 kHz band. Even at the 200 kHz carrier the config allows, the
inverter control loop is competing for the same ADC.

Arc detection needs a **separate acquisition path**: free-running ADC_ETC + DMA
into a ring at 250–500 kSPS, decoupled from the PWM ISR, fed by an HF current
transformer front end that does not exist on the board yet. 12-bit is marginal
for this; an external 16-bit ADC is the safer starting point. This is new
firmware architecture, not a configuration change.

### 1.2 The spread-spectrum dither actively sabotages arc detection

Carrier dither deliberately smears switching energy across a band — which is the
same band an arc detector listens to, and the same band an EMC test measures.
Dither and arc detection cannot both be enabled on one power stage. Worse, it is
a certification finding rather than a bug: a detector characterised without
dither and shipped with it enabled would fail differently in the field than on
the bench.

### 1.3 The safety function shares a superloop with the whole internet stack

Detection, trip, HTTP, MQTT, InfluxDB, SD, MTP and OTA all run in one loop under
one 8 s watchdog, in one flash image, on one MCU. That fails freedom-from-
interference on its face for any functional-safety argument, and it is why the
recommendation below is two MCUs rather than better scheduling.

The hardware trip path (**CMP → XBARA1 → FlexPWM FAULT0**) has the right
architectural shape because no software is in the trip path. It is not yet proven
on this board. The hardening build forces continuous high-speed comparator mode
rather than allowing a sampled filter to add uncertain delay. Keep the hardware
shape, measure pin-to-gate latency, and do not move fast trips into code.

### 1.4 MPPT as written is structurally string-level

The tracker evaluates every ~3 s because a settled measurement needs the meter's
1 s accumulation window plus the soft-start ramp. That is correct for a string,
and it is three orders of magnitude too slow for module-level optimisation.
Module-level wants **ripple correlation control**, which uses the converter's own
switching ripple as the perturbation and converges in a few switching cycles —
and which this codebase is unusually well placed to adopt, because it already
samples V and I once per carrier cycle on both ADC modules.

## 2. What the research changed about the product concept

Recorded because these were counter-intuitive and cost real time to establish:

- **A Tigo TS4-A-O is not a DC-DC optimiser.** It is a *partial-power current
  diverter* — a FET plus capacitor opening a PWM'd "current tunnel" in parallel
  with a weak module. Tigo's own literature says it is "NOT Distributed MPPT".
  It sits in near-total bypass whenever the array is matched, which is why it
  beats full-power optimisers on efficiency. If an optimiser is built here, this
  is the architecture to copy — not SolarEdge's.
- **"Module optimiser + string inverter in one box" is a category error.** An
  optimiser lives at the module; a string inverter lives at the string end. Both
  per module is a two-stage microinverter by another name. The coherent products
  are a microinverter with integrated AFCI, *or* a string-level box (MPPT +
  inverter + arc interrupter + rapid-shutdown), with MLPE staying separate.
- **The existing switch board is a bidirectional AC switch, not an inverter
  leg.** Common-source, no diodes, no defined freewheel path, and thermally sized
  for conduction rather than hard switching. An inverter or optimiser stage needs
  a *second, different* board: a real half-bridge with two isolated gate domains
  and 100–200 V silicon or GaN. The 2 kV board then plugs in unchanged as the DC
  interrupter, which preserves its certification work.

  **Desk-reviewed 2026-07-29 (5-agent adversarial pass, 4 of 5 verdicts
  "unsafe-as-described"; no hardware was built or measured).** The specific proposal of populating one MOSFET per
  board and strapping the vacant footprint to make four bridge switches is
  *electrically* sound — the strap bypasses nothing, the Kelvin gate loop is
  untouched, body-diode orientation is correct for a leg, and the single-switch
  role is thermally kinder than the board's intended one. It fails on five
  independent counts, any one of which destroys devices:

  1. **Firmware pair-mode path — fixed 2026-07-30, never bench-verified.**
     `INDEP` is now cleared for complementary pairs, and a pair that cannot be
     honoured holds both duties at zero. Until that is confirmed on a scope,
     treat first power-up as if the shoot-through risk were live: it was the
     most likely first-power-up device killer, and nothing has measured the fix.
  2. **The strap is an air-bridge, not a pad link.** A 1.97 mm routed slot (the
     drain–source creepage cut) sits between Q2's drain and source holes. It
     needs a formed wire through both plated holes, restrained and insulated.
  3. **No DC-link capacitance, snubber or clamp anywhere on the power nets**
     (only Q1, Q2, H1, H2 touch them), and the commutation loop now spans two
     PCBs: ~108 nH from the boards alone, 250–510 nH realistically, against
     gate resistors built at 1.00 Ω. That is 810–1600 V of turn-off overshoot,
     and the IMYH200R family publishes **no avalanche rating**, so V_DSS is a
     destruction limit. Infineon's own eval ceiling for this device+driver is
     1600 V, not 2000 V.
  4. **No current sensing in the power path at all.** The INA226 and its 10 mΩ
     shunt are on the 14–26 V *auxiliary* input, and the 1ED3124MU12H in
     PG-DSO-8 has no desat, no fault pin and no enable. The only interlock in
     the entire system is one MCU register.
  5. **The boards cannot currently be built**: 43 DNP flags cover the whole
     gate-drive subsystem. Worse, the obvious depopulation plan (keep Q1/IC3,
     drop Q2/IC5) keeps the channel whose `GND1` island has none of the 293
     ground vias, and discards the one with via-in-pad.

  Also corrected: there is **one** Murata MGJ6D122005SC and **one** LT3085
  shared by both channels, not one per channel; both gate drivers are
  hard-paralleled on the same PWM pair, so the board is one logical channel; and
  the 1ED3124MU12H datasheet carries **no V_IORM** (UL 1577 recognition only) —
  the binding figure is the 2300 V `V_OFFSET` destruction limit.

  As a firmware/control test rig at ≤100 V, single-pulse, one leg, the concept is
  usable once the pair-mode and dead-time behaviour is confirmed on a scope (fixed
  in firmware 2026-07-30, unverified). As a route to an inverter product it is not.
- **Speed is the wrong headline for the arc product.** Sandia's ignition data
  shows 750 J already sits below the ignition threshold of PV materials, so extra
  speed buys little additional fire safety — and a competent assessor will say so
  in front of a customer. Worse, a ~100 µs armed comparator trips on every DC
  disconnect operation and inverter inrush, and UL 1699B has an unwanted-trip
  test for exactly that.

  The defensible differentiators are **detection reliability under masking**
  (series inductance and capacitance to ground defeated six of seven listed
  products in Sandia's testing) and **provable self-test** — microsecond
  open-pulse verification, logged, which a mechanical AFCI physically cannot do.

## 3. Firmware work that would actually be needed

Roughly in dependency order. Items marked hardened are implemented for the bench
firmware but still require hardware evidence and do not constitute certification.

| # | Work | Why |
|---|---|---|
| 1 | Resolve the **QNEthernet AGPL** position | Attaches to any algorithm in the binary; irreversible once disclosed. See [SECURITY.md](SECURITY.md) |
| 2 | **Fail-dark boot and configuration transaction — hardened, unverified** | Global inhibit, strict schema/pins and protection-before-release are implemented; hardware proof remains |
| 3 | **Signed A/B OTA + anti-rollback** | Unsigned single-slot field update is both remote-code execution and physical-only recovery after an interrupted/failed copy |
| 4 | **Recoverable settings + fail-closed memory — hardened, unverified** | Generation/CRC live/tmp/backup recovery and mandatory-PSRAM inhibit are implemented; power-cut/fault-injection proof remains |
| 5 | **High-rate acquisition path** (ADC_ETC + DMA ring, 250–500 kSPS, external 16-bit ADC) | Precondition for any arc work |
| 6 | **Split safety from comms** — bare-metal safety MCU with no network stack, comms MCU running this firmware, physically unable to command the switch closed | Freedom from interference; also removes DNS, USB, SD and web stalls from the safety path |
| 7 | **Non-volatile authenticated event log and trusted time** | A RAM ring plus unauthenticated NTP proves nothing after a fire; an evidence trail is what insurers and investigators want |
| 8 | **Self-test scheduler** (µs open-pulse under MPPT, logged) | The actual moat, and currently one sentence with no hardware behind it |
| 9 | **Dither/detection interlock** | They cannot coexist; enforce it in validation rather than documentation |
| 10 | Nuisance-trip dataset: recorded inverter-noise library, MPPT transients, disconnect operations | The discrimination quality *is* the product |

## 4. Features worth adding, ranked

From a survey of ~20 candidates. The valuable ones all exploit the same unique
physical asset: **a series element that can open the string and then measure it
in isolation.** No competitor has that.

**Genuinely differentiating**

1. **Insulation resistance / prove-safe telemetry** with the switch open — report
   "verified isolated ≤ X µA". This is the line item a fire officer and an
   insurer actually want.
2. **Interrupter self-test** — and it yields Voc-based module temperature for free.
3. **Capacitor-load IV sweep** — the gold-standard array diagnostic, only possible
   because you can open the string.

**High value-to-effort market access**

4. **SunSpec Rapid Shutdown receiver** — string-level receivers are explicitly in
   scope, and the board's normally-off/fail-open DNA is already the right polarity.
5. **SunSpec Modbus** over the existing Ethernet stack — the interoperability
   language the installer channel speaks.

**Research/IP, not a datasheet claim**

6. **Connector resistance trending as an arc precursor.** The physics is real and
   published, but a 100 mΩ degraded connector in a 10 A/400 V string is a 0.25 %
   signal — inside 12-bit ADC noise. It becomes observable only via deliberate
   HF perturbation and synchronous detection, which the DDS + PWM + synchronous
   sampling + FFT stack could genuinely attempt. Treat as patentable research.

**Explicitly not worth it**: EV charge coordination and forecast-driven behaviour
(Home Assistant and evcc already do it better — just publish good data, which is
already done); grid services (compliance obligations, not features, and worth
nothing without aggregation); degradation trending (below measurement uncertainty
for years); PID night recovery (needs an HV bias supply, shrinking niche);
soiling analytics without a met station.

**Strategic risk to track**: UL 3741 lets an installer build a NEC 690.12-compliant
array with *no* rapid-shutdown devices at all, which structurally attacks the MLPE
market. The insurer/arc wedge routes around this, which is part of why it is the
right positioning.

## 5. Sequencing

The dataset is the asset that appreciates; the demo is not. Build the analog
front end and an arc-generation rig, characterise detection against the masking
conditions that defeat listed products, and capture everything reproducibly —
before building a box, and before showing anyone the algorithm. Resolve the
licence question and file any provisional patent before public disclosure, since
both become irreversible on first demo.
