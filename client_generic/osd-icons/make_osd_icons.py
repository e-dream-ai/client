#!/usr/bin/env python3
"""Render the OSD icon PNGs in client_generic/Runtime/ from the SVGs in src/.

Usage (from anywhere):
    python3 client_generic/osd-icons/make_osd_icons.py [--scale 1.0] [--out DIR]

Needs rsvg-convert (macOS: `brew install librsvg`). See README.md for where the
source SVGs come from and how to add an icon.
"""
import argparse
import math
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "src"
RUNTIME = HERE.parent / "Runtime"

# Brand peach: the color of the infinidream logo (website accent #FCD9B7).
COLOR = "#FFDAB8"

# Every icon is drawn on a 100x100 canvas, then rendered to its pixel size.
RING_RADIUS = 43.5
RING_WIDTH = 6.5
STROKE_IN_RING = 6  # canvas units, for line-style glyphs placed inside the ring


def read_svg(name):
    text = (SRC / f"{name}.svg").read_text()
    m = re.search(r"<svg([^>]*)>(.*)</svg>", text, re.S)
    attrs, body = m.group(1), m.group(2)
    vx, vy, vw, vh = (float(v) for v in re.search(r'viewBox="([^"]+)"', attrs).group(1).split())
    is_line = 'stroke="currentColor"' in attrs
    return (vx, vy, vw, vh), is_line, body


def glyph(name, x, y, size, stroke=None):
    """Fit source SVG `name` into the square (x, y, size) of the canvas."""
    (vx, vy, vw, vh), is_line, body = read_svg(name)
    k = size / max(vw, vh)
    ox = x + (size - vw * k) / 2 - vx * k
    oy = y + (size - vh * k) / 2 - vy * k
    # Elements may set their own fill/stroke (e.g. a stroked arc in a filled icon);
    # currentColor becomes the icon color, everything else inherits from the group.
    body = body.replace("currentColor", COLOR)
    if is_line:
        width = stroke / k if stroke else 2  # Lucide's native 2px on a 24px grid
        paint = (f'fill="none" stroke="{COLOR}" stroke-width="{width:.3f}" '
                 'stroke-linecap="round" stroke-linejoin="round"')
    else:
        paint = f'fill="{COLOR}"'
    return f'<g transform="translate({ox:.3f} {oy:.3f}) scale({k:.5f})" {paint}>{body}</g>'


def doc(*parts, w=100, h=100):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}">'
            + "".join(parts) + "</svg>")


def ringed(name, size, dx=0.0):
    """A glyph centred in the round button ring (dx nudges it for optical centring)."""
    ring = (f'<circle cx="50" cy="50" r="{RING_RADIUS}" fill="none" '
            f'stroke="{COLOR}" stroke-width="{RING_WIDTH}"/>')
    return doc(ring, glyph(name, 50 - size / 2 + dx, 50 - size / 2, size, STROKE_IN_RING))


def plain(name, pad=0.0):
    return doc(glyph(name, pad, pad, 100 - 2 * pad))


def pill():
    # Speed-meter dot, 16x45 at 1x.
    return doc(f'<rect x="0" y="0" width="16" height="45" rx="8" fill="{COLOR}"/>', w=16, h=45)


# Buffering moon: a sprite sheet of MOON_FRAMES phases, new -> full -> new, in a
# MOON_COLS-wide grid. OSD.hpp's CBufferingIndicator must use the same numbers.
MOON_FRAMES = 24
MOON_COLS = 6
MOON_CELL = 48  # px per frame at scale 1


