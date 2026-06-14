# EQVita

EQVita is a PS Vita system audio equalizer.

It has two parts:

- `eq_speaker.skprx` - taiHEN kernel plugin that hooks Vita audio output.
- `EQVita.vpk` - companion app for presets, tuning, themes, status, and logs.

Current app/plugin ABI: `1.13.0`.

## What It Does

- 10-band graphic EQ: 31 Hz through 16 kHz.
- Simple EQ mode for bass, mids, and treble.
- Advanced EQ mode for every band.
- Preset slots with validated `.eqvp` files.
- Built-in `STOCK Depth` and `MOD Switch` presets.
- Speaker-only mode, or all outputs mode for wired/Bluetooth too.
- Optional bass guard / HPF.
- Safe, Loud, and Direct headroom modes.
- Vita-style UI with themes. `Crimson Vita` is the default.
- Live telemetry for route, bypass reason, clipping, peaks, ports, and hook timing.
- Boot persistence through `ur0:data/eqvita/boot.eqbs`.
- App log at `ur0:data/eqvita/app.log`.

## Repo Layout

- `plugin/` - kernel plugin
- `app/` - Vita companion app and launcher assets
- `common/` - shared ABI, preset, and boot-state helpers
- `tests/` - host-side regression tests
- `docs/` - build and hardware test notes
- `research/vitasdk-audit-2026-06/` - VitaSDK and project audit notes
- `scripts/` - Windows/WSL build helpers

## Build

Install [VitaSDK](https://vitasdk.org/) first.

On Windows, use WSL:

```powershell
.\scripts\build-wsl.ps1 -Clean
```

From WSL/Linux:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Build outputs:

- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

Host tests:

```bash
cmake -S . -B build-host -DEQVITA_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

GitHub Actions runs host tests normally and builds Vita artifacts inside the official pinned VitaSDK Docker image.

## Install On Vita

1. Copy `build/plugin/eq_speaker.skprx` to `ur0:tai/`.
2. Add it under `*KERNEL` in `ur0:tai/config.txt`:

   ```text
   *KERNEL
   ur0:tai/eq_speaker.skprx
   ```

3. Reboot the Vita.
4. Install `build/app/EQVita.vpk`.
5. Open EQVita and tune from the app.

The app and plugin must both be from the same release line. For this build, keep them on ABI `1.13.x`.

## Controls

- D-pad: move through rows.
- Left / right: change the selected value.
- L / R: coarse EQ changes.
- Cross: select, toggle, or activate.
- Circle: back; exits from the main menu.
- Start: quick EQ on/off.
- Triangle: Help.
- Touch: tap rows or drag lists.

## Presets And Data

EQVita stores its data in `ur0:data/eqvita/`.

- Presets: `preset0.eqvp`, `preset1.eqvp`, `preset2.eqvp`
- Boot state: `boot.eqbs`
- Theme: `theme.cfg`
- Log: `app.log`

Old raw `preset%d.bin` files are imported read-only when a matching `.eqvp` slot does not exist.

If you report an issue, share `ur0:data/eqvita/app.log` when possible.

## Route And Bypass Notes

The app detects speakers, wired audio, and Bluetooth through AVConfig and sends that route hint to the plugin.

- `Speakers` mode applies EQ only to Vita speakers.
- `All outputs` mode also allows wired and Bluetooth output.
- If EQ is bypassed, the app shows why: disabled, wrong route, unknown output, invalid port, large buffer, copy failure, unsupported format, or audio busy.

At boot, the plugin loads the saved boot state before the app starts. Opening the app refreshes the route hint and status.

## Testing

Run host tests before every commit. Before publishing binaries, test on real Vita hardware with:

[docs/hardware-test-checklist.md](docs/hardware-test-checklist.md)

Kernel audio hooks can pass host tests and still behave differently on real hardware, so hardware testing matters.

## Credits

Created by shevoK.

Built with VitaSDK, taiHEN, vita2d, and Feather Icons for the in-app icon sheets.
