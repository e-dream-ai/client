#!/usr/bin/env python3
"""Draw the DMG window background (background.png + background@2x.png).

The output PNGs are committed; build.py only reads them. Rerun this after
changing the layout here or the DMG layout in build.py (window size and icon
positions must agree). Needs Pillow (pip install -r requirements.txt here):
    python3 make_background.py

A night sky in the app icon's colors. Finder draws icon labels in black over
a background picture, even in dark mode, so each label sits on a light
cloud-colored pill (LABEL_Y is where Finder puts the label at text_size 13).
"""

import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONT = HERE.parent.parent / "Runtime" / "Lato-Regular.ttf"

# Window content size in points, and icon centers (must match build.py).
# HEIGHT has 8pt of slack below the visible area: the window's title bar is
# 32pt on recent macOS and 28pt on older ones, and the picture must not run
# short on either.
WIDTH, HEIGHT = 660, 408
APP_X, APPS_X, ICON_Y = 165, 495, 190
LABEL_Y, LABEL_W, LABEL_H = 274, 108, 22

# Brand colors, sampled from Runtime/logo.png.
NAVY = (0x17, 0x26, 0x39)
PEACH = (0xFF, 0xDA, 0xB8)
CLOUD = (0x92, 0x9A, 0xAE)
SKY_TOP = (0x0E, 0x18, 0x26)     # a little darker than NAVY
SKY_BOTTOM = (0x22, 0x33, 0x4C)  # a little lighter toward the horizon

SUPER = 4  # draw at 4x the output size and downsample, for anti-aliasing


def mix(a, b, t):
    """Solid color t of the way from a to b (no alpha, so no overlap seams)."""
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def render(scale: int) -> Image.Image:
    k = scale * SUPER  # pixels per point while drawing
    w, h = WIDTH * k, HEIGHT * k

    # Vertical night-sky gradient.
    grad = Image.linear_gradient("L").resize((w, h))
    img = Image.composite(Image.new("RGB", (w, h), SKY_BOTTOM),
                          Image.new("RGB", (w, h), SKY_TOP), grad)
    d = ImageDraw.Draw(img)

    def disc(cx, cy, r, fill):
        d.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], fill=fill)

    # Clouds along the bottom, as in the logo: grey behind, peach in front.
    back = mix(SKY_BOTTOM, CLOUD, 0.55)
    for cx, cy, r in [(40, 395, 70), (150, 410, 60), (520, 400, 72), (640, 385, 70)]:
        disc(cx, cy, r, back)
    for cx, cy, r in [(-10, 425, 70), (95, 430, 58), (215, 440, 66), (330, 432, 62),
                      (445, 440, 64), (565, 430, 60), (680, 425, 70)]:
        disc(cx, cy, r, PEACH)

    # Stars (each turned at random, seeded so reruns match) and a crescent moon.
    rng = random.Random(7)
    for cx, cy, r in [(58, 48, 7), (602, 40, 6), (628, 92, 4.5), (32, 104, 4),
                      (548, 112, 3), (262, 128, 3), (452, 30, 3.5), (300, 108, 2.5),
                      (40, 228, 3), (626, 214, 3.5), (330, 262, 2.5)]:
        pts = []
        tilt = rng.uniform(0, 2 * math.pi / 5)  # a star repeats every 72 degrees
        for i in range(10):
            a = -math.pi / 2 + tilt + i * math.pi / 5
            rr = r if i % 2 == 0 else r * 0.45
            pts.append(((cx + rr * math.cos(a)) * k, (cy + rr * math.sin(a)) * k))
        d.polygon(pts, fill=PEACH)
    moon = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(moon)
    mx, my, mr = 100, 64, 22
    md.ellipse([(mx - mr) * k, (my - mr) * k, (mx + mr) * k, (my + mr) * k], fill=255)
    bite = Image.new("L", (w, h), 0)
    ImageDraw.Draw(bite).ellipse(
        [(mx - mr + 12) * k, (my - mr - 7) * k, (mx + mr + 12) * k, (my + mr - 7) * k], fill=255)
    img.paste(PEACH, (0, 0), ImageChops.subtract(moon, bite))

    # Pills behind the black Finder labels.
    pill = mix(CLOUD, PEACH, 0.25)
    for cx in (APP_X, APPS_X):
        d.rounded_rectangle(
            [(cx - LABEL_W / 2) * k, (LABEL_Y - LABEL_H / 2) * k,
             (cx + LABEL_W / 2) * k, (LABEL_Y + LABEL_H / 2) * k],
            radius=LABEL_H / 2 * k, fill=pill)

    # Arrow from the app to Applications: rounded shaft plus a filled head.
    arrow = mix(SKY_TOP, PEACH, 0.75)
    x0, x1, y = APP_X + 95, APPS_X - 95, ICON_Y
    half = 3.5
    head_len, head_half = 24, 15
    d.rounded_rectangle([x0 * k, (y - half) * k, (x1 - head_len + 2) * k, (y + half) * k],
                        radius=half * k, fill=arrow)
    d.polygon([((x1 - head_len) * k, (y - head_half) * k), (x1 * k, y * k),
               ((x1 - head_len) * k, (y + head_half) * k)], fill=arrow)

    # Instruction text.
    font = ImageFont.truetype(str(FONT), 17 * k)
    text = "Drag infinidream into Applications to install"
    tw = d.textlength(text, font=font)
    d.text(((w - tw) / 2, 52 * k), text, font=font, fill=PEACH)

    return img.resize((WIDTH * scale, HEIGHT * scale), Image.LANCZOS)


def main() -> None:
    render(2).save(HERE / "background@2x.png", optimize=True)
    render(1).save(HERE / "background.png", optimize=True)
    print(f"wrote {HERE / 'background.png'} and @2x")


if __name__ == "__main__":
    main()
