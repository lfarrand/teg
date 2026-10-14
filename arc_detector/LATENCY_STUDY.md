# Latency-first fixed-point preparation

This follow-on makes fast-path arithmetic executable and speed/resource
trade-offs reproducible. It is **passive software evidence**, not a deployed
protection path. Outputs never grant a permit, qualify a profile or establish
market superiority. TEG firmware and OUTEN are unchanged.

## Decisions and assumptions

- Prioritise streaming evidence without discarding independent raw observation
  or lower-frequency confirmation. A fast candidate is not arc extinction.
- Implement exact integer arithmetic before RTL. Neither sampling rate nor
  Python execution time establishes device latency.
- Retain `baseline_4m` as the low-delay comparison. Treat `multirate_2m` as an
  experimental resource-saving alternative, not an accepted replacement.
- Fit separate frozen healthy envelopes for each architecture/numeric domain.
  Do not reuse floating thresholds, learn at startup or adapt to operating data.
- Require explicit full scale and units. A normalised recording does not become
  amperes by naming its column `current`; examples use `synthetic-current-unit`.
- Use signed Q1.23 coefficients and nearest/ties-even rounding. Derive word
  bounds from the integer coefficient L1 norm and signed input range. Widened
  outputs retain the original physical LSB. Pair coefficients only when their
  integer symmetry is exact, preserving the widened pre-adder.
- The FPGA projection assumes 200 MHz and 14 wide-product lanes; neither is a
  timing-closure result. Exports are atomic/exclusive. JSON integers with
  magnitude at least 2^53 are decimal strings; parse them as integers.

## Executable scope

`fixed_point.py` provides numeric domains, exact signed rounding, coefficient
quantisation, certified FIR decimation, absolute sample phase, complete-history
validity and reset. Unsafe arithmetic configurations are rejected before NumPy
integer operations. Invalid whole blocks leave state/clock unchanged.

`fast_oracle.py` observes the **raw 4 MSPS input only**. It implements six bands
in three octaves, eight activity fragments per block, strict excess energy and
eight consecutive 64 us blocks. At least four bands across all octaves must
qualify, with activity in at least six of eight fragments in each qualifying
band. Broken streaks cannot borrow evidence. The multirate path waits for the
entire physical source block, including its unsampled final interval.

Thresholds are in mean squared domain codes. Healthy q999 interpolation and
comparisons use exact integers, including beyond binary64 precision. Calibration
requires explicit healthy attestation/provenance, complete filter warm-up and
more than eight valid blocks. This does not establish that a capture was arc-free.

`latency_study.py` fits variants independently on the same healthy input, then
runs identical evaluation samples. Labels only score candidates afterward.
Half-open event intervals, missed events, benign exposure and host execution
cost remain distinct. Terminal current-zero is not extinction.

Gain/acquisition control, prediction/cancellation, the residual path, spectral
FFT, protection state machine and shutdown are **not** ported here. The original
floating detector remains unchanged. This is not complete fixed-point parity.

## Architecture trade-off

Both variants retain 64 us blocks and nominal 512 us eight-block evidence.
Filters differ and need separate calibration/qualification.

| Quantity | `baseline_4m` | `multirate_2m` |
|---|---:|---:|
| Bank sample rate | 4 MSPS | 2 MSPS |
| Bank filters | Six 257-tap FIRs | Six 129-tap FIRs |
| Additional fast prefix | None | 127-tap /2 at 4 MSPS |
| Samples/block | 256 | 128 |
| Raw fast group delay including first ADC decimator | 35.15 us | 50.90 us |
| Dual-path FIR projection with exact symmetry | 6.576 billion products/s | 2.200 billion products/s |
| Ideal lane clock with 14 wide-product lanes | 469.714 MHz | 157.143 MHz |

The projection includes the shared 127-tap 20-to-4 MSPS FIR, two 255-tap
4-to-0.5 MSPS FIRs and two fast paths. The executable oracle only checks the
raw fast path, not that complete dual-path projection. The variant reduces
projected products by 66.5%, adding 15.75 us nominal group delay. Group delay
is not settling or a detection upper bound; startup and decimation phase differ.

