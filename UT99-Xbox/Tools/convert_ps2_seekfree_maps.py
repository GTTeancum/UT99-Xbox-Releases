#!/usr/bin/env python
"""
Repack UT99 PS2 seek-free map packages into ordinary PC/Xbox UE1 packages.

The PS2 disc stores each map as a normal package summary/name table followed by
console-local import/export tables and a seek-free object area.  Export serial
offsets are logical PC offsets, not physical file offsets.  This tool recovers
the physical object bodies, writes them contiguously after the name table, and
then appends normal import/export tables.
"""

from __future__ import print_function

import argparse
import json
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_console_maps as console_maps
import convert_seekfree_console_maps as seekfree


PACKAGE_MAGIC = b"\xC1\x83\x2A\x9E"
RF_HAS_STACK = 0x02000000
CURRENT_MAP_NAME = None
CURRENT_LEVELINFO_REF = 0
PS2_UMD_DATA = None
PS2_PREVIEW_CACHE = {}
CURRENT_SCREENSHOT_PALETTE = None
CURRENT_SCREENSHOT_PALETTE_REF = 0
PREVIEW_OVERRIDE_DIR = os.path.join("ConvertedConsoleMaps", "PreviewOverrides")

PSMCT32_PAGE_SWIZZLE = [
    0, 1, 4, 5, 16, 17, 20, 21,
    2, 3, 6, 7, 18, 19, 22, 23,
    8, 9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
]
PSMCT32_COLUMN_SWIZZLE = [
    0, 1, 4, 5, 8, 9, 12, 13,
    2, 3, 6, 7, 10, 11, 14, 15,
]
PSMCT32_PAGE_LUT = None

PS2_SURF_STREAM_SKIPS = {
    "DOM-Cinder": {
        476372: 476952,
    },
}

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


IMPORT_PACKAGE_FIXES = {
    ("BotPack", "Barrel"): "UnrealShare",
    ("BotPack", "WaterZone"): "UnrealShare",
}


IMPORT_CLASS_FIXES = {
    # The PS2 cook records these actors under BotPack even though the PC/Xbox
    # classes live elsewhere.  Fix both the package and (where necessary) the
    # class name; renaming only the object leaves an invalid BotPack import.
    ("BotPack", "plant6"): ("Engine", "Effects"),
    ("BotPack", "Sconce"): ("UnrealShare", "Sconce"),
    ("BotPack", "TorchFlame"): ("UnrealShare", "TorchFlame"),
    ("BotPack", "Tree6"): ("UnrealShare", "Tree6"),
    ("BotPack", "Tree7"): ("UnrealShare", "Tree7"),
}


EXCLUDED_ACTOR_CLASSES = set([
    "Effects",
])


NULL_UNSAFE_OBJECT_PROPS_MAPS = set([
    "CTF-Spirito",
    "DOM-Osiris",
])


ACTOR_CLASS_ALLOWLISTS = {
    "DOM-Osiris": set([
        "LevelInfo",
        "PlayerStart",
        "ControlPoint",
    ]),
}


TYPE_IDS = {
    "ByteProperty": 1,
    "IntProperty": 2,
    "BoolProperty": 3,
    "FloatProperty": 4,
    "ObjectProperty": 5,
    "ClassProperty": 5,
    "NameProperty": 6,
    "ArrayProperty": 9,
    "StructProperty": 10,
    "StrProperty": 13,
}

CPF_NATIVE = 0x00001000
CPF_TRANSIENT = 0x00002000

FIXED_STRUCT_SIZES = {
    "Vector": 12,
    "Rotator": 12,
    "Plane": 16,
    "Color": 4,
    "Scale": 17,
    "Coords": 48,
    "Guid": 16,
    "BoundingBox": 25,
}


def load_class_metadata(path=os.path.join("ConvertedConsoleMaps", "pc_classes.jsonl")):
    rows = {}
    with open(path, "r") as f:
        for line in f:
            row = json.loads(line)
            rows[row["class"]] = row
    return rows


CLASS_METADATA = None
RF_LOAD_CONTEXT_FLAGS = 0x00070000


def pack_name_entry(name, flags=0x00070010):
    raw = name.encode("latin1") + b"\0"
    return seekfree.write_compact_index(len(raw)) + raw + struct.pack("<I", flags)


def normalize_name_load_flags(raw_name_bytes, name_count, name_offset=64):
    data = bytearray(raw_name_bytes)
    pos = name_offset
    changed = 0
    for _ in range(name_count):
        length, pos = seekfree.read_compact_index(data, pos)
        if length >= 0:
            pos += length
        else:
            pos += -length * 2
        old_flags = struct.unpack_from("<I", data, pos)[0]
        new_flags = old_flags | RF_LOAD_CONTEXT_FLAGS
        if new_flags != old_flags:
            struct.pack_into("<I", data, pos, new_flags)
            changed += 1
        pos += 4
    return bytes(data), changed


def prepare_imports_and_names(raw_name_bytes, names, imports, exports):
    names = list(names)
    imports = [list(row) for row in imports]

    def ensure_name(name):
        for i, existing in enumerate(names):
            if existing.lower() == name.lower():
                return i, b""
        names.append(name)
        return len(names) - 1, pack_name_entry(name)

    extra_name_bytes = bytearray()

    def outer_name(imp):
        package_index = imp[4]
        if package_index < 0:
            return names[imports[-package_index - 1][5]]
        return None

    def ensure_package_import(package_name):
        for i, imp in enumerate(imports):
            if imp[4] == 0 and names[imp[5]].lower() == package_name.lower():
                return i
        name_index, added = ensure_name(package_name)
        extra_name_bytes.extend(added)
        # Match the package-import shape already present in console maps:
        # ClassPackage=Core, ClassName=Package, Outer=None, ObjectName=<pkg>.
        core_index, added = ensure_name("Core")
        extra_name_bytes.extend(added)
        package_index, added = ensure_name("Package")
        extra_name_bytes.extend(added)
        imports.append([0, 0, core_index, package_index, 0, name_index])
        return len(imports) - 1

    pending_import_fixes = []
    for imp in list(imports):
        old_outer = outer_name(imp)
        if not old_outer:
            continue
        object_name = names[imp[5]]
        class_fix = IMPORT_CLASS_FIXES.get((old_outer, object_name))
        if class_fix:
            new_outer, new_name = class_fix
            renamed_index, added = ensure_name(new_name)
            extra_name_bytes.extend(added)
            imp[5] = renamed_index
            pending_import_fixes.append((imp, new_outer))
            continue
        new_outer = IMPORT_PACKAGE_FIXES.get((old_outer, object_name))
        if new_outer:
            pending_import_fixes.append((imp, new_outer))

    package_imports = {}
    for _imp, new_outer in pending_import_fixes:
        package_imports[new_outer] = ensure_package_import(new_outer)
    for imp, new_outer in pending_import_fixes:
        imp[4] = -(package_imports[new_outer] + 1)

    return raw_name_bytes + bytes(extra_name_bytes), names, [tuple(row) for row in imports]


def ensure_core_object_import(raw_name_bytes, names, imports):
    raw_name_bytes, names = ensure_names(raw_name_bytes, names, ["Core", "Package", "Class", "Object"])
    core_name = name_index(names, "Core")
    package_name = name_index(names, "Package")
    class_name_idx = name_index(names, "Class")
    object_name_idx = name_index(names, "Object")
    core_package = None
    for i, imp in enumerate(imports):
        if imp[4] == 0 and names[imp[5]].lower() == "core":
            core_package = i
            break
    if core_package is None:
        imports = list(imports)
        imports.append((0, 0, core_name, package_name, 0, core_name))
        core_package = len(imports) - 1
    for i, imp in enumerate(imports):
        if imp[4] == -(core_package + 1) and names[imp[5]].lower() == "object":
            return raw_name_bytes, names, imports, -(i + 1)
    imports = list(imports)
    imports.append((0, 0, core_name, class_name_idx, -(core_package + 1), object_name_idx))
    return raw_name_bytes, names, imports, -len(imports)


def ensure_names(raw_name_bytes, names, wanted_names):
    names = list(names)
    extra_name_bytes = bytearray()
    index = {name.lower(): i for i, name in enumerate(names)}
    for name in wanted_names:
        if not name:
            continue
        key = name.lower()
        if key in index:
            continue
        index[key] = len(names)
        names.append(name)
        extra_name_bytes.extend(pack_name_entry(name))
    return raw_name_bytes + bytes(extra_name_bytes), names


def name_index(names, wanted):
    wanted_lower = wanted.lower()
    for i, name in enumerate(names):
        if name.lower() == wanted_lower:
            return i
    raise KeyError("missing name %s" % wanted)


def import_class_ref(names, imports, wanted):
    wanted_lower = wanted.lower()
    for i, imp in enumerate(imports):
        if names[imp[5]].lower() == wanted_lower:
            return -(i + 1)
    raise KeyError("missing imported class %s" % wanted)


