# Known Limits

EQVita is a Vita kernel plugin plus a normal Vita app. That means some things can only be proven on real hardware.

For normal people: if it sounds good on your Vita, that is the test that matters most.

For people who want the details: host tests catch shared logic bugs, but they cannot prove kernel hook timing, port behavior, suspend/resume, Bluetooth, wired output, or heavy-game stability.

## Audio Stability

- Short bypasses can happen when the Vita opens, closes, or reconfigures audio ports.
- Heavy games can stress audio timing more than the home screen.
- A clipping counter does not explain every ugly sound. Crackle, static, popping, and stutter can also come from timing or port changes.
- EQVita logs useful status when the app opens, so open EQVita once after an audio issue before sharing `ur0:data/eqvita/app.log`.

## Output Routes

- Vita speakers, wired headphones, and Bluetooth can behave differently.
- Speaker-only mode is meant to affect only the Vita speakers.
- All-output mode also allows wired headphones and Bluetooth.
- Hardware testing should mention which output was used.

## Version Matching

Keep `EQVita.vpk` and `eq_speaker.skprx` from the same release.

Mixed versions can fail, bypass, or report an ABI mismatch.

## Emulators

Vita emulators are useful for some app-side checks, but they are not a replacement for a real Vita when testing kernel audio behavior.

## Release Honesty

Do not claim a release is audio-stable from CI alone.

Before publishing, test on real hardware and record what was tested in the release notes.
