#!/usr/bin/env python
"""
Convert Unreal Tournament PS2 console map packages into PC-style .unr files.

The PS2 disc stores maps as Unreal package files concatenated inside
PSX2LINS.UMD, with each top-level entry encoded as:

    <4 byte entry tag><ascii map URL NUL><C1 83 2A 9E package magic>...

This tool strips the PS2 entry tag/URL, copies package bytes through the start
of the next entry, and writes the result as a .unr.
"""

from __future__ import print_function

import argparse
import json
import os
import re
import struct
from collections import OrderedDict


PACKAGE_MAGIC = b"\xC1\x83\x2A\x9E"


# Sources used for the default "exclusive" set:
# - Unreal Archive / OriginalPS2maps: confirms the known PS2 conversion set.
PS2_EXCLUSIVE_NAMES = set([
    "CTF-Phalanx",
    "CTF-Sepulchre",
    "CTF-Spirito",
    "CTF-Stormfront",
    "CTF-Sundial",
    "DM-Brickyard",
    "DM-CanyonFear",
    "DM-Coagulate",
    "DM-Core",
    "DM-Flux",
    "DM-Hood",
    "DM-Loathing",
    "DM-Sorayama",
    "DOM-Osiris",
])


def mkdir_p(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def package_version(data):
    if len(data) < 8 or data[:4] != PACKAGE_MAGIC:
        return None
    return struct.unpack_from("<I", data, 4)[0]


def read_compact_index(data, pos):
    b0 = data[pos]
    pos += 1
    value = 0
    if b0 & 0x40:
        b1 = data[pos]
        pos += 1
        if b1 & 0x80:
            b2 = data[pos]
            pos += 1
            if b2 & 0x80:
                b3 = data[pos]
                pos += 1
                if b3 & 0x80:
                    b4 = data[pos]
                    pos += 1
                    value = b4
                value = (value << 7) + (b3 & 0x7f)
            value = (value << 7) + (b2 & 0x7f)
        value = (value << 7) + (b1 & 0x7f)
    value = (value << 6) + (b0 & 0x3f)
    if b0 & 0x80:
        value = -value
    return value, pos


def validate_pc_package(data):
    """Return (ok, reason) for a standard PC UE1 package table layout."""
    try:
        if len(data) < 36 or data[:4] != PACKAGE_MAGIC:
            return False, "bad package magic"
        _, _, _, name_count, name_offset, export_count, export_offset, import_count, import_offset = struct.unpack_from("<IIIIIIIII", data, 0)
        if not (0 < name_count < 100000 and 0 <= export_count < 100000 and 0 <= import_count < 100000):
            return False, "unreasonable table counts"
        for offset in (name_offset, export_offset, import_offset):
            if offset >= len(data):
                return False, "table offset outside file"

        pos = name_offset
        for _ in range(name_count):
            length, pos = read_compact_index(data, pos)
            if length <= 0 or length > 256 or pos + length + 4 > len(data):
                return False, "bad name table entry"
            pos += length + 4

        pos = import_offset
        for _ in range(import_count):
            class_package, pos = read_compact_index(data, pos)
            class_name, pos = read_compact_index(data, pos)
            if not (0 <= class_package < name_count and 0 <= class_name < name_count):
                return False, "bad import name index"
            if pos + 4 > len(data):
                return False, "truncated import table"
            pos += 4
            object_name, pos = read_compact_index(data, pos)
            if not (0 <= object_name < name_count):
                return False, "bad import object name index"

        pos = export_offset
        for _ in range(export_count):
            _, pos = read_compact_index(data, pos)
            _, pos = read_compact_index(data, pos)
            if pos + 4 > len(data):
                return False, "truncated export table"
            pos += 4
            object_name, pos = read_compact_index(data, pos)
            if not (0 <= object_name < name_count):
                return False, "bad export object name index"
            if pos + 4 > len(data):
                return False, "truncated export flags"
            pos += 4
            serial_size, pos = read_compact_index(data, pos)
            if serial_size < 0 or serial_size > len(data):
                return False, "bad export serial size"
            if serial_size:
                serial_offset, pos = read_compact_index(data, pos)
                if serial_offset < 0 or serial_offset + serial_size > len(data):
                    return False, "bad export serial range"
        return True, "ok"
    except Exception as exc:
        return False, str(exc)


def sha1_file(path):
    import hashlib
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def discover_ps2_entries(umd_path):
    with open(umd_path, "rb") as f:
        data = f.read()

    entries = []
    pos = 0
    url_re = re.compile(rb"([A-Za-z0-9_\-]+(?:\?match=[A-Za-z0-9_]+)?)\x00$")
    while True:
        magic = data.find(PACKAGE_MAGIC, pos)
        if magic < 0:
            break
        prefix = data[max(0, magic - 96):magic]
        match = url_re.search(prefix)
        if match and magic >= len(match.group(1)) + 5:
            url = match.group(1).decode("ascii")
            entry_start = magic - len(match.group(1)) - 1 - 4
            if "?" in url:
                map_name = url.split("?", 1)[0]
            else:
                map_name = url
            entries.append({
                "entry_start": entry_start,
                "package_start": magic,
                "url": url,
                "map_name": map_name,
            })
        pos = magic + 1

    for i, entry in enumerate(entries):
        if i + 1 < len(entries):
            entry["package_end"] = entries[i + 1]["entry_start"]
        else:
            entry["package_end"] = len(data)
    return data, entries


def write_map(out_dir, platform, name, payload, source, url=None, dry_run=False, validate=True):
    if validate:
        valid, reason = validate_pc_package(payload)
        if not valid:
            print("warning: skipping %s %s: not PC-loadable (%s)" % (platform, name, reason))
            return None
    platform_dir = os.path.join(out_dir, platform)
    mkdir_p(platform_dir)
    out_path = os.path.join(platform_dir, name + ".unr")
    if not dry_run:
        with open(out_path, "wb") as f:
            f.write(payload)
    return OrderedDict([
        ("platform", platform),
        ("name", name),
        ("output", out_path),
        ("source", source),
        ("url", url),
        ("bytes", len(payload)),
        ("package_version", package_version(payload)),
        ("sha1", None if dry_run else sha1_file(out_path)),
    ])


def convert_ps2(ps2_dir, out_dir, exclusive_only=True, dry_run=False):
    umd_path = os.path.join(ps2_dir, "PSX2LINS.UMD")
    if not os.path.isfile(umd_path):
        raise RuntimeError("Missing PS2 archive: " + umd_path)
    data, entries = discover_ps2_entries(umd_path)
    results = []
    for entry in entries:
        name = entry["map_name"]
        if not re.match(r"^(AS|CTF|DM|DOM)-", name, re.I):
            continue
        if exclusive_only and name not in PS2_EXCLUSIVE_NAMES:
            continue
        payload = data[entry["package_start"]:entry["package_end"]]
        if payload[:4] != PACKAGE_MAGIC:
            raise RuntimeError("Bad package slice for " + name)
        row = write_map(
            out_dir,
            "PS2",
            name,
            payload,
            umd_path,
            url=entry["url"],
            dry_run=dry_run,
        )
        if row:
            results.append(row)
    return results


def install_maps(manifest, install_dir, overwrite=False, dry_run=False):
    mkdir_p(install_dir)
    installed = []
    skipped = []
    for row in manifest:
        if not row["output"].lower().endswith(".unr"):
            continue
        src = row["output"]
        dst = os.path.join(install_dir, os.path.basename(src))
        action = OrderedDict([
            ("platform", row["platform"]),
            ("name", row["name"]),
            ("source", src),
            ("destination", dst),
        ])
        if os.path.exists(dst) and not overwrite:
            action["reason"] = "exists"
            action["existing_bytes"] = os.path.getsize(dst)
            skipped.append(action)
            continue
        if not dry_run:
            with open(src, "rb") as read_f, open(dst, "wb") as write_f:
                write_f.write(read_f.read())
        action["bytes"] = row["bytes"]
        installed.append(action)
    return installed, skipped


def main():
    parser = argparse.ArgumentParser(description="Convert UT99 PS2 maps to PC-style .unr files.")
    parser.add_argument("--root", default=os.getcwd(), help="Repository/root directory.")
    parser.add_argument("--ps2-dir", default=None, help="PS2 extracted disc directory.")
    parser.add_argument("--out", default=None, help="Output directory.")
    parser.add_argument("--all-maps", action="store_true", help="Convert every discovered map, not only known console exclusives.")
    parser.add_argument("--install-to", default=None, help="Copy converted .unr files into this Maps directory after conversion.")
    parser.add_argument("--overwrite", action="store_true", help="Allow --install-to to replace existing map files.")
    parser.add_argument("--dry-run", action="store_true", help="Report what would be written without creating files.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    ps2_dir = os.path.abspath(args.ps2_dir or os.path.join(root, "!PS2"))
    out_dir = os.path.abspath(args.out or os.path.join(root, "ConvertedConsoleMaps"))
    exclusive_only = not args.all_maps

    mkdir_p(out_dir)

    manifest = convert_ps2(ps2_dir, out_dir, exclusive_only, args.dry_run)

    manifest_path = os.path.join(out_dir, "manifest.json")
    if not args.dry_run:
        with open(manifest_path, "w") as f:
            json.dump(manifest, f, indent=2)
            f.write("\n")

    installed = []
    skipped = []
    if args.install_to:
        installed, skipped = install_maps(
            manifest,
            os.path.abspath(args.install_to),
            overwrite=args.overwrite,
            dry_run=args.dry_run,
        )
        install_manifest_path = os.path.join(out_dir, "install_manifest.json")
        if not args.dry_run:
            with open(install_manifest_path, "w") as f:
                json.dump(OrderedDict([
                    ("install_to", os.path.abspath(args.install_to)),
                    ("overwrite", args.overwrite),
                    ("installed", installed),
                    ("skipped", skipped),
                ]), f, indent=2)
                f.write("\n")

    print("Converted entries: %d" % len(manifest))
    print("Output: " + out_dir)
    if not args.dry_run:
        print("Manifest: " + manifest_path)
    if args.install_to:
        print("Install target: " + os.path.abspath(args.install_to))
        print("Installed maps: %d" % len(installed))
        print("Skipped existing maps: %d" % len(skipped))
        if not args.dry_run:
            print("Install manifest: " + install_manifest_path)
    for row in manifest:
        print("%-10s %-28s %8d bytes v%s" % (
            row["platform"],
            row["name"],
            row["bytes"],
            row["package_version"],
        ))
    if skipped:
        print("Skipped because destination already exists:")
        for row in skipped:
            print("%-10s %s" % (row["platform"], os.path.basename(row["destination"])))


if __name__ == "__main__":
    main()
