#!/usr/bin/env python
"""
Verify one map candidate against the PS2 package section layout.

This is intentionally read-only.  It answers two questions:

1. Is the subject present in the PS2 archive?
2. Do its package sections look like raw PS2 seek-free data or PC-shaped output?
"""

from __future__ import print_function

import argparse
import hashlib
import json
import os
import struct
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_console_maps as console_maps
import convert_seekfree_console_maps as seekfree


PACKAGE_MAGIC = b"\xC1\x83\x2A\x9E"


def sha1_bytes(data):
    h = hashlib.sha1()
    h.update(data)
    return h.hexdigest()


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def header_fields(data):
    if len(data) < 36 or data[:4] != PACKAGE_MAGIC:
        return None
    fields = struct.unpack_from("<IIIIIIIII", data, 0)
    return OrderedDict([
        ("magic", "0x%08X" % fields[0]),
        ("version", fields[1]),
        ("flags", fields[2]),
        ("name_count", fields[3]),
        ("name_offset", fields[4]),
        ("export_count", fields[5]),
        ("export_offset", fields[6]),
        ("import_count", fields[7]),
        ("import_offset", fields[8]),
    ])


def safe_read_names(data, count, offset):
    try:
        names, end = seekfree.read_names(data, count, offset)
        return names, end, None
    except Exception as exc:
        return [], None, str(exc)


def safe_read_imports(data, count, offset):
    try:
        imports, end = seekfree.read_imports(data, count, offset)
        return imports, end, None
    except Exception as exc:
        return [], None, str(exc)


def safe_read_exports(data, count, offset, names):
    try:
        exports, end = seekfree.read_exports(data, count, offset, names)
        return exports, end, None
    except Exception as exc:
        return [], None, str(exc)


def sample_names(names, limit=20):
    return names[:min(limit, len(names))]


def table_validity(data, fields, names, label, import_offset, export_offset):
    imports, import_end, import_error = safe_read_imports(data, fields["import_count"], import_offset)
    if export_offset is None:
        exports, export_end, export_error = [], None, None
    else:
        exports, export_end, export_error = safe_read_exports(data, fields["export_count"], export_offset, names)
    ok = import_error is None and export_error is None
    return OrderedDict([
        ("label", label),
        ("ok", ok),
        ("import_offset", import_offset),
        ("import_end", import_end),
        ("import_error", import_error),
        ("export_offset", export_offset),
        ("export_end", export_end),
        ("export_error", export_error),
        ("import_rows", len(imports)),
        ("export_rows", len(exports)),
    ]), imports, exports


def serial_range_summary(data, exports):
    nonzero = [e for e in exports if e["serial_size"]]
    if not nonzero:
        return OrderedDict([
            ("nonzero_exports", 0),
        ])
    starts = [e["serial_offset"] for e in nonzero]
    ends = [e["serial_offset"] + e["serial_size"] for e in nonzero]
    bad = [
        e["object_name"]
        for e in nonzero
        if e["serial_offset"] < 0 or e["serial_offset"] + e["serial_size"] > len(data)
    ]
    return OrderedDict([
        ("nonzero_exports", len(nonzero)),
        ("min_serial_offset", min(starts)),
        ("max_serial_end", max(ends)),
        ("out_of_file_serial_ranges", bad[:25]),
        ("out_of_file_serial_range_count", len(bad)),
    ])


