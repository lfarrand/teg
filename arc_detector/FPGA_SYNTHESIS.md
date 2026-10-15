# Actual HDL verification and synthesis, 2026-10-15

The portable HDL toolchain is installed and has compiled, simulated and
technology-mapped the complete **raw fast tier**. These are actual tool runs,
not the lane-count projections in the earlier [latency study](LATENCY_STUDY.md).
Both architectures match the integer oracle. Neither fits the initially
proposed Certus-NX-40 in this implementation. There is no successful place/route,
timing closure, hardware bitstream, electrical permit or physical arc result.

The arithmetic-first hardware selection is now **AMD Versal Premium VP1802**;
see [selection, alternatives and tool-access gates](FPGA_SELECTION.md).
The `xcup` result below is **UltraScale+ DSP48E2**, not Versal DSP58 synthesis.
The current RTL is a correctness/mapping reference, not the final parallel
VP1802 implementation or an assertion of minimum possible resource cost.

## Installed tools and reproducibility

The official [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build)
2026-10-14 Windows x64 distribution was downloaded, SHA256-verified, extracted
and executed at:

`C:/Users/lee/.codex/tools/oss-cad-suite/20261014/oss-cad-suite`

No administrator install, global PATH modification, vendor account, licence
acceptance or hardware purchase was performed. The archive is retained beside
the installation. The runner accepts an explicit tool directory and records
each executable's SHA256 and actual version; its reference-distribution field
does not attest that an arbitrary user-supplied directory is that release.

| Distribution / executable | Verified value |
|---|---|
| Windows archive | `oss-cad-suite-windows-x64-20261014.tgz` |
| Windows archive SHA256 | `580a1bbe938f4af861ff49e42944dc00933d61d087693e43c316023ea07f6e1e` |
| Yosys | `0.70+16`, git `3bb533450` |
| Icarus Verilog | `14.0 devel`, `s20260301-579-g15d870a52-dirty` |
| nextpnr-nexus | `0.11.1-63-g44a6c739` |
| CI Linux archive SHA256 | `a6ccc6b96b2d985336a1061c78b2fd2bdd73e0f68690da17db88e00457881a11` |

CI downloads the corresponding official Linux release, checks the digest
before extraction and runs real HDL parity. Missing tools fail; no test is
silently skipped. Full traces and manifests are retained as CI artifacts.

## Implemented boundary and latency decisions

`rtl/arc_serial_fir.sv` implements symmetric, signed Q1.23 FIRs with
time-multiplexed pipelined products, 64-bit accumulation, ties-to-even rounding
and overflow checks. `rtl/arc_fast_raw_top.sv` instantiates the six-band fast
bank, fragment activity, block energies, octave coverage, eight-block
persistence and a passive candidate latch. The interface accepts signed 24-bit
samples plus an explicit 64-bit source index. It refuses gaps and busy overrun;
reset clears evidence and in-flight work. Models and thresholds are static ROMs.

| Architecture | Filter schedule | Evidence contract |
|---|---|---|
| `baseline_4m` | Six 257-tap banks at 4 MSPS; three product lanes per band | 256 source samples/block; 64 us |
| `multirate_2m` | 127-tap /2 prefix with two lanes; six 129-tap banks at 2 MSPS with one lane each | Same 256 source samples/block; 64 us |

Both require four strong subbands covering all three octaves, activity in at
least six of eight fragments, and eight consecutive qualifying blocks. They
preserve the oracle's strict thresholds and complete filter-history rules.
The multirate variant reduces work but adds anti-alias delay; it is not an
unconditional latency improvement. Its final partial block cannot report
evidence from a source sample that has not arrived.

Simulation drives one input every 50 clocks on a nominal 200 MHz clock. This
proves the simulated schedule accepts 4 MSPS, **not** that 200 MHz is achievable in
silicon. Eight qualifying blocks still require 512 us; a larger FPGA cannot
remove that observation time without changing and requalifying the evidence
contract. Sensor/AFE delay, initial20-to-4MSPS filtering, onset alignment,
filter history, hardware propagation and arc extinction remain separate.

`rtl/arc_fast_synth_top.sv` removes debug ports and uses an internal source
counter. It exposes 95 I/O bits. That wrapper **cannot detect ADC sample loss**
because its counter advances only on accepted strobes; full acquisition must
supply and verify an independent conversion count. The core's gap tests do not
qualify that unimplemented ADC boundary.

