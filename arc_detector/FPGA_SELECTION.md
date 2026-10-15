# FPGA selection for low-latency arc-detector research

Decision and source check date: **2026-10-15**.

## Decision and boundaries

Use **AMD Versal Premium VP1802**, with **VPK180** as the identified evaluation-board route, as the arithmetic-first engineering target. The priorities are arithmetic capacity, deterministic pipeline latency and suitable acquisition I/O; power and package size are not selection constraints for this research. This is a target selection, not a purchase, a completed VP1802 implementation or a claim that this device is universally fastest.

VP1802 has 14,352 DSP58 engines in AMD's current device table. Its native arithmetic is a better match to the existing 24-bit coefficient format than a multiplier with an 18-bit operand limit. Newer Versal Premium Gen2 remains an I/O-focused comparator, rather than being assumed faster solely because it is newer. [AMD Versal Premium specifications](https://www.amd.com/en/products/adaptive-socs-and-fpgas/versal/premium-series.html), [AMD Premium Gen2 specifications](https://www.amd.com/en/products/adaptive-socs-and-fpgas/versal/gen2/premium-series.html)

The present **serial, raw-only RTL implementation is mapping research**. It is not the complete detector, a sustainable real-time acquisition implementation, a protection product or the future VP1802 parallel implementation. That parallel implementation is **not completed**. Host arithmetic tests and synthesis resource reports do not establish routed clock frequency, hardware fit, input-to-decision latency, arc coverage, interruption performance or certification. TEG firmware, OUTEN and high-voltage hardware are outside this selection work.

## Arithmetic comparison

Resource units below are the manufacturers' advertised units, not interchangeable MAC counts. Multiplier width, pre-adder growth, accumulator width, decomposition and scheduling must be considered before comparing throughput.

