"""Read UE1 Animation exports using Engine/Inc/UnMesh.h's serialized layout."""
import struct
import numpy as np
from convert_seekfree_console_maps import read_compact_index, read_names, read_imports, read_exports
from package_properties import read_tags


def read_animation(path, name):
    data = path.read_bytes()
    h = struct.unpack_from('<9I', data)
    names, _ = read_names(data, h[3], h[4])
    imports, _ = read_imports(data, h[7], h[8])
    exports, _ = read_exports(data, h[5], h[6], names)
    exp = next(e for e in exports if e['object_name'] == name and e['class_index'] < 0
               and names[imports[-e['class_index']-1][5]] == 'Animation')
    pos = exp['serial_offset']
    end = pos + exp['serial_size']
    tags = list(read_tags(data, names, pos, end))
    if tags:
        pos = tags[-1]['end']

    def index():
        nonlocal pos
        value, pos = read_compact_index(data, pos)
        return value

    def raw(fmt):
        nonlocal pos
        size = struct.calcsize(fmt)
        if pos + size > end:
            raise ValueError('Animation exceeds export boundary')
        result = struct.unpack_from(fmt, data, pos)
        pos += size
        return result

    def array(width, fmt='f'):
        count = index()
        if not 0 <= count <= 1000000:
            raise ValueError('Invalid animation array count')
        return np.array(raw('<' + str(count*width) + fmt)).reshape(count, width)

    def track():
        return dict(flags=raw('<I')[0], quat=array(4), position=array(3), time=array(1).ravel())

    assert names[index()] == 'None'
    bones = []
    for _ in range(index()):
        bone_name = names[index()]
        flags, parent = raw('<Ii')
        bones.append(dict(name=bone_name, flags=flags, parent=parent))
    moves = []
    for _ in range(index()):
        speed = raw('<3f')
        duration, start, flags = raw('<fiI')
        indices = array(1, 'i').ravel()
        tracks = [track() for _ in range(index())]
        moves.append(dict(speed=speed, duration=duration, start=start, flags=flags,
                          indices=indices, tracks=tracks, root=track()))
    sequences = []
    for _ in range(index()):
        seq_name, group = names[index()], names[index()]
        start, frames = raw('<ii')
        notices = [(raw('<f')[0], names[index()]) for _ in range(index())]
        sequences.append(dict(name=seq_name, group=group, start=start, frames=frames,
                              notices=notices, rate=raw('<f')[0]))
    if pos != end:
        raise ValueError(f'Unexpected animation trailing bytes: {end-pos}')
    return dict(bones=bones, moves=moves, sequences=sequences)
