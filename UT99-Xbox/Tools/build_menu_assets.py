#!/usr/bin/env python
"""Build loose Xbox menu image assets from repo-root authoring images."""

from __future__ import print_function

import os
import struct
import sys
from PIL import Image


ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
RESAMPLE_LANCZOS = getattr(getattr(Image, "Resampling", Image), "LANCZOS")


BUTTONS_1920 = {
    "button_a.xui":      (397, 205, 457, 265),
    "button_b.xui":      (493, 205, 553, 265),
    "button_x.xui":      (589, 205, 650, 266),
    "button_y.xui":      (685, 205, 746, 266),
    "button_lt.xui":     (608, 293, 685, 369),
    "button_rt.xui":     (704, 293, 779, 369),
    "button_dpad.xui":   (397, 397, 458, 458),
    "button_dup.xui":    (493, 397, 554, 458),
    "button_ddown.xui":  (589, 397, 650, 458),
    "button_dleft.xui":  (685, 397, 746, 458),
    "button_dright.xui": (781, 397, 842, 458),
    "button_rstick.xui": (397, 493, 461, 557),
    "button_lstick.xui": (496, 493, 560, 557),
    "button_lmove.xui":  (604, 493, 649, 557),
    "button_rmove.xui":  (700, 493, 745, 557),
    "button_l.xui":      (397, 593, 462, 655),
    "button_r.xui":      (496, 593, 560, 655),
    "button_start.xui":  (397, 692, 458, 753),
    "button_back.xui":   (493, 692, 554, 753),
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


def fit_controller_facade(src):
    image = Image.open(src).convert("RGBA")
    r, g, b, a = image.split()
    image = Image.merge("RGBA", (b, g, r, a))
    canvas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    image.thumbnail((512, 512), RESAMPLE_LANCZOS)
    canvas.alpha_composite(image, ((512 - image.width) // 2, 0))
    return canvas


def transparent_button_backing(image):
    image = image.convert("RGBA")
    pixels = image.load()
    background = pixels[0, 0]
    visited = set()
    stack = []

    def matches_background(color):
        r, g, b, a = color
        br, bg, bb, ba = background
        if a == 0:
            return True
        return (
            abs(r - br) <= 18 and
            abs(g - bg) <= 18 and
            abs(b - bb) <= 18 and
            abs(a - ba) <= 8
        )

    for x in range(image.width):
        stack.append((x, 0))
        stack.append((x, image.height - 1))
    for y in range(image.height):
        stack.append((0, y))
        stack.append((image.width - 1, y))

    while stack:
        x, y = stack.pop()
        if x < 0 or y < 0 or x >= image.width or y >= image.height:
            continue
        key = (x, y)
        if key in visited:
            continue
        visited.add(key)
        if not matches_background(pixels[x, y]):
            continue
        pixels[x, y] = (0, 0, 0, 0)
        stack.append((x - 1, y))
        stack.append((x + 1, y))
        stack.append((x, y - 1))
        stack.append((x, y + 1))

    return image


def fit_button(crop, size=64):
    crop = transparent_button_backing(crop)
    bbox = crop.getchannel("A").getbbox()
    if bbox:
        crop = crop.crop(bbox)
    crop.thumbnail((size - 4, size - 4), RESAMPLE_LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(crop, ((size - crop.width) // 2, (size - crop.height) // 2))
    return out


def build_round_button(fill, outline, label=None, size=64):
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    try:
        from PIL import ImageDraw, ImageFont
        draw = ImageDraw.Draw(image)
        draw.ellipse((8, 8, size - 8, size - 8), fill=fill, outline=outline, width=3)
        if label:
            font = ImageFont.load_default()
            try:
                bbox = draw.textbbox((0, 0), label, font=font)
                tw = bbox[2] - bbox[0]
                th = bbox[3] - bbox[1]
            except AttributeError:
                tw, th = draw.textsize(label, font=font)
            draw.text(((size - tw) // 2, (size - th) // 2 - 1), label, fill=outline, font=font)
    except Exception:
        pass
    return image


def crop_button(src, box):
    sheet = Image.open(src).convert("RGBA")
    return fit_button(sheet.crop(box))


def button_boxes_for_sheet(src):
    sheet = Image.open(src)
    if sheet.size == (1920, 960):
        return BUTTONS_1920
    raise RuntimeError("Unsupported button sheet size {} for {}".format(sheet.size, src))


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


def find_controller_facade():
    candidates = [
        os.path.join(ROOT, "xbox_controller_s.tga"),
        os.path.join(os.path.dirname(ROOT), "Star-Trek-Elite-Force-X", "base", "menu", "common", "xbox_controller_s.tga"),
        os.path.join(os.path.dirname(ROOT), "Star-Trek-Elite-Force-X", "xbox_controller_s.tga"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    raise RuntimeError("Missing xbox_controller_s.tga")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "UT99-Xbox", "MenuAssets")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    logo = os.path.join(ROOT, "UnrealTournament.webp")
    buttons = find_button_sheet()
    controller = find_controller_facade()
    if not os.path.isfile(logo):
        raise RuntimeError("Missing " + logo)

    write_xui(os.path.join(out_dir, "ut_logo.xui"), fit_logo(logo))
    write_xui(os.path.join(out_dir, "controller_s.xui"), fit_controller_facade(controller))
    button_boxes = button_boxes_for_sheet(buttons)
    for name, box in sorted(button_boxes.items()):
        write_xui(os.path.join(out_dir, name), crop_button(buttons, box))
    write_xui(os.path.join(out_dir, "button_white.xui"), build_round_button((235, 238, 245, 255), (28, 33, 42, 255)))
    write_xui(os.path.join(out_dir, "button_black.xui"), build_round_button((18, 20, 24, 255), (220, 226, 236, 255)))


if __name__ == "__main__":
    main()