def class_name(names, imports, exports, export):
    ci = export["class_index"]
    if ci == 0:
        return "Class"
    if ci > 0:
        return names[exports[ci - 1]["object_name_index"]]
    return names[imports[-ci - 1][5]]


def find_level_body(data, export_end, first_actor_offset, size):
    best = None
    end = min(first_actor_offset, len(data) - 16)
    for off in range(export_end, end):
        if data[off] != 0:
            continue
        count = struct.unpack_from("<i", data, off + 1)[0]
        max_count = struct.unpack_from("<i", data, off + 5)[0]
        if count != max_count or count < 8 or count > 4096:
            continue
        # The old-format actor array follows immediately.  Prefer candidates
        # whose serialized body fits before the first actor object.
        if off + size <= first_actor_offset:
            return off
        if best is None:
            best = off
    if best is not None:
        return best
    raise ValueError("could not locate ULevel body")


def should_drop_early_export(cls_name, export):
    if export["serial_offset"] == 0:
        return False
    if cls_name in ("Texture", "Palette"):
        return True
    if cls_name == "Package" and export["serial_size"] <= 4:
        return True
    return False


def build_empty_string():
    return seekfree.write_compact_index(0)


def build_empty_url():
    out = bytearray()
    for _ in range(4):
        out += build_empty_string()
    out += seekfree.write_compact_index(0)  # Op array.
    out += struct.pack("<ii", 0, 1)  # Port, Valid.
    return bytes(out)


def is_static_brush_actor(cls_name):
    return cls_name.lower() == "brush"


def build_empty_model(names):
    out = bytearray()
    out += seekfree.write_compact_index(name_index(names, "None"))  # UObject tags.
    out += b"\0" * 25  # UPrimitive BoundingBox.
    out += b"\0" * 16  # UPrimitive BoundingSphere.
    for _ in range(5):  # Vectors, Points, Nodes, Surfs, Verts.
        out += seekfree.write_compact_index(0)
    out += struct.pack("<ii", 0, 0)  # NumSharedSides, NumZones.
    out += seekfree.write_compact_index(0)  # Polys.
    for _ in range(6):  # LightMap, LightBits, Bounds, LeafHulls, Leaves, Lights.
        out += seekfree.write_compact_index(0)
    out += struct.pack("<ii", 1, 0)  # RootOutside, Linked.
    return bytes(out)


def append_texture_mips(out, serial_offset, mips):
    out += seekfree.write_compact_index(len(mips))
    for width, height, ubits, vbits, payload in mips:
        count_pos = serial_offset + len(out)
        payload_count = seekfree.write_compact_index(len(payload))
        fields_pos = count_pos + 4 + len(payload_count) + len(payload)
        out += struct.pack("<i", fields_pos)
        out += payload_count
        out += payload
        out += struct.pack("<iiBB", width, height, ubits, vbits)
    return out


