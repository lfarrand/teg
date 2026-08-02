# CI/CD and software-supply-chain controls

The parent repository's `CI` workflow is a regression and evidence pipeline, not
a certification or a substitute for the disconnected-hardware checks. Every
third-party GitHub Action is pinned to a full commit SHA, checkout credentials are
not persisted, the workflow has read-only repository permissions, duplicate runs
are cancelled, and every job has an explicit timeout.

## Required CI evidence

| Job | What it establishes | Deliberate boundary |
|---|---|---|
| Build Teensy 4.1 firmware | Exact PlatformIO 6.1.18 and pinned platform, framework, compiler, Teensy post-build tool, and SCons package produce two byte-identical HEX files; firmware ELF, HEX, SHA-256, build logs, section report, largest-symbol report and parsed memory budget are retained | Reproducible in the selected Ubuntu 24.04 runner family at the time of the run; the runner image itself is mutable and cross-OS identity is not claimed |
| Native tests and coverage | All hardware-independent suites pass under the coverage build and again under ASan/UBSan; tested-source lines stay at or above 90% and branches at or above 60% | `src/*.cpp`, registers, ISR timing, networking and physical outputs are not represented by these figures |
| Forked-library tests | The exact aWOT and eFlexPwm gitlink commits pass their native suites | Does not validate Teensy pin mux, reload timing or electrical behaviour |
| Host microbenchmarks | Google Benchmark 1.9.5 exercises the real modulation duty pipeline, portable FFT, PLL sample step and waveform parser; JSON is retained for trend analysis | Shared runners are noisy and x86 timing is not Cortex-M7 timing, so CI has no absolute-time pass/fail threshold |
| Clang parser fuzzing | ASan/UBSan libFuzzer attacks the OTA ingest, waveform and gzip streaming parsers with valid seeds, arbitrary chunking, and an explicit 64 KiB generated-input ceiling matching the harnesses | A short PR fuzz budget finds regressions, not proof of parser correctness; longer campaigns should reuse and retain corpora |
| Secrets, SBOM and vulnerabilities | Checksum-verified Gitleaks 8.30.1 scans the checked-out tree with redacted reports; a deterministic CycloneDX 1.6 inventory records declared PlatformIO inputs, exact build tools, framework libraries, vendored code, upstream source revisions, and exact gitlinks; checksum-verified OSV-Scanner 2.4.0 checks those C/C++ commits and must first detect a deliberately vulnerable sentinel | The secret scan intentionally excludes Git history. OSV's C/C++ coverage is commit-based and incomplete, so a clean result is useful evidence rather than proof that every dependency is vulnerability-free |

The size gate is intentionally based on headroom rather than exact image size:

- at least 7 MiB remains in program flash for files;
- at least 80 KiB RAM1 remains for stack and locals;
- at least 256 KiB RAM2 remains for `malloc`/`new`;
- at least 768 KiB remains unallocated in the mandatory 8 MiB PSRAM.

The baseline measured when these gates were introduced was 7,633,924 bytes,
90,752 bytes, 277,696 bytes and 1,047,552 bytes respectively. Crossing a gate
requires an explicit review of the map/symbol report and, for RAM1, stack
high-water testing on hardware; raising the budget to make CI green is not an
acceptable standalone fix.

## Reproducibility and update policy

PlatformIO, gcovr, Python, actionlint, the Teensy and native platforms, framework,
compiler, Teensy post-build tool, SCons package, registry libraries, Google
Benchmark, Gitleaks, OSV-Scanner and all Actions are exact-version or
commit pinned. The actionlint and Gitleaks archives are verified against pinned
SHA-256 values taken from their publishers' checksum files; the OSV-Scanner binary
and Google Benchmark archive are likewise checksum verified. Dependabot opens
weekly Action and git-submodule update PRs. PlatformIO registry packages are not
a Dependabot ecosystem, so their deliberate pins in `platformio.ini` still need
manual review and a clean firmware/bench validation before upgrade.

The generated SBOM is deterministic for a commit and dependency declaration: it
omits a wall-clock timestamp and derives its serial UUID from the commit and
component inventory. Keep the SBOM, HEX, ELF, checksum and size reports together
when retaining a build.

## Platform limitations and manual controls

CodeQL for this private repository is unavailable without GitHub Code Security.
No always-failing CodeQL workflow is committed. Enable it if the repository gains
that entitlement, then add C/C++ `security-extended` analysis as a required check.
Likewise, private-repository artifact attestations require the appropriate GitHub
plan; the current SHA-256 artifact is integrity evidence but not a signed
provenance statement.

No Black Duck/Synopsys Detect job is configured because no licensed project,
server or CI credential was supplied. The CycloneDX artifact is suitable input
for that service if it is added later. Grype was evaluated and removed after an
independent review proved that it classified every `pkg:platformio` component as
an unknown package and therefore matched nothing. OSV-Scanner is used instead
because its C/C++ mode accepts exact repository commits. `scripts/osv-dependencies.json`
maps the pinned registry, framework, and vendored sources to reviewed upstream
commits, while recursive source scanning must also discover both gitlink commits.
The sentinel scan is intentionally vulnerable and CI fails if OSV stops detecting
it; the real scan fails on any reported vulnerability. Public commit-level data is
still incomplete, and locally patched vendored code can differ from its upstream
base, so the SBOM and deliberate dependency review remain necessary controls.

Repository settings still matter outside YAML: enable Dependabot alerts/security
updates and private vulnerability reporting where the account plan permits;
require pull requests and successful CI checks on `main`; disallow force pushes
and branch deletion; keep the default Actions token read-only; and rotate any
secret ever committed to history. A clean current-tree Gitleaks result does not
make an old credential safe again.

At the time of this review, GitHub's API rejected branch protection for the
private `teg` repository because the current account plan does not provide that
feature. The workflow checks therefore cannot yet be made server-enforced merge
requirements. Treat a fully green PR as a manual release gate, do not push
directly to `main`, and enable branch protection immediately if the plan changes
or the repository becomes public. The public aWOT and eFlexPwm repositories can
and should enforce their own required checks.
