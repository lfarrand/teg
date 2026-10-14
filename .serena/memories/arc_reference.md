# Independent arc reference

- `arc_detector/` is a standalone Python 3.12 host DSP/calibration/replay package;
  pinned NumPy/SciPy there are not Teensy libraries. Run unittest discovery in
  `arc_detector/tests`; CI is `arc-reference.yml`.
- `FPGA_INTERFACE.md` is an external acquisition/protection contract, not RTL,
  a bitstream, working hardware, FPGA timing evidence or an OUTEN integration.
- Immutable healthy templates; independent raw/residual detection. Synthetic
  profiles stay unqualified; explicit prototype never grants permit. Unavailable,
  uncertain and arc latches are separate; terminal current-zero is not extinction.
- ADS9219 aligned 24-bit capture requires explicit XOR mode: reset NONE, optional
  PRBS appended bit or conversion-LSB XOR. Tail user bits are opaque, not padding.
- Keep the scientific reference out of `src/`, PlatformIO and the TEG safety
  path. Raw20M capture and normalised4M replay are distinct input boundaries.
- Every processing call explicitly binds an exact operating mode; omission or
  mismatch latches unavailable, including before the first ADC decimator output.
  Calibration excludes pre-capture predictor tails before fitting and residual
  FIR/envelope/floor estimation. Publish provenance before the profile with
  exclusive creation and rollback; replay retains tier availability/history.
- Readiness is per spectral path and resets with its history. Apply coverage
  changes before /5 buffering; preserve sample-local invalid-word timing and
  valid prefixes. Quantised coefficients must fit after rounding, not just
  before it. Empty and partial calls retain health/tier telemetry.
- `fixed_point.py` is the signed, bounded Q1.23 FIR oracle; `fast_oracle.py` is
  passive raw-only integer fast detection. `latency_study.py` exports component
  golden vectors and compares `baseline_4m`/`multirate_2m` with independently
  fitted frozen thresholds and explicit domain units. All deny permit and
  hardware qualification. Large JSON integers use decimal strings.
- `LATENCY_STUDY.md` records decisions/results/blockers. Multirate reduces FIR
  work but adds delay; synthesis fit, full cancellation/FFT parity, real arc
  performance, extinction and market superiority remain unproven. Synthetic
  detections AND raw-only misses must be retained, not marketed as guarantees.
