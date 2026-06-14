# Hardware Test Checklist

Use this before sharing a build or closing audio/runtime issues. Host tests are useful, but the Vita audio path needs real hardware.

## Setup

- Install the matching `eq_speaker.skprx` and `EQVita.vpk` from the same build.
- Confirm the app shows the expected ABI/version from [README.md](../README.md).
- Keep a copy of `ur0:data/eqvita/app.log` after testing.

## Boot And Launch

- Reboot after installing the kernel plugin.
- Confirm the Vita boots normally.
- Launch EQVita and check that the app opens without a crash or long hang.
- Confirm the saved EQ state is active after boot before changing settings in the app.

## UI

- Move through every menu with D-pad and buttons.
- Use touch on a few rows and sliders.
- Check that the system back button backs out through screens and exits only from the main menu.
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

## P4G Distortion Flow

Use this exact flow when checking the Persona 4 Golden distortion issue:

- Delete or move the old `ur0:data/eqvita/app.log`.
- Reboot the Vita.
- Open EQVita, choose the output mode and preset you are testing, then exit EQVita.
- Launch Persona 4 Golden.
- At the PSN functions dialog, choose No.
- Dismiss the offline mode dialog and listen for distortion.
- Enter the movie/PV menu and listen for the menu voice distortion.
- Exit the game to the Vita home screen and test bubble/system sounds.
- Open and close the Vita Settings app if that still clears the distortion.
- Open EQVita once more and wait a few seconds on the Telemetry screen.
- Copy `ur0:data/eqvita/app.log` and note the preset and output mode used.

## Logs

- After testing, check `ur0:data/eqvita/app.log`.
- If reporting a bug, include the log, Vita model, firmware, plugin list, output mode, preset, and what was happening when the issue appeared.
