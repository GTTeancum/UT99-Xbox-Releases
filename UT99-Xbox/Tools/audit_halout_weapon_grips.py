"""Check imported world meshes place the authored grip at the attachment pivot."""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from read_native_skeletal import read_mesh
from read_native_animation import read_animation


def mesh_axes(rotation):
    pitch, yaw, roll = np.array(rotation) * (360.0 / 65536.0)
    return Rotation.from_euler('ZYX', [yaw, -pitch, -roll], degrees=True).as_matrix() @ np.diag([1, -1, 1])


def audit_elite_frame(package):
    """An upright idle must keep the gun's top up and barrel forward.

    This checks the complete compiled chain, including the skeletal Y
    reflection and classic weapon frame, against pawn-space directions.
    """
    mesh = read_mesh(package, 'EliteMesh')
    animation = read_animation(package, 'EliteAnims')
    move = animation['moves'][next(i for i, s in enumerate(animation['sequences']) if s['name'].lower() == 'breath1')]
    names = [b['name'].lower() for b in animation['bones']]
    bases = []
    for index, bone in enumerate(mesh['bones']):
        track = move['tracks'][names.index(bone['name'].lower())]
        basis = Rotation.from_quat(track['quat'][0]).as_matrix()
        bases.append(bases[bone['parent']] @ basis.T if index else basis)
    adjusted = bases[mesh['weapon_bone']] @ np.array(mesh['weapon_adjustment'][3:]).reshape(3,3).T
    posed = mesh_axes(mesh['rotation']) @ adjusted
    # Construct the same frame from transformed unit axes; do not assume a
    # socket Euler angle already accounts for reflection or handedness.
    x, y = posed[:,0], posed[:,1]
    cross = np.cross(x, y)
    attachment = np.column_stack([x, -cross, np.cross(x, cross)])
    reports = []
    for name in ('AssaultWorld', 'PistolWorld', 'PlasmaWorld'):
        weapon = read_mesh(package, name)
        axes = attachment @ mesh_axes(weapon['rotation'])
        barrel, up = -axes[:,1], axes[:,2]
        reports.append(dict(mesh=name, barrelPawnSpace=barrel.tolist(), upPawnSpace=up.tolist()))
        if barrel[0] < 0.95 or up[2] < 0.95:
            raise ValueError(f'{name} is not forward and upright in Elite idle: {barrel}, {up}')
    return reports


def audit(package):
    reports = []
    for name in ('AssaultWorld', 'PistolWorld', 'PlasmaWorld'):
        mesh = read_mesh(package, name)
        bases = []
        grip = None
        for index, bone in enumerate(mesh['bones']):
            basis = Rotation.from_quat(bone['quat']).as_matrix()
            position = np.array(bone['position'])
            if index:
                parent_basis, parent_position = bases[bone['parent']]
                position = parent_basis @ position + parent_position
                basis = parent_basis @ basis.T
            bases.append((basis, position))
            if bone['name'].lower() == 'bone_weapon':
                grip = position
        if grip is None:
            raise ValueError('Missing authored grip: ' + name)
        error = float(np.linalg.norm(grip - mesh['origin']))
        reports.append(dict(mesh=name, grip=grip.tolist(), origin=mesh['origin'], error=error))
        # GetFrame subtracts Origin before the attachment transform. A zero
        # difference stays at the socket for every bone pose and weapon scale.
        if error > 0.0001:
            raise ValueError(f'{name} grip misses attachment pivot by {error}')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    package = parser.parse_args().package
    print(json.dumps(dict(grips=audit(package), eliteIdle=audit_elite_frame(package)), indent=2))
