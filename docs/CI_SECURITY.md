# CI/CD and software-supply-chain controls

The parent repository's `CI` workflow is a regression and evidence pipeline, not
a certification or a substitute for the disconnected-hardware checks. Every
third-party GitHub Action is pinned to a full commit SHA, checkout credentials are
not persisted, the workflow has read-only repository permissions, duplicate runs
are cancelled, and every job has an explicit timeout.

## Required CI evidence

| Job | What it establishes | Deliberate boundary |
|---|---|---|
| Build Teensy 4.1 firmware | Exact PlatformIO 6.1.19 and pinned platform, framework, compiler, Teensy post-build tool, and SCons package produce two byte-identical HEX files; firmware ELF, HEX, SHA-256, build logs, section report, largest-symbol report and parsed memory budget are retained | Reproducible in the selected Ubuntu 24.04 runner family at the time of the run; the runner image itself is mutable and cross-OS identity is not claimed |
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
weekly Action, git-submodule, and CI Python (`requirements-ci.txt`) update PRs.
PlatformIO registry packages are not a Dependabot ecosystem; the Monday
`PlatformIO updates` workflow runs `pio pkg outdated` for every
`platformio.ini` environment and refreshes a stable `deps/platformio-updates`
branch (lease-aware force-push) when a non-skipped pin has a newer registry
version. That job then dispatches `CI` on the branch because `GITHUB_TOKEN`
pushes do not start `pull_request` workflows. The Teensy platform, framework, toolchain and `tool-teensy` stay skipped.
They already track Teensyduino 1.62 / GCC 15.2.1 together; `skip_core_mtp.py`
compiles patched 1.62 MTP sources from `scripts/mtp_core162/`. A later core
bump can re-break that remap or the framework/compiler pairing.
Review those PRs and keep a clean firmware/bench validation
before merging a library bump onto an energised power stage.

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
The two `requirements-ci.txt` pins are scanned as declared versions only
(`--no-resolve`); resolving that file invents transitive lower bounds that are
not the installed CI tree.
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

`main` is branch-protected: changes require an up-to-date pull request, every
CI job (firmware build, native tests and coverage, forked-library tests, host
microbenchmarks, parser fuzzing, and secrets/SBOM/vulnerability scanning),
linear history and resolved conversations; force pushes and branch deletion are
disabled. The required approval count is zero because the owner cannot approve
their own pull request, but direct pushes remain blocked and administrator
enforcement is on.

The public aWOT and eFlexPwm repositories protect `master` the same way, with
their own CI contexts as required checks.

Both submodules use CodeQL's `manual` C/C++ build mode so analysis sees the
actual host/Teensy compilation surface. GitHub may annotate those successful jobs
to say that its pull-request overlay optimization supports only
`build-mode: none`; the action then builds a normal full database. This is an intentional
accuracy-over-speed choice: GitHub documents manual mode as the most accurate
compiled-language option. See [CodeQL for compiled
languages](https://docs.github.com/en/code-security/how-tos/find-and-fix-code-vulnerabilities/manage-your-configuration/codeql-for-compiled-languages).
