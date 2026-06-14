# Requirements Checklist

## Status
- **Pass:** Implemented and verified.
- **Fail:** Missing or incomplete.

## Items
1. **System-wide EQ:** **Pass** (Hooks `sceAudioOut`)
2. **Speaker-only gating:** **Pass pending hardware validation** (App supplies AVConfig route hints; plugin bypasses unknown routes)
3. **No added latency:** **Partial** (Output hook now bypasses instead of waiting on a busy audio lock, but hook CPU cost must be hardware-tested)
4. **44.1/48 kHz support:** **Pass pending hardware validation** (Port sample rate is tracked per open/set-config)
5. **10-band EQ + Preamp:** **Pass** (31Hz - 16kHz)
6. **Instant updates:** **Pass** (Generated syscall stubs update validated control snapshots)
7. **Stability:** **Partial** (Known sparse-port and hard-clipping risks reduced in code; real hardware stress testing still required)
8. **Boot safety:** **Partial** (Plugin disabled by default; uninstall/recovery path still needs hardware validation)
9. **Install compatibility:** **Pass pending hardware validation** (3.65 Ensō target)

## Summary
- **Implemented in 1.11:** validated ABI, safer port config handling, per-channel DSP state, new preset wrapper, AVConfig route hints, bypass reasons, and host tests.
- **Implemented in 1.12:** sparse port registry for IDs such as `256`, non-blocking audio-lock bypass, cheaper flat-band DSP, no-op smoothing detection, soft-knee overflow limiting, and hook timing/contention counters.
- **Implemented in 1.13:** per-port DSP scratch buffers, GetConfig-based unknown-port recovery, deferred SetConfig/Release handling while a port is processing, startup preset sync before default control, and explicit Safe/Loud/Raw headroom modes.
- **Still required:** full hardware validation across speaker, headphones, Bluetooth, BGM/music player, lpp-vita, games, suspend/resume, and uninstall/reboot recovery.

