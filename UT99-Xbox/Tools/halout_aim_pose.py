"""Author UT99's up/down aim poses from Halo's level aim, preserving the grip."""
import struct
import numpy as np
from scipy.spatial.transform import Rotation


def aimed_frame(chunks, mesh_chunks, frame, degrees):
    _, bone_size, count, bones = chunks['BONENAMES']
    _, key_size, _, keys = chunks['ANIMKEYS']
    if bone_size != 120 or key_size != 32:
        raise ValueError('Unexpected ActorX bone/key format')
    data = bytearray(keys[frame * count * key_size:(frame + 1) * count * key_size])
    names = [bones[i*bone_size:i*bone_size+64].split(b'\0')[0].decode().lower() for i in range(count)]
    # PSA BONENAMES is a track-name table. UE Viewer writes placeholder parents
    # there; only the PSK reference skeleton describes the runtime hierarchy.
    _, mesh_bone_size, mesh_count, mesh_bones = mesh_chunks['REFSKELT']
    if mesh_bone_size != 120:
        raise ValueError('Unexpected ActorX reference skeleton format')
    mesh_names = [mesh_bones[i*mesh_bone_size:i*mesh_bone_size+64].split(b'\0')[0].decode().lower() for i in range(mesh_count)]
    parents = [struct.unpack_from('<i', mesh_bones, i*mesh_bone_size+72)[0] for i in range(mesh_count)]
    track_indices = [names.index(name) for name in mesh_names]
    quats = [np.array(struct.unpack_from('<4f', data, i*key_size+12)) for i in range(count)]
    # The source includes unused tail/tentacle tracks with zero quaternions.
    # Match SkelNormalizeQuat's identity fallback for those absent bones.
    local = [Rotation.from_quat(q if np.dot(q, q) > 1e-6 else [0, 0, 0, 1]).as_matrix() for q in quats]
    global_basis = []
    for i in range(mesh_count):
        parent = parents[i]
        if i and not 0 <= parent < i:
            raise ValueError('Skeleton must be parent-first')
        # Matches USkeletalMesh's SkelComposeCoords / ApplyPivot convention.
        basis = local[track_indices[i]]
        global_basis.append(global_basis[parent] @ basis.T if i else basis)
    spine = mesh_names.index('bip01 spine2')
    weapon = mesh_names.index('bone_weapon')
    forward = -global_basis[weapon][:, 1].copy()  # authored barrel axis, before classic-axis adjustment
    forward[2] = 0
    forward /= np.linalg.norm(forward)
    axis = np.cross(forward, [0., 0., 1.])
    pitch = Rotation.from_rotvec(axis * np.deg2rad(degrees)).as_matrix()
    parent = global_basis[parents[spine]]
    adjusted = local[track_indices[spine]] @ parent.T @ pitch.T @ parent
    # A world-space rotation of this subtree must preserve all child offsets.
    if not np.allclose(parent @ adjusted.T, pitch @ global_basis[spine], atol=1e-6):
        raise ValueError('Aim rotation failed its subtree transform invariant')
    struct.pack_into('<4f', data, track_indices[spine]*key_size+12, *Rotation.from_matrix(adjusted).as_quat())
    return data