def analyze_package(name, data):
    fields = header_fields(data)
    row = OrderedDict([
        ("name", name),
        ("bytes", len(data)),
        ("sha1", sha1_bytes(data)),
        ("header", fields),
    ])
    if fields is None:
        row["error"] = "not an Unreal package"
        return row

    names, name_end, name_error = safe_read_names(data, fields["name_count"], fields["name_offset"])
    row["name_table"] = OrderedDict([
        ("ok", name_error is None),
        ("offset", fields["name_offset"]),
        ("end", name_end),
        ("error", name_error),
        ("sample", sample_names(names)),
    ])
    if name_error:
        return row

    local_import_offset = name_end
    local_probe, local_imports, local_exports = table_validity(
        data,
        fields,
        names,
        "tables_immediately_after_names",
        local_import_offset,
        None,
    )
    if local_probe["import_end"] is not None:
        local_export_offset = local_probe["import_end"]
        local_probe, local_imports, local_exports = table_validity(
            data,
            fields,
            names,
            "tables_immediately_after_names",
            local_import_offset,
            local_export_offset,
        )

    header_probe, header_imports, header_exports = table_validity(
        data,
        fields,
        names,
        "tables_at_header_offsets",
        fields["import_offset"],
        fields["export_offset"],
    )

    row["table_probes"] = [local_probe, header_probe]

    pc_valid, pc_reason = console_maps.validate_pc_package(data)
    row["pc_table_validation"] = OrderedDict([
        ("ok", pc_valid),
        ("reason", pc_reason),
    ])

    if header_probe["ok"]:
        table_end = max(header_probe["import_end"], header_probe["export_end"])
        row["header_table_serial_ranges"] = serial_range_summary(data, header_exports)
        row["pc_shaped_sections"] = OrderedDict([
            ("name_table_end", name_end),
            ("object_data_start", name_end),
            ("import_table_start", fields["import_offset"]),
            ("export_table_start", fields["export_offset"]),
            ("table_end", table_end),
            ("trailing_bytes", len(data) - table_end),
        ])

    if local_probe["ok"]:
        table_end = max(local_probe["import_end"], local_probe["export_end"])
        row["local_table_serial_ranges"] = serial_range_summary(data, local_exports)
        row["ps2_seekfree_sections"] = OrderedDict([
            ("name_table_end", name_end),
            ("local_import_table_start", local_import_offset),
            ("local_export_table_start", local_probe["export_offset"]),
            ("local_table_end", table_end),
            ("seekfree_data_start", table_end),
            ("seekfree_data_bytes", len(data) - table_end),
        ])

    if local_probe["ok"] and header_probe["ok"]:
        if fields["import_offset"] == local_probe["import_offset"]:
            layout = "pc_or_repacked"
        elif fields["import_offset"] > local_probe["import_offset"]:
            layout = "raw_ps2_seekfree_candidate"
        else:
            layout = "unexpected"
    elif header_probe["ok"]:
        layout = "pc_or_repacked"
    elif local_probe["ok"]:
        layout = "raw_ps2_seekfree_candidate"
    else:
        layout = "invalid_or_unknown"
    row["layout_classification"] = layout
    return row


def ps2_entry_payload(umd_data, entries, map_name):
    for entry in entries:
        if entry["map_name"].lower() == map_name.lower():
            return entry, bytes(umd_data[entry["package_start"]:entry["package_end"]])
    return None, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--subject", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--ps2-umd", default=os.path.join("!PS2", "PSX2LINS.UMD"))
    parser.add_argument("--reference", action="append", default=[])
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    umd_data, entries = console_maps.discover_ps2_entries(args.ps2_umd)
    ps2_entry, ps2_payload = ps2_entry_payload(umd_data, entries, args.name)

    report = OrderedDict([
        ("subject_name", args.name),
        ("ps2_archive", os.path.abspath(args.ps2_umd)),
        ("ps2_entry_present", ps2_entry is not None),
        ("ps2_entry", ps2_entry),
        ("ps2_map_names", [e["map_name"] for e in entries]),
        ("subject", analyze_package(args.subject, read_file(args.subject))),
        ("references", []),
    ])
    if ps2_payload is not None:
        report["ps2_raw_subject"] = analyze_package(args.name + " raw PS2", ps2_payload)

    for ref in args.reference:
        label, path = ref.split("=", 1) if "=" in ref else (os.path.basename(ref), ref)
        report["references"].append(analyze_package(label, read_file(path)))

    text = json.dumps(report, indent=2)
    if args.out:
        out_dir = os.path.dirname(os.path.abspath(args.out))
        if out_dir and not os.path.isdir(out_dir):
            os.makedirs(out_dir)
        with open(args.out, "w") as f:
            f.write(text)
            f.write("\n")
    print(text)


if __name__ == "__main__":
    main()