The [Certus-NX datasheet](https://www.latticesemi.com/view_document?document_id=52890)
(FPGA-DS-02078-2.5, Table 1.1/sysDSP section) lists 56 **18-by-18** multipliers
for LFD2NX-40, not 56 full-width products. Its DSP decomposition gives an
analytical equivalent of 14 36-by-36 products. At the assumed 200 MHz these
provide 2.8 billion products/s. Baseline FIR demand alone exceeds that model.
Variant arithmetic headroom does not prove fit: packing, RAM, routing, FFT,
prediction, features, control and pipeline stalls remain unaccounted for.

For 24-bit domain inputs, the variant prefix needs a 26-bit output and 49-bit
minimum accumulator. Its banks use 26-bit input, 27-bit output and 50-bit
minimum accumulators. Certificates are emitted per filter; do not assume
24-bit outputs throughout the cascade.

## Reproduce in PowerShell

Use Python 3.12 with `requirements.txt`, from the repo root. Choose new output
paths; existing artifacts are never overwritten.

```powershell
& .pio/arc-venv/Scripts/python.exe -m arc_detector.latency_study demo --full-scale 16 --unit-label synthetic-current-unit --output .pio/arc-latency-demo.json
& .pio/arc-venv/Scripts/python.exe -m arc_detector.latency_study vectors --full-scale 16 --unit-label synthetic-current-unit --samples 2048 --start-index 5 --output .pio/arc-latency-vectors.json
& .pio/arc-venv/Scripts/python.exe -m arc_detector.latency_study compare --healthy-recording healthy.npz --healthy-metadata healthy.json --evaluation-recording arc.npz --evaluation-metadata arc.json --verified-healthy --full-scale 16 --unit-label synthetic-current-unit --output .pio/arc-latency-compare.json
```

`compare` requires separate inspected healthy/evaluation captures, matching
node/mode and valid acquisition. It rejects arc-labelled training, failed
health, both gains clipped, the same physical file and out-of-domain samples.
This is not a live health latch. Processing chunks are 1..32768 samples; file
import has a separate minimum chunk size. Each capture is bounded to 2M samples.

Golden vectors contain 15 **elementary** interfaces: first decimator, spectral
decimator, six baseline banks, variant prefix and six variant banks. Each has
coefficient/output codes, physical indices, validity and word certificates.
These are not one ADC-to-shutdown cascade. Retain absolute phase, signed widths,
rounding and validity, and refuse architecture/domain mismatches.

## Observed software results, 2026-10-15

Clean validation commands:

```powershell
python -m unittest discover -s arc_detector/tests -p 'test_*.py' -v
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
python scripts/verify_ci_policy.py
git diff --check
```

The arc suite passed 139 tests, including 45 new tests. The separate tooling
suite passed 52. These are scoped command results, not a TEG firmware/bench
census. They do not test FPGA timing or ISR/OUTEN.

The actual `demo` CLI used 60000 healthy/90000 evaluation samples and constructed
broadband onset at index 16003. First candidates followed onset by **543.25 us**
(baseline) and **574.75 us** (variant). The variant emitted two candidates during
one labelled event after its streak restarted, not two independent faults.
Outputs were passive/unqualified with `permit=false`. There was no labelled
benign exposure, so no nuisance-rate result is established.

One-run host processing throughput was approximately 0.636/1.377 MSamples/s;
neither sustains 4 MSPS in Python. These costs exclude I/O, encoding, acquisition
and shutdown. Faster host execution did **not** mean an earlier sample-clock
candidate. Measured complete-history startup differs by about 31.5 us, not just
the nominal 15.75 us group-delay difference.

A second actual `compare` CLI used the prior reference demo's separate
300000-sample healthy/evaluation NPZ pair. **Both raw-only variants missed its
labelled synthetic arc.** Retain this result, do not retune thresholds to pass
the fixture, and do not generalise the first fixture's latency to all arcs.
Cancelled and lower-frequency coverage must be qualified alongside the fast
tier; passing original floating tests does not qualify this new oracle.
On that recording the original floating reference reported `spectral_strong`
on its **raw 10-to-100 kHz** path at 5.78225 ms after onset. No fast block had
four strong bands: the top two consistently qualified, while switching
background/activity rules prevented sufficient evidence in the others.
Lower-frequency coverage, not cancellation, was the demonstrated route to
detection for this fixture.

The vectors CLI exported 2048 codes, start index 5, seed 903, and all 15 streams.
Canonical little-endian signed-int64 input SHA256:
`46d1d9800bc3fa08f54258c5d423bdbeb01b78caaa9cbd6bfbfb45d00a812f20`.
Artifacts are ignored under `.pio/`. Demo host timings vary, so that report is
not byte-reproducible; the deterministic vector payload is.

### Requirement-to-test evidence

| Requirement | Exact tests |
|---|---|
| Signed rounding and overflow | `test_round_shift_signed_ties_matches_fraction_oracle`; `test_int64_boundary_arithmetic_does_not_wrap` |
| Independent convolution/phase | `test_symmetry_pairing_and_nonsymmetric_fallback_match_scalar_convolution`; `test_absolute_phase_and_random_chunk_partition_invariance` |
| Complete history/no future decision | `test_multirate_waits_for_complete_unsampled_source_end`; `test_complete_fir_history_empty_reset_and_no_output_intervals` |
| Fresh evidence/transient rejection | `test_baseline_seven_blocks_are_insufficient_and_eighth_is_exact`; `test_six_of_eight_activity_four_subbands_and_all_three_octaves_required`; `test_single_impulse_and_interrupted_noise_cannot_inherit_a_streak` |
| Exact frozen/domain-bound thresholds | `test_exact_quantile_ceiling_never_uses_float_or_underflows`; `test_frozen_thresholds_cannot_cross_variant_or_numeric_domain` |
| Passive denial/qualification | `test_real_integer_noise_requires_eight_blocks_and_is_always_passive`; `test_cli_demo_reports_synthetic_candidates_not_hardware_superiority` |
| Labels only score/extinction separation | `test_labels_only_score_and_partitions_cannot_change_candidates`; `test_candidate_metrics_keep_half_open_events_and_extinction_separate` |
| Independent golden arithmetic/publication | `test_golden_ports_match_independent_integer_convolution_and_phase`; `test_atomic_artifact_publication_preserves_large_integers_and_collisions`; `test_nonfinite_json_and_publish_failure_leave_no_partial_artifact` |
| Recording import constraints | `test_recording_compare_keeps_one_sample_processing_separate_from_import`; `test_recording_compare_rejects_training_arc_failed_health_and_same_file` |

## Remaining blockers and claims gate

1. Independently labelled real arcs/nuisance exposure across held-out hardware,
   including startup, slow/intermittent faults, attenuation and PWM correlation.
   No electrical missed-event or false-trip rate is established.
2. Complete AFE/sensor impulse response, saturation/headroom, gain handover,
   clipping recovery and reference timing/jitter. Sampling cannot recover an
   absent or heavily attenuated signature.
3. Quantised variant passband/alias rejection and complete impulse-tail
   qualification; bit-exact prediction, residual, spectral features and protection
   RTL; fault injection and parity beyond these component interfaces.
4. FPGA package/speed grade, clocks/pins/constraints, DSP packing, memory schedule,
   synthesis and worst-case timing. No HDL/synthesis toolchain or target
   constraints was available; no bitstream was built.
5. Independent hardwired permit/fault and source interruption coverage, reset
   and broken-wire behavior. Instrument onset, decision, command and extinction
   separately. Existing NO-SHIP/disconnected-bench gates remain.
6. Matched competitive testing. The
   [TI reference guide](https://www.ti.com/lit/ug/tiduez9b/tiduez9b.pdf) uses
   1024 samples at 250 kSPS (4.096 ms capture), but leaves thresholds and
   consecutive filtering to the implementer. That is not measured competitor
   detection/interruption latency. Whole-system shutdown figures cannot be
   compared with a passive sample-clock candidate. No fastest-on-market,
   market-wide margin or prevention claim without matched faults, nuisance
   tolerance, sensing paths and actual extinction data.

The architectural direction is frozen event-synchronous cancellation alongside
independent streaming fast detection and robust low-frequency confirmation.
Novelty, patentability, product performance and compliance are not established.
Next comes qualified signal-path evidence and constrained bit-exact FPGA work,
not a software-controlled high-voltage trial.
