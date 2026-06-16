# Contributing To EQVita

EQVita is a hobby PS Vita audio project. The goal is simple: make the Vita sound better without making the system unstable.

This page is for people who want to build, test, or change the project.

## Project Layout

- `plugin/` - the kernel plugin that hooks Vita audio output.
- `app/` - the Vita app, UI, presets, themes, and logs.
- `common/` - shared app/plugin structs, preset helpers, and ABI versioning.
- `tests/` - host tests that catch common regressions.
- `docs/` - indexed build, release, audio, and hardware testing notes.
- `scripts/` - WSL and release helper scripts.

## Important Rules

- Keep the audio hook small and fast. It runs on a timing-sensitive path.
- Do not do file I/O, logging, allocations, sleeps, or heavy API calls inside the audio output hook.
- Keep app/control logic separate from plugin/audio logic when possible.
- Do not change shared app/plugin structs casually. If the ABI changes, update the version and tests.
- Host tests are useful, but real Vita hardware is the final test for audio behavior.
- Do not claim a build is stable until it has been tested on hardware.

For normal people: if it touches audio timing, test it on a real Vita.

For people who know the pain: keep the hot path boring, bounded, and predictable.

## Build And Test

On Windows, use WSL:

```bash
EQVITA_BUILD_TYPE=Debug bash scripts/test-host-wsl.sh
EQVITA_ARTIFACT_DIR=build EQVITA_BUILD_TYPE=Release bash scripts/build-wsl.sh
```

Before sharing a build, also run:

```powershell
.\scripts\check-release-hygiene.ps1
```

## Hardware Testing

Use [docs/testing/hardware-test-checklist.md](docs/testing/hardware-test-checklist.md) before calling audio work done.

When reporting a bug, include:

- Vita model and firmware.
- Output mode: speakers, wired, or Bluetooth.
- Preset and preamp.
- What app/game was running.
- `ur0:data/eqvita/app.log`.

## Documentation Style

Keep docs simple. Start with the plain explanation, then add the technical detail.

Good style:

- "EQ is off because this output is not selected."
- "For devs: the plugin bypassed because the route hint did not match the active port."

Avoid making the repo feel like internal automation notes. This is a public hobby project.
