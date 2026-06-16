## Summary

- 

## Testing

- [ ] Host tests run.
- [ ] Vita Release build run.
- [ ] Release hygiene check run.
- [ ] Real Vita hardware tested, if this touches audio, boot, plugin, suspend/resume, install, or output routing.

## Vita Test Notes

- Vita model:
- Firmware:
- Output used:
- Preset used:
- Games/apps tested:
- Known limits:

## Audio/Plugin Changes

Leave this alone if the PR does not touch audio or plugin code.

- [ ] Hot-path work was kept small.
- [ ] No file I/O, heap allocation, or noisy logging was added to the audio hook.
- [ ] App and plugin ABI stayed compatible, or the ABI version was updated.
- [ ] `ur0:data/eqvita/app.log` was checked after testing.

## Release Notes

- 
