#!/usr/bin/env python
"""Recover transparent character sources from paired black/white game captures."""

from __future__ import print_function

import argparse
import os
import re

from PIL import Image


CAPTURE_RE = re.compile(r"-(\d+)-(char_[a-z0-9_]+)-(black|white)\.png$", re.IGNORECASE)


def largest_component_bbox(mask):
    width, height = mask.size
    data = mask.load()
    visited = bytearray(width * height)
    best_count = 0
    best_bbox = None

    for y in range(height):
        for x in range(width):
            offset = y * width + x
            if visited[offset] or not data[x, y]:
                continue

            stack = [(x, y)]
            visited[offset] = 1
            count = 0
            min_x = max_x = x
            min_y = max_y = y
            while stack:
                px, py = stack.pop()
                count += 1
                min_x = min(min_x, px)
                max_x = max(max_x, px)
                min_y = min(min_y, py)
                max_y = max(max_y, py)
                for nx, ny in ((px - 1, py), (px + 1, py), (px, py - 1), (px, py + 1)):
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    neighbor = ny * width + nx
                    if visited[neighbor] or not data[nx, ny]:
                        continue
                    visited[neighbor] = 1
                    stack.append((nx, ny))

            if count > best_count:
                best_count = count
                best_bbox = (min_x, min_y, max_x + 1, max_y + 1)

    return best_bbox, best_count


def find_white_panel(image):
    rgb = image.convert("RGB")
    mask = Image.new("1", rgb.size, 0)
    source = rgb.load()
    target = mask.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            r, g, b = source[x, y]
            if r >= 252 and g >= 252 and b >= 252:
                target[x, y] = 1

    bbox, count = largest_component_bbox(mask)
    if not bbox or count < 10000:
        raise RuntimeError("Could not locate the white model-capture panel")
    return bbox


def recover_foreground(black, white):
    if black.size != white.size:
        raise RuntimeError("Black and white captures have different dimensions")

    out = Image.new("RGBA", black.size, (0, 0, 0, 0))
    black_pixels = black.convert("RGB").load()
    white_pixels = white.convert("RGB").load()
    out_pixels = out.load()

    for y in range(out.height):
        for x in range(out.width):
            br, bg, bb = black_pixels[x, y]
            wr, wg, wb = white_pixels[x, y]
            differences = sorted((max(0, wr - br), max(0, wg - bg), max(0, wb - bb)))
            background = differences[1]
            alpha = max(0, min(255, 255 - background))
            if alpha <= 2:
                continue

            r = max(0, min(255, int(round(br * 255.0 / alpha))))
            g = max(0, min(255, int(round(bg * 255.0 / alpha))))
            b = max(0, min(255, int(round(bb * 255.0 / alpha))))
            out_pixels[x, y] = (r, g, b, alpha)
    return out


def collect_pairs(capture_dir):
    pairs = {}
    for name in os.listdir(capture_dir):
        match = CAPTURE_RE.search(name)
        if not match:
            continue
        index = int(match.group(1))
        portrait = match.group(2).lower()
        phase = match.group(3).lower()
        pairs.setdefault((index, portrait), {})[phase] = os.path.join(capture_dir, name)
    return pairs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture_dir")
    parser.add_argument("output_dir")
    parser.add_argument("--expected", type=int, default=56)
    parser.add_argument("--report", default=None)
    args = parser.parse_args()

    pairs = collect_pairs(args.capture_dir)
    if len(pairs) != args.expected:
        raise RuntimeError("Expected {} character pairs, found {}".format(args.expected, len(pairs)))

    if not os.path.isdir(args.output_dir):
        os.makedirs(args.output_dir)
    report_lines = ["index\tportrait\tvisible_width\tvisible_height\tleft\ttop\tright\tbottom"]

    for (index, portrait), phases in sorted(pairs.items()):
        if "black" not in phases or "white" not in phases:
            raise RuntimeError("{} is missing a black or white capture".format(portrait))

        black_full = Image.open(phases["black"]).convert("RGB")
        white_full = Image.open(phases["white"]).convert("RGB")
        panel = find_white_panel(white_full)
        black = black_full.crop(panel)
        white = white_full.crop(panel)
        character = recover_foreground(black, white)
        visible = character.getchannel("A").getbbox()
        if not visible:
            raise RuntimeError("{} produced an empty alpha mask".format(portrait))

        left, top, right, bottom = visible
        if left <= 1 or top <= 1 or right >= character.width - 1 or bottom >= character.height - 1:
            raise RuntimeError("{} is clipped at panel edge: {} in {}".format(portrait, visible, character.size))

        cropped = character.crop(visible)
        padded = Image.new("RGBA", (cropped.width + 4, cropped.height + 4), (0, 0, 0, 0))
        padded.alpha_composite(cropped, (2, 2))
        output = os.path.join(args.output_dir, portrait + ".png")
        padded.save(output)
        report_lines.append("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}".format(
            index, portrait, right - left, bottom - top, left, top, right, bottom
        ))
        print("Wrote {} from {} / {}".format(output, phases["black"], phases["white"]))

    report_path = args.report or os.path.join(args.capture_dir, "portrait-extraction.tsv")
    with open(report_path, "w") as report:
        report.write("\n".join(report_lines) + "\n")
    print("Report " + report_path)


if __name__ == "__main__":
    main()
