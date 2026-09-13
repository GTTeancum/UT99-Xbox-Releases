"""Read UE1 tagged properties, preserving array indices and byte ranges."""
import struct
import convert_seekfree_console_maps as package

def read_tags(data, names, pos, end):
    while pos < end:
        start = pos
        name, pos = package.read_compact_index(data, pos)
        if names[name] == "None":
            return
        info = data[pos]
        pos += 1
        kind = info & 15
        if kind == 10:
            _, pos = package.read_compact_index(data, pos)
        size_kind = (info >> 4) & 7
        if size_kind < 5:
            size = (1, 2, 4, 12, 16)[size_kind]
        else:
            fmt = ("<B", "<H", "<I")[size_kind - 5]
            size = struct.unpack_from(fmt, data, pos)[0]
            pos += struct.calcsize(fmt)
        index = 0
        if info & 128 and kind != 3:
            first = data[pos]
            pos += 1
            if first < 128:
                index = first
            elif first < 192:
                index = ((first & 63) << 8) | data[pos]
                pos += 1
            else:
                index = ((first & 63) << 24) | int.from_bytes(data[pos:pos+3], "big")
                pos += 3
        value_end = pos if kind == 3 else pos + size
        if value_end > end:
            raise ValueError("Property extends past export")
        yield {"name": names[name], "kind": kind, "index": index,
               "value": data[pos:value_end], "start": start, "end": value_end}
        pos = value_end
    raise ValueError("Missing property terminator")

