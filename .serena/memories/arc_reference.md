# Independent arc reference

- `arc_detector/` is a standalone Python 3.12 host DSP/calibration/replay package;
  pinned NumPy/SciPy there are not Teensy libraries. Run unittest discovery in
  `arc_detector/tests`; CI is `arc-reference.yml`.
- `FPGA_INTERFACE.md` is an external acquisition/protection contract, not complete detector RTL,
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
- `rtl/` now implements the two raw-fast integer banks, feature evidence and
  passive candidates; no ADC receiver/cancellation/residual/spectral/full-safety
  RTL. `rtl_config.py` binds immutable ROMs to exact architecture/domain/envelope.
  `fpga_build.py verify` runs actual Icarus parity, not a renamed host projection;
  `hdl_tests/` is separate required discovery with explicit `ARC_FPGA_TOOLS`.
  Missing tools fail. Retained artifact directories must be new.
- `fpga_build.py synth` accepts verified unchanged ROM/RTL hashes and maps Nexus
  (`synth_lattice -family lfd2nx`) or UltraScale+ (`xcup`, not Versal). Explicit
  `--route --research-pdc` generates bonded-pad synthetic constraints, never a
  physical board pinout or programming permission. Pack-only success is not fit.
  Actual baseline/multirate Nexus results exceed Certus-NX-40 capacity; no routed
  timing or bitstream. Keep large local logs/netlists in ignored `.pio/`.
- `FPGA_SYNTHESIS.md` records actual installed tool versions/hashes, parity,
  resource/failure results and reproduction. `FPGA_SELECTION.md` chooses VP1802
  arithmetic-first with unconstrained power/size; vendor licence/download access,
  parallel implementation and vendor timing remain gates. More DSP/I/O does not
  remove the512us evidence interval or establish market superiority.
