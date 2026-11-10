#!/usr/bin/env python3
"""Write the hand-tuned source SVGs: src/Replay10.svg, src/Forward10.svg, src/Repeat.svg.

These are drawn here rather than taken from an icon set so their proportions can
match the rest of the OSD: rings and lines as thick as the button rings, filled
arrowheads sized and rounded like the shuffle icon's, and a "10" in Lato Black
whose stroke matches the ring.

Usage:
    python3 client_generic/osd-icons/make_custom_svgs.py --font /path/to/Lato-Black.ttf
    python3 client_generic/osd-icons/make_osd_icons.py

Needs fontTools (pip install fonttools). Lato Black: https://fonts.google.com/specimen/Lato
(SIL Open Font License); only its digit outlines end up in the SVGs, so the
client does not need the font.
"""
import argparse
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "src"

# Canvas units below are make_osd_icons.py's 100x100 canvas.
RING_W = 6.5          # ring/line thickness, same as the button rings
CORNER = 2.0          # arrowhead corner radius, like FaRandom's arrowheads
LATO_BLACK_STEM = 0.189  # stem width / digit height of Lato Black's "1" (measured)


def rounded_triangle(pts, d, **attrs):
    """Triangle `pts` with corners rounded to radius d (inset by d, stroked 2d, round joins).
    Pointed corners lose some length in the process; see outline_box()."""
    q = _inset(pts, d)
    extra = "".join(f' {k.replace("_", "-")}="{v}"' for k, v in attrs.items())
    return ('<path d="M' + "L".join(f"{x:.3f} {y:.3f}" for x, y in q) +
            f'z" stroke="currentColor" stroke-width="{2 * d:.3f}" stroke-linejoin="round"{extra}></path>')


def _inset(pts, d):
    (ax, ay), (bx, by), (cx, cy) = pts
    a = math.dist((bx, by), (cx, cy)); b = math.dist((ax, ay), (cx, cy)); c = math.dist((ax, ay), (bx, by))
    p = a + b + c
    ix = (a * ax + b * bx + c * cx) / p; iy = (a * ay + b * by + c * cy) / p
    area = abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) / 2
    rin = area / (p / 2); f = (rin - d) / rin
    return [(ix + (x - ix) * f, iy + (y - iy) * f) for x, y in pts]


def lato_digits(font_path, text, center, height):
    """Outline group for `text` in the given font, centred on `center`, `height` units tall."""
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.ttLib import TTFont
    font = TTFont(font_path); gs = font.getGlyphSet(); cmap = font.getBestCmap(); hmtx = font["hmtx"]
    x = 0; parts = []; x0 = y0 = 1e9; x1 = y1 = -1e9
    for ch in text:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs); gs[g].draw(pen)
        bp = BoundsPen(gs); gs[g].draw(bp); b = bp.bounds
        x0 = min(x0, x + b[0]); x1 = max(x1, x + b[2]); y0 = min(y0, b[1]); y1 = max(y1, b[3])
        parts.append((x, pen.getCommands()))
        x += hmtx[g][0]
    k = height / (y1 - y0)
    return (f'<g transform="translate({center[0]} {center[1]}) scale({k:.6f} {-k:.6f}) '
            f'translate({-(x0 + x1) / 2:.2f} {-(y0 + y1) / 2:.2f})">'
            + "".join(f'<path transform="translate({dx} 0)" d="{d}"></path>' for dx, d in parts) + "</g>")


def skip10(font_path):
    """Replay10 / Forward10: open ring, arrowhead at the top, "10" inside.
    Grid: Material's 24-unit replay_10 (ring centre 12,13, outer radius 8)."""
    cx, cy, outer = 12.0, 13.0, 8.0
    half, length = 1.875, 3.75          # arrowhead half-height and length (sharp triangle)
    # The viewBox height sets the canvas scale, which sets the stroke width: iterate.
    sw = 1.2
    for _ in range(20):
        r = outer - sw / 2
        top = cy - r - half
        H = (cy + outer) - top
        sw = RING_W * H / 96            # make_osd_icons renders these with pad=2 -> 96 canvas units
    r = outer - sw / 2; ay = cy - r
    units = H / 96                      # svg units per canvas unit
    digits = lato_digits(font_path, "10", (cx, cy), RING_W / LATO_BLACK_STEM * units)
    phi = math.asin(1.0 / r)            # run the ring 1 unit under the arrowhead: no seam
    for name, sweep, side in [("Replay10", 1, -1), ("Forward10", 0, 1)]:
        sx, sy = cx + side * r * math.sin(phi), cy - r * math.cos(phi)
        end_x = cx + side * r
        arc = (f'<path d="M{sx:.3f} {sy:.3f}A{r:.3f} {r:.3f} 0 1 {sweep} {end_x:.3f} {cy}" fill="none" '
               f'stroke="currentColor" stroke-width="{sw:.3f}"></path>')
        arrow = rounded_triangle([(cx, ay - half), (cx + side * length, ay), (cx, ay + half)], CORNER * units)
        (SRC / f"{name}.svg").write_text(
            f'<svg viewBox="4 {top:.3f} 16 {H:.3f}" xmlns="http://www.w3.org/2000/svg">{arc}{arrow}{digits}</svg>')


def repeat():
    """Repeat: Lucide's repeat loop with filled arrowheads proportioned like FaRandom's
    (rounded outline 2.8x the line thickness tall, 1.6x long). Drawn with ringed(.., 46)."""
    k = 46 / 24                         # canvas units per svg unit
    line = 6 / k                        # make_osd_icons draws line glyphs in rings 6 canvas units wide
    want_l, want_h, d = 1.6 * line, 2.8 * line, CORNER / k

    def outline(L, H):                  # rounded outline size of a sharp L x H triangle
        q = _inset([(0, -H / 2), (L, 0), (0, H / 2)], d)
        return (max(x for x, _ in q) - min(x for x, _ in q) + 2 * d,
                max(y for _, y in q) - min(y for _, y in q) + 2 * d, q)
    L, H = want_l, want_h
    for _ in range(50):
        w, h, _ = outline(L, H)
        L *= want_l / w; H *= want_h / h
    w, _, q = outline(L, H)
    tip = max(x for x, _ in q) + d

    def head(tip_x, y, direction):
        pts = "L".join(f"{tip_x + direction * (x - tip):.3f} {y + yy:.3f}" for x, yy in q)
        return f'<path d="M{pts}z" fill="currentColor" stroke-width="{2 * d:.3f}"></path>'
    tip_r, tip_l = 22.0, 2.0
    (SRC / "Repeat.svg").write_text(
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" '
        'stroke-linejoin="round" xmlns="http://www.w3.org/2000/svg">'
        f'<path d="M3 11v-1a4 4 0 0 1 4-4h{tip_r - w + 1 - 7:.3f}"></path>'
        f'<path d="M21 13v1a4 4 0 0 1-4 4H{tip_l + w - 1:.3f}"></path>'
        f'{head(tip_r, 6, 1)}{head(tip_l, 18, -1)}</svg>')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--font", required=True, type=Path, help="path to Lato-Black.ttf")
    args = ap.parse_args()
    skip10(args.font)
    repeat()
    for n in ("Replay10", "Forward10", "Repeat"):
        print(SRC / f"{n}.svg")


if __name__ == "__main__":
    main()
