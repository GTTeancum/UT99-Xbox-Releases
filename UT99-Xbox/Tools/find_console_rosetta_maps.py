#!/usr/bin/env python
"""
Find valid PC maps that have PS2 console counterparts.

These pairs are the useful reverse-engineering corpus: the PC package is known
loadable, while the console package has the same map identity but a cooked table
layout.  The output manifest records paths, offsets, sizes, package summary
fields, and SHA1s for repeatable analysis.
"""

from __future__ import print_function

import argparse
import hashlib
import json
import os
import struct
from collections import OrderedDict

import convert_console_maps as ccm


PACKAGE_MAGIC = b"\xC1\x83\x2A\x9E"


def sha1_bytes(data):
    h = hashlib.sha1()
    h.update(data)
    return h.hexdigest()


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def package_summary(data, offset=0):
    if data[offset:offset + 4] != PACKAGE_MAGIC:
        return None
    fields = struct.unpack_from("<IIIIIIIII", data, offset)
    return OrderedDict([
        ("version", fields[1]),
        ("flags", fields[2]),
        ("name_count", fields[3]),
        ("name_offset", fields[4]),
        ("export_count", fields[5]),
        ("export_offset", fields[6]),
        ("import_count", fields[7]),
        ("import_offset", fields[8]),
    ])


def normalize_key(name):
    return name.lower()


def pc_maps_by_key(pc_maps_dir):
    maps = OrderedDict()
    for filename in sorted(os.listdir(pc_maps_dir)):
        if not filename.lower().endswith(".unr"):
            continue
        path = os.path.join(pc_maps_dir, filename)
        with open(path, "rb") as f:
            data = f.read()
        valid, reason = ccm.validate_pc_package(data)
        if not valid:
            continue
        maps[normalize_key(os.path.splitext(filename)[0])] = OrderedDict([
            ("name", os.path.splitext(filename)[0]),
            ("path", path),
            ("bytes", len(data)),
            ("sha1", sha1_bytes(data)),
            ("summary", package_summary(data)),
        ])
    return maps


def ps2_maps(ps2_dir):
    umd_path = os.path.join(ps2_dir, "PSX2LINS.UMD")
    data, entries = ccm.discover_ps2_entries(umd_path)
    rows = []
    for i, entry in enumerate(entries):
        name = entry["map_name"]
        if not re.match(r"^(AS|CTF|DM|DOM)-", name, re.I):
            continue
        if i + 1 < len(entries):
            next_entry = entries[i + 1]
            end = next_entry["entry_start"]
        else:
            end = len(data)
        payload = data[entry["package_start"]:end]
        rows.append(OrderedDict([
            ("platform", "PS2"),
            ("name", name),
            ("key", normalize_key(name)),
            ("path", umd_path),
            ("url", entry["url"]),
            ("entry_start", entry["entry_start"]),
            ("package_offset", entry["package_start"]),
            ("package_end", end),
            ("bytes", len(payload)),
            ("sha1", sha1_bytes(payload)),
            ("summary", package_summary(data, entry["package_start"])),
        ]))
    return rows


def pair_console_with_pc(console_rows, pc_rows):
    pairs = []
    seen = set()
    for row in console_rows:
        key = row["key"]
        if key not in pc_rows:
            continue
        if (row["platform"], key) in seen:
            continue
        seen.add((row["platform"], key))
        pairs.append(OrderedDict([
            ("name", row["name"]),
            ("platform", row["platform"]),
            ("pc", pc_rows[key]),
            ("console", row),
        ]))
    return pairs


def main():
    parser = argparse.ArgumentParser(description="Find PC/console Rosetta map pairs.")
    parser.add_argument("--root", default=os.getcwd())
    parser.add_argument("--pc-maps", required=True, help="Directory containing known PC-loadable .unr maps.")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    pc_dir = os.path.abspath(args.pc_maps)
    out_path = os.path.abspath(args.out or os.path.join(root, "ConvertedConsoleMaps", "rosetta_pairs.json"))

    pc_rows = pc_maps_by_key(pc_dir)
    ps2_rows = ps2_maps(os.path.join(root, "!PS2"))
    ps2_pairs = pair_console_with_pc(ps2_rows, pc_rows)

    manifest = OrderedDict([
        ("pc_maps_dir", pc_dir),
        ("valid_pc_map_count", len(pc_rows)),
        ("ps2_pair_count", len(ps2_pairs)),
        ("ps2_pairs", ps2_pairs),
    ])

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    with open(out_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print("Valid PC maps: %d" % len(pc_rows))
    print("PS2 Rosetta pairs: %d" % len(ps2_pairs))
    print("Manifest: " + out_path)


if __name__ == "__main__":
    main()
