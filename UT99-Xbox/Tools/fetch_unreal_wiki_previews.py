#!/usr/bin/env python
"""Fetch Unreal Wiki map preview images for PS2 map conversion."""

from __future__ import print_function

import argparse
import json
import os
import re
import sys
from pathlib import Path

import requests
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_ps2_seekfree_maps as ps2


API_URL = "https://unreal.fandom.com/api.php"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (UT99 PS2 map conversion preview fetcher)",
}


def api(params):
    response = requests.get(API_URL, params=params, headers=HEADERS, timeout=30)
    response.raise_for_status()
    return response.json()


def imageinfo(title):
    data = api({
        "action": "query",
        "titles": title,
        "prop": "imageinfo",
        "iiprop": "url|size",
        "format": "json",
    })
    page = next(iter(data["query"]["pages"].values()))
    info = page.get("imageinfo")
    return info[0] if info else None


def page_images(title):
    images = []
    cont = {}
    while True:
        params = {
            "action": "query",
            "titles": title,
            "prop": "images|pageimages",
            "format": "json",
            "pithumbsize": 512,
            "imlimit": 500,
        }
        params.update(cont)
        data = api(params)
        page = next(iter(data["query"]["pages"].values()))
        if page.get("missing") is not None:
            return None, [], None
        images.extend(img["title"] for img in page.get("images", []))
        thumbnail = page.get("thumbnail", {}).get("source")
        if "continue" not in data:
            return page, images, thumbnail
        cont = data["continue"]


def score_image(map_name, title):
    low = title.lower()
    name = map_name.lower()
    score = -1000
    if name in low:
        score += 100
    if "-dc" in low or "dreamcast" in low:
        score += 1000
    if "-ps2" in low:
        score += 500
    if "thumbnail" in low or "screen" in low:
        score += 25
    if not low.endswith((".jpg", ".jpeg", ".png")):
        score -= 100
    return score


def choose_image(map_name, images, thumbnail):
    candidates = [
        title for title in images
        if map_name.lower() in title.lower()
        and title.lower().endswith((".jpg", ".jpeg", ".png"))
    ]
    if candidates:
        candidates.sort(key=lambda title: score_image(map_name, title), reverse=True)
        return candidates[0], None
    if thumbnail:
        return None, thumbnail
    return None, None


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def download_image(source_title, source_url, map_name, out_dir):
    if source_url is None:
        info = imageinfo(source_title)
        if not info:
            raise RuntimeError("no imageinfo for %s" % source_title)
        source_url = info["url"]

    response = requests.get(
        source_url,
        headers=dict(HEADERS, Referer="https://unreal.fandom.com/wiki/" + map_name),
        timeout=30)
    response.raise_for_status()

    out_dir.mkdir(parents=True, exist_ok=True)
    source_path = out_dir / (map_name + ".source" + Path(source_url.split("?")[0]).suffix)
    source_path.write_bytes(response.content)

    override_path = out_dir / (map_name + ".png")
    image = Image.open(source_path).convert("RGBA").resize((256, 256), Image.Resampling.LANCZOS)
    image.save(override_path)
    return str(override_path), str(source_path), source_url


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("ConvertedConsoleMaps", "PreviewOverrides"))
    parser.add_argument("--report", default=os.path.join("ConvertedConsoleMaps", "PreviewOverrides", "unreal_wiki_preview_report.json"))
    parser.add_argument("--maps", nargs="*", default=sorted(ps2.PS2_EXCLUSIVE_NAMES))
    args = parser.parse_args()

    out_dir = Path(args.out)
    report = {
        "downloaded": [],
        "missing": [],
        "errors": [],
    }

    for map_name in args.maps:
        try:
            page, images, thumbnail = page_images(map_name)
            if page is None:
                report["missing"].append({"map": map_name, "reason": "wiki page missing"})
                print("%s: missing page" % map_name)
                continue
            title, url = choose_image(map_name, images, thumbnail)
            if title is None and url is None:
                report["missing"].append({"map": map_name, "reason": "no map thumbnail image"})
                print("%s: no thumbnail" % map_name)
                continue
            override_path, source_path, source_url = download_image(title, url, map_name, out_dir)
            report["downloaded"].append({
                "map": map_name,
                "page": page["title"],
                "image_title": title,
                "source_url": source_url,
                "source_path": source_path,
                "override_path": override_path,
            })
            print("%s: %s" % (map_name, title or source_url))
        except Exception as exc:
            report["errors"].append({"map": map_name, "error": str(exc)})
            print("%s: ERROR %s" % (map_name, exc))

    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True))
    print("downloaded=%d missing=%d errors=%d report=%s" % (
        len(report["downloaded"]), len(report["missing"]), len(report["errors"]), report_path))


if __name__ == "__main__":
    main()
