#!/usr/bin/env python
"""Build loose Xbox menu image assets from repo-root authoring images."""

from __future__ import print_function

import os
import struct
import sys
from PIL import Image


ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def write_xui(path, image):
    image = image.convert("RGBA")
    width, height = image.size
    pixels = bytearray()
    for r, g, b, a in image.getdata():
        pixels += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    with open(path, "wb") as f:
        f.write(b"XUI0")
        f.write(struct.pack("<II", width, height))
        f.write(pixels)
    print("Wrote {} {}x{}".format(path, width, height))


def fit_logo(src):
    image = Image.open(src).convert("RGBA")
    canvas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    image.thumbnail((500, 180), Image.LANCZOS)
    canvas.alpha_composite(image, ((512 - image.width) // 2, 16))
    return canvas


def crop_button(src, center_x, center_y):
    sheet = Image.open(src).convert("RGBA")
    crop = sheet.crop((center_x - 32, center_y - 32, center_x + 32, center_y + 32))
    bg = sheet.getpixel((0, 0))[:3]
    out = Image.new("RGBA", crop.size, (0, 0, 0, 0))
    for y in range(crop.height):
        for x in range(crop.width):
            r, g, b, a = crop.getpixel((x, y))
            dist = abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2])
            if dist < 34:
                out.putpixel((x, y), (r, g, b, 0))
            else:
                out.putpixel((x, y), (r, g, b, a))
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "UT99-Xbox", "MenuAssets")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    logo = os.path.join(ROOT, "UnrealTournament.webp")
    buttons = os.path.join(ROOT, "xboxButtons.png")
    if not os.path.isfile(logo):
        raise RuntimeError("Missing " + logo)
    if not os.path.isfile(buttons):
        raise RuntimeError("Missing " + buttons)

    write_xui(os.path.join(out_dir, "ut_logo.xui"), fit_logo(logo))
    write_xui(os.path.join(out_dir, "button_a.xui"), crop_button(buttons, 427, 236))
    write_xui(os.path.join(out_dir, "button_b.xui"), crop_button(buttons, 524, 236))


if __name__ == "__main__":
    main()
