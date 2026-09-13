"""Check compiled Halo view meshes against UT99's first-person camera units."""
import argparse
import json
import re
from pathlib import Path
import numpy as np
from render_halout_model import pose
from read_native_animation import read_animation
from read_native_skeletal import read_mesh
from audit_halout_weapon_grips import mesh_axes


def audit(package, classes):
    reports = []
    for name, cls in [('Assault','HaloAssaultRifle'),('Pistol','HaloPistol'),('Plasma','HaloPlasmaRifle')]:
        source = (classes/(cls+'.uc')).read_text()
        scale = float(re.search(r'PlayerViewScale=([\d.]+)',source)[1])
        offset = np.array([float(v) for v in re.search(r'PlayerViewOffset=\(X=([^,]+),Y=([^,]+),Z=([^\)]+)\)',source).groups()])
        # Weapon.SetHand(-1) scales serialized offsets by 100; CalcDrawOffset
        # multiplies by 0.9/FOV. At 90 degrees these cancel, yielding UT units.
        offset[1] *= -1
        mesh = read_mesh(package,name+'View')
        if name == 'Assault':
            counts = np.bincount(mesh['faces'][:,3])
            if len(counts) != 6 or counts[1] != 22 or counts[2] != 4:
                raise ValueError('Rifle backing must precede compass in compiled face sections')
            # ActorX material bits differ from EPolyFlags. Check the compiled
            # result so an opaque/modulated display cannot silently return.
            for flags, slot in mesh['materials']:
                if slot == 1 and (int(flags) & 0x00400004) != 0x00400000:
                    raise ValueError('Rifle display backing must be opaque and unlit')
                if slot in (2,3,4,5) and int(flags) & 0x00400004 != 0x00400004:
                    raise ValueError('Rifle display must be translucent and unlit')
        axes = mesh_axes(mesh['rotation'])
        if (-axes[:,1])[0] < .99:
            raise ValueError(name+' barrel does not point along camera forward')
        animation = name+('ViewAnims' if name == 'Assault' else 'Anims')
        native = read_animation(package,animation)
        samples, nearest, center = 0, float('inf'), 0
        for seq in native['sequences']:
            if seq['name'].lower() not in ('idle','fire'): continue
            for frame in range(seq['frames']):
                points = pose(package,name+'View',animation,seq['name'],frame/seq['rate'])
                points = (points-mesh['origin']) @ axes.T*scale+offset
                nearest = min(nearest,float(points[:,0].min()))
                projected = points[:,1:]/np.maximum(points[:,0,None],.001)
                center = max(center,int(np.sum((points[:,0]>.01)&(abs(projected[:,0])<.025)&(abs(projected[:,1])<.025))))
                samples += 1
        # A first-person stock may extend behind the eye, as in Halo CE.
        # Validate aim clearance using only geometry in front of the camera;
        # clipping/offscreen rear geometry is not itself an obstruction.
        if not samples or center:
            raise ValueError(f'{cls} obstructs first-person view: samples={samples} depth={nearest} center={center}')
        reports.append(dict(weapon=cls,frames=samples,nearestCameraDepth=nearest,verticesInAimCenter=center,
                            rotation=mesh['rotation'],scale=scale,offset=offset.tolist()))
    return reports


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('package',type=Path)
    p.add_argument('--classes',type=Path,default=Path(__file__).resolve().parents[1]/'HaloUTXbox/Classes')
    args=p.parse_args()
    print(json.dumps(audit(args.package,args.classes),indent=2))