def moon_phase(p, ox, oy):
    """One moon on the 100x100 cell at (ox, oy): black disc, ring, and the lit part
    for phase p (0 = new, 0.5 = full; lit on the right while waxing)."""
    cx, cy, r = ox + 50, oy + 50, RING_RADIUS
    parts = [f'<circle cx="{cx}" cy="{cy}" r="{r + RING_WIDTH / 2}" fill="#000"/>',
             f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{COLOR}" '
             f'stroke-width="{RING_WIDTH}"/>']
    k = math.cos(p * 2 * math.pi)        # 1 at new, -1 at full
    if k < 0.999:
        side = 1 if p < 0.5 else -1      # lit limb
        bulge = side if k > 0 else -side  # crescent: terminator bows toward the lit limb
        rx = abs(k) * r
        # SVG y points down, so sweep-flag 1 is clockwise.
        limb = 1 if side > 0 else 0
        term = 0 if bulge > 0 else 1
        parts.append(f'<path fill="{COLOR}" d="M {cx} {cy - r} '
                     f'A {r} {r} 0 0 {limb} {cx} {cy + r} '
                     f'A {rx:.3f} {r} 0 0 {term} {cx} {cy - r} Z"/>')
    return "".join(parts)


def moon_sheet():
    rows = math.ceil(MOON_FRAMES / MOON_COLS)
    cells = [moon_phase(i / MOON_FRAMES, (i % MOON_COLS) * 100, (i // MOON_COLS) * 100)
             for i in range(MOON_FRAMES)]
    return doc(*cells, w=MOON_COLS * 100, h=rows * 100)


# output name -> (svg, width, height at scale 1)
def icons():
    big, animal, symbol = 135, 96, 65
    return {
        "osd-play": (ringed("FaPlay", 38, dx=3), big, big),
        "osd-pause": (ringed("FaPause", 38), big, big),
        "osd-back": (ringed("FaStepBackward", 35, dx=-1), big, big),
        "osd-next": (ringed("FaStepForward", 35, dx=1), big, big),
        "osd-like": (ringed("FaThumbsUp", 42), big, big),
        "osd-dislike": (ringed("FaThumbsDown", 42), big, big),
        "osd-repeat": (ringed("Repeat", 46), big, big),
        "osd-shuffle": (ringed("FaRandom", 42), big, big),
        "osd-back10": (plain("Replay10", pad=2), big, big),
        "osd-forward10": (plain("Forward10", pad=2), big, big),
        "osd-report": (plain("LuMessageSquareWarning"), big, big),
        "osd-turtle": (plain("LuTurtle"), animal, animal),
        "osd-rabbit": (plain("LuRabbit"), animal, animal),
        "osd-snail": (plain("LuSnail"), animal, animal),
        "osd-bird": (plain("FlyingBird"), animal, animal),
        "osd-brightness": (plain("LuSun"), symbol, symbol),
        "osd-buffering": (moon_sheet(), MOON_COLS * MOON_CELL,
                          math.ceil(MOON_FRAMES / MOON_COLS) * MOON_CELL),
        "osd-pill": (pill(), 16, 45),
    }


def strip_ancillary_chunks(path):
    """Keep only IHDR, IDAT and IEND. rsvg-convert writes a bKGD chunk, and the
    client's PNG loader (DisplayOutput/LoadPNG.cpp) flattens any image with a bKGD
    onto that color and drops the alpha channel, so the icons would not draw."""
    data = path.read_bytes()
    out, i = [data[:8]], 8
    while i < len(data):
        n = struct.unpack(">I", data[i:i + 4])[0]
        if data[i + 4:i + 8] in (b"IHDR", b"IDAT", b"IEND"):
            out.append(data[i:i + 12 + n])
        i += 12 + n
    path.write_bytes(b"".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scale", type=float, default=1.0,
                    help="pixel scale relative to the sizes the OSD was laid out for")
    ap.add_argument("--out", type=Path, default=RUNTIME, help="output directory")
    args = ap.parse_args()

    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        sys.exit("rsvg-convert not found; install librsvg (brew install librsvg)")
    args.out.mkdir(parents=True, exist_ok=True)
    for name, (svg, w, h) in icons().items():
        out = args.out / f"{name}.png"
        subprocess.run([rsvg, "-w", str(round(w * args.scale)), "-h", str(round(h * args.scale)),
                        "-o", str(out)], input=svg.encode(), check=True)
        strip_ancillary_chunks(out)
        print(out)


if __name__ == "__main__":
    main()
