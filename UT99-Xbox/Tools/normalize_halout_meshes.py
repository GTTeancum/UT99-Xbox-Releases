"""Convert 469e's reserved WORD in FMeshExtWedge to the console UE1 layout.

469e SDK Engine/Inc/UnMesh.h serializes iVertex, Flags, U, V (12 bytes).
The Xbox and original console packages serialize iVertex, U, V (10 bytes).
Keep each export at its original offset so its earlier lazy arrays remain valid.
"""
import struct
import math
from convert_seekfree_console_maps import read_compact_index, write_compact_index, read_names, read_imports, read_exports
from package_properties import read_tags


def normalize(path):
    data = bytearray(path.read_bytes())
    header = list(struct.unpack_from('<9I', data))
    if header[1] != 69:
        raise ValueError('Expected UE1 package version 69')
    names, _ = read_names(data, header[3], header[4])
    imports, _ = read_imports(data, header[7], header[8])
    exports, _ = read_exports(data, header[5], header[6], names)
    report = []
    for exp in exports:
        ci = exp['class_index']
        if ci >= 0 or names[imports[-ci - 1][5]] != 'SkeletalMesh':
            continue
        start = exp['serial_offset']
        end = start + exp['serial_size']
        tags = list(read_tags(data, names, start, end))
        pos = tags[-1]['end'] if tags else start
        none, pos = read_compact_index(data, pos)
        assert names[none] == 'None'
        pos += 41  # UPrimitive bounding box and sphere

        def index():
            nonlocal pos
            value, pos = read_compact_index(data, pos)
            return value

        def array(size):
            nonlocal pos
            count = index()
            if not 0 <= count <= 1000000:
                raise ValueError('Invalid array count')
            pos += count * size
            return count

        def lazy():
            nonlocal pos
            target = struct.unpack_from('<i', data, pos)[0]
            if not pos < target <= end:
                raise ValueError('Invalid lazy array endpoint')
            pos = target

        lazy()  # Verts
        lazy()  # Tris
        for _ in range(index()):  # AnimSeqs
            index(); index()
            pos += 8
            for _ in range(index()):
                pos += 4
                index()
            pos += 4
        lazy()  # Connects
        pos += 41
        lazy()  # VertLinks
        for _ in range(index()):
            index()  # Texture object references
        array(25); array(16)
        pos += 60
        array(4)  # TextureLOD
        for size in (2, 2, 8, 2, 4, 8, 8):
            array(size)
        pos += 32
        array(2)
        pos += 4
        count_start = pos
        count = index()
        wedge_start = pos
        compact = bytearray(data[start:wedge_start])
        reserved_nonzero = 0
        for i in range(count):
            vertex, flags, u, v = struct.unpack_from('<HHff', data, pos)
            # 469e labels this WORD reserved; its importer leaves some values
            # uninitialized. It is not a material or vertex index.
            reserved_nonzero += bool(flags)
            if not math.isfinite(u) or not math.isfinite(v):
                raise ValueError('Invalid extended wedge UV')
            compact += struct.pack('<Hff', vertex, u, v)
            pos += 12
        points, _ = read_compact_index(data, pos)
        if not 0 < points <= 65535 or count < points:
            raise ValueError('Invalid skeletal point/wedge counts')
        skeletal_start = pos
        array(12)
        bones = index()
        if not 0 < bones <= 256:
            raise ValueError('Invalid reference skeleton')
        for _ in range(bones):
            bone_name = index()
            if not 0 <= bone_name < len(names):
                raise ValueError('Invalid bone name')
            pos += 56
        if array(8) != bones:
            raise ValueError('Influence index count does not match skeleton')
        weights = array(4)
        local_points = array(12)
        if local_points not in (0, weights):
            raise ValueError('Unexpected local-point count')
        pos += 4  # SkeletalDepth
        index()   # UAnimation reference
        pos += 52 # WeaponBoneIndex and FCoords
        if pos != end:
            raise ValueError('Skeletal export did not end at its declared boundary')
        compact += data[skeletal_start:end]
        data[start:start + len(compact)] = compact
        exp['serial_size'] = len(compact)
        report.append({'mesh': exp['object_name'], 'wedges': count, 'points': points,
                       'wedge_offset': count_start, 'removed_reserved_bytes': count * 2,
                       'nonzero_reserved_words': reserved_nonzero})
    header[6] = len(data)
    for exp in exports:
        data += write_compact_index(exp['class_index']) + write_compact_index(exp['super_index'])
        data += struct.pack('<i', exp['package_index']) + write_compact_index(exp['object_name_index'])
        data += struct.pack('<I', exp['object_flags']) + write_compact_index(exp['serial_size'])
        if exp['serial_size']:
            data += write_compact_index(exp['serial_offset'])
    struct.pack_into('<9I', data, 0, *header)
    path.write_bytes(data)
    return report
