"""Verify authored aim elevation and grip preservation using the real mesh hierarchy."""
import pathlib
import struct
import numpy as np
from scipy.spatial.transform import Rotation
from build_halout import chunks
from halout_aim_pose import aimed_frame

base = pathlib.Path(__file__).resolve().parents[1] / 'build_cli/halout_source/compiler469e'
anim = chunks(base / 'HaloUTExport/HaloCreatures/MeshAnimation/EliteAni.psa')
mesh = chunks(base / 'HaloUTXbox/Models/Elite.psk')
_, size, count, data = anim['ANIMINFO']
row = next(data[i*size:(i+1)*size] for i in range(count)
           if data[i*size:i*size+64].split(b'\0')[0] == b'Rifle_Aimed')
frame = struct.unpack_from('<i', row, 160)[0]
_, size, count, data = anim['BONENAMES']
tracks = [data[i*size:i*size+64].split(b'\0')[0].decode().lower() for i in range(count)]
_, size, bones, data = mesh['REFSKELT']
names = [data[i*size:i*size+64].split(b'\0')[0].decode().lower() for i in range(bones)]
parents = [struct.unpack_from('<i', data, i*size+72)[0] for i in range(bones)]
indices = [tracks.index(name) for name in names]
_, key_size, _, keys = anim['ANIMKEYS']
original = keys[frame*count*key_size:(frame+1)*count*key_size]

def evaluate(keys):
    transforms = []
    for i, track in enumerate(indices):
        position = struct.unpack_from('<3f', keys, track*key_size)
        quat = struct.unpack_from('<4f', keys, track*key_size+12)
        local = np.eye(4)
        local[:3, :3] = Rotation.from_quat(quat).as_matrix()
        if i:
            local[:3, :3] = local[:3, :3].T
        local[:3, 3] = position
        transforms.append(transforms[parents[i]] @ local if i else local)
    return transforms

before = evaluate(original)
socket = names.index('bone_weapon')
hand = names.index('bip01 r hand')
spine = names.index('bip01 spine2')
forward = -before[socket][:3, 1]
start_elevation = np.degrees(np.arctan2(forward[2], np.linalg.norm(forward[:2])))
grip = np.linalg.inv(before[hand]) @ before[socket]
for angle in (-45, 45):
    posed = aimed_frame(anim, mesh, frame, angle)
    after = evaluate(posed)
    direction = -after[socket][:3, 1]
    elevation = np.degrees(np.arctan2(direction[2], np.linalg.norm(direction[:2])))
    assert abs(elevation - start_elevation - angle) < .001, (angle, elevation, start_elevation)
    np.testing.assert_allclose(np.linalg.inv(after[hand]) @ after[socket], grip, atol=1e-5)
    for i in range(bones):
        p = i
        while p and p != spine:
            p = parents[p]
        if p != spine:
            np.testing.assert_allclose(after[i], before[i], atol=1e-6)
    # Animation table parents are intentionally irrelevant to the result.
    altered = dict(anim)
    kind, size, count, data = anim['BONENAMES']
    data = bytearray(data)
    for i in range(count):
        struct.pack_into('<i', data, i*size+72, -1)
    altered['BONENAMES'] = kind, size, count, data
    assert aimed_frame(altered, mesh, frame, angle) == posed
print('Both aim elevations, unchanged legs/root, and hand-to-socket transforms pass with real PSK parents')
