#!/usr/bin/env python
"""
Convert UT PS2 seek-free map packages into PC-style UE1 package structure.

PS2 packages keep normal UE1 name/import/export metadata, but their
import/export tables are duplicated immediately after the name table and the
serialized objects are left in a seek-free data area.  The stock header still
points at a PC-style table location, so a PC/Xbox UE1 loader seeks into object
data and explodes.

This converter rebuilds the tables at the front of the package and repoints
exports at the actual object data in the seek-free area.  It intentionally uses
only the extracted console packages.  Matching PC maps are useful for analysis,
but they are not input to conversion.
"""

from __future__ import print_function

import re
import struct
from collections import defaultdict


PACKAGE_MAGIC = b"\xC1\x83\x2A\x9E"
RF_HAS_STACK = 0x02000000


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


def write_compact_index(value):
    sign = 0x80 if value < 0 else 0
    v = -value if value < 0 else value
    out = [sign | (v & 0x3f)]
    v >>= 6
    if v:
        out[0] |= 0x40
        out.append(v & 0x7f)
        v >>= 7
        i = 1
        while v:
            out[i] |= 0x80
            out.append(v & 0x7f)
            v >>= 7
            i += 1
    return bytes(bytearray(out))


def read_names(data, count, offset):
    pos = offset
    names = []
    for _ in range(count):
        length, pos = read_compact_index(data, pos)
        raw = data[pos:pos + length]
        pos += length + 4
        if raw.endswith(b"\0"):
            raw = raw[:-1]
        names.append(raw.decode("latin1", "replace"))
    return names, pos


def read_imports(data, count, offset):
    rows = []
    pos = offset
    for _ in range(count):
        start = pos
        class_package, pos = read_compact_index(data, pos)
        class_name, pos = read_compact_index(data, pos)
        package_index = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        object_name, pos = read_compact_index(data, pos)
        rows.append((start, pos, class_package, class_name, package_index, object_name))
    return rows, pos


def read_exports(data, count, offset, names):
    rows = []
    pos = offset
    for i in range(count):
        start = pos
        class_index, pos = read_compact_index(data, pos)
        super_index, pos = read_compact_index(data, pos)
        package_index = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        object_name_index, pos = read_compact_index(data, pos)
        object_flags = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        serial_size, pos = read_compact_index(data, pos)
        serial_offset = 0
        if serial_size:
            serial_offset, pos = read_compact_index(data, pos)
        object_name = names[object_name_index] if 0 <= object_name_index < len(names) else str(object_name_index)
        rows.append({
            "index": i,
            "start": start,
            "end": pos,
            "class_index": class_index,
            "super_index": super_index,
            "package_index": package_index,
            "object_name_index": object_name_index,
            "object_name": object_name,
            "object_flags": object_flags,
            "serial_size": serial_size,
            "serial_offset": serial_offset,
        })
    return rows, pos


def parse_seekfree(data):
    if data[:4] != PACKAGE_MAGIC:
        raise ValueError("bad package magic")
    fields = list(struct.unpack_from("<IIIIIIIII", data, 0))
    names, name_end = read_names(data, fields[3], fields[4])
    imports, import_end = read_imports(data, fields[7], name_end)
    exports, export_end = read_exports(data, fields[5], import_end, names)
    return fields, names, name_end, imports, import_end, exports, export_end


def parse_property_stream(data, names, pos, end):
    try:
        for _ in range(1024):
            name_index, pos = read_compact_index(data, pos)
            if name_index < 0 or name_index >= len(names):
                return False
            if names[name_index] == "None":
                return True
            if pos >= end:
                return False
            info = data[pos]
            pos += 1
            prop_type = info & 0x0f
            size_type = info & 0x70
            if prop_type < 1 or prop_type > 15:
                return False
            if prop_type == 10:
                item_name, pos = read_compact_index(data, pos)
                if item_name < 0 or item_name >= len(names):
                    return False
            if size_type == 0x00:
                size = 1
            elif size_type == 0x10:
                size = 2
            elif size_type == 0x20:
                size = 4
            elif size_type == 0x30:
                size = 12
            elif size_type == 0x40:
                size = 16
            elif size_type == 0x50:
                if pos >= end:
                    return False
                size = data[pos]
                pos += 1
            elif size_type == 0x60:
                if pos + 2 > end:
                    return False
                size = struct.unpack_from("<H", data, pos)[0]
                pos += 2
            else:
                if pos + 4 > end:
                    return False
                size = struct.unpack_from("<I", data, pos)[0]
                pos += 4
            if (info & 0x80) and prop_type != 3:
                if pos >= end:
                    return False
                b = data[pos]
                pos += 1
                if b & 0x80:
                    pos += 1 if (b & 0xC0) == 0x80 else 3
            if pos + size > end:
                return False
            pos += size
        return False
    except Exception:
        return False


def stack_payload_start(data, export, offset):
    node, pos = read_compact_index(data, offset)
    state_node, pos = read_compact_index(data, pos)
    if node != export["class_index"] or state_node != export["class_index"]:
        return None
    pos += 8
    pos += 4
    if node:
        _, pos = read_compact_index(data, pos)
    return pos


