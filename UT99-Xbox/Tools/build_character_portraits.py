#!/usr/bin/env python
"""Build static Xbox player portraits from verified transparent sources."""

from __future__ import print_function

import hashlib
import os
import struct
import sys
from PIL import Image, ImageDraw


ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(ROOT, "UT99-Xbox", "MenuAssets")
SRC_DIR = os.path.join(OUT_DIR, "character_portrait_src")
RESAMPLE_LANCZOS = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
CANVAS_SIZE = (256, 512)
VISIBLE_HEIGHT = 372
MAX_VISIBLE_WIDTH = 252


PORTRAITS = [
    "char_annaka.xui",
    "char_arkon.xui",
    "char_aryss.xui",
    "char_atomiccow.xui",
    "char_baetal.xui",
    "char_berserker.xui",
    "char_blake.xui",
    "char_boris.xui",
    "char_cathode.xui",
    "char_cilia.xui",
    "char_cryss.xui",
    "char_damien.xui",
    "char_disconnect.xui",
    "char_dominator.xui",
    "char_dominator_ps2.xui",
    "char_firewall.xui",
    "char_freylis.xui",
    "char_fury.xui",
    "char_gorn.xui",
    "char_grail.xui",
    "char_graves.xui",
    "char_guardian.xui",
    "char_isis.xui",
    "char_jayce.xui",
    "char_johnson.xui",
    "char_kragoth.xui",
    "char_kregore.xui",
    "char_kyla.xui",
    "char_lauren.xui",
    "char_lilith.xui",
    "char_luthor.xui",
    "char_malcom.xui",
    "char_malise.xui",
    "char_mariana.xui",
    "char_masterchief.xui",
    "char_matrix.xui",
    "char_othello.xui",
    "char_ouboudah.xui",
    "char_pharoh.xui",
    "char_priest.xui",
    "char_ramirez.xui",
    "char_rampage.xui",
    "char_rankin.xui",
    "char_riker.xui",
    "char_sara.xui",
    "char_sarena.xui",
    "char_skaarj_boss.xui",
    "char_skrilax.xui",
    "char_tanya.xui",
    "char_tensor.xui",
    "char_vector.xui",
    "char_visse.xui",
    "char_vixen.xui",
    "char_warcow.xui",
    "char_xan.xui",
    "char_xan_ps2.xui",
]


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def write_xui(path, image):
    image = image.convert("RGBA")
    width, height = image.size
    pixels = image.tobytes("raw", "BGRA")
    with open(path, "wb") as f:
        f.write(b"XUI0")
        f.write(struct.pack("<II", width, height))
        f.write(pixels)
    print("Wrote {} {}x{}".format(path, width, height))


def validate_transparency(name, src):
    alpha = src.getchannel("A")
    if not alpha.getbbox():
        raise RuntimeError("{} has no visible character pixels".format(name))

    width, height = src.size
    edge_alpha = []
    for x in range(width):
        edge_alpha.extend((alpha.getpixel((x, 0)), alpha.getpixel((x, height - 1))))
    for y in range(height):
        edge_alpha.extend((alpha.getpixel((0, y)), alpha.getpixel((width - 1, y))))
    if max(edge_alpha) != 0:
        raise RuntimeError("{} still has an opaque image background".format(name))


def build_portrait(name, src_path):
    src = Image.open(src_path).convert("RGBA")
    validate_transparency(name, src)

    bbox = src.getchannel("A").getbbox()
    src = src.crop(bbox)

    scale = float(VISIBLE_HEIGHT) / float(src.height)
    scaled_width = int(round(src.width * scale))
    if scaled_width > MAX_VISIBLE_WIDTH:
        raise RuntimeError(
            "{} cannot reach the normalized visible height without exceeding width: {}x{}".format(
                name, scaled_width, VISIBLE_HEIGHT
            )
        )
    src = src.resize((scaled_width, VISIBLE_HEIGHT), RESAMPLE_LANCZOS)

    canvas = Image.new("RGBA", CANVAS_SIZE, (0, 0, 0, 0))
    x = (CANVAS_SIZE[0] - src.width) // 2
    y = (CANVAS_SIZE[1] - src.height) // 2
    canvas.alpha_composite(src, (x, y))

    visible = canvas.getchannel("A").getbbox()
    if not visible or visible[3] - visible[1] != VISIBLE_HEIGHT:
        raise RuntimeError("{} failed visible-height normalization: {}".format(name, visible))
    return canvas


def build_missing():
    image = Image.new("RGBA", CANVAS_SIZE, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((98, 48, 206, 156), fill=(90, 110, 130, 160))
    draw.rounded_rectangle((82, 156, 222, 464), radius=22, fill=(80, 96, 116, 130))
    return image


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR
    ensure_dir(out_dir)
    ensure_dir(SRC_DIR)

    if PORTRAITS != sorted(PORTRAITS):
        raise RuntimeError("Portrait manifest is not alphabetized")

    seen_images = {}
    for name in PORTRAITS:
        src_name = os.path.splitext(name)[0] + ".png"
        src_path = os.path.join(SRC_DIR, src_name)
        if not os.path.isfile(src_path):
            raise RuntimeError("Missing local portrait source " + src_path)
        portrait = build_portrait(name, src_path)
        digest = hashlib.sha256(portrait.tobytes()).hexdigest()
        if digest in seen_images:
            raise RuntimeError("{} exactly duplicates {}".format(name, seen_images[digest]))
        seen_images[digest] = name
        write_xui(os.path.join(out_dir, name), portrait)

    write_xui(os.path.join(out_dir, "char_missing.xui"), build_missing())


if __name__ == "__main__":
    main()
