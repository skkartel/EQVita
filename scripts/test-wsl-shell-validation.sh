#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

fail() {
  echo "$1" >&2
  exit 1
}

test -f scripts/build-wsl.sh || fail "scripts/build-wsl.sh should provide the Vita WSL build entry point."
grep -q 'native_build_dir=' scripts/build-wsl.sh || fail "build-wsl.sh should build in a WSL-native cache directory."

test -f scripts/test-host-wsl.sh || fail "scripts/test-host-wsl.sh should provide the host-test WSL entry point."
grep -q 'native_host_build_dir=' scripts/test-host-wsl.sh || fail "test-host-wsl.sh should build host tests in a WSL-native cache directory."
grep -q -- '-DEQVITA_HOST_TESTS=ON' scripts/test-host-wsl.sh || fail "test-host-wsl.sh should enable host tests."

if grep -q -- '-B build-host' scripts/test-host-wsl.sh; then
  fail "test-host-wsl.sh should not build directly in the Windows-mounted build-host directory."
fi

grep -q 'bash scripts/test-host-wsl.sh' docs/build/how-to-build.md || fail "docs/build/how-to-build.md should document the WSL host-test script."
grep -q 'bash scripts/test-host-wsl.sh' docs/release/release-checklist.md || fail "docs/release/release-checklist.md should document the WSL host-test script."
