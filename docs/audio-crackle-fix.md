# Audio Crackle Fix Verification

This file records earlier crackle fixes. Current stabilization adds further changes: sparse audio-port tracking, non-blocking audio-hook busy bypass, cheaper flat-band DSP, no-op target detection, soft-knee overflow limiting, safer clip/peak accounting, per-port scratch buffers, deferred config changes, and explicit bypass reasons for unsafe port states.

## Issues & Fixes

### 1. Filter State Reset
**Issue:** Filter state (`z1`, `z2`) was reset on parameter updates, causing clicks.
**Fix:** `eq_dsp_apply` now preserves filter state when updating coefficients.

### 2. Incorrect RBJ Formula
**Issue:** `biquad_compute` used linear gain ($10^{dB/20}$) instead of $\sqrt{10^{dB/20}}$ for the $A$ parameter.
**Fix:** Updated to calculate $A = \sqrt{gain}$.

### 3. Denormals
**Issue:** Small floating point numbers caused CPU spikes.
**Fix:** Added check in `process_biquad` to flush denormals to zero.

### 4. High Frequency Stability
**Issue:** High frequencies near Nyquist caused instability.
**Fix:** Clamped center frequency to `sample_rate / 2 - 100`.

### 5. Incorrect Gain Calculation
**Issue:** `mdB_to_gain` used wrong divisor, causing 10x gain.
**Fix:** Updated divisor to `20000.0f`.

## Verification

1.  **Build & Install:**
    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ```
    Copy `build/plugin/eq_speaker.skprx` to `ur0:tai/`.

2.  **Test:**
    *   **Preamp:** Verify clarity at -6dB, -12dB, and 0dB.
    *   **EQ Bands:** Move sliders; ensure no crackling.
    *   **Extreme Settings:** Verify no distortion at +6dB.
    *   **Games:** Test with games and system sounds.


New debug counters are available in `eq_status_t`:
*   `debug_port`: Last audio port ID used.
*   `debug_len`: Number of frames processed in last call.
*   `debug_channels`: Number of channels processed.
*   `debug_run_count`: Total number of hook executions.
*   `debug_active_ports`: Number of currently tracked sparse audio ports.
*   `debug_busy_bypass_count`: Number of times the output hook skipped DSP rather than blocking on the audio mutex.
*   `debug_unknown_port_count`: Number of output calls for ports not tracked by the plugin.
*   `debug_last_us` / `debug_max_us`: Last and maximum hook processing time in microseconds.
*   `clip_events`, `peak_l`, `peak_r`: Cumulative limiter hits and recent peak levels copied into `app.log`.

Monitor these values to ensure `debug_len` matches expected buffer sizes (e.g., 256, 512, 1024), `debug_channels` is correct (usually 2), `debug_unknown_port_count` no longer rises repeatedly for sparse port IDs such as `256`, and `clip_events` does not climb during settings that should sound clean.
