# Hardware Test Checklist

Use this before sharing a build or closing audio issues.

For normal people: if it sounds clean on the Vita, we are in good shape.

For devs: host tests do not prove kernel audio timing, so real hardware still gets the final vote.

## Setup

- Install the matching `eq_speaker.skprx` and `EQVita.vpk` from the same build.
- Confirm the app shows the expected ABI/version from [README.md](../../README.md).
- Keep a copy of `ur0:data/eqvita/app.log` after testing.

## Boot And Launch

- Reboot after installing the kernel plugin.
- Confirm the Vita boots normally.
- Launch EQVita and check that the app opens without a crash or long hang.
- Confirm the saved EQ state is active after boot before changing settings in the app.

## App UI

- Move through every menu with D-pad and buttons.
- Use touch on a few rows and sliders.
- Check that the Vita back button backs out through screens and exits only from the main menu.
- Change theme and reopen the app to confirm the theme sticks.

## Presets

- Load `STOCK Depth`.
- Load `MOD Switch`.
- Edit Simple EQ, save to a preset slot, reopen, and confirm values are still there.
- Edit Advanced EQ, save to a preset slot, reopen, and confirm values are still there.

## Output Modes

- Test Speakers mode on the Vita speakers.
- Test All outputs mode with wired headphones if available.
- Test Bluetooth if available.
- Confirm labels in the app match the selected mode.

## Audio Stability

- Toggle EQ on/off and listen for obvious changes.
- Change presets while audio is playing.
- Open, minimize, and close a few apps.
- Start a heavier game and listen during loading screens, menus, and notifications.
- Watch for clipping, crackle, audio dropouts, or EQ randomly bypassing.

## Game Stress Flow

Use this exact flow when checking a game's distortion issue:

- You do not have to delete `ur0:data/eqvita/app.log`; each app launch writes a `run_id`.
- Delete or move the old log only if you want a smaller file.
- Reboot the Vita.
- Open EQVita, choose the output mode and preset you are testing, then exit EQVita.
- Launch your game.
- Replicate the scenario when distortion happens.
- Open EQVita once more and wait a few seconds on the Telemetry screen.
- Copy `ur0:data/eqvita/app.log` and note the preset and output mode used.

## Diagnostic Log Pass

Use this when checking clipping/static/distortion regressions:

- Install `EQVita.vpk` and `eq_speaker.skprx` from the same build.
- Reboot after replacing the plugin.
- Reproduce the issue without changing extra settings during the flow.
- Open EQVita after the issue happens and leave it open for 5-10 seconds so the app can drain plugin diagnostics into `app.log`.
- Copy `ur0:data/eqvita/app.log`.
- Include the whole log even if it contains older runs; `run_id` separates app launches.

Normal Release builds keep the audio hook lighter. They still log app runs, status snapshots, timing summaries, and port lifecycle events. If a problem needs deeper block-level data, make a diagnostic build with `-DEQVITA_AUDIO_DIAGNOSTICS=ON` as shown in [how-to-build.md](../build/how-to-build.md), then repeat the same flow.

Important log fields:

- `run_id`: one app launch. Use this to separate old and new testing in one log file.
- `diag-core:`: compact plugin event identity drained by the app.
- `diag-time:`: timing and control epoch for the same `seq`.
- `diag-level:`: peak, clip, preamp, headroom, and HPF values for the same `seq`.
- `evt`: event type, such as `active`, `clip`, `slow`, `config-mismatch`, `bypass`, `copy-error`, `output-error`, `dsp-retarget`, or port lifecycle events.
- `port`, `gen`, `type`: SceAudio port number, EQVita port generation, and port type (`main`, `bgm`, `voice`, or `unknown`).
- `len`, `sr`, `ch`: audio buffer length, sample rate, and channel count.
- `route`, `reason`: selected output route and current bypass/pass-through reason.
- `ret`: return code from the original SceAudio call for output events.
- `elapsed_us`, `budget_us`: rough time spent around an EQ attempt compared with the block duration budget.
- `status-time`, `status-margin`, `status-stage`: summary timing fields for checking whether DSP or the original Vita audio call used the most time.
- `clips`: limiter/clip events counted for that block.
- `in_peak`, `out_peak`: source peak before EQ and output peak after EQ, left/right.
- `preamp`, `eff_preamp`, `max_boost`, `hpf`: requested gain, effective gain, highest positive band boost, and HPF state.

Some block-level fields are only expected from a diagnostic build. If they are missing in a normal Release log, that is expected.

What to report:

- Whether the distortion was constant or only during loading/menus.
- Whether the custom Vita theme music was clean while system/game sounds distorted.
- Whether opening and closing the official Settings app cleared the issue.
- The exact preset, preamp, output mode, and whether EQ was enabled.
- Any `diag-core:`, `diag-time:`, and `diag-level:` lines near the run where the issue happened.

## Logs

- After testing, check `ur0:data/eqvita/app.log`.
- If reporting a bug, include the log, Vita model, firmware, plugin list, output mode, preset, and what was happening when the issue appeared.
- More detail: [audio-stability.md](../audio/audio-stability.md).
