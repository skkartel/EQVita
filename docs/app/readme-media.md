# README Media

README images live in `media/`.

They are only for GitHub. They are not packed into the VPK.

Current files:

- `EQVITA.png` - README banner.
- `app-flow.png` - app walkthrough APNG.
- `music-preview.png` - Music Preview APNG.
- `themes.png` - theme preview APNG.

## Source Screenshots

Keep raw screenshot dumps outside the repo.

The current local folders were:

```text
C:/Users/Administrator/Downloads/vitascreens/APP_FLOW/
C:/Users/Administrator/Downloads/vitascreens/THEMES/
```

Sort by filename. Vita screenshots include the date and time in the name, so filename sorting gives the right order.

## APNG Settings

Use APNG instead of GIF when possible. GIF made the UI look crunchy because of dithering.

Good settings:

- size: `960x544`;
- app walkthrough speed: `1250 ms` per frame;
- theme preview speed: `950 ms` per frame;
- loop forever;
- true-color PNG/APNG, not palette GIF.

## If The Preview Looks Cropped

Some APNG tools try to be clever and save only the changed part of each frame. GitHub or local preview tools can then show cropped frames.

If that happens, every frame needs to be saved as the full Vita screen:

```text
width=960 height=544 x=0 y=0
```

With Pillow, save like this:

```python
frames[0].save(
    "media/app-flow.png",
    save_all=True,
    append_images=frames[1:],
    format="PNG",
    duration=1250,
    loop=0,
    disposal=1,
    blend=0,
    optimize=False,
)
```

Quick check:

```python
from pathlib import Path
import struct

data = Path("media/app-flow.png").read_bytes()
pos = 8
rects = []

while pos + 8 <= len(data):
    length = struct.unpack(">I", data[pos:pos + 4])[0]
    typ = data[pos + 4:pos + 8]
    payload = data[pos + 8:pos + 8 + length]
    if typ == b"fcTL":
        seq, w, h, x, y, delay_num, delay_den, dispose, blend = struct.unpack(">IIIIIHHBB", payload)
        rects.append((w, h, x, y))
    pos += 12 + length

print(sum(1 for rect in rects if rect == (960, 544, 0, 0)), "/", len(rects))
```

The two numbers should match.

## Updating App Flow

To add new screenshots to `app-flow.png`:

1. Put the screenshots in the source folder.
2. Sort them by name.
3. Insert the new shots where they make sense.
4. Regenerate the APNG with the full-frame settings above.
5. Open the README and make sure nothing is cropped.

For the current Music Preview shots, they were inserted after:

```text
2026-06-16-020744.png
```

and before:

```text
2026-06-16-020803.png
```

## Music Preview APNG

`media/music-preview.png` is the short preview for just the Music Preview feature.

Current frames:

- `2026-06-16-070219.png`
- `2026-06-16-070222.png`
- `2026-06-16-070235.png`

Keep this one short. The main app preview already shows the full app.
