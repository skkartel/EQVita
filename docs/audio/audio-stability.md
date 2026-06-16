# Audio Stability Notes

EQVita works by touching audio while the Vita is already busy playing it. That means performance matters a lot.

For normal people: if EQVita does too much work, the Vita can pop, crackle, distort, or skip EQ for a moment.

For devs: the plugin sits on the `sceAudioOutOutput` path, so the hot path has to stay small, predictable, and allocation-free.

## Current Direction

The current plugin keeps the audio path focused on:

- validating the current output port;
- copying the input block into a bounded scratch buffer;
- applying the active EQ bands;
- copying the processed block back;
- passing the block to the original Vita audio function.

It avoids doing slow work in the hook:

- no file logging;
- no heap allocation;
- no blocking config work;
- no broad route discovery;
- no expensive text formatting.

## What Was Stabilized

Recent stabilization work focused on:

- sparse Vita audio port IDs, including ports such as `256`;
- safer handling when ports are opened, configured, released, or reused;
- per-port DSP state and scratch buffers;
- faster steady-state stereo DSP;
- active-band caches so flat bands do not waste time;
- safer app/plugin preset and boot-state sync;
- timing fields that show where time is spent.

The important lesson from hardware testing was that clipping counters do not explain every ugly sound. Some distortion was really timing pressure.

## Logs

EQVita writes its app log to:

```text
ur0:data/eqvita/app.log
```

Useful lines:

- `start:` - app version, preset source, active slot, and route.
- `status:` - current route, active state, port, buffer size, clipping, and peaks.
- `status-time:` - recent hook timing and budget margin.
- `status-margin:` - worst timing margin seen so far.
- `status-stage:` - time spent in major hook stages.
- `diag-core:` - compact plugin events such as port open/release/config.

If a log has old runs in it, that is okay. Each app launch has a `run_id`.

## Clip Count Versus Distortion

The clip counter means the limiter or sample bounds were hit. It is useful, but it is not the whole story.

You can have:

- high clip count with audio that still sounds okay;
- ugly crackle/static with little or no clip count;
- short bypasses when a port is busy or unknown.

That is why hardware testing still matters.

## Known Sensitive Areas

Be careful with changes in:

- `plugin/main.c` audio output hook;
- `plugin/dsp.c` sample processing;
- `plugin/port_registry.h` port tracking;
- `plugin/port_state.h` lifecycle state;
- `common/eq_shared.h` app/plugin ABI.

Small changes in these files can affect real hardware even when host tests pass.
