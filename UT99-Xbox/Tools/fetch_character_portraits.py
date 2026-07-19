#!/usr/bin/env python
"""Fetch roster-verified UT99 character portraits from the Unreal Wiki."""

from __future__ import print_function

import argparse
import hashlib
import json
import os
from pathlib import Path

import requests
from PIL import Image


API_URL = "https://unreal.fandom.com/api.php"
PAGE_URL = "https://unreal.fandom.com/wiki/Unreal_Tournament/Character_Cards"
HEADERS = {"User-Agent": "UT99-Xbox character portrait builder"}


WIKI_PORTRAITS = {
    "char_annaka.png": "File:Ut99-thundercrash-annaka-red.png",
    "char_arkon.png": "File:Ut99-rawsteel-arkon-default.png",
    "char_aryss.png": "File:Ut99-thundercrash-aryss-red.png",
    "char_atomiccow.png": "File:Ut99-naliwarcow-atomiccow-default.png",
    "char_blake.png": "File:Ut99-darkphalanx-blake-default.png",
    "char_boris.png": "File:Ut99-bloodreavers-boris-blue.png",
    "char_cathode.png": "File:Ut99-thecorrupt-cathode-default.png",
    "char_cilia.png": "File:Ut99-venom-cilia-gold.png",
    "char_cryss.png": "File:Ut99-blacklegion-cryss-default.png",
    "char_damien.png": "File:Ut99-ps2-damien.png",
    "char_dominator_ps2.png": "File:Ut99-ps2-dominator.png",
    "char_freylis.png": "File:Ut99-blacklegion-freylis-green.png",
    "char_fury.png": "File:Ut99-thecorrupt-fury-default.png",
    "char_gorn.png": "File:Ut99-darkphalanx-gorn-default.png",
    "char_grail.png": "File:Ut99-blacklegion-grail-default.png",
    "char_graves.png": "File:Ut99-bloodreavers-graves-default.png",
    "char_isis.png": "File:Ut99-metalguard-isis-blue.png",
    "char_jayce.png": "File:Ut99-bloodreavers-jayce-gold.png",
    "char_johnson.png": "File:Ut99-ironguard-johnson-default.png",
    "char_kragoth.png": "File:Ut99-blacklegion-kragoth-default.png",
    "char_kregore.png": "File:Ut99-rawsteel-kregore-default.png",
    "char_kyla.png": "File:Ut99-bloodreavers-kyla-gold.png",
    "char_lauren.png": "File:Ut99-ironguard-lauren-green.png",
    "char_lilith.png": "File:Ut99-thecorrupt-lilith-default.png",
    "char_luthor.png": "File:Ut99-bloodreavers-luthor-blue.png",
    "char_malcom.png": "File:Ut99-thundercrash-malcom-default.png",
    "char_malise.png": "File:Ut99-blacklegion-malise-blue.png",
    "char_mariana.png": "File:Ut99-bloodreavers-mariana-red.png",
    "char_matrix.png": "File:Ut99-thecorrupt-matrix-blue.png",
    "char_othello.png": "File:Ut99-thundercrash-othello-default.png",
    "char_ouboudah.png": "File:Ut99-nali-ouboudah.png",
    "char_priest.png": "File:Ut99-nali-priest.png",
    "char_ramirez.png": "File:Ut99-bloodreavers-ramirez-default.png",
    "char_rampage.png": "File:Ut99-ps2-rampage.png",
    "char_rankin.png": "File:Ut99-ironguard-rankin-default.png",
    "char_riker.png": "File:Ut99-thundercrash-riker-default.png",
    "char_sara.png": "File:Ut99-ironguard-sara-green.png",
    "char_sarena.png": "File:Ut99-venom-sarena-gold.png",
    "char_tanya.png": "File:Ut99-bloodreavers-tanya-red.png",
    "char_tensor.png": "File:Ut99-thecorrupt-tensor-blue.png",
    "char_vector.png": "File:Ut99-thecorrupt-vector-default.png",
    "char_visse.png": "File:Ut99-blacklegion-visse-green.png",
    "char_vixen.png": "File:Ut99-metalguard-vixen-default.png",
    "char_warcow.png": "File:Ut99-naliwarcow-warcow-default.png",
    "char_xan.png": "File:Ut99-boss-xan-default.png",
    "char_xan_ps2.png": "File:Ut99-ps2-xan.png",
}


LOCAL_RENDER_PORTRAITS = [
    "char_baetal.png",
    "char_berserker.png",
    "char_disconnect.png",
    "char_dominator.png",
    "char_firewall.png",
    "char_guardian.png",
    "char_masterchief.png",
    "char_pharoh.png",
    "char_skaarj_boss.png",
    "char_skrilax.png",
]


