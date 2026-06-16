#!/usr/bin/env bash
set -euo pipefail

if [ -z "${EQVITA_REPO_ROOT:-}" ]; then
  script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
  EQVITA_REPO_ROOT=$(CDPATH= cd -- "$script_dir/.." && pwd)
fi

EQVITA_BUILD_TYPE="${EQVITA_BUILD_TYPE:-Debug}"
EQVITA_CLEAN="${EQVITA_CLEAN:-0}"

cd "$EQVITA_REPO_ROOT"

build_key=$(printf '%s' "$EQVITA_REPO_ROOT/host-tests" | sha256sum | awk '{print substr($1, 1, 16)}')
native_host_build_root="${XDG_CACHE_HOME:-$HOME/.cache}/eqvita-host-build"
native_host_build_dir="$native_host_build_root/host-${build_key}"

if [ "$EQVITA_CLEAN" = "1" ]; then
  rm -rf -- "$native_host_build_dir"
fi

cmake -S . -B "$native_host_build_dir" -DEQVITA_HOST_TESTS=ON -DCMAKE_BUILD_TYPE="$EQVITA_BUILD_TYPE"
cmake --build "$native_host_build_dir" --parallel
ctest --test-dir "$native_host_build_dir" --output-on-failure

