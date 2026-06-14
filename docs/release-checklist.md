# Release Checklist

Use this before tagging or uploading a release.

## Clean Checks

- Run host tests:

  ```bash
  cmake -S . -B build-host -DEQVITA_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-host --parallel
  ctest --test-dir build-host --output-on-failure
  ```

- Run release hygiene:

  ```powershell
  .\scripts\check-release-hygiene.ps1
  ```

- Run a Vita Release build:

  ```bash
  export VITASDK=/usr/local/vitasdk
  export PATH="$VITASDK/bin:$PATH"
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --parallel
  ```

## Artifact Checks

- Confirm these files exist:
  - `build/app/EQVita.vpk`
  - `build/plugin/eq_speaker.skprx`
- Confirm the VPK does not include test audio files.
- Confirm `icon0.png`, `bg.png`, and `startup.png` are indexed 8-bit PNGs.
- Generate SHA256 sums for the VPK and plugin.

## Version Checks

- Keep `common/eq_shared.h`, `plugin/exports.yml`, `app/CMakeLists.txt`, and [README.md](../README.md) on the same ABI line.
- Keep the app and plugin from the same build together.
- Do not publish mixed old/new app and plugin binaries.

## Hardware Checks

- Run [hardware-test-checklist.md](hardware-test-checklist.md) on real Vita hardware.
- Do not claim audio stability from CI alone.
- Save `ur0:data/eqvita/app.log` from the test run.

## Upload

- Upload:
  - `EQVita.vpk`
  - `eq_speaker.skprx`
  - SHA256 checksum file
- In the release notes, mention the ABI version and any hardware test limits.