LOCAL_RENDER_METADATA = {
    "char_baetal.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.PitF", "face": "TSkMSkins.Baetal", "method": "paired-black-white-cxbx-render"},
    "char_berserker.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.Warr", "face": "TSkMSkins.Berserker", "method": "paired-black-white-cxbx-render"},
    "char_disconnect.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.MekS", "face": "TSkMSkins.Disconnect", "method": "paired-black-white-cxbx-render"},
    "char_dominator.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.Warr", "face": "TSkMSkins.Dominator", "method": "paired-black-white-cxbx-render"},
    "char_firewall.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.MekS", "face": "TSkMSkins.Firewall", "method": "paired-black-white-cxbx-render"},
    "char_guardian.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.Warr", "face": "TSkMSkins.Guardian", "method": "paired-black-white-cxbx-render"},
    "char_masterchief.png": {
        "class": "HaloMasterChief.HaloMasterChief",
        "skin": "HaloMasterChiefSkins.chef",
        "face": "HaloMasterChiefSkins.chef2Face",
        "method": "exact-local-package-mesh-render",
        "mesh": "HaloMasterChief.HaloMasterChief",
        "texture": "HaloMasterChiefSkins.chef1T_2",
    },
    "char_pharoh.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.PitF", "face": "TSkMSkins.Pharoh", "method": "paired-black-white-cxbx-render"},
    "char_skaarj_boss.png": {
        "class": "UTPS2Characters.SkaarjBossPS2",
        "skin": "SkaarjBPS2Skins.Warr",
        "face": "SkaarjBPS2Skins.Superfly",
        "method": "exact-local-package-mesh-render",
        "mesh": "UTPS2Characters.SkaarjBPS2",
        "resolved_textures": [
            "UTPS2CharactersSkins.skbpSkins.bWarr1Superfly",
            "UTPS2CharactersSkins.skbpSkins.Warr2Superfly",
            "TSkMSkins.Warr3",
            "TSkMSkins.Warr4",
        ],
    },
    "char_skrilax.png": {"class": "MultiMesh.TSkaarj", "skin": "TSkMSkins.PitF", "face": "TSkMSkins.Skrilax", "method": "paired-black-white-cxbx-render"},
}


def image_info(title):
    response = requests.get(
        API_URL,
        params={
            "action": "query",
            "titles": title,
            "prop": "imageinfo",
            "iiprop": "url|size|sha1",
            "format": "json",
        },
        headers=HEADERS,
        timeout=30,
    )
    response.raise_for_status()
    page = next(iter(response.json()["query"]["pages"].values()))
    if not page.get("imageinfo"):
        raise RuntimeError("No image metadata for " + title)
    return page["imageinfo"][0]


def validate_portrait(path):
    image = Image.open(path).convert("RGBA")
    if image.size != (360, 690):
        raise RuntimeError("Unexpected portrait dimensions for {}: {}".format(path, image.size))
    alpha = image.getchannel("A")
    if not alpha.getbbox():
        raise RuntimeError("Portrait is fully transparent: " + str(path))
    edges = []
    for x in range(image.width):
        edges.extend((alpha.getpixel((x, 0)), alpha.getpixel((x, image.height - 1))))
    for y in range(image.height):
        edges.extend((alpha.getpixel((0, y)), alpha.getpixel((image.width - 1, y))))
    if max(edges) != 0:
        raise RuntimeError("Portrait background reaches an image edge: " + str(path))
    return image.getchannel("A").getbbox()


def validate_local_render(path):
    image = Image.open(path).convert("RGBA")
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if not bbox:
        raise RuntimeError("Portrait is fully transparent: " + str(path))
    edges = []
    for x in range(image.width):
        edges.extend((alpha.getpixel((x, 0)), alpha.getpixel((x, image.height - 1))))
    for y in range(image.height):
        edges.extend((alpha.getpixel((0, y)), alpha.getpixel((image.width - 1, y))))
    if max(edges) != 0:
        raise RuntimeError("Portrait background reaches an image edge: " + str(path))
    return bbox


def main():
    root = Path(__file__).resolve().parents[1]
    default_out = root / "MenuAssets" / "character_portrait_src"
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=str(default_out))
    parser.add_argument("--report", default="")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for filename in WIKI_PORTRAITS:
            path = out_dir / filename
            if path.exists():
                path.unlink()

    report = {"source_page": PAGE_URL, "downloaded": [], "local_renders": [], "local_render_required": LOCAL_RENDER_PORTRAITS}
    for filename, title in sorted(WIKI_PORTRAITS.items()):
        info = image_info(title)
        response = requests.get(
            info["url"],
            headers=dict(HEADERS, Referer=PAGE_URL),
            timeout=30,
        )
        response.raise_for_status()
        path = out_dir / filename
        path.write_bytes(response.content)
        bbox = validate_portrait(path)
        report["downloaded"].append({
            "portrait": filename,
            "wiki_title": title,
            "source_url": info["url"],
            "source_sha1": info.get("sha1", ""),
            "visible_bbox": list(bbox),
        })
        print("{} <- {}".format(filename, title))

    for filename in LOCAL_RENDER_PORTRAITS:
        path = out_dir / filename
        if not path.is_file():
            continue
        entry = dict(LOCAL_RENDER_METADATA[filename])
        entry["portrait"] = filename
        entry["source_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        entry["visible_bbox"] = list(validate_local_render(path))
        report["local_renders"].append(entry)

    report_path = Path(args.report) if args.report else out_dir / "portrait_sources.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + os.linesep)
    print("downloaded={} local_render_required={} report={}".format(
        len(report["downloaded"]), len(LOCAL_RENDER_PORTRAITS), report_path
    ))


if __name__ == "__main__":
    main()
