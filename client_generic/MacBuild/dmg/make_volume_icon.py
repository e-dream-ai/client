#!/usr/bin/env python3
"""Make the DMG's volume icon (volume.icns) from the app icon.

The output is committed; build.py only reads it. Rerun this after changing
../infinidream.icns. Needs Pillow (pip install -r requirements.txt here) and
iconutil (part of macOS):
    python3 make_volume_icon.py

The app icon is a full-bleed square, because macOS masks app icons to its own
tile shape. A volume icon is drawn as is, so the square would show. This cuts
the logo out along the outer edge of its ring and leaves the rest transparent.
"""

import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
APP_ICON = HERE.parent / "infinidream.icns"
OUT = HERE / "volume.icns"

# The ring's outer edge in the app icon's 1024px image: centered, radius 420.
# Cut 2px inside it so no sky color fringes the circle.
SRC_SIZE, SRC_RADIUS = 1024, 420 - 2
FILL = 0.96   # circle diameter as a fraction of the icon's width
SUPER = 4     # draw the mask at 4x and downsample, for anti-aliasing
# Largest image to include, to keep the file small. 256px covers the desktop
# at Finder's largest icon size (128pt) and the DMG window, both at 2x; only a
# Finder window zoomed past 128pt scales it up.
MAX_SIZE = 256


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src_set = Path(tmp) / "src.iconset"
        out_set = Path(tmp) / "volume.iconset"
        out_set.mkdir()
        subprocess.run(["iconutil", "-c", "iconset", str(APP_ICON), "-o", str(src_set)],
                       check=True)
        src = Image.open(src_set / "icon_512x512@2x.png").convert("RGBA")
        if src.size != (SRC_SIZE, SRC_SIZE):
            raise SystemExit(f"expected a {SRC_SIZE}px app icon, got {src.size}")

        # Circular alpha mask, then crop to the circle's bounding box.
        c, r = SRC_SIZE / 2, SRC_RADIUS
        mask = Image.new("L", (SRC_SIZE * SUPER, SRC_SIZE * SUPER), 0)
        ImageDraw.Draw(mask).ellipse(
            [(c - r) * SUPER, (c - r) * SUPER, (c + r) * SUPER, (c + r) * SUPER], fill=255)
        src.putalpha(mask.resize(src.size, Image.LANCZOS))
        circle = src.crop((round(c - r), round(c - r), round(c + r), round(c + r)))

        for points in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                size = points * scale
                if size > MAX_SIZE:
                    continue
                inner = round(size * FILL)
                icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
                icon.paste(circle.resize((inner, inner), Image.LANCZOS),
                           ((size - inner) // 2, (size - inner) // 2))
                suffix = "@2x" if scale == 2 else ""
                icon.save(out_set / f"icon_{points}x{points}{suffix}.png", optimize=True)

        subprocess.run(["iconutil", "-c", "icns", str(out_set), "-o", str(OUT)], check=True)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
