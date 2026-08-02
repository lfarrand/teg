# Security policy

Please do not disclose a suspected vulnerability in a public issue. Use
[GitHub's private vulnerability report](https://github.com/lfarrand/teg/security/advisories/new)
or contact the repository owner privately. Include the affected commit, a minimal
reproducer, likely impact, and whether the issue can energise or prevent shutdown
of a PWM output.

There is no supported production release or Internet-facing deployment. This is
bench firmware for a trusted, isolated network, and hardware behaviour remains
unverified. The detailed threat model, residual risks, OTA restrictions and
release gates are in [docs/SECURITY.md](docs/SECURITY.md) and
[docs/BENCH_CHECKS.md](docs/BENCH_CHECKS.md).

Do not include real credentials, bearer tokens, private firmware images, or
dangerous high-voltage test instructions in a report. Rotate any credential that
may have entered a commit, even if a later commit removed it.