def object_sort_key(name):
    parts = re.match(r"^(.*?)(\d+)$", name)
    if parts:
        return (parts.group(1).lower(), int(parts.group(2)))
    return (name.lower(), -1)


def infer_stack_offsets(data, names, exports):
    wanted = defaultdict(list)
    max_size = 0
    for export in exports:
        if export["serial_size"] and (export["object_flags"] & RF_HAS_STACK):
            wanted[(export["class_index"], export["serial_size"])].append(export)
            max_size = max(max_size, export["serial_size"])
    for exports_for_key in wanted.values():
        exports_for_key.sort(key=lambda e: object_sort_key(e["object_name"]))

    starts = []
    for offset in range(0, len(data) - 20):
        try:
            node, pos = read_compact_index(data, offset)
            state_node, _ = read_compact_index(data, pos)
        except Exception:
            continue
        if node == state_node and node < 0:
            starts.append((offset, node))
    starts = sorted(set(starts))

    chunks = defaultdict(list)
    loose_chunks = defaultdict(list)
    for i, (offset, class_index) in enumerate(starts):
        next_offset = starts[i + 1][0] if i + 1 < len(starts) else len(data)
        size = next_offset - offset
        for key, exports_for_key in wanted.items():
            wanted_class, wanted_size = key
            if wanted_class != class_index:
                continue
            probe_export = exports_for_key[0]
            prop_pos = stack_payload_start(data, probe_export, offset)
            if prop_pos is None:
                continue
            if parse_property_stream(data, names, prop_pos, min(len(data), offset + wanted_size)):
                loose_chunks[key].append(offset)
                if size == wanted_size:
                    chunks[key].append(offset)

    inferred = {}
    unresolved = []
    for key, exports_for_key in wanted.items():
        offsets = sorted(chunks.get(key, []))
        if len(offsets) < len(exports_for_key):
            seen = set(offsets)
            for offset in sorted(loose_chunks.get(key, [])):
                if offset not in seen:
                    offsets.append(offset)
                    seen.add(offset)
                if len(offsets) >= len(exports_for_key):
                    break
            offsets.sort()
        if len(offsets) < len(exports_for_key):
            unresolved.extend(e["object_name"] for e in exports_for_key[len(offsets):])
        for export, offset in zip(exports_for_key, offsets):
            inferred[export["index"]] = offset
    return inferred, unresolved


def build_import_table(imports):
    out = bytearray()
    for _, _, class_package, class_name, package_index, object_name in imports:
        out += write_compact_index(class_package)
        out += write_compact_index(class_name)
        out += struct.pack("<i", package_index)
        out += write_compact_index(object_name)
    return bytes(out)


def build_export_table(exports, offset_map):
    out = bytearray()
    for export in exports:
        out += write_compact_index(export["class_index"])
        out += write_compact_index(export["super_index"])
        out += struct.pack("<i", export["package_index"])
        out += write_compact_index(export["object_name_index"])
        out += struct.pack("<I", export["object_flags"])
        out += write_compact_index(export["serial_size"])
        if export["serial_size"]:
            out += write_compact_index(offset_map[export["index"]])
    return bytes(out)


def convert_package(data):
    fields, names, name_end, imports, import_end, exports, export_end = parse_seekfree(data)
    stack_offsets, unresolved = infer_stack_offsets(data, names, exports)
    seekfree_data_start = export_end
    search_pos = 1
    while True:
        magic = data.find(PACKAGE_MAGIC, search_pos)
        if magic < 0:
            break
        try:
            _, _, _, _, _, _, _, embedded_end = parse_seekfree(data[magic:])
            seekfree_data_start = max(seekfree_data_start, magic + embedded_end)
        except Exception:
            pass
        search_pos = magic + 1

    import_table = build_import_table(imports)
    export_table = b""
    data_tail = data[export_end:]
    table_start = name_end
    import_offset = table_start
    export_offset = import_offset + len(import_table)

    offset_map = {}
    for _ in range(8):
        data_start = export_offset + len(export_table)
        offset_map.clear()
        for export in exports:
            if not export["serial_size"]:
                continue
            if export["object_name"] == "MyLevel":
                old_offset = seekfree_data_start
            elif export["index"] in stack_offsets:
                old_offset = stack_offsets[export["index"]]
            elif export["serial_offset"] >= export_end:
                old_offset = export["serial_offset"]
            else:
                raise ValueError("no data offset for %s at old serial offset %d" % (
                    export["object_name"], export["serial_offset"]))
            offset_map[export["index"]] = data_start + (old_offset - export_end)
        new_export_table = build_export_table(exports, offset_map)
        if len(new_export_table) == len(export_table):
            export_table = new_export_table
            break
        export_table = new_export_table

    header = bytearray(data[:name_end])
    # UT PC/Xbox 400 expects the v68 object-reference semantics used by the
    # stock PC packages.  The console seek-free tables are tagged v69 even
    # though the converted package layout is the ordinary v68-compatible form.
    struct.pack_into("<I", header, 4, 68)
    struct.pack_into("<I", header, 24, export_offset)
    struct.pack_into("<I", header, 32, import_offset)
    return bytes(header) + import_table + export_table + data_tail
