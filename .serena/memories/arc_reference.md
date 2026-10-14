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
