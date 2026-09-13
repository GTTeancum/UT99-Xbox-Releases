#!/usr/bin/env python
"""
Repack Dreamcast UT map DAT packages into ordinary PC/Xbox UE1 packages.

Dreamcast map DATs use the same broad cooked/seek-free package shape as the
PS2 maps: useful import/export metadata immediately follows the name table,
while the summary table offsets point into cooked data.  This tool reuses the
PS2 recovery primitives but keeps the entry point separate so the proven PS2
workflow stays stable.
"""

from __future__ import print_function

import argparse
import json
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_console_maps as console_maps
import convert_ps2_seekfree_maps as ps2
import convert_seekfree_console_maps as seekfree


DC_CANDIDATE_NAMES = set([
    "DM-Core",
])

DC_QUARANTINED_NAMES = set([
    "DM-Sorayama",
])

DC_UNUSED_IMPORT_ROOTS = {
    "DM-Halberd": set(["newwindow"]),
    "DM-Outskirts": set(["animfire"]),
    "DM-Pantheon": set(["animfire"]),
}

DC_IMPORT_REPARENTS = {
    "DM-Canyonfear": [
        ("BotPack.Effects", "Engine"),
    ],
    "DM-Halberd": [
        ("ShaneChurch. .Demon1", "ShaneChurch"),
        ("ShaneChurch. .Angle1", "ShaneChurch"),
        ("ShaneChurch. .Demon2", "ShaneChurch"),
        ("ShaneChurch. .Angle2", "ShaneChurch"),
    ],
    "DOM-Osiris": [
        ("BotPack.Effects", "Engine"),
    ],
}

DC_UNUSED_IMPORT_PATHS = {
    "DM-Halberd": set(["shanechurch. "]),
}


def normalize_map_name(name):
    return "".join(ch for ch in name.lower() if ch.isalnum())


