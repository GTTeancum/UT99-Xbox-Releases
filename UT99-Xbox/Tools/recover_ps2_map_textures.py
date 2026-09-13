"""Recover verified embedded world textures from the original PS2 map cook.

Offsets are pinned to complete source-package hashes, not searched heuristically.
Mip lazy-array end offsets retain their original PC serialization coordinates;
the final offset plus the ten-byte mip dimensions identifies the texture export.
The PS2 stream omits mip zero's array entirely, retaining its skip and dimensions.
Palettes are ordinary UPalette serialization (None, count, 256 FColors).
"""
import hashlib
import struct

import convert_seekfree_console_maps as package
from package_properties import read_tags


SOURCES = {
    "CTF-Phalanx": {
        "sha256": "3ab35b13b5de485d7ec60a31fbb6e0f62d2522705dcfc2649ea05bc2c47e0e58",
        # name: (Palette tag physical offset, UPalette physical offset)
        "textures": {
            "WALL222": (983535, 1957058),
            "humptywall21": (1012300, 1962287),
            "wall3phal": (1034354, 1963314),
            "r21edvall": (1058894, 1964341),
            "blah11": (1083689, 1965368),
        },
    },
    "CTF-Sundial": {
        "sha256": "ecdbe00ea8f4377cc9ccb9c329163861cd4cf18a2b6e0d36e5e1b22889da8fac",
        "textures": {
            "A_bs_ps3": (1327918, 2241833),
            "A_bs_ps6": (1333571, 2242860),
        },
    },
}


def recover_textures(map_name, data, names, exports):
    source = SOURCES.get(map_name)
    if source is None:
        return {}, {}
    if hashlib.sha256(data).hexdigest() != source["sha256"]:
        raise ValueError("Unrecognized PS2 source for " + map_name)
    textures, palettes = {}, {}
    for name, (start, palette_start) in source["textures"].items():
        export = next(e for e in exports if e["object_name"] == name)
        tags = list(read_tags(data, names, start, start + 1024))
        props = {t["name"]: t["value"] for t in tags}
        palette_ref, _ = package.read_compact_index(props["Palette"], 0)
        if not 0 < palette_ref <= len(exports):
            raise ValueError("Invalid embedded palette reference")
        pos = tags[-1]["end"]
        none, pos = package.read_compact_index(data, pos)
        count, pos = package.read_compact_index(data, pos)
        if names[none] != "None" or not 2 <= count <= 12:
            raise ValueError("Invalid texture mip header")
        mips = []
        original_size = None
        previous_skip = None
        for i in range(count):
            skip = struct.unpack_from("<i", data, pos)[0]
            pos += 4
            pixels = b""
            if i:
                size, pos = package.read_compact_index(data, pos)
                if size < 1 or size > 1048576:
                    raise ValueError("Invalid mip data length")
                pixels = data[pos:pos+size]
                pos += size
                expected_skip = previous_skip + 10 + 4 + len(package.write_compact_index(size)) + size
                if skip != expected_skip:
                    raise ValueError("Mip lazy offset sequence mismatch")
            width, height, ubits, vbits = struct.unpack_from("<iiBB", data, pos)
            pos += 10
            if ubits > 10 or vbits > 10 or width != 1 << ubits or height != 1 << vbits:
                raise ValueError("Invalid mip dimensions")
            if i:
                if len(pixels) != width * height:
                    raise ValueError("Not a linear P8 mip")
                mips.append((width, height, ubits, vbits, pixels))
            else:
                original_size = (width, height)
            previous_skip = skip
        if skip + 10 != export["serial_offset"] + export["serial_size"]:
            raise ValueError("Mip chain does not belong to " + name)
        none, p = package.read_compact_index(data, palette_start)
        colors, p = package.read_compact_index(data, p)
        if names[none] != "None" or colors != 256:
            raise ValueError("Invalid UPalette")
        palette = data[p:p+1024]
        if len(palette) != 1024 or any(a not in (0, 255) for a in palette[3::4]):
            raise ValueError("Invalid palette colors")
        # Source MaxColor supplies an independent check on palette assignment.
        if bytes(max(palette[channel::4]) for channel in range(3)) != props["MaxColor"][:3]:
            raise ValueError("Texture/palette MaxColor mismatch")
        palettes[palette_ref - 1] = data[palette_start:p+1024]
        textures[export["index"]] = {
            "name": name, "palette_ref": palette_ref, "palette": palette,
            "mips": mips, "original_size": original_size,
            "max_color": props["MaxColor"], "mip_zero": props["MipZero"],
            "source_offset": start, "palette_offset": palette_start,
        }
    return textures, palettes


def build_texture(texture, names, serial_offset):
    import convert_ps2_seekfree_maps as ps2
    width, height, ubits, vbits, _ = texture["mips"][0]
    out = bytearray()
    for name, kind, value in (
        ("Palette", "ObjectProperty", package.write_compact_index(texture["palette_ref"])),
        ("UBits", "ByteProperty", bytes([ubits])),
        ("VBits", "ByteProperty", bytes([vbits])),
        ("USize", "IntProperty", struct.pack("<i", width)),
        ("VSize", "IntProperty", struct.pack("<i", height)),
        ("UClamp", "IntProperty", struct.pack("<i", width)),
        ("VClamp", "IntProperty", struct.pack("<i", height)),
        # UTexture::Lock multiplies BSP UV scale by Scale. Keeping the cooked
        # half-resolution mip requires doubling Scale to preserve world tiling.
        ("Scale", "FloatProperty", struct.pack("<f", texture["original_size"][0] / float(width))),
    ):
        out += ps2.encode_tag(names, {"name": name, "type": kind}, value, 0)
    for name, value in (("MaxColor", texture["max_color"]), ("MipZero", texture["mip_zero"])):
        out += ps2.encode_tag(names, {"name": name, "type": "StructProperty", "struct": "Color"}, value, 0)
    out += package.write_compact_index(ps2.name_index(names, "None"))
    return bytes(ps2.append_texture_mips(out, serial_offset, texture["mips"]))
