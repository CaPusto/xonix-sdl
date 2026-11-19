#!/usr/bin/env python3
"""Generate the pixel-art Xonix icon used as the Windows executable icon.

Requires Pillow.  Run from anywhere:

    python3 tools/generate_icon.py

Outputs:
    assets/xonix.ico            ICO with 16x16 and 32x32 PNG-compressed entries
    assets/xonix-icon.png       32x32 source image (handy for reference)
"""

from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SIZE = 32

SEA = (14, 30, 58)
SEA_LIGHT = (25, 55, 96)
LAND = (45, 164, 92)
LAND_DARK = (27, 110, 66)
TRAIL = (60, 214, 240)
TRAIL_LIGHT = (165, 245, 255)
PLAYER = (255, 194, 61)
PLAYER_DARK = (198, 106, 25)
BALL = (240, 247, 255)
BALL_RED = (255, 77, 77)


def draw_icon() -> Image.Image:
    img = Image.new("RGB", (SIZE, SIZE), SEA)
    d = ImageDraw.Draw(img)

    # Sparse sea texture; keeps the background from looking flat at 32px.
    for y in range(2, SIZE, 4):
        for x in range(2, SIZE, 4):
            if (x + y) % 8 == 0:
                d.point((x, y), fill=SEA_LIGHT)

    # Captured land inside the trail loop, with a few darker pixels for texture.
    for box in ((10, 11, 21, 19), (12, 9, 19, 10),
                (9, 13, 10, 18), (20, 12, 22, 17)):
        d.rectangle(box, fill=LAND)
    for point in ((12, 13), (16, 12), (18, 16), (13, 17),
                  (19, 14), (15, 15)):
        d.point(point, fill=LAND_DARK)

    # The player's trail: an almost closed loop, open near the sprite.
    d.rectangle((7, 6, 25, 7), fill=TRAIL)
    d.rectangle((24, 6, 25, 23), fill=TRAIL)
    d.rectangle((8, 22, 25, 23), fill=TRAIL)
    d.rectangle((7, 12, 8, 23), fill=TRAIL)
    d.line(((8, 6), (25, 6)), fill=TRAIL_LIGHT, width=1)
    d.line(((24, 8), (24, 22)), fill=TRAIL_LIGHT, width=1)
    d.point((9, 22), fill=TRAIL_LIGHT)

    # Enemy ball inside the captured area.
    d.rectangle((16, 14, 19, 17), fill=BALL)
    d.point((18, 15), fill=BALL_RED)

    # Player sprite at the start of the line.
    d.rectangle((4, 4, 9, 9), fill=PLAYER_DARK)
    d.rectangle((5, 5, 8, 8), fill=PLAYER)
    d.point((6, 6), fill=(255, 238, 150))

    return img


def write_ico(images: list[tuple[int, Image.Image]], path: Path) -> None:
    """Write a PNG-compressed ICO without PCX resampling (keeps pixels exact)."""
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    directory = bytearray()
    payload = bytearray()

    for size, image in images:
        buffer = io.BytesIO()
        image.save(buffer, format="PNG")
        data = buffer.getvalue()
        dimension = size if size < 256 else 0
        directory += struct.pack(
            "<BBBBHHII", dimension, dimension, 0, 0, 1, 32,
            len(data), offset)
        payload += data
        offset += len(data)

    path.write_bytes(header + directory + payload)


def main() -> None:
    icon = draw_icon()
    small = icon.resize((16, 16), Image.Resampling.NEAREST)

    assets = ROOT / "assets"
    assets.mkdir(exist_ok=True)
    icon.save(assets / "xonix-icon.png")
    write_ico([(16, small), (32, icon)], assets / "xonix.ico")
    print(f"wrote {assets / 'xonix.ico'} and {assets / 'xonix-icon.png'}")


if __name__ == "__main__":
    main()
