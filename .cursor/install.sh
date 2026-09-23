#!/usr/bin/env bash
# Cloud Agent bootstrap for the TEG Teensy 4.1 firmware repo.
# Idempotent: safe to run repeatedly and against cached state.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# sudo helper (install phase may run as root or as an unprivileged user).
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

# 1. System toolchain packages not guaranteed in the base image.
#    - ninja-build:        Google Benchmark + libFuzzer CMake generators
#    - build-essential:    make/g++ for the forked-library CMake tests
#    - libstdc++-14-dev:   clang (default c++) selects the gcc-14 libstdc++
#    - libclang-rt-18-dev: libFuzzer + ASan/UBSan static runtimes for clang-18
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -qq
$SUDO apt-get install -y --no-install-recommends \
  ninja-build \
  build-essential \
  libstdc++-14-dev \
  libclang-rt-18-dev

# 2. Pinned host Python tooling (PlatformIO 6.2.0, gcovr 8.6) + Playwright.
#    Installed system-wide so pio/gcovr/playwright are on PATH for every agent.
$SUDO pip install --break-system-packages --disable-pip-version-check \
  -r requirements-ci.txt playwright

# 3. Playwright Chromium for the operator web-UI fixture capture.
$SUDO playwright install-deps chromium
playwright install chromium

# 4. Forked libraries live in submodules whose .gitmodules use SSH URLs.
#    Rewrite to HTTPS for token-less checkout, then init recursively.
git config url."https://github.com/".insteadOf "git@github.com:"
git submodule sync --recursive
git submodule update --init --recursive

# 5. Pre-fetch PlatformIO platforms/frameworks/toolchains so the first build
#    and test runs are offline-fast. Non-fatal if the registry is unreachable.
pio pkg install -e teensy41 || true
pio pkg install -e native || true
pio pkg install -e native-sanitize || true

echo "TEG environment bootstrap complete."
