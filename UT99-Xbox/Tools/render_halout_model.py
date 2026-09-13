"""Render the actual converted Halo model for portraits and projection checks.

Uses the compiled skeleton, animation and skin weights, with the source PSK
triangle/UV data. No desktop capture or invented character artwork is involved.
"""
import argparse
import struct
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation, Slerp
from scipy.spatial import cKDTree
from build_halout import chunks
from read_native_skeletal import read_mesh
from read_native_animation import read_animation


def pose(package, mesh_name, anim_name, sequence, time=0.):
    mesh = read_mesh(package, mesh_name)
    anim = read_animation(package, anim_name)
    move = anim['moves'][next(i for i,s in enumerate(anim['sequences']) if s['name'].lower() == sequence.lower())]
    names = [b['name'].lower() for b in anim['bones']]
    bases, origins = [], []
    for i,b in enumerate(mesh['bones']):
        t = move['tracks'][names.index(b['name'].lower())] if b['name'].lower() in names else dict(quat=[],position=[])
        q = t['quat'][0] if len(t['quat']) else b['quat']
        if len(t['quat']) > 1:
            q = Slerp(t['time'],Rotation.from_quat(t['quat']))([np.clip(time,t['time'][0],t['time'][-1])]).as_quat()[0]
        r = Rotation.from_quat(q).as_matrix()
        v = np.array(t['position'][0] if len(t['position']) else b['position'])
        if len(t['position']) > 1:
            v = np.array([np.interp(time,t['time'],t['position'][:,j]) for j in range(3)])
        parent = b['parent']
        origins.append(origins[parent] + bases[parent] @ v if i else v)
        bases.append(bases[parent] @ r.T if i else r)
    points = np.zeros_like(mesh['points'], dtype=float)
    spans = mesh['influence_spans'].astype('<i4').view('<u2').reshape(-1,4)
    for i,(start,count,_,__) in enumerate(spans):
        inf = mesh['influences'][start:start+count]
        local = mesh['local_points'][start:start+count]
        np.add.at(points, inf[:,0], (local @ bases[i].T + origins[i]) * (inf[:,1,None]/65535.))
    return points


def source_order(package, mesh_name, psk, points):
    # UCC reorders skeletal points. Source faces must use the corresponding
    # converted point index, never the original PSK index directly.
    source = np.frombuffer(chunks(psk)['PNTS0000'][3],dtype='<f4').reshape(-1,3)
    distance, indices = cKDTree(read_mesh(package,mesh_name)['points']).query(source)
    if distance.max() > 0.001:
        raise ValueError('Source/compiled vertex correspondence failed')
    return points[indices]


def geometry(psk):
    c = chunks(psk)
    w = c['VTXW0000']; f = c['FACE0000']
    wedges = [struct.unpack_from('<HHffBBH',w[3],i*w[1]) for i in range(w[2])]
    faces = [struct.unpack_from('<HHHBBI',f[3],i*f[1]) for i in range(f[2])]
    return np.array([w[0] for w in wedges]), np.array([w[2:4] for w in wedges]), faces


def raster(points, psk, textures, size=(512,1024), azimuth=15, perspective=False):
    indices, uv, faces = geometry(psk)
    angle = np.deg2rad(azimuth)
    # Camera horizontal right and forward, Z-up. Orthographic portrait.
    right = np.array([np.cos(angle),np.sin(angle),0])
    forward = np.array([-np.sin(angle),np.cos(angle),0])
    v = np.column_stack((points @ right, -points[:,2], points @ forward))
    if perspective:
        v = np.column_stack((points[:,1],-points[:,2],points[:,0]))
        v[:,:2] = v[:,:2] / np.maximum(v[:,2,None],.01) * (size[0]/2) + np.array(size)/2
    else:
        lo,hi = v[:,:2].min(0),v[:,:2].max(0)
        scale = min((size[0]-24)/(hi[0]-lo[0]),(size[1]-24)/(hi[1]-lo[1]))
        v[:,:2] = (v[:,:2]-(lo+hi)/2)*scale + np.array(size)/2
    out = np.zeros((size[1],size[0],4),dtype=np.uint8)
    depth = np.full((size[1],size[0]),np.inf)
    tex = [np.array(Image.open(t).convert('RGB')) for t in textures]
    light = np.array([-.4,-.6,.7]); light /= np.linalg.norm(light)
    for face in faces:
        wi = np.array(face[:3]); pi = indices[wi]; tri = v[pi]
        if perspective and tri[:,2].min() <= .01: continue
        a,b,c = tri[:,:2]
        mn = np.maximum(np.floor(tri[:,:2].min(0)).astype(int),0)
        mx = np.minimum(np.ceil(tri[:,:2].max(0)).astype(int),np.array(size)-1)
        if np.any(mx<mn): continue
        yy,xx = np.mgrid[mn[1]:mx[1]+1,mn[0]:mx[0]+1]
        den = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-8: continue
        u=((b[1]-c[1])*(xx+.5-c[0])+(c[0]-b[0])*(yy+.5-c[1]))/den
        w=((c[1]-a[1])*(xx+.5-c[0])+(a[0]-c[0])*(yy+.5-c[1]))/den
        weights=np.stack((u,w,1-u-w),axis=-1)
        inside = weights.min(-1)>=-1e-6
        if perspective:
            weights = weights / tri[:,2]
            weights /= weights.sum(-1)[:,:,None]
        z=weights @ tri[:,2]
        region=depth[mn[1]:mx[1]+1,mn[0]:mx[0]+1]
        mask=inside&(z<region)
        if not mask.any():continue
        texuv=weights @ uv[wi]
        texture=tex[min(face[3],len(tex)-1)]
        tx=np.clip((texuv[:,:,0]*texture.shape[1]).astype(int),0,texture.shape[1]-1)
        ty=np.clip((texuv[:,:,1]*texture.shape[0]).astype(int),0,texture.shape[0]-1)
        normal=np.cross(points[pi[1]]-points[pi[0]],points[pi[2]]-points[pi[0]])
        normal/=max(np.linalg.norm(normal),1e-8)
        shade=.55+.65*abs(normal @ light)
        color=np.clip(texture[ty,tx]*shade,0,255).astype(np.uint8)
        target=out[mn[1]:mx[1]+1,mn[0]:mx[0]+1]
        target[mask,:3]=color[mask];target[mask,3]=255;region[mask]=z[mask]
    return Image.fromarray(out)


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    # Character selection uses a neutral, straight-on pose like the existing
    # Chief/UT portraits, not the twisted weapon-holding gameplay idle.
    points=read_mesh(a.package,'EliteMesh')['points']
    points=source_order(a.package,'EliteMesh',a.assets/'Models/Elite.psk',points)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    raster(points,a.assets/'Models/Elite.psk',[a.assets/'Textures/EliteBlue.pcx'],azimuth=0).save(a.output)
    print(a.output)