| Device | Advertised arithmetic resources | Native arithmetic relevant here | Role in this research |
| --- | --- | --- | --- |
| Lattice Certus-NX LFD2NX-40 | 56 18x18 multipliers | Larger multiply modes combine the smaller multiplier resources; the sysDSP architecture also provides wider accumulation modes. | Original small-device target and mapping comparison, not the arithmetic-headroom selection. [Lattice selection table](https://www.latticesemi.com/CertusNX), [current family data sheet](https://www.latticesemi.com/view_document?document_id=52890) |
| AMD Versal Premium VP1802 | 14,352 DSP58 engines | 27x24 multiplier, 27-bit pre-adder, 58-bit ALU/accumulator. | Selected arithmetic-first target. [Device table](https://www.amd.com/en/products/adaptive-socs-and-fpgas/versal/premium-series.html), [DSP58 architecture](https://docs.amd.com/r/en-US/am004-versal-dsp-engine/DSP58-Features) |
| AMD Virtex UltraScale+ VU13P | 12,288 DSP48E2 slices | 27x18 multiplier and 48-bit arithmetic result path. | Mature comparator; 24-bit coefficients and wide sums need additional arithmetic resources. [Selection guide](https://docs.amd.com/api/khub/documents/Fz8MLna3N2KIo_xWN_Iv3w/content), [DSP48E2 primitive](https://docs.amd.com/r/en-US/ug1704-spartan-ultrascaleplus-libraries/DSP48E2) |
| Altera Agilex7 I-Series AGI040 | 12,792 variable-precision DSP blocks | Native 27x27 mode and 64-bit accumulation/cascade. | Strong second-vendor comparator for the same fixed-point design. Its separately advertised 25,584 18x19 multipliers are not 25,584 independent 27x27 MACs. [Device table](https://docs.altera.com/r/docs/683458/current/agilextm-7-fpgas-and-socs-device-overview/agilextm-7-fpgas-and-socs-i-series), [DSP architecture](https://www.intel.com/programmable/technical-pdfs/683037.pdf) |
| AMD Versal Premium Gen2 2VP3602 | 7,616 DSP58 engines | DSP58 family, retaining the 27x24 multiplier architecture. | Newer-I/O comparator, with fewer aggregate DSP58 engines than VP1802. [Gen2 device table](https://www.amd.com/en/products/adaptive-socs-and-fpgas/versal/gen2/premium-series.html), [DSP58 architecture](https://docs.amd.com/r/en-US/am004-versal-dsp-engine/DSP58-Features) |

DSP58 can accept a 24-bit coefficient and a paired sample that fits its 27-bit pre-adder/multiplier input without splitting that product. Its 58-bit accumulation path is useful for the existing certified sums. This does **not** mean every stage fits one DSP58: a residual operand exceeding 27 bits, a pre-addition exceeding its input range or an accumulator exceeding 58 bits still needs decomposition or a separately justified numeric-format change. Preserve each component's integer certificate, rounding and overflow contract; do not silently truncate to obtain a favorable resource count. [DSP58 architectural highlights](https://docs.amd.com/r/en-US/am004-versal-dsp-engine/Architectural-Highlights-of-DSP58)

AMD specifies at least three pipeline registers for multiply/MAC operations at full speed, and at least two for add/subtract/logic operations. Therefore one DSP engine is not a zero-latency arithmetic operation. Architecture selection must count actual registers, cascade stages, scheduling and clock-domain crossings. DSP58 supports pipelined cascades; a long fully cascaded FIR and a balanced parallel reduction have different latency/resource tradeoffs. [DSP58 pipeline and cascade guidance](https://docs.amd.com/r/en-US/am004-versal-dsp-engine/DSP58-Features)

The intended next VP1802 architecture is a direct acquisition-to-local-memory-to-pipelined-arithmetic path, with separately scheduled raw and residual evidence paths. External DRAM, host software, PCIe and telemetry are not required in the protection timing path. Frozen cancellation models must not acquire the arc signal, and cancellation must never veto an independent raw-path decision. This is a design direction, not implemented or measured hardware behavior.

## Newer I/O is not automatically lower detector latency

Premium Gen2 adds PCIe6/CXL3.1, X5IO and newer transceivers. AMD lists rates up to 128 Gb/s, but marks the data preliminary and requires sales contact for 128G support; usable lane rates and counts depend on the device and configuration. These capabilities may be valuable for future multichannel capture, but they do not by themselves shorten this detector's ADC capture, FIR delays or required evidence interval. [Gen2 interfaces and qualification footnotes](https://www.amd.com/en/products/adaptive-socs-and-fpgas/versal/gen2/premium-series.html)

The fast detector currently specifies eight qualifying 64 us blocks: 512 us of evidence, before the other measured delays are added. Increasing arithmetic parallelism can reduce processing delay or make the required schedule achievable; it does not remove that observation requirement. Shortening it would be a separately tested algorithm change, not a benefit established by selecting a newer FPGA. No market-beating latency or protection-performance claim follows from this selection.

ADC-facing I/O must be checked at the actual board connector: voltage standards, differential pairs, forwarded clocks, routing and timing constraints matter more than the chip's maximum networking transceiver rate. Keep physical arc-onset, sensor response, acquisition, decision, shutdown request and actual current extinction as separate measurements.

## Evaluation-board and availability facts

These are public listings checked on 2026-10-15, not supplier commitments or verified stock reservations.

- **VP1802 / VPK180:** AMD lists EK-VPK180-G at **USD 17,995**, with an **8-week lead time**. The board has an FMC+ connector exposing 68 user-defined signals and eight GTYP links. AMD explicitly states that not all device transceivers are exposed and that PCIe is not available on this evaluation kit. Connector compatibility with the intended ADC front end remains to be verified. [VPK180 listing and board features](https://www.amd.com/en/products/adaptive-socs-and-fpgas/evaluation-boards/vpk180.html)
- **2VP3602 / VPK360:** AMD identifies XC2VP3602-2MSEVSVC3340, two FMC interfaces (LPC and HPC), PCIe6/CXL3.1 and two QSFP-112 interfaces. The checked page offers **Notify Me**, not a quoted order price or lead time. AMD's June 30, 2026 update says standard Premium Gen2 devices are shipping; that does not prove immediate VPK360 supply. The separate Gen2 memory-on-package devices are scheduled to begin sampling at the end of 2026, with production expected in the second half of 2027. [VPK360 listing](https://www.amd.com/en/products/adaptive-socs-and-fpgas/evaluation-boards/vpk360.html), [AMD shipping and MoP update](https://www.amd.com/en/blogs/2026/amd-packs-more-memory-capacity--performance-in-a-smalle.html)
- **Agilex9 AGRW039:** an alternative when integrated wideband RF conversion is itself a requirement. Altera's current development-kit listing says **Offering Status: Production** and identifies AGRW039R31F2I2VC. Its integrated converters are 10-bit, up to 64 GSPS; that is not proof of equivalent 18-bit low-frequency arc-sensing performance or better detector latency. A production listing is not a verified stock/lead-time commitment. [Altera AGRW039 development kit](https://www.altera.com/products/devkit/po-3377/agilex-9-direct-rf-series-agrw039-development-kit)

No hardware, license or service purchase has been made or authorized by this note.

## Tool-access blocker and mapping limits

Completing VP1802 implementation and routed timing requires an appropriate AMD vendor tool license and device access. Vivado 2026.1 places Versal Premium and Premium Gen2 in **PRO or higher**, not the free BASIC tier. AMD currently lists PRO at **USD 2,400 node-locked / USD 3,000 floating for an annual subscription**. Account, download/EULA and license entitlement must be resolved explicitly; a kit entitlement must be checked rather than assumed to cover every device or tool version. No licensed-tool access is established by an open-source synthesis run. [AMD device availability by subscription tier](https://docs.amd.com/r/en-US/ug973-vivado-release-notes-install-license/Device-Availability-by-Subscription-Tier), [AMD licensing prices and terms](https://www.amd.com/en/products/software/adaptive-socs-and-fpgas/vivado/vivado-buy.html)

The Agilex comparator likewise needs **Quartus Prime Pro** and its paid license for the vendor implementation flow. [Altera Quartus editions](https://www.altera.com/products/development-tools/quartus)

**Yosys `synth_xilinx -family xcup` is an UltraScale+ mapping flow, not Versal mapping.** A DSP48E2 resource report must not be labeled DSP58 utilization, VP1802 fit or VP1802 timing. Similarly, a Certus-NX mapping is not a completed board implementation. Preserve the actual target, tool version, arithmetic contract and raw-only scope in every synthesis result. Vendor place-and-route, constraint verification and physical measurements remain outstanding for the chosen target.

The next decision gate is a like-for-like parallel implementation comparison: identical numeric contracts and evidence rules, actual port constraints, achieved sample throughput, routed clock/timing margins, cycle latency and measured end-to-end latency. Hardware fit, nuisance rejection, real-arc detection and shutdown/extinction qualification remain unproven.
