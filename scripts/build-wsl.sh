#!/usr/bin/env bash
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

if [ -z "${EQVITA_REPO_ROOT:-}" ]; then
  script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
  EQVITA_REPO_ROOT=$(CDPATH= cd -- "$script_dir/.." && pwd)
fi

EQVITA_ARTIFACT_DIR="${EQVITA_ARTIFACT_DIR:-build}"
EQVITA_BUILD_TYPE="${EQVITA_BUILD_TYPE:-Release}"
EQVITA_CLEAN="${EQVITA_CLEAN:-0}"

cd "$EQVITA_REPO_ROOT"

if [ ! -f "$VITASDK/share/vita.toolchain.cmake" ]; then
  echo "VitaSDK not found at $VITASDK. Install it from https://vitasdk.org/ first." >&2
  exit 1
fi

case "$EQVITA_ARTIFACT_DIR" in
  ""|/*|*\.\.*)
    echo "EQVITA_ARTIFACT_DIR must be a relative generated build directory." >&2
    exit 1
    ;;
esac

build_key=$(printf '%s' "$EQVITA_REPO_ROOT/$EQVITA_ARTIFACT_DIR" | sha256sum | awk '{print substr($1, 1, 16)}')
safe_build_name=$(printf '%s' "$EQVITA_ARTIFACT_DIR" | sed 's#[/\]#-#g' | tr -cd 'A-Za-z0-9._-')
native_build_root="${XDG_CACHE_HOME:-$HOME/.cache}/eqvita-build"
native_build_dir="$native_build_root/${safe_build_name}-${build_key}"

if [ "$EQVITA_CLEAN" = "1" ]; then
  rm -rf -- "$native_build_dir" "$EQVITA_ARTIFACT_DIR"
fi

cmake -S . -B "$native_build_dir" -DCMAKE_BUILD_TYPE="$EQVITA_BUILD_TYPE"
cmake --build "$native_build_dir"

mkdir -p "$EQVITA_ARTIFACT_DIR/app" "$EQVITA_ARTIFACT_DIR/plugin"
cp -f "$native_build_dir/app/EQVita.vpk" "$EQVITA_ARTIFACT_DIR/app/EQVita.vpk"
cp -f "$native_build_dir/plugin/eq_speaker.skprx" "$EQVITA_ARTIFACT_DIR/plugin/eq_speaker.skprx"

echo "Artifacts copied to $EQVITA_ARTIFACT_DIR/app/EQVita.vpk and $EQVITA_ARTIFACT_DIR/plugin/eq_speaker.skprx"