Excluded from this HDL deliverable: ADC DDR/alignment/de-randomisation,
20-to-4MSPS decimation, gain handover, PWM cancellation, residual fast path,
spectral confirmation, complete protection state machine, independent
fault/permit hardware and TEG integration. The floating implementation remains
the broader reference; the [interface contract](FPGA_INTERFACE.md) still governs
the future complete detector.

## Measured tool results

All synthesis runs use the same frozen RTL and verified ROM configuration.
Yosys `check -assert` completed. Mapped cells below are whole raw-fast wrappers,
including energy squares and control; DSP block names/counts are not equivalent
between families. Generic wide arithmetic and register history are expensive;
these counts are not an optimised vendor-IP lower bound.

| Architecture / mapping | Actual multiplier cells | Actual register cells | Actual mapped logic |
|---|---|---|---|
| Baseline / LFD2NX | 24 `MULT36X36` | 40,093 `FD1P3I/JX` | 148,549 `LUT4`; 15,476 `WIDEFN9`; 3,223 `CCU2` |
| Multirate / LFD2NX | 14 `MULT36X36` | 26,654 `FD1P3I/JX` | 38,036 `LUT4`; 2,559 `WIDEFN9`; 2,666 `CCU2` |
| Baseline / UltraScale+ | 60 `DSP48E2` | 41,977 `FDRE/FDSE` | 100,561 `LUT2..6`; 1,403 `CARRY4`; 1 `MUXF7` |

Nexus implementation was checked against the explicitly inventoried
`LFD2NX-40-9BG256C` using nextpnr's device database:

| Run | Actual package/pack result | Fit/timing conclusion |
|---|---|---|
| Baseline initial route | Refused unconstrained I/O, exit 125 | No fit or timing result |
| Baseline with explicit synthetic 95-pad constraints, pack only | Accepted all pads; 185,983/32,256 `OXIDE_COMB`; 40,093/32,256 FF; 24/14 `MULT36_CORE` | Pack exit 0 is **not fit**; resources exceed device capacity |
| Multirate full place/route attempt with same pad policy | Accepted pads; 48,500/32,256 `OXIDE_COMB`; 26,654/32,256 FF; 14/14 `MULT36_CORE`; placement exited 125, no logic BELs remaining | Does not fit; no routed timing |
| UltraScale+ baseline mapping | Yosys exit 0; no device-specific placement/routing | Mapping only; no device fit/timing/Versal proof |

Each Nexus 36x36 cell occupies four native 18x18 multiplier resources in this
packing: baseline 96/56, multirate 56/56. The simultaneous `MULT18_CORE`,
`MULT18X36_CORE` and `MULT36_CORE` utilisation rows describe the same occupied
DSP structures; do not add them together as independent resources.

