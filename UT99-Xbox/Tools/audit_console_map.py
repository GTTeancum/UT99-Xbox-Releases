"""Read converted UE1 maps and check navigation references without running them."""
import argparse
from pathlib import Path
import struct

import convert_seekfree_console_maps as package
import convert_ps2_seekfree_maps as ps2


from package_properties import read_tags


def read_map(path):
    data = Path(path).read_bytes()
    h = struct.unpack_from("<9I", data)
    names, _ = package.read_names(data, h[3], h[4])
    imports, _ = package.read_imports(data, h[7], h[8])
    exports, _ = package.read_exports(data, h[5], h[6], names)
    return data, names, imports, exports


def audit_navigation(path):
    data, names, imports, exports = read_map(path)
    metadata = ps2.load_class_metadata()
    level = next(e for e in exports if e["object_name"] == "MyLevel")
    _, pos = package.read_compact_index(data, level["serial_offset"])
    count, capacity = struct.unpack_from("<ii", data, pos)
    pos += 8
    actors = set()
    for _ in range(count):
        ref, pos = package.read_compact_index(data, pos)
        actors.add(ref)
    for _ in range(4):
        pos = ps2.skip_fstring(data, pos)
    options, pos = package.read_compact_index(data, pos)
    for _ in range(options):
        pos = ps2.skip_fstring(data, pos)
    pos += 8
    _, pos = package.read_compact_index(data, pos)
    count, pos = package.read_compact_index(data, pos)
    reaches = []
    for _ in range(count):
        distance = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        start, pos = package.read_compact_index(data, pos)
        end, pos = package.read_compact_index(data, pos)
        pos += 13
        reaches.append((start, end, distance))
    errors = []
    nodes = 0
    for e in exports:
        cls = ps2.class_name(names, imports, exports, e)
        if e["index"]+1 not in actors or not ps2.is_subclass(metadata, cls, "NavigationPoint"):
            continue
        nodes += 1
        pos = ps2.stack_body_payload_offset(data, e, e["serial_offset"])
        for tag in read_tags(data, names, pos, e["serial_offset"]+e["serial_size"]):
            if tag["name"].lower() in ("paths", "upstreampaths", "prunedpaths"):
                index = struct.unpack("<i", tag["value"])[0]
                if index == -1:
                    continue
                if not 0 <= index < len(reaches):
                    errors.append((e["object_name"], tag["name"], index, "out of range"))
                else:
                    r = reaches[index]
                    endpoint = r[1] if tag["name"].lower() == "upstreampaths" else r[0]
                    if endpoint != e["index"]+1:
                        errors.append((e["object_name"], tag["name"], index, "wrong endpoint", r))
            elif tag["kind"] == 5 and tag["name"].lower() == "markeditem":
                ref, _ = package.read_compact_index(tag["value"], 0)
                if ref and (ref not in actors or not ps2.is_subclass(metadata,
                        ps2.class_name(names, imports, exports, exports[ref-1]), "Inventory")):
                    errors.append((e["object_name"], "markedItem", ref, "invalid inventory"))
    return {"map": str(path), "nodes": nodes, "reachspecs": count, "errors": errors}


def audit_world_textures(path):
    data, names, imports, exports = read_map(path)
    errors = []
    embedded_surfaces = {}
    surfaces = 0
    for model in exports:
        if ps2.class_name(names, imports, exports, model) != "Model" or not model["serial_size"]:
            continue
        pos = model["serial_offset"] + 42
        _, pos = ps2.parse_fixed_array(data, pos, 12)
        _, pos = ps2.parse_fixed_array(data, pos, 12)
        count, pos = package.read_compact_index(data, pos)
        for _ in range(count):
            pos = ps2.parse_bsp_node(data, pos)
        count, pos = package.read_compact_index(data, pos)
        for surface in range(count):
            ref, _ = package.read_compact_index(data, pos)
            row = ps2.parse_bsp_surf_row(data, pos, names, imports, exports, set(), sanitize=False)
            if row is None:
                raise ValueError("Invalid surface")
            pos = row["end"]
            surfaces += 1
            if ref == 0:
                errors.append((model["object_name"], surface, "null texture"))
            elif ref > 0:
                texture = exports[ref-1]
                name = texture["object_name"]
                embedded_surfaces[name] = embedded_surfaces.get(name, 0) + 1
                tags = list(read_tags(data, names, texture["serial_offset"], texture["serial_offset"]+texture["serial_size"]))
                props = {t["name"]: t["value"] for t in tags}
                palette_ref, _ = package.read_compact_index(props.get("Palette", b"\0"), 0)
                if palette_ref <= 0:
                    errors.append((name, "missing palette"))
                    continue
                palette = exports[palette_ref-1]
                _, p = package.read_compact_index(data, palette["serial_offset"])
                colors, p = package.read_compact_index(data, p)
                if colors != 256:
                    errors.append((name, "empty palette"))
                p = tags[-1]["end"]
                _, p = package.read_compact_index(data, p)
                mips, p = package.read_compact_index(data, p)
                for _ in range(mips):
                    end = struct.unpack_from("<i", data, p)[0]
                    p += 4
                    size, p = package.read_compact_index(data, p)
                    p += size
                    width, height, ub, vb = struct.unpack_from("<iiBB", data, p)
                    if p != end or size != width * height or not size:
                        errors.append((name, "invalid mip"))
                    p += 10
    return {"map": str(path), "surfaces": surfaces, "embedded_surfaces": embedded_surfaces, "errors": errors}


if __name__ == "__main__":
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="+")
    parser.add_argument("--textures", action="store_true")
    args = parser.parse_args()
    results = [(audit_world_textures if args.textures else audit_navigation)(path) for path in args.maps]
    print(json.dumps(results, indent=2))
    raise SystemExit(any(result["errors"] for result in results))