def build_p8_texture(names, serial_offset, palette_ref, pixels, width=256, height=256):
    out = bytearray()
    if palette_ref:
        out += encode_tag(
            names,
            {"name": "Palette", "type": "ObjectProperty"},
            seekfree.write_compact_index(palette_ref),
            0)
    for prop_name, prop_type, value in (
            ("UBits", "ByteProperty", bytes([8])),
            ("VBits", "ByteProperty", bytes([8])),
            ("USize", "IntProperty", struct.pack("<i", width)),
            ("VSize", "IntProperty", struct.pack("<i", height)),
            ("UClamp", "IntProperty", struct.pack("<i", width)),
            ("VClamp", "IntProperty", struct.pack("<i", height))):
        out += encode_tag(names, {"name": prop_name, "type": prop_type}, value, 0)
    out += seekfree.write_compact_index(name_index(names, "None"))
    mips = []
    cur = bytes(pixels)
    cur_width = width
    cur_height = height
    while cur_width >= 1 and cur_height >= 1:
        ubits = (cur_width.bit_length() - 1)
        vbits = (cur_height.bit_length() - 1)
        mips.append((cur_width, cur_height, ubits, vbits, cur))
        if cur_width == 1 and cur_height == 1:
            break
        next_width = max(1, cur_width // 2)
        next_height = max(1, cur_height // 2)
        next_pixels = bytearray(next_width * next_height)
        for y in range(next_height):
            for x in range(next_width):
                next_pixels[y * next_width + x] = cur[(y * 2) * cur_width + (x * 2)]
        cur = bytes(next_pixels)
        cur_width = next_width
        cur_height = next_height
    return bytes(append_texture_mips(out, serial_offset, mips))


def build_dummy_texture(names, serial_offset):
    out = bytearray()
    bits = 6
    size = 1 << bits
    for prop_name, prop_type, value in (
            ("UBits", "ByteProperty", bytes([bits])),
            ("VBits", "ByteProperty", bytes([bits])),
            ("USize", "IntProperty", struct.pack("<i", size)),
            ("VSize", "IntProperty", struct.pack("<i", size)),
            ("UClamp", "IntProperty", struct.pack("<i", size)),
            ("VClamp", "IntProperty", struct.pack("<i", size))):
        out += encode_tag(names, {"name": prop_name, "type": prop_type}, value, 0)
    out += seekfree.write_compact_index(name_index(names, "None"))  # End UObject tags.
    mip_bytes = bytes([0]) * (size * size)
    return bytes(append_texture_mips(out, serial_offset, [(size, size, bits, bits, mip_bytes)]))


def build_empty_palette(names):
    out = bytearray()
    out += seekfree.write_compact_index(name_index(names, "None"))  # UObject tags.
    out += seekfree.write_compact_index(0)  # UPalette::Colors TArray<FColor>.
    return bytes(out)


def build_palette_from_rgb(names, palette_rgb):
    out = bytearray()
    out += seekfree.write_compact_index(name_index(names, "None"))  # UObject tags.
    out += seekfree.write_compact_index(256)
    for r, g, b in palette_rgb:
        out += bytes([r, g, b, 255])
    return bytes(out)


def psmct32_page_lut():
    global PSMCT32_PAGE_LUT
    if PSMCT32_PAGE_LUT is not None:
        return PSMCT32_PAGE_LUT
    lut = []
    for y in range(32):
        row = []
        for x in range(64):
            block = (y // 8) * 8 + (x // 8)
            pixel = (y % 8) * 8 + (x % 8)
            column = pixel // 16
            word = column * 16 + PSMCT32_COLUMN_SWIZZLE[pixel % 16]
            row.append(PSMCT32_PAGE_SWIZZLE[block] * 256 + word * 4)
        lut.append(row)
    PSMCT32_PAGE_LUT = lut
    return lut


def unswizzle_psmct32_rgba(data, width, height):
    lut = psmct32_page_lut()
    out = bytearray(width * height * 4)
    pages_per_row = (width + 63) // 64
    for y in range(height):
        for x in range(width):
            page = (y // 32) * pages_per_row + (x // 64)
            src = page * 8192 + lut[y % 32][x % 64]
            dst = (y * width + x) * 4
            out[dst:dst + 4] = data[src:src + 4]
    return bytes(out)


def quantize_rgba_to_p8(rgba, width, height):
    try:
        from PIL import Image
    except Exception:
        return None
    opaque = bytearray(rgba)
    for i in range(3, len(opaque), 4):
        opaque[i] = 255
    image = Image.frombytes("RGBA", (width, height), bytes(opaque))
    image = image.resize((256, 256), Image.Resampling.BILINEAR)
    paletted = image.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    pixels = paletted.tobytes()
    raw_palette = paletted.getpalette()[:256 * 3]
    palette = []
    for i in range(256):
        base = i * 3
        palette.append(tuple(raw_palette[base:base + 3]))
    return pixels, palette


def load_preview_override(map_name):
    try:
        from PIL import Image
    except Exception:
        return None
    for ext in (".png", ".jpg", ".jpeg"):
        path = os.path.join(PREVIEW_OVERRIDE_DIR, map_name + ext)
        if not os.path.isfile(path) and os.path.isdir(PREVIEW_OVERRIDE_DIR):
            wanted = (map_name + ext).lower()
            for candidate in os.listdir(PREVIEW_OVERRIDE_DIR):
                if candidate.lower() == wanted:
                    path = os.path.join(PREVIEW_OVERRIDE_DIR, candidate)
                    break
        if not os.path.isfile(path):
            continue
        image = Image.open(path).convert("RGBA").resize((256, 256), Image.Resampling.LANCZOS)
        decoded = quantize_rgba_to_p8(image.tobytes("raw", "RGBA"), 256, 256)
        if decoded:
            pixels, palette = decoded
            return {
                "pixels": pixels,
                "palette": palette,
                "source_package_offset": None,
                "source_texture": os.path.basename(path),
                "source_palette": "quantized override",
            }
    return None


def psmct32_luma_rgba(rgba):
    out = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        y = (rgba[i] + rgba[i + 1] + rgba[i + 2] + rgba[i + 3]) // 4
        out[i:i + 4] = bytes([y, y, y, 255])
    return bytes(out)


def extract_ps2_frontend_preview(map_name):
    override = load_preview_override(map_name)
    if override:
        return override
    if PS2_UMD_DATA is None:
        return None
    key = map_name.lower()
    if key in PS2_PREVIEW_CACHE:
        return PS2_PREVIEW_CACHE[key]

    needle = map_name.encode("ascii", "ignore")
    pos = 0
    best = None
    while True:
        hit = PS2_UMD_DATA.find(needle, pos)
        if hit < 0:
            break
        start = PS2_UMD_DATA.rfind(PACKAGE_MAGIC, 0, hit)
        pos = hit + 1
        if start < 0:
            continue
        try:
            fields, names, _name_end, imports, _import_end, exports, _export_end = seekfree.parse_seekfree(PS2_UMD_DATA[start:])
        except Exception:
            continue
        for i, export in enumerate(exports[:-1]):
            if names[export["object_name_index"]].lower() != key:
                continue
            palette_export = exports[i + 1]
            if not names[palette_export["object_name_index"]].lower().startswith("palette"):
                continue
            if export["serial_size"] < 65536 or palette_export["serial_size"] < 1024:
                continue
            texture_body = PS2_UMD_DATA[
                start + export["serial_offset"]:start + export["serial_offset"] + export["serial_size"]]
            palette_body = PS2_UMD_DATA[
                start + palette_export["serial_offset"]:start + palette_export["serial_offset"] + palette_export["serial_size"]]
            decoded = None
            if len(texture_body) >= 128 * 128 * 4:
                rgba = unswizzle_psmct32_rgba(texture_body[:128 * 128 * 4], 128, 128)
                rgba = psmct32_luma_rgba(rgba)
                decoded = quantize_rgba_to_p8(rgba, 128, 128)
            if not decoded:
                pixels = texture_body[:256 * 256]
                palette = []
                for n in range(256):
                    r, g, b = palette_body[n * 4:n * 4 + 3]
                    palette.append((r, g, b))
            else:
                pixels, palette = decoded
            best = {
                "pixels": pixels,
                "palette": palette,
                "source_package_offset": start,
                "source_texture": names[export["object_name_index"]],
                "source_palette": names[palette_export["object_name_index"]],
            }
            break
        if best:
            break
    PS2_PREVIEW_CACHE[key] = best
    return best


def sanitize_level_tail(level_tail, valid_actor_refs):
    pos = 0
    for _ in range(4):
        pos = skip_fstring(level_tail, pos)
    op_count, pos = seekfree.read_compact_index(level_tail, pos)
    for _ in range(op_count):
        pos = skip_fstring(level_tail, pos)
    pos += 8
    _model_ref, pos = seekfree.read_compact_index(level_tail, pos)

    reach_count_pos = pos
    reach_count, pos = seekfree.read_compact_index(level_tail, pos)
    rows = []
    for _ in range(reach_count):
        row_start = pos
        pos += 4
        start_ref, pos = seekfree.read_compact_index(level_tail, pos)
        end_ref, pos = seekfree.read_compact_index(level_tail, pos)
        pos += 13
        if start_ref in valid_actor_refs and end_ref in valid_actor_refs:
            rows.append(level_tail[row_start:pos])

    out = bytearray(level_tail[:reach_count_pos])
    out += seekfree.write_compact_index(len(rows))
    for row in rows:
        out += row

    # Preserve ApproxTime and FirstDeleted, then clear transient text/travel arrays.
    out += level_tail[pos:pos + 4]
    pos += 4
    _first_deleted, first_deleted_end = seekfree.read_compact_index(level_tail, pos)
    out += level_tail[pos:first_deleted_end]
    pos = first_deleted_end
    for _ in range(16):
        _old, pos = seekfree.read_compact_index(level_tail, pos)
        out += seekfree.write_compact_index(0)
    travel_count, travel_pos = seekfree.read_compact_index(level_tail, pos)
    if travel_count == 0:
        out += level_tail[pos:travel_pos]
    else:
        out += seekfree.write_compact_index(0)
    return bytes(out), reach_count, len(rows)


def build_synthetic_level(names, imports, exports, level_tail, valid_actor_indices=None):
    global CURRENT_LEVELINFO_REF
    actor_refs = []
    levelinfo_ref = None
    for export in exports:
        if not (export["object_flags"] & RF_HAS_STACK):
            continue
        if valid_actor_indices is not None and export["index"] not in valid_actor_indices:
            continue
        cls = class_name(names, imports, exports, export)
        if cls == "Model" or is_static_brush_actor(cls):
            continue
        if cls in EXCLUDED_ACTOR_CLASSES:
            continue
        allowlist = ACTOR_CLASS_ALLOWLISTS.get(CURRENT_MAP_NAME)
        if allowlist is not None and cls not in allowlist:
            continue
        if cls == "LevelInfo":
            levelinfo_ref = export["index"] + 1
            CURRENT_LEVELINFO_REF = levelinfo_ref
        else:
            actor_refs.append(export["index"] + 1)
    if levelinfo_ref is not None:
        actor_refs.insert(0, levelinfo_ref)
    level_tail, _old_reach_count, _new_reach_count = sanitize_level_tail(level_tail, set(actor_refs))

    out = bytearray()
    out += seekfree.write_compact_index(name_index(names, "None"))  # No tagged UObject properties.
    out += struct.pack("<ii", len(actor_refs), len(actor_refs))
    for ref in actor_refs:
        out += seekfree.write_compact_index(ref)
    out += level_tail
    return bytes(out)


def stack_body_payload_offset(data, export, offset):
    node, pos = seekfree.read_compact_index(data, offset)
    state_node, pos = seekfree.read_compact_index(data, pos)
    if node != export["class_index"] or state_node != export["class_index"]:
        return None
    pos += 8
    pos += 4
    if node:
        code_offset, pos = seekfree.read_compact_index(data, pos)
        if code_offset != -1:
            return None
        return pos


def read_fstring(data, pos):
    length, pos = seekfree.read_compact_index(data, pos)
    if length > 0:
        raw = data[pos:pos + length]
        pos += length
        if raw.endswith(b"\0"):
            raw = raw[:-1]
        return raw.decode("latin1", "replace"), pos
    if length < 0:
        pos += -length * 2
        return "", pos
    return "", pos


def skip_fstring(data, pos):
    length, pos = seekfree.read_compact_index(data, pos)
    if length > 0:
        pos += length
    elif length < 0:
        pos += -length * 2
    return pos


def try_parse_level_tail(data, names, imports, exports, url_start, reach_offset):
    try:
        pos = url_start
        for _ in range(4):
            _value, pos = read_fstring(data, pos)
        op_count, pos = seekfree.read_compact_index(data, pos)
        if op_count < 0 or op_count > 128:
            return None
        for _ in range(op_count):
            pos = skip_fstring(data, pos)
        if pos + 8 > len(data):
            return None
        port, valid = struct.unpack_from("<ii", data, pos)
        pos += 8
        if port != 7777 or valid not in (0, 1):
            return None
        model_ref, pos = seekfree.read_compact_index(data, pos)
        if pos != reach_offset:
            return None
        if model_ref <= 0 or model_ref > len(exports):
            return None
        if class_name(names, imports, exports, exports[model_ref - 1]) != "Model":
            return None
        reach_count, pos = seekfree.read_compact_index(data, pos)
        if reach_count < 0 or reach_count > 10000:
            return None
        for _ in range(reach_count):
            pos += 4
            _start, pos = seekfree.read_compact_index(data, pos)
            _end, pos = seekfree.read_compact_index(data, pos)
            pos += 13
            if pos > len(data):
                return None
        if pos + 4 > len(data):
            return None
        pos += 4  # ApproxTime.
        _first_deleted, pos = seekfree.read_compact_index(data, pos)
        for _ in range(16):
            _text_block, pos = seekfree.read_compact_index(data, pos)
        travel_count, pos = seekfree.read_compact_index(data, pos)
        if travel_count < 0 or travel_count > 128:
            return None
        for _ in range(travel_count):
            pos = skip_fstring(data, pos)
            pos = skip_fstring(data, pos)
        return {
            "start": url_start,
            "end": pos,
            "model_ref": model_ref,
            "reach_count": reach_count,
        }
    except Exception:
        return None


def parse_reach_array_end(data, offset, export_count, need=10):
    try:
        count, pos = seekfree.read_compact_index(data, offset)
        if count < 100 or count > 10000:
            return None
        probe_count = min(count, need)
        probe_pos = pos
        for _ in range(probe_count):
            if probe_pos + 17 > len(data):
                return None
            distance = struct.unpack_from("<i", data, probe_pos)[0]
            probe_pos += 4
            start_ref, probe_pos = seekfree.read_compact_index(data, probe_pos)
            end_ref, probe_pos = seekfree.read_compact_index(data, probe_pos)
            radius, height, flags = struct.unpack_from("<iii", data, probe_pos)
            probe_pos += 12
            pruned = data[probe_pos]
            probe_pos += 1
            if not (
                0 < distance < 200000
                and 0 < abs(start_ref) <= export_count
                and 0 < abs(end_ref) <= export_count
                and 0 <= radius <= 1000
                and 0 <= height <= 1000
                and 0 <= flags < 65536
                and pruned in (0, 1)
            ):
                return None
        pos = seekfree.read_compact_index(data, offset)[1]
        for _ in range(count):
            pos += 4
            _start_ref, pos = seekfree.read_compact_index(data, pos)
            _end_ref, pos = seekfree.read_compact_index(data, pos)
            pos += 13
            if pos > len(data):
                return None
        return count, pos
    except Exception:
        return None


def find_level_tail(data, names, imports, exports):
    hits = []
    for offset in range(0, len(data) - 400):
        parsed = parse_reach_array_end(data, offset, len(exports))
        if not parsed:
            continue
        count, _end = parsed
        for url_start in range(max(0, offset - 128), offset):
            tail = try_parse_level_tail(data, names, imports, exports, url_start, offset)
            if tail:
                tail["reach_offset"] = offset
                tail["score"] = count
                hits.append(tail)
    if not hits:
        raise ValueError("could not locate PS2 ULevel tail")
    hits.sort(key=lambda item: (item["score"], item["model_ref"]), reverse=True)
    return hits[0]


def zero_level_textblocks(level_tail):
    pos = 0
    for _ in range(4):
        pos = skip_fstring(level_tail, pos)
    op_count, pos = seekfree.read_compact_index(level_tail, pos)
    for _ in range(op_count):
        pos = skip_fstring(level_tail, pos)
    pos += 8
    _model_ref, pos = seekfree.read_compact_index(level_tail, pos)
    reach_count, pos = seekfree.read_compact_index(level_tail, pos)
    for _ in range(reach_count):
        pos += 4
        _start_ref, pos = seekfree.read_compact_index(level_tail, pos)
        _end_ref, pos = seekfree.read_compact_index(level_tail, pos)
        pos += 13
    pos += 4
    _first_deleted, pos = seekfree.read_compact_index(level_tail, pos)
    out = bytearray(level_tail[:pos])
    for _ in range(16):
        _old, pos = seekfree.read_compact_index(level_tail, pos)
        out += seekfree.write_compact_index(0)
    travel_count, travel_pos = seekfree.read_compact_index(level_tail, pos)
    if travel_count == 0:
        out += level_tail[pos:travel_pos]
        return bytes(out)
    out += seekfree.write_compact_index(0)
    return bytes(out)


def build_level_summary_from_levelinfo(data, names, levelinfo_export, levelinfo_offset):
    payload_pos = stack_body_payload_offset(data, levelinfo_export, levelinfo_offset)
    if payload_pos is None:
        return seekfree.write_compact_index(name_index(names, "None"))
    return keep_tagged_properties(
        data,
        names,
        payload_pos,
        levelinfo_offset + levelinfo_export["serial_size"],
        ["Title", "Author", "IdealPlayerCount", "RecommendedEnemies", "RecommendedTeammates"])


def parse_bsp_node(data, pos):
    pos += 16  # Plane.
    pos += 8   # ZoneMask.
    pos += 1   # NodeFlags.
    for _ in range(7):
        _value, pos = seekfree.read_compact_index(data, pos)
    pos += 3   # iZone[0], iZone[1], NumVertices.
    pos += 8   # iLeaf[0], iLeaf[1] are raw INTs, not compact indices.
    return pos


def parse_bsp_surf_row(data, pos, names, imports, exports, bad_export_refs, sanitize=True, vector_count=None, point_count=None, surf_count=None):
    start = pos
    texture_ref, pos = seekfree.read_compact_index(data, pos)
    poly_flags = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    fields = []
    for _ in range(6):
        value, pos = seekfree.read_compact_index(data, pos)
        fields.append(value)
    pos += 4  # PanU, PanV.
    _actor_ref, actor_start = seekfree.read_compact_index(data, pos)
    actor_end = actor_start

    max_ref = len(exports)
    min_import_ref = -len(imports)
    p_base, v_normal, v_u, v_v, i_light_map, _i_brush_poly = fields
    if not (texture_ref == 0 or min_import_ref <= texture_ref <= max_ref):
        return None
    if texture_ref > 0 and (texture_ref - 1) in bad_export_refs:
        texture_ref = 0
    for value in (p_base, v_normal, v_u, v_v):
        if value < 0 or value > 10000000:
            return None
    if point_count is not None and p_base >= point_count:
        return None
    if vector_count is not None and (v_normal >= vector_count or v_u >= vector_count or v_v >= vector_count):
        return None
    if i_light_map < -1 or i_light_map > 10000000:
        return None
    if surf_count is not None and i_light_map > surf_count + 1024:
        return None

    raw = bytearray(data[start:actor_end])
    if sanitize:
        tex_bytes = seekfree.write_compact_index(texture_ref)
        raw = bytearray(tex_bytes + raw[len(data[start:pos]) - (actor_end - start) - len(tex_bytes):])
        raw = bytearray(data[start:pos]) if texture_ref == seekfree.read_compact_index(data, start)[0] else raw
        raw = bytearray()
        raw += seekfree.write_compact_index(texture_ref)
        raw += data[seekfree.read_compact_index(data, start)[1]:pos]
        raw += seekfree.write_compact_index(0)
    return {
        "start": start,
        "end": actor_end,
        "raw": bytes(raw) if sanitize else data[start:actor_end],
        "i_light_map": i_light_map,
    }


def parse_bsp_surfs(data, pos, count, names, imports, exports, bad_export_refs, vector_count=None, point_count=None):
    rows = []
    for _ in range(count):
        pos = PS2_SURF_STREAM_SKIPS.get(CURRENT_MAP_NAME, {}).get(pos, pos)
        parsed = parse_bsp_surf_row(
            data, pos, names, imports, exports, bad_export_refs,
            vector_count=vector_count, point_count=point_count, surf_count=count)
        if parsed is None:
            found = None
            for probe in range(pos + 1, min(pos + 2048, len(data))):
                parsed_probe = parse_bsp_surf_row(
                    data, probe, names, imports, exports, bad_export_refs,
                    vector_count=vector_count, point_count=point_count, surf_count=count)
                if parsed_probe is not None:
                    found = parsed_probe
                    break
            if found is None:
                raise ValueError("could not parse FBspSurf at %d" % pos)
            parsed = found
        rows.append(parsed)
        pos = parsed["end"]
    return rows, pos


def parse_compact_int_array(data, pos):
    count, pos = seekfree.read_compact_index(data, pos)
    if count < 0 or count > 10000000:
        raise ValueError("bad compact int array count %d" % count)
    end = pos + count * 4
    if end > len(data):
        raise ValueError("int array overflow")
    return count, end


def parse_fixed_array(data, pos, item_size, max_count=10000000):
    count, pos = seekfree.read_compact_index(data, pos)
    if count < 0 or count > max_count:
        raise ValueError("bad fixed array count %d" % count)
    end = pos + count * item_size
    if end > len(data):
        raise ValueError("fixed array overflow")
    return count, end


def parse_lightmap_array(data, pos):
    count, pos = seekfree.read_compact_index(data, pos)
    if count < 0 or count > 10000000:
        raise ValueError("bad lightmap count %d" % count)
    max_data_offset = -1
    for _ in range(count):
        data_offset = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        pos += 12
        _uclamp, pos = seekfree.read_compact_index(data, pos)
        _vclamp, pos = seekfree.read_compact_index(data, pos)
        pos += 8
        pos += 4  # iLightActors is a raw INT.
        max_data_offset = max(max_data_offset, data_offset)
    return count, pos, max_data_offset


def parse_leaf_array(data, pos):
    count, pos = seekfree.read_compact_index(data, pos)
    if count < 0 or count > 10000000:
        raise ValueError("bad leaf count %d" % count)
    for _ in range(count):
        _zone, pos = seekfree.read_compact_index(data, pos)
        _permeating, pos = seekfree.read_compact_index(data, pos)
        _volumetric, pos = seekfree.read_compact_index(data, pos)
        pos += 8
    return count, pos


def parse_object_ref_array(data, pos):
    count, pos = seekfree.read_compact_index(data, pos)
    if count < 0 or count > 500000:
        raise ValueError("bad object ref count %d" % count)
    for _ in range(count):
        _ref, pos = seekfree.read_compact_index(data, pos)
    return count, pos


def parse_model_prefix(data, start, names, imports, exports, bad_export_refs):
    if start < 0 or start + 64 > len(data):
        raise ValueError("model prefix outside file")
    none_bytes = seekfree.write_compact_index(name_index(names, "None"))
    if data[start:start + len(none_bytes)] != none_bytes:
        raise ValueError("model does not start with UObject None tag")
    out = bytearray(data[start:start + 42])
    pos = start + 42

    array_start = pos
    vector_count, pos = parse_fixed_array(data, pos, 12)
    out += data[array_start:pos]

    array_start = pos
    point_count, pos = parse_fixed_array(data, pos, 12)
    out += data[array_start:pos]

    nodes_start = pos
    node_count, pos = seekfree.read_compact_index(data, pos)
    if node_count < 0 or node_count > 1000000:
        raise ValueError("bad node count %d" % node_count)
    for _ in range(node_count):
        pos = parse_bsp_node(data, pos)
    out += data[nodes_start:pos]

    surfs_count_pos = pos
    surf_count, pos = seekfree.read_compact_index(data, pos)
    if surf_count < 0 or surf_count > 1000000:
        raise ValueError("bad surf count %d" % surf_count)
    surf_rows, pos = parse_bsp_surfs(
        data, pos, surf_count, names, imports, exports, bad_export_refs,
        vector_count=vector_count, point_count=point_count)
    out += data[surfs_count_pos:seekfree.read_compact_index(data, surfs_count_pos)[1]]
    for row in surf_rows:
        out += row["raw"]
    max_lightmap_index = max([row["i_light_map"] for row in surf_rows] + [-1])

    verts_start = pos
    vert_count, pos = seekfree.read_compact_index(data, pos)
    if vert_count < 0 or vert_count > 10000000:
        found = None
        for probe in range(pos, min(pos + 64, len(data))):
            try:
                probe_count, probe_pos = seekfree.read_compact_index(data, probe)
            except Exception:
                continue
            if 0 <= probe_count <= 10000000:
                found = (probe_count, probe_pos)
                break
        if found is None:
            raise ValueError("bad vert count %d" % vert_count)
        vert_count, pos = found
    for _ in range(vert_count):
        _p_vertex, pos = seekfree.read_compact_index(data, pos)
        _i_side, pos = seekfree.read_compact_index(data, pos)
    out += data[verts_start:pos]

    tail_start = pos
    _shared_sides, num_zones = struct.unpack_from("<ii", data, pos)
    pos += 8
    if num_zones < 0 or num_zones > 64:
        raise ValueError("bad zone count %d" % num_zones)
    for _ in range(num_zones):
        _zone_actor, pos = seekfree.read_compact_index(data, pos)
        pos += 16
    polys_ref, polys_end = seekfree.read_compact_index(data, pos)
    out += data[tail_start:pos]
    out += seekfree.write_compact_index(0)
    pos = polys_end

    return {
        "body_prefix": bytes(out),
        "prefix_end": pos,
        "polys_ref": polys_ref,
        "max_lightmap_index": max_lightmap_index,
    }


def parse_model_post(data, pos, min_lightmap_count):
    start = pos
    lightmap_count, pos, max_data_offset = parse_lightmap_array(data, pos)
    if lightmap_count < min_lightmap_count:
        raise ValueError("lightmap count too small")
    lightbits_count, lightbits_payload = seekfree.read_compact_index(data, pos)
    if lightbits_count < 0 or lightbits_count > len(data):
        raise ValueError("bad lightbits count")
    if max_data_offset >= lightbits_count:
        raise ValueError("lightbits do not cover lightmap offsets")
    pos = lightbits_payload + lightbits_count
    _bounds_count, pos = parse_fixed_array(data, pos, 25)
    _leaf_hulls_count, pos = parse_compact_int_array(data, pos)
    _leaves_count, pos = parse_leaf_array(data, pos)
    _lights_count, pos = parse_object_ref_array(data, pos)
    if pos + 8 > len(data):
        raise ValueError("missing model root flags")
    _root_outside, _linked = struct.unpack_from("<ii", data, pos)
    pos += 8
    return data[start:pos], pos


def recover_model_body_at(data, start, export, names, imports, exports, bad_export_refs, max_shrink=0):
    prefix = parse_model_prefix(data, start, names, imports, exports, bad_export_refs)
    min_lightmap_count = 0 if prefix["max_lightmap_index"] < 0 else prefix["max_lightmap_index"] + 1
    if 0 < prefix["polys_ref"] <= len(exports):
        likely_post_start = prefix["prefix_end"] + exports[prefix["polys_ref"] - 1]["serial_size"]
        try:
            post, post_end = parse_model_post(data, likely_post_start, min_lightmap_count)
            body = prefix["body_prefix"] + post
            if len(body) <= export["serial_size"] and export["serial_size"] - len(body) <= max_shrink:
                return body, post_end, prefix
        except Exception:
            pass
    post_limit = min(prefix["prefix_end"] + 300000, len(data))
    post_count_bytes = seekfree.write_compact_index(min_lightmap_count)
    post_start = prefix["prefix_end"]
    while post_start < post_limit:
        post_start = data.find(post_count_bytes, post_start, post_limit)
        if post_start < 0:
            break
        try:
            post, post_end = parse_model_post(data, post_start, min_lightmap_count)
        except Exception:
            post_start += 1
            continue
        body = prefix["body_prefix"] + post
        if len(body) <= export["serial_size"] and export["serial_size"] - len(body) <= max_shrink:
            return body, post_end, prefix
        post_start += 1
    raise ValueError("could not recover post arrays for %s" % export["object_name"])


def find_model_body(data, export, names, imports, exports, bad_export_refs, required_polys_ref=None, preferred_start=None, scan_min=0, max_shrink=0):
    if preferred_start is not None:
        body, _end, prefix = recover_model_body_at(
            data, preferred_start, export, names, imports, exports, bad_export_refs, max_shrink=max_shrink)
        return body, prefix
    expected_polys_ref = required_polys_ref
    if expected_polys_ref is None and export["index"] + 1 < len(exports):
        expected_polys_ref = export["index"] + 2
    none_bytes = seekfree.write_compact_index(name_index(names, "None"))

    def try_start(start, expected_prefix_end=None):
        if start >= len(data) - 64:
            return None
        try:
            prefix = parse_model_prefix(data, start, names, imports, exports, bad_export_refs)
        except Exception:
            return None
        if expected_prefix_end is not None and prefix["prefix_end"] != expected_prefix_end:
            return None
        if expected_polys_ref is not None and prefix["polys_ref"] != expected_polys_ref:
            return None
        try:
            body, _end, prefix = recover_model_body_at(
                data, start, export, names, imports, exports, bad_export_refs, max_shrink=max_shrink)
        except Exception:
            return None
        return body, prefix

    if expected_polys_ref is not None:
        ref_bytes = seekfree.write_compact_index(expected_polys_ref)
        ref_pos = scan_min
        while True:
            ref_pos = data.find(ref_bytes, ref_pos)
            if ref_pos < 0:
                break
            scan_start = max(scan_min, ref_pos - min(export["serial_size"], 50000))
            candidate = data.rfind(none_bytes, scan_start, ref_pos)
            while candidate >= 0:
                found = try_start(candidate, ref_pos + len(ref_bytes))
                if found is not None:
                    return found
                candidate = data.rfind(none_bytes, scan_start, candidate)
            ref_pos += 1
    start = scan_min
    while start < len(data) - 64:
        start = data.find(none_bytes, start)
        if start < 0:
            break
        found = try_start(start)
        if found is not None:
            return found
        start += 1
    raise ValueError("could not locate model body for %s" % export["object_name"])


def extract_object_property_refs(data, names, pos, end, wanted_names):
    refs = []
    wanted = set(name.lower() for name in wanted_names)
    while pos < end:
        name_index, pos = seekfree.read_compact_index(data, pos)
        if name_index < 0 or name_index >= len(names):
            return refs
        name = names[name_index]
        if name == "None":
            return refs
        info = data[pos]
        pos += 1
        prop_type = info & 0x0f
        size_type = info & 0x70
        if prop_type == TYPE_IDS["StructProperty"]:
            _item_name, pos = seekfree.read_compact_index(data, pos)
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
            size = data[pos]
            pos += 1
        elif size_type == 0x60:
            size = struct.unpack_from("<H", data, pos)[0]
            pos += 2
        else:
            size = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        if (info & 0x80) and prop_type != TYPE_IDS["BoolProperty"]:
            b = data[pos]
            pos += 1
            if (b & 0x80) == 0:
                pass
            elif (b & 0xC0) == 0x80:
                pos += 1
            else:
                pos += 3
        value_start = pos
        if prop_type != TYPE_IDS["BoolProperty"]:
            pos += size
        if name.lower() in wanted and prop_type == TYPE_IDS["ObjectProperty"]:
            ref, ref_end = seekfree.read_compact_index(data, value_start)
            if ref_end == pos:
                refs.append(ref)
    return refs


def find_referenced_brush_models(data, names, imports, exports, stack_offsets):
    # PS2 mover UModels reference editor Polys objects.  This converter does
    # not reconstruct those objects and deliberately writes Polys=NULL in
    # every recovered model.  Attaching such a model to a Mover makes UE1's
    # FMovingBrushTracker dereference Brush->Polys during ClientInit.  Do not
    # retain mover-only models until their Polys payload can also be recovered;
    # the normal bad-export sanitation below will NULL each actor's Brush tag.
    return set()


def infer_ps2_stack_offsets(data, names, exports):
    stack_exports = [e for e in exports if e["serial_size"] and (e["object_flags"] & RF_HAS_STACK)]
    if not stack_exports:
        return {}, []
    wanted = defaultdict(list)
    for export in stack_exports:
        wanted[(export["class_index"], export["serial_size"])].append(export)
    wanted_classes = set(key[0] for key in wanted)
    chunks = defaultdict(list)
    for off in range(0, len(data) - 20):
        try:
            node, pos = seekfree.read_compact_index(data, off)
            state_node, pos = seekfree.read_compact_index(data, pos)
            if node != state_node or node not in wanted_classes:
                continue
            pos += 8 + 4
            code_offset, payload_pos = seekfree.read_compact_index(data, pos)
            if code_offset != -1:
                continue
            for key in wanted:
                wanted_class, wanted_size = key
                if wanted_class != node:
                    continue
                end = off + wanted_size
                if end <= len(data) and parse_property_stream_exact(data, names, payload_pos, end):
                    chunks[key].append(off)
        except Exception:
            pass

    inferred = {}
    unresolved = []
    for key, exports_for_key in wanted.items():
        offsets = sorted(set(chunks.get(key, [])))
        if len(offsets) < len(exports_for_key):
            unresolved.extend(e["object_name"] for e in exports_for_key[len(offsets):])
        for export, off in zip(exports_for_key, offsets):
            inferred[export["index"]] = off
    return inferred, unresolved


def parse_property_stream_exact(data, names, pos, end):
    try:
        for _ in range(4096):
            name_index, pos = seekfree.read_compact_index(data, pos)
            if name_index < 0 or name_index >= len(names):
                return False
            if names[name_index] == "None":
                return pos == end
            if pos >= end:
                return False
            info = data[pos]
            pos += 1
            prop_type = info & 0x0f
            size_type = info & 0x70
            if prop_type < 1 or prop_type > 15:
                return False
            if prop_type == 10:
                item_name, pos = seekfree.read_compact_index(data, pos)
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
                size = data[pos]
                pos += 1
            elif size_type == 0x60:
                size = struct.unpack_from("<H", data, pos)[0]
                pos += 2
            else:
                size = struct.unpack_from("<I", data, pos)[0]
                pos += 4
            if (info & 0x80) and prop_type != 3:
                b = data[pos]
                pos += 1
                if (b & 0x80) == 0:
                    pass
                elif (b & 0xC0) == 0x80:
                    pos += 1
                else:
                    pos += 3
            if prop_type != 3:
                pos += size
            if pos > end:
                return False
        return False
    except Exception:
        return False


def filter_tagged_properties(data, names, pos, end, drop_names):
    out = bytearray()
    drop = set(name.lower() for name in drop_names)
    while pos < end:
        tag_start = pos
        name_index, pos = seekfree.read_compact_index(data, pos)
        if name_index < 0 or name_index >= len(names):
            raise ValueError("bad tag name index %d" % name_index)
        name = names[name_index]
        if name == "None":
            out += data[tag_start:pos]
            return bytes(out)
        info = data[pos]
        pos += 1
        prop_type = info & 0x0f
        size_type = info & 0x70
        if prop_type == 10:
            _item_name, pos = seekfree.read_compact_index(data, pos)
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
            size = data[pos]
            pos += 1
        elif size_type == 0x60:
            size = struct.unpack_from("<H", data, pos)[0]
            pos += 2
        else:
            size = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        if (info & 0x80) and prop_type != 3:
            b = data[pos]
            pos += 1
            if (b & 0x80) == 0:
                pass
            elif (b & 0xC0) == 0x80:
                pos += 1
            else:
                pos += 3
        if prop_type != 3:
            pos += size
        if name.lower() not in drop:
            out += data[tag_start:pos]
    raise ValueError("unterminated tag stream")


def keep_tagged_properties(data, names, pos, end, keep_names):
    out = bytearray()
    keep = set(name.lower() for name in keep_names)
    while pos < end:
        tag_start = pos
        name_index, pos = seekfree.read_compact_index(data, pos)
        if name_index < 0 or name_index >= len(names):
            raise ValueError("bad tag name index %d" % name_index)
        name = names[name_index]
        if name == "None":
            out += data[tag_start:pos]
            return bytes(out)
        info = data[pos]
        pos += 1
        prop_type = info & 0x0f
        size_type = info & 0x70
        if prop_type == 10:
            _item_name, pos = seekfree.read_compact_index(data, pos)
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
            size = data[pos]
            pos += 1
        elif size_type == 0x60:
            size = struct.unpack_from("<H", data, pos)[0]
            pos += 2
        else:
            size = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        if (info & 0x80) and prop_type != 3:
            b = data[pos]
            pos += 1
            if (b & 0x80) == 0:
                pass
            elif (b & 0xC0) == 0x80:
                pos += 1
            else:
                pos += 3
        if prop_type != 3:
            pos += size
        if name.lower() in keep:
            out += data[tag_start:pos]
    raise ValueError("unterminated tag stream")


def sanitize_object_property_refs(data, names, pos, end, bad_export_indices):
    out = bytearray()
    while pos < end:
        tag_start = pos
        name_index, pos = seekfree.read_compact_index(data, pos)
        if name_index < 0 or name_index >= len(names):
            raise ValueError("bad tag name index %d" % name_index)
        if names[name_index] == "None":
            out += data[tag_start:pos]
            return bytes(out)

        name_bytes = data[tag_start:pos]
        info = data[pos]
        pos += 1
        prop_type = info & 0x0f
        size_type = info & 0x70
        struct_bytes = b""
        if prop_type == 10:
            struct_start = pos
            _item_name, pos = seekfree.read_compact_index(data, pos)
            struct_bytes = data[struct_start:pos]

        size_extra_start = pos
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
            size = data[pos]
            pos += 1
        elif size_type == 0x60:
            size = struct.unpack_from("<H", data, pos)[0]
            pos += 2
        else:
            size = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        size_extra = data[size_extra_start:pos]

        array_extra = b""
        if (info & 0x80) and prop_type != 3:
            array_start = pos
            b = data[pos]
            pos += 1
            if (b & 0x80) == 0:
                pass
            elif (b & 0xC0) == 0x80:
                pos += 1
            else:
                pos += 3
            array_extra = data[array_start:pos]

        value_start = pos
        if prop_type != 3:
            pos += size
        value = data[value_start:pos]

        if prop_type == TYPE_IDS["ObjectProperty"] and size > 0:
            try:
                ref, ref_end = seekfree.read_compact_index(value, 0)
                if (
                    ref_end == len(value)
                    and CURRENT_MAP_NAME in NULL_UNSAFE_OBJECT_PROPS_MAPS
                ):
                    replacement = CURRENT_LEVELINFO_REF if names[name_index] == "Level" else 0
                    out += name_bytes
                    out.append((info & 0x8f) | 0x00)
                    out += struct_bytes
                    out += array_extra
                    out += seekfree.write_compact_index(replacement)
                    continue
                if ref_end == len(value) and ref > 0 and (ref - 1) in bad_export_indices:
                    out += name_bytes
                    out.append((info & 0x8f) | 0x00)
                    out += struct_bytes
                    out += array_extra
                    out += seekfree.write_compact_index(0)
                    continue
            except Exception:
                pass

        out += data[tag_start:pos]
    raise ValueError("unterminated tag stream")


def class_chain_props(metadata, cls_name):
    out = []
    seen = set()
    cur = cls_name
    while cur and cur not in seen:
        seen.add(cur)
        row = metadata.get(cur)
        if not row:
            break
        for prop in row.get("properties", []):
            if prop.get("parse_error"):
                continue
            flags = prop.get("prop_flags", 0)
            if flags & (CPF_NATIVE | CPF_TRANSIENT):
                continue
            out.append(prop)
        cur = row.get("super")
    return out


def is_subclass(metadata, cls_name, base_name):
    cur = cls_name
    seen = set()
    while cur and cur not in seen:
        if cur == base_name:
            return True
        seen.add(cur)
        row = metadata.get(cur)
        if not row:
            return False
        cur = row.get("super")
    return False


def read_value_bytes(data, pos, prop):
    typ = prop["type"]
    if typ in ("ByteProperty", "BoolProperty"):
        return data[pos:pos + 1], pos + 1
    if typ in ("IntProperty", "FloatProperty"):
        return data[pos:pos + 4], pos + 4
    if typ in ("ObjectProperty", "ClassProperty", "NameProperty"):
        _value, end = seekfree.read_compact_index(data, pos)
        return data[pos:end], end
    if typ == "StrProperty":
        strlen, end = seekfree.read_compact_index(data, pos)
        if strlen < 0:
            raise ValueError("wide strings are not supported")
        return data[pos:end + strlen], end + strlen
    if typ == "StructProperty":
        struct_name = prop.get("struct")
        if struct_name == "PointRegion":
            _zone, end = seekfree.read_compact_index(data, pos)
            return data[pos:end + 5], end + 5
        size = FIXED_STRUCT_SIZES.get(struct_name)
        if size is None:
            raise ValueError("unsupported struct %s" % struct_name)
        return data[pos:pos + size], pos + size
    if typ == "ArrayProperty":
        raise ValueError("unsupported dynamic array property %s" % prop["name"])
    raise ValueError("unsupported property type %s" % typ)


def encode_array_index(index):
    if index <= 127:
        return bytes([index])
    if index <= 16383:
        return bytes([(index >> 8) + 0x80, index & 255])
    return bytes([(index >> 24) + 0xC0, (index >> 16) & 255, (index >> 8) & 255, index & 255])


def encode_tag(names, prop, value, array_index):
    prop_type = TYPE_IDS[prop["type"]]
    size = 1 if prop["type"] == "BoolProperty" else len(value)
    if size == 1:
        size_bits = 0x00
        size_extra = b""
    elif size == 2:
        size_bits = 0x10
        size_extra = b""
    elif size == 4:
        size_bits = 0x20
        size_extra = b""
    elif size == 12:
        size_bits = 0x30
        size_extra = b""
    elif size == 16:
        size_bits = 0x40
        size_extra = b""
    elif size <= 255:
        size_bits = 0x50
        size_extra = bytes([size])
    elif size <= 65536:
        size_bits = 0x60
        size_extra = struct.pack("<H", size)
    else:
        size_bits = 0x70
        size_extra = struct.pack("<I", size)

    info = prop_type | size_bits
    array_extra = b""
    if prop["type"] == "BoolProperty":
        if value and value[0]:
            info |= 0x80
    elif array_index:
        info |= 0x80
        array_extra = encode_array_index(array_index)

    out = bytearray()
    out += seekfree.write_compact_index(name_index(names, prop["name"]))
    out.append(info)
    if prop["type"] == "StructProperty":
        out += seekfree.write_compact_index(name_index(names, prop.get("struct")))
    out += size_extra
    out += array_extra
    if prop["type"] != "BoolProperty":
        out += value
    return bytes(out)


def convert_binary_properties_to_tags(data, names, metadata, cls_name, payload_pos, payload_end):
    out = bytearray()
    pos = payload_pos
    for prop in class_chain_props(metadata, cls_name):
        for array_index in range(prop.get("array_dim", 1)):
            try:
                value, pos = read_value_bytes(data, pos, prop)
            except Exception as e:
                raise ValueError("%s.%s[%d] at %d: %s" % (
                    cls_name, prop.get("name"), array_index, pos, e))
            out += encode_tag(names, prop, value, array_index)
    if pos != payload_end:
        raise ValueError("%s SerializeBin mismatch: got %d expected %d" % (cls_name, pos, payload_end))
    out += seekfree.write_compact_index(name_index(names, "None"))
    return bytes(out)


def names_needed_for_actor_properties(metadata, class_names):
    needed = set(["None"])
    for cls_name in class_names:
        for prop in class_chain_props(metadata, cls_name):
            needed.add(prop["name"])
            if prop["type"] == "StructProperty":
                needed.add(prop.get("struct"))
    return needed


def convert_ps2_package(data):
    global CLASS_METADATA, CURRENT_LEVELINFO_REF, CURRENT_SCREENSHOT_PALETTE, CURRENT_SCREENSHOT_PALETTE_REF
    CURRENT_LEVELINFO_REF = 0
    CURRENT_SCREENSHOT_PALETTE = None
    CURRENT_SCREENSHOT_PALETTE_REF = 0
    if CLASS_METADATA is None:
        CLASS_METADATA = load_class_metadata()
    fields, names, name_end, imports, _import_end, exports, export_end = seekfree.parse_seekfree(data)
    raw_name_bytes, names, imports = prepare_imports_and_names(data[:name_end], names, imports, exports)
    actor_class_names = set()
    for export in exports:
        if export["serial_size"] and (export["object_flags"] & RF_HAS_STACK):
            cls = class_name(names, imports, exports, export)
            if cls != "Model":
                actor_class_names.add(cls)
    raw_name_bytes, names = ensure_names(
        raw_name_bytes,
        names,
        names_needed_for_actor_properties(CLASS_METADATA, actor_class_names))
    raw_name_bytes, names = ensure_names(
        raw_name_bytes,
        names,
        ["Screenshot", "Palette", "Palette1", "UBits", "VBits", "USize", "VSize", "UClamp", "VClamp"])
    raw_name_bytes, normalized_name_flags = normalize_name_load_flags(raw_name_bytes, len(names))
    name_end = len(raw_name_bytes)
    stack_offsets, unresolved_stacks = infer_ps2_stack_offsets(data, names, exports)
    level_tail_info = find_level_tail(data, names, imports, exports)
    level_tail = data[level_tail_info["start"]:level_tail_info["end"]]
    needed_model_refs = set([level_tail_info["model_ref"] - 1])
    needed_model_refs.update(find_referenced_brush_models(data, names, imports, exports, stack_offsets))
    import_table = seekfree.build_import_table(imports)

    bodies = bytearray()
    offset_map = {}
    size_map = {}
    flags_map = {}
    class_map = {}
    diagnostics = {
        "dropped": [],
        "stack_unresolved": unresolved_stacks,
        "level_tail": level_tail_info,
        "recovered_models": [],
        "name_flags_normalized": normalized_name_flags,
    }
    bad_export_refs = set()
    for export in exports:
        if not export["serial_size"]:
            continue
        cls = class_name(names, imports, exports, export)
        if should_drop_early_export(cls, export):
            bad_export_refs.add(export["index"])
        elif cls in ("Model", "Polys") and export["index"] not in needed_model_refs:
            bad_export_refs.add(export["index"])
        elif export["object_flags"] & RF_HAS_STACK:
            if export["index"] not in stack_offsets:
                bad_export_refs.add(export["index"])
            elif is_static_brush_actor(cls):
                bad_export_refs.add(export["index"])
    for export in exports:
        if export["serial_size"] and export["index"] in stack_offsets:
            if class_name(names, imports, exports, export) == "LevelInfo":
                CURRENT_LEVELINFO_REF = export["index"] + 1
                break

    for export in exports:
        index = export["index"]
        size = export["serial_size"]
        if not size:
            size_map[index] = 0
            continue

        cls = class_name(names, imports, exports, export)
        old_offset = None
        new_size = size

        if export["object_name"] == "MyLevel":
            body = build_synthetic_level(names, imports, exports, level_tail, set(stack_offsets.keys()))
            new_size = len(body)
            size_map[index] = new_size
            offset_map[index] = name_end + len(bodies)
            bodies += body
            continue
        elif cls == "LevelSummary":
            levelinfo_export = None
            levelinfo_offset = None
            for candidate in exports:
                if candidate["index"] in stack_offsets and class_name(names, imports, exports, candidate) == "LevelInfo":
                    levelinfo_export = candidate
                    levelinfo_offset = stack_offsets[candidate["index"]]
                    break
            if levelinfo_export is None:
                body = seekfree.write_compact_index(name_index(names, "None"))
            else:
                body = build_level_summary_from_levelinfo(data, names, levelinfo_export, levelinfo_offset)
            new_size = len(body)
            size_map[index] = new_size
            offset_map[index] = name_end + len(bodies)
            bodies += body
            continue
        elif cls == "Texture":
            serial_offset = name_end + len(bodies)
            body = None
            if export["object_name"] == "Screenshot":
                preview = extract_ps2_frontend_preview(CURRENT_MAP_NAME)
                if preview:
                    palette_ref = 0
                    for candidate in exports[index + 1:]:
                        if class_name(names, imports, exports, candidate) == "Palette":
                            palette_ref = candidate["index"] + 1
                            break
                    if palette_ref:
                        CURRENT_SCREENSHOT_PALETTE = preview["palette"]
                        CURRENT_SCREENSHOT_PALETTE_REF = palette_ref
                    body = build_p8_texture(names, serial_offset, palette_ref, preview["pixels"])
                    diagnostics["preview"] = {
                        "source_package_offset": preview["source_package_offset"],
                        "source_texture": preview["source_texture"],
                        "source_palette": preview["source_palette"],
                        "palette_ref": palette_ref,
                    }
            if body is None:
                body = build_dummy_texture(names, serial_offset)
            new_size = len(body)
            size_map[index] = new_size
            offset_map[index] = serial_offset
            bodies += body
            continue
        elif cls == "Palette":
            if export["index"] + 1 == CURRENT_SCREENSHOT_PALETTE_REF and CURRENT_SCREENSHOT_PALETTE:
                body = build_palette_from_rgb(names, CURRENT_SCREENSHOT_PALETTE)
            else:
                body = build_empty_palette(names)
            new_size = len(body)
            size_map[index] = new_size
            offset_map[index] = name_end + len(bodies)
            bodies += body
            continue
        elif cls == "Model":
            if index in needed_model_refs:
                preferred_start = level_tail_info["end"] if index == level_tail_info["model_ref"] - 1 else None
                body, prefix = find_model_body(
                    data,
                    export,
                    names,
                    imports,
                    exports,
                    bad_export_refs,
                    required_polys_ref=index + 2,
                    preferred_start=preferred_start,
                    scan_min=max(stack_offsets.values()) if stack_offsets else export_end,
                    max_shrink=3000 if preferred_start is not None else 10)
                diagnostics["recovered_models"].append({
                    "name": export["object_name"],
                    "ref": index + 1,
                    "polys_ref": prefix["polys_ref"],
                })
            else:
                diagnostics["dropped"].append({
                    "name": export["object_name"],
                    "class": cls,
                    "old_size": size,
                    "reason": "unreferenced_model",
                })
                size_map[index] = 0
                continue
            new_size = len(body)
            size_map[index] = new_size
            offset_map[index] = name_end + len(bodies)
            bodies += body
            continue
        elif cls == "Polys":
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
                "reason": "editor_polys",
            })
            size_map[index] = 0
            continue
        elif index in stack_offsets:
            cls = class_name(names, imports, exports, export)
            if is_static_brush_actor(cls):
                diagnostics["dropped"].append({
                    "name": export["object_name"],
                    "class": cls,
                    "old_size": size,
                    "reason": "static_brush",
                })
                new_size = 0
            elif cls != "Model":
                old_offset = stack_offsets[index]
                if stack_body_payload_offset(data, export, old_offset) is None:
                    raise ValueError("bad stack frame for %s" % export["object_name"])
                payload_pos = stack_body_payload_offset(data, export, old_offset)
                if cls == "LevelInfo":
                    if CURRENT_MAP_NAME in ("CTF-Spirito", "DM-CanyonFear", "DOM-Osiris"):
                        body = data[old_offset:payload_pos] + keep_tagged_properties(
                            data, names, payload_pos, old_offset + size, ["Level"])
                    else:
                        body = data[old_offset:payload_pos] + filter_tagged_properties(
                            data, names, payload_pos, old_offset + size, ["Screenshot"])
                else:
                    body = data[old_offset:payload_pos] + sanitize_object_property_refs(
                        data, names, payload_pos, old_offset + size, bad_export_refs)
                new_size = len(body)
                size_map[index] = new_size
                offset_map[index] = name_end + len(bodies)
                bodies += body
                continue
            else:
                old_offset = stack_offsets[index]
        elif export["object_flags"] & RF_HAS_STACK:
            if cls in ("Model", "Polys") and export["serial_offset"] >= export_end and export["serial_offset"] + size <= len(data):
                old_offset = export["serial_offset"]
            else:
                diagnostics["dropped"].append({
                    "name": export["object_name"],
                    "class": cls,
                    "old_size": size,
                    "reason": "unresolved_stack",
                })
                size_map[index] = 0
                continue
        elif export["serial_offset"] >= export_end or cls == "LevelSummary":
            old_offset = export["serial_offset"]
        elif should_drop_early_export(cls, export):
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
            })
            size_map[index] = 0
            continue
        else:
            raise ValueError("no physical data for %s (%s) at logical offset %d" % (
                export["object_name"], cls, export["serial_offset"]))

        size_map[index] = new_size
        if not new_size:
            continue
        offset_map[index] = name_end + len(bodies)
        bodies += data[old_offset:old_offset + new_size]

    has_screenshot = any(
        export["object_name"].lower() == "screenshot" and class_name(names, imports, exports, export) == "Texture"
        for export in exports)
    if not has_screenshot:
        preview = extract_ps2_frontend_preview(CURRENT_MAP_NAME)
        if preview:
            texture_class_ref = import_class_ref(names, imports, "Texture")
            palette_class_ref = import_class_ref(names, imports, "Palette")

            screenshot_index = len(exports)
            palette_index = screenshot_index + 1
            palette_ref = palette_index + 1

            serial_offset = name_end + len(bodies)
            body = build_p8_texture(names, serial_offset, palette_ref, preview["pixels"])
            exports.append({
                "index": screenshot_index,
                "class_index": texture_class_ref,
                "super_index": 0,
                "package_index": 0,
                "object_name_index": name_index(names, "Screenshot"),
                "object_name": "Screenshot",
                "object_flags": 0x000F0004,
                "serial_size": len(body),
                "serial_offset": serial_offset,
            })
            size_map[screenshot_index] = len(body)
            offset_map[screenshot_index] = serial_offset
            bodies += body

            palette_body = build_palette_from_rgb(names, preview["palette"])
            palette_offset = name_end + len(bodies)
            exports.append({
                "index": palette_index,
                "class_index": palette_class_ref,
                "super_index": 0,
                "package_index": 0,
                "object_name_index": name_index(names, "Palette1"),
                "object_name": "Palette1",
                "object_flags": 0x00070004,
                "serial_size": len(palette_body),
                "serial_offset": palette_offset,
            })
            size_map[palette_index] = len(palette_body)
            offset_map[palette_index] = palette_offset
            bodies += palette_body

            diagnostics["preview"] = {
                "source_package_offset": preview["source_package_offset"],
                "source_texture": preview["source_texture"],
                "source_palette": preview["source_palette"],
                "palette_ref": palette_ref,
                "added_missing_screenshot": True,
            }

    export_table = build_export_table_with_sizes(exports, offset_map, size_map, flags_map, class_map)

    header = bytearray(raw_name_bytes)
    struct.pack_into("<I", header, 4, 69)
    struct.pack_into("<I", header, 12, len(names))
    struct.pack_into("<I", header, 20, len(exports))
    struct.pack_into("<I", header, 28, len(imports))
    import_offset = name_end + len(bodies)
    export_offset = import_offset + len(import_table)
    struct.pack_into("<I", header, 24, export_offset)
    struct.pack_into("<I", header, 32, import_offset)

    out = bytes(header) + bytes(bodies) + import_table + export_table
    diagnostics["bytes"] = len(out)
    diagnostics["imports"] = len(imports)
    diagnostics["exports"] = len(exports)
    return out, diagnostics


