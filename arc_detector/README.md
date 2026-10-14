# DC/PV arc detector host reference

This package implements the first deliverable of the recorded switching-noise
cancellation design: numerical DSP, immutable profiles, protection decisions,
ADC word decoding, calibration, recorded-data replay and an
[FPGA interface contract](FPGA_INTERFACE.md). It is separate from Teensy
firmware. It contains no hardware bitstream or electrical shutdown implementation.

The detector analyses both raw HF current and a residual formed by subtracting
frozen switching-edge responses. Neither cancellation nor a missing PWM
reference can veto a valid raw-path arc decision. Synthetic profiles are
experimental and cannot grant a run permit. No real arc recordings, sensor
measurements or event qualification are bundled.

## Run from the repository root

Python 3.12 and the pinned scientific libraries are required. PowerShell:

```powershell
py -3.12 -m venv .pio/arc-venv
& .pio/arc-venv/Scripts/python.exe -m pip install -r arc_detector/requirements.txt
& .pio/arc-venv/Scripts/python.exe -m unittest discover -s arc_detector/tests -p 'test_*.py' -v
& .pio/arc-venv/Scripts/python.exe -m arc_detector demo --output-dir .pio/arc-demo
```

The demo creates a verified-as-synthetic healthy calibration recording and a
separate injected-arc recording, then replays actual samples through the DSP.
Use a new output directory so existing captures are not overwritten. The result
is software evidence only. Sample-time latency is not measured execution time,
FPGA timing closure, gate latency, current-zero or arc extinction.

For your own separately inspected healthy capture and an independently labelled
fault capture:

```powershell
& .pio/arc-venv/Scripts/python.exe -m arc_detector calibrate --recording healthy.npz --metadata healthy.json --output profile.json --profile-id string-1-mode-a --verified-healthy
& .pio/arc-venv/Scripts/python.exe -m arc_detector replay --recording arc.npz --metadata arc.json --profile profile.json --output report.json --prototype
```

`--verified-healthy` is an operator attestation about the calibration data, not
an automatic arc-free check. Calibration outputs remain unqualified. Without
`--prototype`, an unqualified profile reports protection unavailability.
Prototype processing reports experimental states with `permit=false` throughout.

## Recording and calibration

Replay accepts 4 MSPS simultaneous high/low gain current recordings in CSV or
NPZ. The five sample fields are `sample_index`, `current_high`, `current_low`,
`high_clipped` and `low_clipped`. Gain scales in metadata convert both current
channels to the same input-referred units. ADC captures must first be aligned,
decoded, calibrated and anti-alias filtered to this rate. See `acquisition.py`
and the FPGA contract for the raw 20 MSPS boundary.

The JSON sidecar has schema version 1, sample rate, start/count, node, operating
mode, gain scales, acquisition health, switching edges, independently labelled
events and provenance. The synthetic demo emits complete input examples; use
those instead of filling omitted metadata with guessed defaults. Samples are
strictly ordered and contiguous, metadata finite and typed, and files bounded.
NPZ object arrays and pickle are refused. Event labels are used only by the
reporter, never by the detector.

`operating_mode` identifies the complete characterised operating-condition cell:
switching topology, frequency, duty, voltage/current/temperature envelope,
sensor gains and installation characteristics. Selection/validation of that
cell from real slow sensors belongs to the acquisition controller. It must set
`raw_profile_valid=false` outside the raw envelope. An invalid residual model
disables subtraction while a still valid raw model continues monitoring.

Fit rising/falling responses only from verified healthy samples. Stored
coefficients and healthy raw/residual envelopes are immutable during replay.
Runtime learning, arbitrary startup baseline learning, phase-independent
subtraction and use of the low-gain channel as a noise-only reference are excluded.
Profiles include distinct qualification and impulse-tail declarations; these
are trusted engineering inputs, not a certificate or a signing mechanism.

## Processing and protection behavior

The signal module implements the 127-tap 20-to-4 MSPS and 255-tap 4-to-0.5 MSPS
FIR decimators. Their floating-point and quantised coefficient response is
tested. The spectral detector uses non-overlapping 1024-sample Hann frames,
four 10-to-100 kHz bands and at least 60% usable bins per qualifying band.
Energy and bin occupancy both qualify evidence. Three strong frames latch an
arc; weak evidence adds elapsed sample time with quarter-rate decay and a 20 ms
threshold. Persistent suspicion requests protective shutdown with a distinct
uncertainty reason at 100 ms.

The fast tier analyses six subbands between 80 and 640 kHz using 256-sample
blocks and eight consecutive qualifying blocks. It requires separately
qualified fast envelopes and impulse-tail behavior. Reduced fast availability
does not disable a valid spectral tier. Clipping on both gain paths, invalid
frames, sequence gaps, sensor failure, deadline failure or loss of usable
spectral coverage latch protection unavailability.

Keep processing after a latch. Reset requires inhibited output and passing
diagnostics, and clears filter/history state before monitoring restarts.
States and first-latched reasons remain distinct from arc-type inference:
HF detection reports unknown type. A terminal-current-zero label is never used
as proof that a parallel arc has extinguished.

The approximate 10 ms spectral and sub-ms to 2 ms fast targets require physical
qualification. The Python reference is not a real-time protection implementation.
The reporter separates independently supplied onset, detector decision,
shutdown command, terminal current-zero and actual extinction labels. Missed
events and nuisance shutdowns are separate from per-frame classifications.

## What follows this deliverable

Capture real series/parallel arcs and nuisance events across held-out hardware
and wiring families; validate complete impulse responses, noise masking and
cancellation of PWM-correlated events. Port the reference to fixed-point RTL,
verify bit-accurate parity, close FPGA timing, and measure the full signal path.
Then design the AFE/ADC/FPGA board and source shutdown coverage, and integrate
the independent fault/permit and telemetry interface into TEG. The existing
Teensy capture, modulation, security contracts and OUTEN paths are not changed
by this package.
