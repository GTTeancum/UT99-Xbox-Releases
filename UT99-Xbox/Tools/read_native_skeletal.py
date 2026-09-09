"""Read the console-compatible UE1 skeletal data for conversion diagnostics."""
import struct
import numpy as np
from convert_seekfree_console_maps import read_compact_index, read_names, read_imports, read_exports
from package_properties import read_tags


def read_mesh(path, mesh_name):
    data = path.read_bytes()
    header = struct.unpack_from('<9I', data)
    names, _ = read_names(data, header[3], header[4])
    imports, _ = read_imports(data, header[7], header[8])
    exports, _ = read_exports(data, header[5], header[6], names)
    exp = next(e for e in exports if e['object_name'] == mesh_name
               and e['class_index'] < 0 and names[imports[-e['class_index']-1][5]] == 'SkeletalMesh')
    start, end = exp['serial_offset'], exp['serial_offset']+exp['serial_size']
    tags = list(read_tags(data, names, start, end))
    pos = tags[-1]['end'] if tags else start

    def index():
        nonlocal pos
        value, pos = read_compact_index(data, pos)
        return value

    def raw(size):
        nonlocal pos
        if pos+size > end:
            raise ValueError('Skeletal data exceeds export boundary')
        value = data[pos:pos+size]
        pos += size
        return value

    def array(size):
        count = index()
        if not 0 <= count <= 1000000:
            raise ValueError('Invalid array count')
        return raw(count*size)

    def lazy():
        nonlocal pos
        target = struct.unpack_from('<i', data, pos)[0]
        if not pos < target <= end:
            raise ValueError('Invalid lazy endpoint')
        pos = target

    assert names[index()] == 'None'
    raw(41)
    lazy(); lazy()
    for _ in range(index()):
        index(); index(); raw(8)
        for _ in range(index()):
            raw(4); index()
        raw(4)
    lazy(); raw(41); lazy()
    for _ in range(index()):
        index()
    array(25); array(16)
    raw(16)
    scale = struct.unpack('<3f', raw(12))
    origin = struct.unpack('<3f', raw(12))
    rotation = struct.unpack('<3i', raw(12))
    raw(8); array(4)
    array(2); array(2)
    faces = np.frombuffer(array(8), dtype='<u2').reshape(-1,4).copy()
    array(2); array(4)
    materials = np.frombuffer(array(8), dtype='<u4').reshape(-1,2).copy()
    array(8)
    raw(32); array(2); raw(4); array(10)
    points = np.frombuffer(array(12), dtype='<f4').reshape(-1,3).copy()
    bones = []
    for _ in range(index()):
        name = names[index()]
        b = raw(56)
        bones.append({'name':name, 'quat':struct.unpack_from('<4f',b,4),
                      'position':struct.unpack_from('<3f',b,20),
                      'parent':struct.unpack_from('<i',b,52)[0]})
    influence_spans = np.frombuffer(array(8), dtype='<i4').reshape(-1,2).copy()
    influences = np.frombuffer(array(4), dtype='<u2').reshape(-1,2).copy()
    local_points = np.frombuffer(array(12), dtype='<f4').reshape(-1,3).copy()
    raw(4); animation = index()
    bone = struct.unpack('<i',raw(4))[0]
    adjustment = struct.unpack('<12f',raw(48))
    if pos != end:
        raise ValueError('Unexpected trailing skeletal data')
    return dict(scale=scale,origin=origin,rotation=rotation,points=points,bones=bones,materials=materials,faces=faces,
                influence_spans=influence_spans,influences=influences,local_points=local_points,
                animation=animation,weapon_bone=bone,weapon_adjustment=adjustment)