def map_name_from_dc_file(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    if stem.upper().endswith("_DC"):
        stem = stem[:-3]
    parts = stem.split("_", 1)
    if len(parts) != 2:
        return stem.replace("_", "-")
    prefix, rest = parts
    return prefix.upper() + "-" + "".join(word[:1].upper() + word[1:].lower() for word in rest.split("_"))


def discover_dc_maps(dc_dir):
    out = []
    for name in sorted(os.listdir(dc_dir)):
        upper = name.upper()
        if not upper.endswith("_DC.DAT"):
            continue
        path = os.path.join(dc_dir, name)
        out.append({
            "map_name": map_name_from_dc_file(path),
            "path": path,
        })
    return out


def import_root_name(names, imports, import_index):
    index = import_index
    seen = set()
    root = None
    while index not in seen:
        seen.add(index)
        imp = imports[index]
        root = names[imp[5]]
        if imp[4] < 0:
            index = -imp[4] - 1
            continue
        break
    return root


def neutralize_unused_import_roots(map_name, names, imports):
    roots = DC_UNUSED_IMPORT_ROOTS.get(map_name)
    paths = DC_UNUSED_IMPORT_PATHS.get(map_name)
    if not roots and not paths:
        return imports, []
    core_index = ps2.name_index(names, "Core")
    package_index = ps2.name_index(names, "Package")
    out = [list(row) for row in imports]
    neutralized = []
    for i, _imp in enumerate(imports):
        root = import_root_name(names, imports, i)
        path = import_full_path(names, imports, i).lower()
        if (root and roots and root.lower() in roots) or (paths and path in paths):
            out[i][2] = core_index
            out[i][3] = package_index
            out[i][4] = 0
            out[i][5] = core_index
            neutralized.append(i + 1)
    return [tuple(row) for row in out], neutralized


def import_full_path(names, imports, import_index):
    parts = []
    index = import_index
    seen = set()
    while index not in seen:
        seen.add(index)
        imp = imports[index]
        parts.append(names[imp[5]])
        if imp[4] < 0:
            index = -imp[4] - 1
            continue
        break
    return ".".join(reversed(parts))


def reparent_imports(map_name, names, imports):
    fixes = DC_IMPORT_REPARENTS.get(map_name)
    if not fixes:
        return imports, []
    root_imports = {}
    for i, imp in enumerate(imports):
        if imp[4] == 0:
            root_imports[names[imp[5]].lower()] = i
    out = [list(row) for row in imports]
    changed = []
    for wanted_path, new_root in fixes:
        root_index = root_imports.get(new_root.lower())
        if root_index is None:
            continue
        for i, _imp in enumerate(imports):
            if import_full_path(names, imports, i).lower() == wanted_path.lower():
                out[i][4] = -(root_index + 1)
                changed.append({
                    "import": i + 1,
                    "path": wanted_path,
                    "new_root": new_root,
                })
    return [tuple(row) for row in out], changed


def parse_tag(data, names, pos, end):
    tag_start = pos
    name_index, pos = seekfree.read_compact_index(data, pos)
    if name_index < 0 or name_index >= len(names):
        raise ValueError("bad tag name index %d" % name_index)
    name = names[name_index]
    if name == "None":
        return {
            "name": name,
            "start": tag_start,
            "end": pos,
            "prop_type": None,
            "size": 0,
            "value": b"",
            "raw": data[tag_start:pos],
        }

    info = data[pos]
    pos += 1
    prop_type = info & 0x0f
    size_type = info & 0x70
    if prop_type == ps2.TYPE_IDS["StructProperty"]:
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
    if (info & 0x80) and prop_type != ps2.TYPE_IDS["BoolProperty"]:
        b = data[pos]
        pos += 1
        if (b & 0x80) == 0:
            pass
        elif (b & 0xC0) == 0x80:
            pos += 1
        else:
            pos += 3
    value_start = pos
    if prop_type != ps2.TYPE_IDS["BoolProperty"]:
        pos += size
    if pos > end:
        raise ValueError("tag stream overflow")
    return {
        "name": name,
        "start": tag_start,
        "end": pos,
        "prop_type": prop_type,
        "size": size,
        "value": data[value_start:pos],
        "raw": data[tag_start:pos],
    }


def levelinfo_tags_with_screenshot(data, names, pos, end, bad_export_indices, screenshot_ref):
    out = bytearray()
    wrote_screenshot = False
    while pos < end:
        tag = parse_tag(data, names, pos, end)
        pos = tag["end"]
        if tag["name"] == "None":
            if screenshot_ref and not wrote_screenshot:
                out += ps2.encode_tag(
                    names,
                    {"name": "Screenshot", "type": "ObjectProperty"},
                    seekfree.write_compact_index(screenshot_ref),
                    0)
                wrote_screenshot = True
            out += tag["raw"]
            return bytes(out), wrote_screenshot

        if tag["name"].lower() == "screenshot":
            if screenshot_ref:
                out += ps2.encode_tag(
                    names,
                    {"name": "Screenshot", "type": "ObjectProperty"},
                    seekfree.write_compact_index(screenshot_ref),
                    0)
                wrote_screenshot = True
            continue

        if tag["prop_type"] == ps2.TYPE_IDS["ObjectProperty"] and tag["value"]:
            try:
                ref, ref_end = seekfree.read_compact_index(tag["value"], 0)
            except Exception:
                ref, ref_end = 0, -1
            if ref_end == len(tag["value"]) and ref > 0 and (ref - 1) in bad_export_indices:
                out += ps2.encode_tag(
                    names,
                    {"name": tag["name"], "type": "ObjectProperty"},
                    seekfree.write_compact_index(0),
                    0)
                continue

        out += tag["raw"]
    raise ValueError("unterminated LevelInfo tag stream")


def find_screenshot_palette_ref(names, imports, exports, screenshot_index):
    for candidate in exports[screenshot_index + 1:]:
        if ps2.class_name(names, imports, exports, candidate) == "Palette":
            return candidate["index"] + 1
    return 0


def append_screenshot_exports(names, imports, exports, bodies, name_end, preview):
    texture_class_ref = ps2.import_class_ref(names, imports, "Texture")
    palette_class_ref = ps2.import_class_ref(names, imports, "Palette")
    screenshot_index = len(exports)
    palette_index = screenshot_index + 1
    palette_ref = palette_index + 1

    serial_offset = name_end + len(bodies)
    body = ps2.build_p8_texture(names, serial_offset, palette_ref, preview["pixels"])
    exports.append({
        "index": screenshot_index,
        "class_index": texture_class_ref,
        "super_index": 0,
        "package_index": 0,
        "object_name_index": ps2.name_index(names, "Screenshot"),
        "object_name": "Screenshot",
        "object_flags": 0x000F0004,
        "serial_size": len(body),
        "serial_offset": serial_offset,
    })
    bodies += body

    palette_body = ps2.build_palette_from_rgb(names, preview["palette"])
    palette_offset = name_end + len(bodies)
    exports.append({
        "index": palette_index,
        "class_index": palette_class_ref,
        "super_index": 0,
        "package_index": 0,
        "object_name_index": ps2.name_index(names, "Palette1"),
        "object_name": "Palette1",
        "object_flags": 0x00070004,
        "serial_size": len(palette_body),
        "serial_offset": palette_offset,
    })
    bodies += palette_body
    return screenshot_index, palette_index


def append_palette_export(names, imports, exports):
    palette_class_ref = ps2.import_class_ref(names, imports, "Palette")
    palette_index = len(exports)
    exports.append({
        "index": palette_index,
        "class_index": palette_class_ref,
        "super_index": 0,
        "package_index": 0,
        "object_name_index": ps2.name_index(names, "Palette1"),
        "object_name": "Palette1",
        "object_flags": 0x00070004,
        "serial_size": 1,
        "serial_offset": 0,
    })
    return palette_index


def fallback_preview(map_name):
    palette = []
    for i in range(256):
        if i < 64:
            palette.append((8 + i // 4, 10 + i // 5, 14 + i // 3))
        elif i < 128:
            palette.append((32 + (i - 64) // 2, 38 + (i - 64) // 3, 48 + (i - 64) // 4))
        elif i < 192:
            palette.append((64 + (i - 128), 72 + (i - 128) // 2, 84 + (i - 128) // 3))
        else:
            palette.append((128 + (i - 192), 132 + (i - 192), 140 + (i - 192)))
    pixels = bytearray()
    seed = sum(ord(ch) for ch in map_name) & 31
    for y in range(256):
        for x in range(256):
            frame = 180 if x < 3 or y < 3 or x > 252 or y > 252 else 0
            checker = 18 if ((x // 32) ^ (y // 32)) & 1 else 0
            vignette = min(80, (abs(x - 128) + abs(y - 128)) // 3)
            pixels.append(max(frame, 34 + checker + ((x + y + seed) & 31) - vignette // 2))
    return {
        "pixels": bytes(pixels),
        "palette": palette,
        "source_texture": "generated placeholder",
        "source_palette": "generated placeholder",
    }


def ensure_imported_class(names, imports, package_name, class_name):
    package_ref = None
    for i, imp in enumerate(imports):
        if imp[4] == 0 and names[imp[5]].lower() == package_name.lower():
            package_ref = -(i + 1)
            break
    if package_ref is None:
        imports = list(imports)
        imports.append((
            0,
            0,
            ps2.name_index(names, "Core"),
            ps2.name_index(names, "Package"),
            0,
            ps2.name_index(names, package_name)))
        package_ref = -len(imports)
    for i, imp in enumerate(imports):
        if imp[4] == package_ref and names[imp[5]].lower() == class_name.lower():
            return imports, -(i + 1)
    imports = list(imports)
    imports.append((
        0,
        0,
        ps2.name_index(names, "Core"),
        ps2.name_index(names, "Class"),
        package_ref,
        ps2.name_index(names, class_name)))
    return imports, -len(imports)


def parse_dc_model_post(data, pos, min_lightmap_count):
    start = pos
    lightmap_count, pos, max_data_offset = ps2.parse_lightmap_array(data, pos)
    if lightmap_count < min_lightmap_count:
        raise ValueError("lightmap count too small")
    lightbits_count, lightbits_payload = seekfree.read_compact_index(data, pos)
    if lightbits_count < 0 or lightbits_count > len(data):
        raise ValueError("bad lightbits count")
    # Some DC maps use max_data_offset == LightBits.Num(); UE's bit reader never
    # consumes that sentinel byte, so accept equality here.  The stricter PS2
    # helper keeps its original bounds check.
    if max_data_offset > lightbits_count:
        raise ValueError("lightbits do not cover lightmap offsets")
    pos = lightbits_payload + lightbits_count
    _bounds_count, pos = ps2.parse_fixed_array(data, pos, 25)
    _leaf_hulls_count, pos = ps2.parse_compact_int_array(data, pos)
    _leaves_count, pos = ps2.parse_leaf_array(data, pos)
    _lights_count, pos = ps2.parse_object_ref_array(data, pos)
    if pos + 8 > len(data):
        raise ValueError("missing model root flags")
    pos += 8
    return data[start:pos], pos


def find_dc_model_body(data, export, names, imports, exports, bad_export_refs, level_tail_info, stack_offsets, export_end):
    try:
        return ps2.find_model_body(
            data,
            export,
            names,
            imports,
            exports,
            bad_export_refs,
            required_polys_ref=export["index"] + 2,
            preferred_start=level_tail_info["end"],
            scan_min=max(stack_offsets.values()) if stack_offsets else export_end,
            max_shrink=3000)
    except ValueError as strict_error:
        prefix = ps2.parse_model_prefix(data, level_tail_info["end"], names, imports, exports, bad_export_refs)
        min_lightmap_count = 0 if prefix["max_lightmap_index"] < 0 else prefix["max_lightmap_index"] + 1
        if not (0 < prefix["polys_ref"] <= len(exports)):
            raise strict_error
        post_start = prefix["prefix_end"] + exports[prefix["polys_ref"] - 1]["serial_size"]
        post, _post_end = parse_dc_model_post(data, post_start, min_lightmap_count)
        body = prefix["body_prefix"] + post
        if len(body) > export["serial_size"] or export["serial_size"] - len(body) > 3000:
            raise strict_error
        return body, prefix


def convert_dc_package(data, map_name):
    ps2.CURRENT_MAP_NAME = map_name
    ps2.CURRENT_LEVELINFO_REF = 0
    ps2.CURRENT_SCREENSHOT_PALETTE = None
    ps2.CURRENT_SCREENSHOT_PALETTE_REF = 0
    if ps2.CLASS_METADATA is None:
        ps2.CLASS_METADATA = ps2.load_class_metadata()

    fields, names, name_end, imports, _import_end, exports, export_end = seekfree.parse_seekfree(data)
    raw_name_bytes, names, imports = ps2.prepare_imports_and_names(data[:name_end], names, imports, exports)
    raw_name_bytes, names = ps2.ensure_names(
        raw_name_bytes,
        names,
        ["Core", "Engine", "Class", "Package", "Texture", "Screenshot", "Palette", "Palette1", "UBits", "VBits", "USize", "VSize", "UClamp", "VClamp"])
    imports, _texture_class_ref = ensure_imported_class(names, imports, "Engine", "Texture")
    imports, _palette_class_ref = ensure_imported_class(names, imports, "Engine", "Palette")
    imports, reparented_imports = reparent_imports(map_name, names, imports)
    imports, neutralized_imports = neutralize_unused_import_roots(map_name, names, imports)
    raw_name_bytes, normalized_name_flags = ps2.normalize_name_load_flags(raw_name_bytes, len(names))
    name_end = len(raw_name_bytes)

    stack_offsets, unresolved_stacks = ps2.infer_ps2_stack_offsets(data, names, exports)
    level_tail_info = ps2.find_level_tail(data, names, imports, exports)
    level_tail = data[level_tail_info["start"]:level_tail_info["end"]]
    needed_model_refs = set([level_tail_info["model_ref"] - 1])

    bad_export_refs = set()
    for export in exports:
        if not export["serial_size"]:
            continue
        cls = ps2.class_name(names, imports, exports, export)
        if ps2.should_drop_early_export(cls, export):
            bad_export_refs.add(export["index"])
        elif cls in ("Model", "Polys") and export["index"] not in needed_model_refs:
            bad_export_refs.add(export["index"])
        elif export["object_flags"] & ps2.RF_HAS_STACK:
            if export["index"] not in stack_offsets:
                bad_export_refs.add(export["index"])
            elif ps2.is_static_brush_actor(cls):
                bad_export_refs.add(export["index"])

    for export in exports:
        if export["serial_size"] and export["index"] in stack_offsets:
            if ps2.class_name(names, imports, exports, export) == "LevelInfo":
                ps2.CURRENT_LEVELINFO_REF = export["index"] + 1
                break

    preview = ps2.load_preview_override(map_name) or fallback_preview(map_name)
    screenshot_ref = 0
    existing_screenshot = None
    reused_screenshot_index = None
    reused_palette_index = None
    for export in exports:
        if export["object_name"].lower() == "screenshot" and ps2.class_name(names, imports, exports, export) == "Texture":
            export["package_index"] = 0
            export["object_flags"] = 0x000F0004
            existing_screenshot = export
            screenshot_ref = export["index"] + 1
            break

    if not existing_screenshot and preview and map_name == "DM-Sorayama":
        for export in exports:
            if ps2.class_name(names, imports, exports, export) == "Texture":
                reused_screenshot_index = export["index"]
                export["object_name_index"] = ps2.name_index(names, "Screenshot")
                export["object_name"] = "Screenshot"
                export["package_index"] = 0
                export["object_flags"] = 0x000F0004
                existing_screenshot = export
                screenshot_ref = export["index"] + 1
                break
        for export in exports:
            if ps2.class_name(names, imports, exports, export) == "Palette":
                reused_palette_index = export["index"]
                export["object_name_index"] = ps2.name_index(names, "Palette1")
                export["object_name"] = "Palette1"
                export["package_index"] = 0
                export["object_flags"] = 0x00070004
                break

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
        "preview": None,
        "neutralized_imports": neutralized_imports,
        "reparented_imports": reparented_imports,
        "name_flags_normalized": normalized_name_flags,
    }

    if not existing_screenshot and preview:
        screenshot_ref = len(exports) + 1

    appended_palette_index = None
    if existing_screenshot and preview:
        existing_palette_ref = (
            reused_palette_index + 1
            if reused_palette_index is not None
            else find_screenshot_palette_ref(names, imports, exports, existing_screenshot["index"])
        )
        if not existing_palette_ref:
            appended_palette_index = append_palette_export(names, imports, exports)

    for export in exports:
        index = export["index"]
        size = export["serial_size"]
        if not size:
            size_map[index] = 0
            continue

        cls = ps2.class_name(names, imports, exports, export)
        if map_name == "DM-Sorayama" and cls == "Package" and export["object_name"] == "Secret":
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
                "reason": "unused_group_package",
            })
            flags_map[index] = 0
            size_map[index] = 0
            continue
        if export["object_name"] == "MyLevel":
            old_current_map_name = ps2.CURRENT_MAP_NAME
            if map_name == "DOM-Osiris":
                ps2.CURRENT_MAP_NAME = None
            try:
                body = ps2.build_synthetic_level(names, imports, exports, level_tail, set(stack_offsets.keys()))
            finally:
                ps2.CURRENT_MAP_NAME = old_current_map_name
            size_map[index] = len(body)
            offset_map[index] = name_end + len(bodies)
            bodies += body
        elif cls == "LevelSummary":
            levelinfo_export = None
            levelinfo_offset = None
            for candidate in exports:
                if candidate["index"] in stack_offsets and ps2.class_name(names, imports, exports, candidate) == "LevelInfo":
                    levelinfo_export = candidate
                    levelinfo_offset = stack_offsets[candidate["index"]]
                    break
            if levelinfo_export is None:
                body = seekfree.write_compact_index(ps2.name_index(names, "None"))
            else:
                body = ps2.build_level_summary_from_levelinfo(data, names, levelinfo_export, levelinfo_offset)
            size_map[index] = len(body)
            offset_map[index] = name_end + len(bodies)
            bodies += body
        elif cls == "Texture":
            serial_offset = name_end + len(bodies)
            body = None
            if export["object_name"].lower() == "screenshot" and preview:
                if reused_palette_index is not None:
                    palette_ref = reused_palette_index + 1
                elif appended_palette_index is not None:
                    palette_ref = appended_palette_index + 1
                else:
                    palette_ref = find_screenshot_palette_ref(names, imports, exports, index)
                if palette_ref:
                    ps2.CURRENT_SCREENSHOT_PALETTE = preview["palette"]
                    ps2.CURRENT_SCREENSHOT_PALETTE_REF = palette_ref
                body = ps2.build_p8_texture(names, serial_offset, palette_ref, preview["pixels"])
                diagnostics["preview"] = {
                    "source_texture": preview["source_texture"],
                    "source_palette": preview["source_palette"],
                    "palette_ref": palette_ref,
                }
            if body is None:
                body = ps2.build_dummy_texture(names, serial_offset)
            size_map[index] = len(body)
            offset_map[index] = serial_offset
            bodies += body
        elif cls == "Palette":
            if (
                (export["index"] + 1 == ps2.CURRENT_SCREENSHOT_PALETTE_REF and ps2.CURRENT_SCREENSHOT_PALETTE)
                or (reused_palette_index is not None and export["index"] == reused_palette_index and preview)
                or (appended_palette_index is not None and export["index"] == appended_palette_index and preview)
            ):
                if reused_palette_index is not None and export["index"] == reused_palette_index:
                    ps2.CURRENT_SCREENSHOT_PALETTE_REF = export["index"] + 1
                    ps2.CURRENT_SCREENSHOT_PALETTE = preview["palette"]
                if appended_palette_index is not None and export["index"] == appended_palette_index:
                    ps2.CURRENT_SCREENSHOT_PALETTE_REF = export["index"] + 1
                    ps2.CURRENT_SCREENSHOT_PALETTE = preview["palette"]
                export["package_index"] = 0
                export["object_flags"] = 0x00070004
                body = ps2.build_palette_from_rgb(names, ps2.CURRENT_SCREENSHOT_PALETTE)
            else:
                body = ps2.build_empty_palette(names)
            size_map[index] = len(body)
            offset_map[index] = name_end + len(bodies)
            bodies += body
        elif cls == "Model":
            if index not in needed_model_refs:
                diagnostics["dropped"].append({
                    "name": export["object_name"],
                    "class": cls,
                    "old_size": size,
                    "reason": "unreferenced_model",
                })
                size_map[index] = 0
                continue
            body, prefix = find_dc_model_body(
                data,
                export,
                names,
                imports,
                exports,
                bad_export_refs,
                level_tail_info,
                stack_offsets,
                export_end)
            diagnostics["recovered_models"].append({
                "name": export["object_name"],
                "ref": index + 1,
                "polys_ref": prefix["polys_ref"],
            })
            size_map[index] = len(body)
            offset_map[index] = name_end + len(bodies)
            bodies += body
        elif cls == "Polys":
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
                "reason": "editor_polys",
            })
            size_map[index] = 0
        elif index in stack_offsets:
            if ps2.is_static_brush_actor(cls):
                diagnostics["dropped"].append({
                    "name": export["object_name"],
                    "class": cls,
                    "old_size": size,
                    "reason": "static_brush",
                })
                size_map[index] = 0
                continue
            old_offset = stack_offsets[index]
            payload_pos = ps2.stack_body_payload_offset(data, export, old_offset)
            if payload_pos is None:
                raise ValueError("bad stack frame for %s" % export["object_name"])
            if cls == "LevelInfo":
                tags, wrote_screenshot = levelinfo_tags_with_screenshot(
                    data, names, payload_pos, old_offset + size, bad_export_refs, screenshot_ref)
                body = data[old_offset:payload_pos] + tags
                diagnostics["levelinfo_screenshot"] = {
                    "ref": screenshot_ref,
                    "written": wrote_screenshot,
                }
            else:
                old_current_map_name = ps2.CURRENT_MAP_NAME
                if map_name == "DOM-Osiris":
                    ps2.CURRENT_MAP_NAME = None
                try:
                    sanitized_tags = ps2.sanitize_object_property_refs(
                        data, names, payload_pos, old_offset + size, bad_export_refs)
                finally:
                    ps2.CURRENT_MAP_NAME = old_current_map_name
                body = data[old_offset:payload_pos] + sanitized_tags
            size_map[index] = len(body)
            offset_map[index] = name_end + len(bodies)
            bodies += body
        elif export["object_flags"] & ps2.RF_HAS_STACK:
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
                "reason": "unresolved_stack",
            })
            size_map[index] = 0
        else:
            diagnostics["dropped"].append({
                "name": export["object_name"],
                "class": cls,
                "old_size": size,
                "reason": "no_physical_body",
            })
            size_map[index] = 0

    if not existing_screenshot and preview:
        screenshot_index, palette_index = append_screenshot_exports(names, imports, exports, bodies, name_end, preview)
        offset_map[screenshot_index] = exports[screenshot_index]["serial_offset"]
        size_map[screenshot_index] = exports[screenshot_index]["serial_size"]
        offset_map[palette_index] = exports[palette_index]["serial_offset"]
        size_map[palette_index] = exports[palette_index]["serial_size"]
        diagnostics["preview"] = {
            "source_texture": preview["source_texture"],
            "source_palette": preview["source_palette"],
            "palette_ref": palette_index + 1,
            "added_missing_screenshot": True,
        }

    import_table = seekfree.build_import_table(imports)
    export_table = ps2.build_export_table_with_sizes(exports, offset_map, size_map, flags_map, class_map)

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dc-dir", default="!DC")
    parser.add_argument("--out", default=os.path.join("ConvertedConsoleMaps", "DCConverted"))
    parser.add_argument("--deploy", default=None)
    parser.add_argument("--deploy-mode", choices=("overwrite", "missing"), default="overwrite")
    parser.add_argument("--only", default=None)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--skip-quarantined", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    args = parser.parse_args()

    if not os.path.isdir(args.out):
        os.makedirs(args.out)

    wanted = normalize_map_name(args.only) if args.only else None
    manifest = []
    converted = 0
    for entry in discover_dc_maps(args.dc_dir):
        map_name = entry["map_name"]
        if wanted and normalize_map_name(map_name) != wanted:
            continue
        if args.skip_quarantined and map_name in DC_QUARANTINED_NAMES:
            manifest.append({
                "map": map_name,
                "source": entry["path"],
                "status": "skipped",
                "reason": "quarantined",
            })
            print("%s -> skipped (quarantined)" % map_name)
            continue
        if not args.all and map_name not in DC_CANDIDATE_NAMES:
            continue
        try:
            data = open(entry["path"], "rb").read()
            out, diag = convert_dc_package(data, map_name)
            valid, reason = console_maps.validate_pc_package(out)
            if not valid:
                raise ValueError("%s did not validate: %s" % (map_name, reason))
            dst = os.path.join(args.out, map_name + ".unr")
            with open(dst, "wb") as f:
                f.write(out)
            deployed = None
            deploy_reason = None
            if args.deploy:
                if not os.path.isdir(args.deploy):
                    os.makedirs(args.deploy)
                deployed = os.path.join(args.deploy, os.path.basename(dst))
                if args.deploy_mode == "missing" and os.path.exists(deployed):
                    deploy_reason = "exists"
                    deployed = None
                else:
                    shutil.copy2(dst, deployed)
                    deploy_reason = "copied"
            diag["map"] = map_name
            diag["source"] = entry["path"]
            diag["path"] = dst
            diag["valid"] = reason
            diag["status"] = "converted"
            diag["deployed"] = deployed
            diag["deploy_reason"] = deploy_reason
            manifest.append(diag)
            converted += 1
            deploy_text = ""
            if args.deploy:
                deploy_text = ", deploy=%s" % deploy_reason
            print("%s -> %s (%d bytes, dropped=%d%s)" % (map_name, dst, len(out), len(diag["dropped"]), deploy_text))
        except Exception as exc:
            if not args.keep_going:
                raise
            manifest.append({
                "map": map_name,
                "source": entry["path"],
                "status": "failed",
                "reason": str(exc),
            })
            print("%s -> failed (%s)" % (map_name, exc))

    manifest_path = os.path.join(args.out, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print("converted=%d manifest=%s" % (converted, manifest_path))


if __name__ == "__main__":
    main()
