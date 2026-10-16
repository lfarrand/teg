# Independent detector FPGA interface, revision 1

This is the interface specification for a host reference implementation. No
hardware bitstream, physical board pin constraints, synthesizable complete detector, electrical
shutdown circuit or measured latency is supplied by this package. It does not
connect a host score to TEG OUTEN. Hardware layout and firmware integration follow
qualification of this reference and this contract.

## Acquisition boundary

The proposed device is TI ADS9219 (two simultaneous 18-bit channels, 20 MSPS per
channel). The original Certus-NX LFD2NX-40 is now a mapping comparator;
[VP1802 is the arithmetic-first target](FPGA_SELECTION.md), not a completed
ADC/board implementation. Feed the same HF current sensor into
calibrated high/low gain paths; these paths share common failure modes. Add
independent DC-capable current, isolated voltage and residual-current sensing.
The analogue path must be characterised from approximately 10 to 700 kHz, with
anti-alias rejection above the ADC's usable Nyquist bandwidth. ADC headroom does
not prove the CT has not saturated under its DC bias.

Explicitly configure raw 24-bit frames, two data lanes, DDR, two's-complement
format, OSR/averaging disabled and DDC disabled. At 20 MSPS the framed interface
is 480 Mbps per lane (960 Mbps total); the conversion payload alone is 720 Mbps.
Use a free-running external sampling clock and validate clock/data alignment.
Clock restart incurs an ADC data-valid settling interval (datasheet specifies
100 microseconds); withhold protection availability until diagnostics pass.

### Word decoding

The software accepts already aligned unsigned 24-bit words. Choose and verify
the XOR configuration; do not guess it from waveform appearance.

| Selected mode | Bits 23:6 | Remaining bits | Inverse operation |
|---|---|---|---|
| NONE | Signed 18-bit conversion | 5:0 appended user bits | Sign extend bit 17 |
| PRBS | Conversion XOR PRBS | 5:1 user bits, 0 PRBS | XOR all 18 conversion bits with the appended PRBS |
| LSB | Upper 17 conversion bits XOR conversion LSB; LSB retained | 5:0 user bits | XOR upper 17 bits with conversion bit 0, then sign extend |

ADC reset has XOR disabled. The PRBS selector is the default **when XOR is
enabled**; it is not the reset data mode. Select PRBS explicitly with XOR_EN=1
and XOR_MODE=0, or select the appropriate NONE/LSB decoder. Treat the appended
user bits as opaque unless a capture-specific expectation has been established;
do not assume zero padding or protection-status flags. `decode_words` optionally
checks a caller-supplied expectation but does not configure these bits.

Use independent ramp, test-pattern and fixed-word checks during bring-up.
Track received conversions with an FPGA-generated monotonically increasing
sample counter. Alignment, overflow, invalid clock, missed samples, repeated
blocks or queue starvation invalidate acquisition. A FIFO is a bounded buffer,
not permission to postpone the live protection decision.

## DSP and switching reference

Stream the selected calibrated channel through a 127-tap linear-phase FIR
decimator by five, producing 4 MSPS. The rising/falling switching-edge timestamps
use this same physical time base. The reference predictor includes the first
FIR's group delay (63 ADC periods = 3.15 microseconds = 12.6 samples at 4 MSPS).
Do not add host wall-clock scheduling delay to a sample timestamp.

Maintain raw and digitally cancelled paths. Frozen 512-coefficient edge
responses describe each known channel and operating mode. Unknown mode/reference
disables cancellation; if the qualified raw profile is still applicable it must
continue observing. Never use the low-gain current measurement as a noise-only
reference or fit coefficients to samples from an operating, possibly arcing line.
The reference binds raw applicability to an explicitly supplied matching
profile mode: omission or mismatch requests protection unavailability. A
missing PWM reference alone does not invalidate that matching raw profile.

The 4 MSPS fast tier uses fresh, non-overlapping 256-sample blocks (64
microseconds). The spectral tier further decimates by eight with a 255-tap FIR
and uses non-overlapping 1024-sample frames at 500 kSPS (2.048 milliseconds).
Characterise passbands and alias rejection after coefficient quantisation;
discarding samples without filtering is not a decimator. The FIR group-delay
calculation alone is not the analogue/filter impulse-tail qualification.

