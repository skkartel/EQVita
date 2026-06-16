# Themes And Icons

EQVita themes are colors plus a matching icon sheet.

Theme code lives in:

- `app/ui_vita.c`
- `app/assets/ui_icons_*.png`

Default theme: `Crimson Vita`.

## Themes

Themes should look different, but the app should still feel like the same app.

Keep this in mind:

- text must be easy to read;
- selected rows must stand out;
- do not make the UI muddy;
- do not move controls around per theme;
- avoid huge decorative stuff that wastes screen space.

Add a new theme only if it has its own look and still reads well on the Vita screen.

## Icon Sheets

The app expects every icon sheet to use the same layout:

- icon cell: `64x64`;
- sheet size: `1472x192`;
- `23` icons across;
- `3` rows of variants.

Variant rows:

- row `0`: normal;
- row `1`: read-only / dim;
- row `2`: selected.

Do not resize only one sheet. If one theme sheet changes size or order, the icons will be wrong.

## Icon Order

The icon order is matched in `icon_index()` inside `app/ui_vita.c`.

Current columns:

- `0` simple/tune
- `1` advanced
- `2` speaker/route
- `3` settings/theme/HPF/headroom
- `4` about/info/help
- `5` preset
- `6` load/nav
- `7` reset
- `8` power
- `9` level
- `10` status
- `11` save
- `12` Circle footer icon
- `13` Cross footer icon
- `14` Triangle footer icon
- `15` left chevron
- `16` right chevron
- `17` music
- `18` play
- `19` stop
- `20` loop
- `21` folder
- `22` file

## Adding An Icon

Quick version:

1. Use a Feather icon when possible.
2. Add a new `64x64` column to every `ui_icons_*.png`.
3. Draw normal, dim, and selected variants.
4. Keep the icon centered in the circle.
5. Match the size and thickness of the existing icons.
6. Update `icon_index()` in `app/ui_vita.c`.
7. Update `app/assets/THIRD_PARTY_NOTICES.md` if the icon source changes.
8. Run tests and build the VPK.

## Centering

Some icons are annoying. Music notes, play buttons, and arrows can look off-center even when the pixels are technically centered.

Use a Vita screenshot as the final check.

The icon should:

- look centered inside the circle;
- line up between normal and selected versions;
- not jump between themes.

Tiny manual nudges are fine.

## Please Do Not

- Do not commit random raw icon source folders.
- Do not add an icon to only one theme.
- Do not change the atlas size without changing the renderer.
- Do not use Sony assets or copied app assets.