`fpga_constraints.py` uses 95 bonded caBGA256 pads from a pinned, SHA-bound
[Oxide database](https://raw.githubusercontent.com/gatecat/prjoxide-db/8d8174509356fa5bde12db614c230f7c425f9d2b/LIFCL/LFD2NX-40/iodb.json).
F6 is the research clock; 85 EIO use LVCMOS18 and 10 SIO use LVCMOS18H, assuming 1.8 V
banks. These are **synthetic package constraints, not a board pinout**. No ADC,
interrupter, isolation, supply or external I/O timing is qualified. Never use
them to program hardware. Full implementation requires explicit
`--route --research-pdc`; modeled timing failures remain non-successful CLI
results. No bitstream-generation command is supplied.

## Reproduce in PowerShell

Run from the repository root. Create the Python environment using the
[reference setup](README.md#run-from-the-repository-root), then point to the
installed toolchain. Every output directory below must be new.

```powershell
$arcTools = 'C:/Users/lee/.codex/tools/oss-cad-suite/20261014/oss-cad-suite'
$env:ARC_FPGA_TOOLS = $arcTools
$env:ARC_FPGA_ARTIFACTS = '.pio/arc-hdl-check'
& .pio/arc-venv/Scripts/python.exe -m unittest discover -s arc_detector/tests -p 'test_*.py' -v
& .pio/arc-venv/Scripts/python.exe -m unittest discover -s arc_detector/hdl_tests -p 'test_*.py' -v
& .pio/arc-venv/Scripts/python.exe -m arc_detector.fpga_build synth --toolchain $arcTools --verified-dir .pio/arc-hdl-check/baseline_4m-broadband-0-4096 --output-dir .pio/arc-baseline-xcup --family xcup
& .pio/arc-venv/Scripts/python.exe -m arc_detector.fpga_build synth --toolchain $arcTools --verified-dir .pio/arc-hdl-check/multirate_2m-broadband-0-4096 --output-dir .pio/arc-multirate-nexus --family lfd2nx --route --research-pdc
```

The final command is expected to return 1 for the recorded resource failure;
inspect `synthesis-report.json` and `route.log`, not just the shell status.
Verification rejects changed source/ROM hashes before synthesis. Reports are
exclusive-create; preserve failed-run artifacts and choose another directory
for a genuinely new run. No loop repeatedly restarts an unchanged heavy job.

Local full artifacts are retained under the managed worktree's ignored
`.pio/arc-fpga-20261015/`: `final-hdl-suite/`, `baseline-nexus/`,
`multirate-nexus/` and `baseline-xcup/`. Large netlists/tool binaries are not
committed. Reproduction regenerates them rather than treating this summary as
a signed certification.

## Requirement-to-test evidence

Six real HDL test methods execute 17 compile/simulation cases. All passed on
the installed Windows toolchain. The host reporting/configuration/constraint
unit tests are separate from those executions; mock report tests are never
counted as FPGA implementation evidence.

| Requirement | Evidence |
|---|---|
| Signed FIR codes, energies, activity, persistence, candidate indices for both banks | `test_both_real_hdl_fast_paths_match_integer_codes_energy_and_candidates` |
| Absolute /2 phase and source timestamps above 2^53 | `test_real_hdl_absolute_phase_and_wide_sample_clock_are_exact` |
| Busy/index faults inhibit candidate latch | `test_real_hdl_busy_and_index_gap_faults_inhibit_candidates` |
| No future evidence after a terminal partial block | `test_real_hdl_partial_decimated_block_cannot_emit_future_evidence` |
| Quiet data and signed extreme impulse are not persistent arcs | `test_real_hdl_quiet_and_signed_impulse_do_not_create_arc_candidates` |
| Candidate reset and in-flight MAC reset | `test_real_hdl_reset_clears_candidate_latch_and_inflight_warmup_mac` |
| Exact frozen ROMs and collision refusal | `test_static_roms_bind_exact_signed_coefficients_thresholds_and_hashes`; `test_config_does_not_overwrite_existing_rom_or_manifest` |
| No unverified/stale synthesis inputs | `test_synthesis_refuses_unverified_or_changed_inputs_before_creating_output` |
| Fit/timing reports never grant electrical permission | `test_mapping_and_routing_reports_do_not_turn_fit_or_timing_into_permission` |
| Deterministic, bounded, non-board PDC | `test_full_wrapper_uses_95_unique_pads_and_explicit_research_denials`; `test_invalid_netlists_and_capacity_are_rejected_before_publication` |

## Remaining blockers

1. Appropriate Vivado device entitlement/download access for selected VP1802;
   actual DSP58 inference, vendor-specific memories and parallel scheduling,
   placement/routing and worst-case timing. Current tools do not implement
   Versal. No licence/board purchase has been made.
2. Complete detector RTL and acquisition-health boundary listed above; fixed
   point parity beyond the raw fast tier, formal/CDC/reset checks and independent
   safety hardware. Direct HDL threshold-equality, exactly-four-band/octave and
   five-versus-six-fragment boundary stimuli are not yet covered by this suite;
   host boundary tests are not equivalent hardware evidence. A candidate port
   is not a shutdown guarantee.
3. Real labelled arcs and nuisance exposure, sensor transfer/headroom and
   suppression safety for PWM-correlated/pre-start arcs. No measured detection
   distribution, comparative advantage or prevention claim exists.
4. Board pinout/electrical design, source interruption coverage, disconnected
   bench gates and separately measured onset-to-decision/command/extinction.
   Existing TEG NO-SHIP and OUTEN restrictions are unchanged.
