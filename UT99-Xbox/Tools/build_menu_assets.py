#!/usr/bin/env python
"""Build loose Xbox menu image assets from repo-root authoring images."""

from __future__ import print_function

import os
import struct
import sys
from PIL import Image


ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
RESAMPLE_LANCZOS = getattr(getattr(Image, "Resampling", Image), "LANCZOS")


DUKE_BUTTONS = {
    "button_a.xui":      (56, 40, 184, 200),
    "button_b.xui":      (246, 40, 374, 200),
    "button_x.xui":      (436, 40, 564, 200),
    "button_y.xui":      (626, 40, 754, 200),
    "button_white.xui":  (828, 68, 932, 172),
    "button_black.xui":  (998, 68, 1100, 163),
    "button_start.xui":  (1176, 92, 1264, 148),
    "button_back.xui":   (1196, 332, 1284, 388),
    "button_lstick.xui": (48, 288, 192, 431),
    "button_lmove.xui":  (238, 288, 382, 431),
    "button_l.xui":      (428, 288, 572, 431),
    "button_rstick.xui": (618, 288, 762, 431),
    "button_rmove.xui":  (808, 288, 952, 431),
    "button_r.xui":      (998, 288, 1142, 431),
    "button_dpad.xui":   (36, 576, 204, 744),
    "button_dup.xui":    (226, 576, 394, 744),
    "button_ddown.xui":  (416, 576, 584, 744),
    "button_dleft.xui":  (606, 576, 774, 744),
    "button_dright.xui": (796, 576, 964, 744),
    "button_lt.xui":     (1034, 616, 1146, 704),
    "button_rt.xui":     (1184, 616, 1296, 704),
}


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
    image.thumbnail((500, 180), RESAMPLE_LANCZOS)
    canvas.alpha_composite(image, ((512 - image.width) // 2, 16))
    return canvas


def transparent_black(image):
    image = image.convert("RGBA")
    out = Image.new("RGBA", image.size, (0, 0, 0, 0))
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            if a > 0 and (r + g + b) > 28:
                out.putpixel((x, y), (r, g, b, a))
    return out


def fit_button(crop, size=64):
    crop = transparent_black(crop)
    bbox = crop.getchannel("A").getbbox()
    if bbox:
        crop = crop.crop(bbox)
    crop.thumbnail((size - 4, size - 4), RESAMPLE_LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(crop, ((size - crop.width) // 2, (size - crop.height) // 2))
    return out


def crop_button(src, box):
    sheet = Image.open(src).convert("RGBA")
    return fit_button(sheet.crop(box))


def find_button_sheet():
    candidates = [
        os.path.join(ROOT, "xbox_duke_buttons.png"),
        os.path.join(os.path.expanduser("~"), "Downloads", "xbox_duke_buttons.png"),
        os.path.join(ROOT, "xboxButtons.png"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    raise RuntimeError("Missing xbox_duke_buttons.png or xboxButtons.png")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "UT99-Xbox", "MenuAssets")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    logo = os.path.join(ROOT, "UnrealTournament.webp")
    buttons = find_button_sheet()
    if not os.path.isfile(logo):
        raise RuntimeError("Missing " + logo)

    write_xui(os.path.join(out_dir, "ut_logo.xui"), fit_logo(logo))
    for name, box in sorted(DUKE_BUTTONS.items()):
        write_xui(os.path.join(out_dir, name), crop_button(buttons, box))


if __name__ == "__main__":
    main()
