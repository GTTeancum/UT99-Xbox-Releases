#!/usr/bin/env python
"""Build static Xbox player setup portraits from approved character card art."""

from __future__ import print_function

import os
import struct
import sys
import urllib.request

from PIL import Image, ImageDraw


ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(ROOT, "UT99-Xbox", "MenuAssets")
SRC_DIR = os.path.join(OUT_DIR, "character_portrait_src")
RESAMPLE_LANCZOS = getattr(getattr(Image, "Resampling", Image), "LANCZOS")


def fandom(path, cb):
    return "https://static.wikia.nocookie.net/unreal/images/{}/revision/latest/scale-to-width-down/200?cb={}&path-prefix=en".format(path, cb)


PORTRAITS = [
    ("char_archon.xui", fandom("d/d0/Ut99-darkphalanx-blake-default.png", "20251213133415")),
    ("char_aryss.xui", fandom("8/8e/Ut99-thundercrash-aryss-red.png", "20251213175130")),
    ("char_alarik.xui", fandom("e/e2/Ut99-thundercrash-malcom-default.png", "20251213184647")),
    ("char_dessloch.xui", fandom("f/f3/Ut99-bloodreavers-luthor-blue.png", "20251212212752")),
    ("char_cryss.xui", fandom("0/0f/Ut99-blacklegion-cryss-default.png", "20251213162015")),
    ("char_nikita.xui", fandom("8/89/Ut99-blacklegion-visse-green.png", "20251213162249")),
    ("char_drimacus.xui", fandom("0/04/Ut99-rawsteel-kregore-default.png", "20251213184647")),
    ("char_rhea.xui", fandom("2/23/Ut99-venom-cilia-gold.png", "20251213175132")),
    ("char_raynor.xui", fandom("7/77/Ut99-blacklegion-kragoth-default.png", "20251213140222")),
    ("char_kira.xui", fandom("6/6c/Ut99-bloodreavers-tanya-red.png", "20251213161948")),
    ("char_karag.xui", fandom("2/28/Ut99-ironguard-johnson-default.png", "20251213184647")),
    ("char_zenith.xui", fandom("e/e0/Ut99-bloodreavers-boris-blue.png", "20251213135850")),
    ("char_cali.xui", fandom("3/32/Ut99-metalguard-vixen-default.png", "20251213173319")),
    ("char_alys.xui", fandom("f/f9/Ut99-ironguard-sara-green.png", "20251213175130")),
    ("char_kosak.xui", fandom("9/96/Ut99-thundercrash-othello-default.png", "20251213184647")),
    ("char_illana.xui", fandom("d/d1/Ut99-bloodreavers-kyla-gold.png", "20251213161551")),
    ("char_barak.xui", fandom("0/08/Ut99-darkphalanx-gorn-default.png", "20251213133504")),
    ("char_kara.xui", fandom("6/66/Ut99-thundercrash-annaka-red.png", "20251213175130")),
    ("char_tamerlane.xui", fandom("0/0a/Ut99-thundercrash-riker-default.png", "20251213184644")),
    ("char_arachne.xui", fandom("5/58/Ut99-blacklegion-malise-blue.png", "20251213162132")),
    ("char_liche.xui", fandom("6/63/Ut99-bloodreavers-ramirez-default.png", "20251213140125")),
    ("char_jared.xui", fandom("a/a8/Ut99-blacklegion-freylis-green.png", "20251212214606")),
    ("char_ichthys.xui", fandom("e/ef/Ut99-rawsteel-arkon-default.png", "20251213184646")),
    ("char_tamara.xui", fandom("9/92/Ut99-venom-sarena-gold.png", "20251213175132")),
    ("char_loque.xui", fandom("c/c2/Ut99-blacklegion-grail-default.png", "20251213140159")),
    ("char_athena.xui", fandom("c/c9/Ut99-bloodreavers-mariana-red.png", "20251213161645")),
    ("char_cilia.xui", fandom("3/35/Ut99-ironguard-rankin-default.png", "20251213184647")),
    ("char_sarena.xui", fandom("7/77/Ut99-metalguard-isis-blue.png", "20251213175132")),
    ("char_malakai.xui", fandom("8/85/Ut99-bloodreavers-graves-default.png", "20251213135934")),
    ("char_visse.xui", fandom("5/5d/Ut99-ironguard-lauren-green.png", "20251213175126")),
    ("char_necroth.xui", fandom("e/e2/Ut99-thundercrash-malcom-default.png", "20251213184647")),
    ("char_kragoth.xui", fandom("9/99/Ut99-bloodreavers-jayce-gold.png", "20251212215231")),
    ("char_ouboudah.xui", fandom("8/8e/Ut99-nali-ouboudah.png", "20251213143601")),
    ("char_priest.xui", fandom("6/6f/Ut99-nali-priest.png", "20251213143622")),
    ("char_atomiccow.xui", fandom("d/de/Ut99-naliwarcow-atomiccow-default.png", "20251213142846")),
    ("char_warcow.xui", fandom("6/66/Ut99-naliwarcow-warcow-default.png", "20251213142916")),
    ("char_cathode.xui", fandom("f/f5/Ut99-thecorrupt-cathode-default.png", "20251213173322")),
    ("char_divisor.xui", fandom("7/79/Ut99-thecorrupt-fury-default.png", "20251213173322")),
    ("char_matrix.xui", fandom("4/4c/Ut99-thecorrupt-matrix-blue.png", "20251213185951")),
    ("char_silicon.xui", fandom("7/71/Ut99-thecorrupt-lilith-default.png", "20251213173322")),
    ("char_vector.xui", fandom("0/03/Ut99-thecorrupt-vector-default.png", "20251213184646")),
    ("char_tensor.xui", fandom("e/e9/Ut99-thecorrupt-tensor-blue.png", "20251213185949")),
    ("char_xan.xui", fandom("a/a3/Ut99-boss-xan-default.png", "20251213163230")),
    ("char_skaarj_berserker.xui", fandom("b/b1/Ut99-skaarjhybrid-berserker-default.png", "20251213144644")),
    ("char_skaarj_dominator.xui", fandom("e/e3/Ut99-skaarjhybrid-dominator-default.png", "20251213144711")),
    ("char_skaarj_guardian.xui", fandom("e/ee/Ut99-skaarjhybrid-guardian-default.png", "20251213144736")),
    ("char_skaarj_baetal.xui", fandom("9/93/Ut99-skaarjhybrid-baetal-default.png", "20251213152440")),
    ("char_skaarj_pharoh.xui", fandom("5/5b/Ut99-skaarjhybrid-pharoh-default.png", "20251213152507")),
    ("char_skaarj_skrilax.xui", fandom("8/8a/Ut99-skaarjhybrid-skrilax-default.png", "20251213152538")),
    ("char_skaarj_firewall.xui", fandom("5/55/UT99PS2-Firewall.png", "20251213144223")),
    ("char_skaarj_disconnect.xui", fandom("a/ad/Ut99-skaarjhybrid-disconnect-default.png", "20251213153214")),
    ("char_damien.xui", fandom("8/88/Ut99-ps2-damien.png", "20251213143023")),
    ("char_rampage.xui", fandom("9/92/Ut99-ps2-rampage.png", "20251213143023")),
    ("char_dominator.xui", fandom("6/62/Ut99-ps2-dominator.png", "20251213143021")),
    ("char_ps2_xan.xui", fandom("5/53/Ut99-ps2-xan.png", "20251213143023")),
    ("char_skaarj_boss.xui", fandom("9/9f/UT99-PS2-Dominator.jpg", "20191127040418")),
]


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def download(url, path):
    if os.path.isfile(path) and os.path.getsize(path) > 0:
        return
    req = urllib.request.Request(url, headers={"User-Agent": "UT99-Xbox-portrait-builder/1.0"})
    with urllib.request.urlopen(req, timeout=30) as response:
        data = response.read()
    with open(path, "wb") as f:
        f.write(data)


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


def build_portrait(src_path):
    src = Image.open(src_path).convert("RGBA")
    bbox = src.getchannel("A").getbbox()
    if bbox:
        src = src.crop(bbox)

    canvas = Image.new("RGBA", (256, 512), (0, 0, 0, 0))
    src.thumbnail((232, 492), RESAMPLE_LANCZOS)
    x = (256 - src.width) // 2
    y = (512 - src.height) // 2
    canvas.alpha_composite(src, (x, y))
    return canvas


def build_missing():
    image = Image.new("RGBA", (256, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((74, 42, 182, 150), fill=(90, 110, 130, 160))
    draw.rounded_rectangle((58, 150, 198, 444), radius=22, fill=(80, 96, 116, 130))
    return image


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR
    ensure_dir(out_dir)
    ensure_dir(SRC_DIR)

    for name, url in PORTRAITS:
        src_name = os.path.splitext(name)[0] + ".png"
        src_path = os.path.join(SRC_DIR, src_name)
        download(url, src_path)
        write_xui(os.path.join(out_dir, name), build_portrait(src_path))

    write_xui(os.path.join(out_dir, "char_missing.xui"), build_missing())


if __name__ == "__main__":
    main()
