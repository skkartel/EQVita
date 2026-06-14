# Build Instructions (VitaSDK)

## Prerequisites
- VitaSDK (`VITASDK` env set)
- CMake ≥3.16, make (or ninja)

Windows builds should use WSL2. The expected VitaSDK environment is:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

If VitaSDK is missing, follow the current installer flow from https://vitasdk.org/.

From WSL as root or with sudo:

```bash
apt-get update
apt-get install -y make git cmake python3 wget ca-certificates curl xz-utils
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
git clone https://github.com/vitasdk/vdpm /tmp/vdpm
cd /tmp/vdpm
./bootstrap-vitasdk.sh
./install-all.sh
```

## Build
From Windows PowerShell:

```powershell
.\scripts\build-wsl.ps1
```

From WSL/Linux:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
**Artifacts:**
- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

## Host Tests

Portable regression tests do not require VitaSDK:

```bash
cmake -S . -B build-host -DEQVITA_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

These cover shared ABI/preset validation, DSP channel-state behavior, sparse audio-port tracking, app persistence, and UI source checks. They do not replace hardware testing.

## Release Hygiene

Before sharing a build, run:

```powershell
.\scripts\check-release-hygiene.ps1
```

This checks version text, VPK asset references, LiveArea PNG format, missing docs links, and accidental packaged audio files.

## Common Issues
- **Missing math symbols:** Ensure `m` and `gcc` are linked.
- **`vita-elf-create` warnings:** Ignore if using `libraries` in `exports.yml`.
- **Clock skew warnings under WSL:** When building from `/mnt/c`, Make can warn about timestamps a fraction of a second in the future. If artifacts are produced, the build is usable; clone/build inside the WSL filesystem to avoid the warning.
- **GitHub Actions Vita build:** CI uses the official pinned `vitasdk/vitasdk` Docker image. Do not use `gnuton/vitasdk-docker:latest`; that image has broken before because its compiler required a newer glibc than the container provided.

## Install
1. Copy `eq_speaker.skprx` to `ur0:tai/`.
2. Add `ur0:tai/eq_speaker.skprx` to `ur0:tai/config.txt` under `*KERNEL`.
3. Reboot, install `EQVita.vpk`, and run.

## Presets

Current presets are stored as validated wrapper files:

- `ur0:data/eqvita/preset0.eqvp`
- `ur0:data/eqvita/preset1.eqvp`
- `ur0:data/eqvita/preset2.eqvp`

Legacy raw `preset%d.bin` files are imported read-only when no `.eqvp` file exists for the slot.

## Hardware Testing

Before publishing binaries or closing runtime issues, run [hardware-test-checklist.md](hardware-test-checklist.md).

For the full publish flow, use [release-checklist.md](release-checklist.md).