def build_export_table_with_sizes(exports, offset_map, size_map, flags_map, class_map):
    out = bytearray()
    for export in exports:
        size = size_map.get(export["index"], export["serial_size"])
        out += seekfree.write_compact_index(class_map.get(export["index"], export["class_index"]))
        out += seekfree.write_compact_index(export["super_index"])
        out += struct.pack("<i", export["package_index"])
        out += seekfree.write_compact_index(export["object_name_index"])
        out += struct.pack("<I", flags_map.get(export["index"], export["object_flags"]))
        out += seekfree.write_compact_index(size)
        if size:
            out += seekfree.write_compact_index(offset_map[export["index"]])
    return bytes(out)


def main():
    global CURRENT_MAP_NAME, PS2_UMD_DATA
    parser = argparse.ArgumentParser()
    parser.add_argument("--ps2-umd", default=os.path.join("!PS2", "PSX2LINS.UMD"))
    parser.add_argument("--out", default=os.path.join("ConvertedConsoleMaps", "PS2Converted"))
    parser.add_argument("--only", default=None)
    parser.add_argument("--all", action="store_true", help="convert all PS2 entries, not only known exclusives")
    args = parser.parse_args()

    if not os.path.isdir(args.out):
        os.makedirs(args.out)

    umd, entries = console_maps.discover_ps2_entries(args.ps2_umd)
    PS2_UMD_DATA = umd
    manifest = []
    converted = 0
    for entry in entries:
        name = entry["map_name"]
        if args.only and name.lower() != args.only.lower():
            continue
        if not args.all and name not in PS2_EXCLUSIVE_NAMES:
            continue
        CURRENT_MAP_NAME = name
        data = bytes(umd[entry["package_start"]:entry["package_end"]])
        out, diag = convert_ps2_package(data)
        dst = os.path.join(args.out, name + ".unr")
        with open(dst, "wb") as f:
            f.write(out)
        diag["map"] = name
        diag["path"] = dst
        manifest.append(diag)
        converted += 1
        print("%s -> %s (%d bytes, dropped=%d)" % (name, dst, len(out), len(diag["dropped"])))

    manifest_path = os.path.join(args.out, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print("converted=%d manifest=%s" % (converted, manifest_path))


if __name__ == "__main__":
    main()
