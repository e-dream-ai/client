# OSD icons

The on-screen display PNGs in `../Runtime/` are generated from the SVGs in
`src/` by `make_osd_icons.py`. Edit the SVGs or the script, not the PNGs.

```bash
brew install librsvg            # provides rsvg-convert
python3 client_generic/osd-icons/make_osd_icons.py
```

The script writes straight into `client_generic/Runtime/`. Commit the PNGs it
produces; the builds use them as-is.

## Look

- One flat color, `#FFDAB8` at full opacity: the peach of the infinidream logo
  (the website's accent `#FCD9B7` is the same color to the eye). Transparency,
  when wanted, belongs in the draw call (`DrawQuad`'s color alpha), not in the PNG.
- Round buttons are a ring plus a glyph, laid out on a 100×100 canvas; `ringed()`
  takes the glyph size and an optional horizontal nudge for optical centring.
- Pixel sizes match what `OSD.hpp` was laid out for (135 px buttons, 96 px speed
  animals, 65 px brightness symbol). `--scale` renders larger, but the renderer
  has no mipmaps for these textures, so larger PNGs alias on small screens.

Not generated (hand-made, unchanged): `osd-bg-700.png`, `osd-bg-sq.png` (the grey
panels) and `osd-pill-u.png` (unlit speed dot).

## Sources

The icons match the website's remote control. The SVGs were exported from the
frontend's `react-icons` package (v5.5.0) and its `src/icons/` folder.

| Prefix | Set | License |
| --- | --- | --- |
| `Fa*` | Font Awesome Free 5 (step back/forward stretched square; thumbs without the cuff dot) | CC BY 4.0 (icons) |
| `Lu*`, `Repeat` | Lucide (`Repeat` is Lucide `repeat` with filled arrowheads sized like `FaRandom`'s) | ISC |
| `Replay10`, `Forward10` | Google Material Icons `replay_10` / `forward_10`, redrawn with a thinner stroked ring, smaller rounded arrowhead and larger digits | Apache 2.0 |
| `FlyingBird` | frontend `src/icons/flying-bird.tsx` | ours |

To add an icon, drop its SVG in `src/` (24×24 line icons need
`stroke="currentColor"` on the root `<svg>`; filled icons need nothing), add a
line to `icons()` in the script, and load the PNG in `OSD.hpp`.
