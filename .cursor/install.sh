#!/usr/bin/env bash
# Cloud Agent bootstrap for the TEG Teensy 4.1 firmware repo.
# Idempotent: safe to run repeatedly and against cached state.
set -euo pipefail

# The script lives in <repo>/.cursor/, but multi-repo agents start in the
# workspace root (/agent), where a relative .cursor/install.sh does not exist.
find_repo() {
  local script_dir parent
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  parent="$(cd "${script_dir}/.." && pwd)"
  if [ -f "${parent}/platformio.ini" ]; then
    printf '%s\n' "${parent}"
    return
  fi
  if [ -f /agent/repos/teg/platformio.ini ]; then
    printf '%s\n' /agent/repos/teg
    return
  fi
  if [ -f ./platformio.ini ]; then
    pwd
    return
  fi
  echo "TEG repo not found from $(pwd)" >&2
  exit 1
}

REPO_ROOT="$(find_repo)"
cd "${REPO_ROOT}"

# sudo helper (install phase may run as root or as an unprivileged user).
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

# PlatformIO state and Playwright browsers belong to the agent user. A root
# install otherwise writes ~/.platformio under /root, and the ubuntu agent
# later resolves a different tool-scons than the one this script pinned.
run_as_agent() {
  if [ "$(id -u)" -eq 0 ]; then
    sudo -u ubuntu -H -- "$@"
  else
    "$@"
  fi
}

if [ "$(id -u)" -eq 0 ]; then
  if ! id ubuntu >/dev/null 2>&1; then
    echo "ubuntu user is required when install runs as root" >&2
    exit 1
  fi
  AGENT_HOME="$(getent passwd ubuntu | cut -d: -f6)"
else
  AGENT_HOME="${HOME}"
fi
if [ -z "${AGENT_HOME}" ] || [ ! -d "${AGENT_HOME}" ]; then
  echo "agent home not found: ${AGENT_HOME:-<empty>}" >&2
  exit 1
fi

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
command -v ninja >/dev/null
command -v clang++-18 >/dev/null

# 2. Pinned host Python tooling (PlatformIO 6.2.0, gcovr 8.6, Playwright 1.63.0).
#    Installed system-wide so pio/gcovr/playwright are on PATH for every agent.
#    The explicit Playwright pin covers a checkout whose requirements-ci.txt
#    does not list it yet.
$SUDO pip install --break-system-packages --disable-pip-version-check \
  -r requirements-ci.txt 'playwright==1.63.0'
python3 -c 'import importlib.metadata as m; v=m.version("playwright"); assert v=="1.63.0", v'
pio --version | grep -q '6\.2\.0'

# 3. Playwright Chromium for the operator web-UI fixture capture.
#    Browsers must land in the ubuntu user's cache. `sudo pip` runs as root,
#    but `playwright install` must not, or the agent user cannot see them.
$SUDO playwright install-deps chromium
run_as_agent playwright install chromium

# 4. Forked libraries live in submodules whose .gitmodules use SSH URLs.
#    Rewrite to HTTPS for token-less checkout, then init recursively.
git config url."https://github.com/".insteadOf "git@github.com:"
git submodule sync --recursive
git submodule update --init --recursive
test -s lib/aWOT/library.properties
test -s lib/eFlexPwm/library.properties

# 5. Pre-fetch PlatformIO platforms/frameworks/toolchains so the first build
#    and test runs are offline-fast. Fail the bootstrap if the registry
#    install fails. The teensy platform also pulls tool-scons ~4.41101.0 and
#    can remove the project pin (tool-scons @ 4.40801.0). Reinstall that pin
#    last, as the agent user, and refuse to finish on any other version.
#    A previous root run may have left .pio owned by root; only that directory
#    is handed to ubuntu so the package install can write its build cache.
if [ "$(id -u)" -eq 0 ] && [ -d .pio ] && [ "$(stat -c '%U' .pio)" != "ubuntu" ]; then
  chown -R ubuntu:ubuntu .pio
fi
run_as_agent pio pkg install -e teensy41
run_as_agent pio pkg install -e native
run_as_agent pio pkg install -e native-sanitize
scons_pin="$(sed -n 's/^[[:space:]]*tool-scons[[:space:]]*@[[:space:]]*//p' platformio.ini | head -1 | tr -d '[:space:]')"
if [ -z "${scons_pin}" ]; then
  echo "platformio.ini has no tool-scons pin" >&2
  exit 1
fi
run_as_agent pio pkg install -g --skip-dependencies --tool "platformio/tool-scons@${scons_pin}"
scons_pkg="${AGENT_HOME}/.platformio/packages/tool-scons@${scons_pin}/package.json"
run_as_agent python3 - "${scons_pkg}" "${scons_pin}" <<'PY'
import json
import sys

path, expected = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as handle:
    version = json.load(handle).get("version")
if version != expected:
    raise SystemExit(f"tool-scons {version} != {expected} ({path})")
print(f"tool-scons pin ok: {version}")
PY

echo "TEG environment bootstrap complete."