The [latency-first preparation](LATENCY_STUDY.md) supplies certified integer
elementary FIR vectors and a passive raw fast-path oracle. It also compares an
experimental 2 MSPS fast bank with additional anti-alias filtering. It is not
complete ADC/predictor/spectral/protection RTL or a qualified architecture
change. Bind its separate thresholds to the exact filter recipe/numeric domain;
do not reuse the original floating profile or infer device fit from tap counts.

[Actual raw-fast HDL and synthesis](FPGA_SYNTHESIS.md) now covers those two
integer banks, activity/evidence and passive candidates. The core accepts
`in_valid`, signed24-bit `in_sample` and64-bit `in_index`, with ready/busy,
candidate/index/latch and protocol-fault outputs. It is not the ADC receiver,
cancellation/residual/spectral pipeline or protection-state implementation.
The synthesis wrapper's internal index cannot establish ADC continuity.
Synthetic bonded-pad PDC is explicitly non-board/non-programming research;
neither Nexus variant fits the tested40-class device and no routed timing
has been established for the chosen VP1802.

Either raw or residual evidence can latch an arc. Fast-tier enable additionally
requires independent fast-band, nuisance and complete impulse-tail qualification.
Loss of that tier reports robust-only operation. Loss of all qualified spectral
coverage requests shutdown for protection unavailability. Profile qualification
flags represent a trusted deployment decision, not proof supplied by Python or
by a JSON document. All generated demonstration profiles remain unqualified.

## Protection and telemetry boundary

The future hardware exports a latched FAULT request and an independently gated
RUN_PERMIT. Their electrical polarity, isolation, reset/broken-wire behavior and
interrupter action must be chosen and verified in the board design. A telemetry
message cannot provide RUN_PERMIT. Prototype mode always reports permit=false.

The detector states are INITIALISING, MONITORING, SUSPECT, ARC_DETECTED,
PROTECTION_UNCERTAIN and PROTECTION_UNAVAILABLE. A health fault cannot be
relabelled as an arc or silently cleared by quiet samples. Keep acquiring after
a trip. Explicit reset requires inhibited outputs and passing diagnostics;
restart also reinitialises filter, frame, cancellation and evidence history.

Telemetry carries node/profile identity, physical sample time, state, first
latched reason, tier availability, cancellation validity, raw/residual band
evidence and coverage metadata. An eventual transport must include framing,
version, sequence and corruption detection. TEG exposes it through existing
status/API mechanisms only after a separate integration change. TEG's current
ordinary capture and HTTP spectrum endpoint are not this acquisition path.

Do not infer series versus parallel arc type from HF spectrum alone. A
string-end sensor can miss an upstream loop. Record observed zones, source paths,
shutdown zones and excluded loops. Current-zero at the terminal is only terminal
interruption; it does not prove that an upstream arc is extinguished. Module
internal faults and other unobserved source loops require explicit exclusions.

## Timing and hardware acceptance

The three-frame strong spectral rule collects 6.144 ms of evidence. Worst
alignment requires up to 8.192 ms; add actual AFE/sensor delay, FIR delay,
processing and output propagation. The 10 ms spectral and sub-ms to 2 ms fast
goals are qualification targets, never measured results of host replay.
Independently instrument arc onset, decision output, shutdown actuation and
actual fault-current extinction. Fault datasets, nuisance exposure, missed-event
rate and latency distribution are separate evidence from nominal conversion
latency or a frame-classification accuracy percentage.

Primary references: [ADS921x datasheet](https://www.ti.com/lit/ds/symlink/ads9218.pdf)
(ADS9219 member, data-interface Figures 7-6/7-8/7-9),
[Certus-NX datasheet](https://www.latticesemi.com/view_document?document_id=52890),
[TI HF sensing reference](https://www.ti.com/lit/ug/tiduez9b/tiduez9b.pdf), and
[Sandia series/parallel study](https://www.sandia.gov/app/uploads/sites/273/gallery/38thPVSC_SeriesvParallelFaults_SAND2012-4443C.pdf).
