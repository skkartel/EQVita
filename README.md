# PS Vita Equalizer (EQVita)

System-wide 10-band graphic equalizer kernel plugin for PS Vita homebrew, with a companion UI app for controls and presets.

EQVita is built as two Vita artifacts:

- `eq_speaker.skprx`: taiHEN kernel plugin that hooks `sceAudioOut`.
- `EQVita.vpk`: companion app used to configure EQ, route hints, presets, and status.

## Status

Current ABI: `1.13.0`.

This version focuses on stability and compatibility:

- safer `sceAudioOutSetConfig(..., -1, -1, -1)` handling;
- no guessed audio-buffer lengths;
- per-channel DSP filter state;
- validated app/plugin control ABI;
- validated `.eqvp` preset files with legacy `.bin` import;
- AVConfig-based route hints from the companion app;
- persistent route hints for game audio after the app exits;
- explicit bypass reasons in the UI;
- sparse audio-port tracking for Vita port IDs such as `256`;
- non-blocking audio-hook bypass when the plugin audio lock is busy;
- cheaper DSP for flat bands and no-op control updates;
- soft-knee overflow limiting instead of hard rail clipping;
- runtime counters for active ports, busy bypasses, unknown ports, and hook processing time;
- per-port DSP scratch buffers and deferred port config changes to reduce app-switch/load-time races;
- explicit Safe/Loud/Raw headroom modes for stable or more forceful EQ tuning;
- app log for hardware testing.

Hardware testing is still required before publishing release binaries.

## Layout

- `plugin/` - kernel plugin (`eq_speaker.skprx`)
- `app/` - UI app (`EQVita.vpk`)
- `common/` - shared ABI and validation helpers
- `tests/` - host-side unit tests for portable logic
- `docs/` - build, test, and implementation notes
- `research/` - VitaSDK audit notes used for the stabilization work

## Build

Install [VitaSDK](https://vitasdk.org/) first. On Windows, build through WSL:

```powershell
.\scripts\build-wsl.ps1 -Clean
```

Inside WSL/Linux:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Outputs:

- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

Host-side regression tests:

```bash
cmake -S . -B build-host -DEQVITA_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Install

1. Copy `build/plugin/eq_speaker.skprx` to `ur0:tai/`.
2. Add it to `ur0:tai/config.txt` under `*KERNEL`:

   ```text
   *KERNEL
   ur0:tai/eq_speaker.skprx
   ```

3. Reboot.
4. Install `build/app/EQVita.vpk`.
5. Run EQVita to enable and configure the equalizer.

The plugin starts disabled by default. The app and plugin must match ABI `1.13.x` exactly for the current route/status/preset syscall payloads.

## Usage

- Bands: 31 Hz, 62 Hz, 125 Hz, 250 Hz, 500 Hz, 1 kHz, 2 kHz, 4 kHz, 8 kHz, 16 kHz.
- Gain range: +/-12 dB.
- Preamp range: +/-12 dB.
- HPF: optional 70 Hz high-pass; forced for speaker route.
- Headroom: Safe keeps boost headroom, Loud adds controlled makeup gain, Raw removes automatic headroom and can distort.
- Speaker-only mode: applies EQ only when the route is classified as speaker.
- Presets: `ur0:data/eqvita/preset0.eqvp` through `preset2.eqvp`.
- Legacy presets: old raw `preset%d.bin` files are imported read-only when no `.eqvp` preset exists.
- Boot persistence: the app writes the active EQ state to `ur0:data/eqvita/boot.eqbs` on save/exit; the plugin loads this at boot before falling back to preset slot 1.

Controls:

- D-pad: move or adjust selected value.
- L/R: coarse gain adjustment.
- Cross: toggle or activate selected action.
- Start: toggle enabled/bypass.
- Select: switch simple/advanced view.
- Circle: exit.

The app writes runtime test notes to `ur0:data/eqvita/app.log`.

## Route and Bypass Behavior

The companion app uses AVConfig to classify speaker, wired headphones, and Bluetooth, then sends that route hint to the plugin. The route hint persists after the app exits so launched games can still be processed. The kernel plugin still lets wired-headphone detection override a persisted hint; if there is no usable route hint and no wired-headphone signal, it bypasses instead of assuming speaker.

At boot, the companion app is not running yet, so the plugin restores the saved boot state from `boot.eqbs`. If an enabled saved state has no route hint, the plugin assumes speaker output until the app supplies a fresher AVConfig hint; wired headphones still override this in kernel before speaker-only EQ is applied.

The UI status shows whether DSP was applied to the last observed buffer and why it may be bypassed: disabled, speaker-only route mismatch, unknown route, invalid port, oversized buffer, copy failure, unsupported format, or audio lock busy. It also shows active tracked ports, busy bypass count, unknown-port count, and last/max hook processing time in microseconds.

## Credits

Built with VitaSDK and taiHEN. Earlier UI assets were adapted from VitaShell; verify asset licensing before distributing release packages.
