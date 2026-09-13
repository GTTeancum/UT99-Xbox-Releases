#!/usr/bin/env python
"""Render UT source pickup meshes into loose XUI sprites for the Xbox weapon wheel."""

from __future__ import print_function

import math
import os
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "UT99-Xbox" / "MenuAssets"


WEAPONS = [
    ("weapon_impact.xui", "Impact Hammer", "Botpack", "Models/imppick_a.3D", "Models/imppick_d.3D", {1: "Models/imp0.PCX"}, (0, 0, -64), 0),
    ("weapon_enforcer.xui", "Enforcer", "Botpack", "Models/autopick_a.3D", "Models/autopick_d.3D", {1: "Models/mag0.PCX"}, (0, 64, 0), 0),
    ("weapon_bio.xui", "Bio Rifle", "Botpack", "Models/Biopick_a.3D", "Models/Biopick_d.3D", {1: "Models/Bio.PCX"}, (0, 64, 0), 0),
    ("weapon_shock.xui", "Shock Rifle", "Botpack", "Models/ASMDpick_a.3D", "Models/ASMDpick_d.3D", {1: "Models/ASMD.PCX"}, (0, 64, 0), 0),
    ("weapon_pulse.xui", "Pulse Gun", "Botpack", "Models/PulsePickup_a.3d", "Models/PulsePickup_d.3d", {1: "Models/P-pickup.pcx"}, (0, 0, 0), 0),
    ("weapon_ripper.xui", "Ripper", "Botpack", "Models/razorpick_a.3D", "Models/razorpick_d.3D", {2: "Models/razwhole.PCX"}, (0, 64, 0), 0),
    ("weapon_mini.xui", "Minigun", "Botpack", "Models/minipick_a.3D", "Models/minipick_d.3D", {1: "Models/Mini.PCX"}, (0, 64, 0), 0),
    ("weapon_flak.xui", "Flak Cannon", "Botpack", "Models/FlakPick2_a.3D", "Models/FlakPick2_d.3D", {1: "Models/Flak.PCX"}, (0, 64, 0), 0),
    ("weapon_rocket.xui", "Rocket Launcher", "Botpack", "Models/EightPick_a.3D", "Models/EightPick_d.3D", {2: "Models/eightballpick.PCX"}, (0, 64, 0), 0),
    ("weapon_sniper.xui", "Sniper Rifle", "Botpack", "Models/Riflehand_a.3D", "Models/Riflehand_d.3D", {2: "Models/Rifle.PCX"}, (0, 64, 0), 0),
    ("weapon_redeemer.xui", "Redeemer", "Botpack", "Models/WHPick_a.3D", "Models/WHPick_d.3D", {1: "Models/whpick.PCX"}, (0, 0, -64), 0),
    ("weapon_trans.xui", "Translocator", "Botpack", "Models/Tran3rd_a.3D", "Models/Tran3rd_d.3D", {0: "Models/tran1.PCX", 1: "Models/tran2.PCX"}, (0, -64, 0), 0),
    ("weapon_chainsaw.xui", "Chainsaw", "Botpack", "Models/chainpick_a.3D", "Models/chainpick_d.3D", {1: "Models/ChainSaw.PCX"}, (0, 0, -64), 0),
]


def read_mesh(aniv, data):
    with open(aniv, "rb") as f:
        frames, frame_size = struct.unpack("<HH", f.read(4))
        anim_data = f.read()
    with open(data, "rb") as f:
        num_polys, num_verts = struct.unpack("<HH", f.read(4))
        f.seek(36 + 12)
        tris = []
        for _ in range(num_polys):
            raw = f.read(16)
            if len(raw) != 16:
                break
            v0, v1, v2, typ, color, u0, vv0, u1, vv1, u2, vv2, texnum, flags = struct.unpack("<HHHBBBBBBBBBB", raw)
            if (typ & 15) == 8:
                continue
            tris.append(((v0, v1, v2), ((u0, vv0), (u1, vv1), (u2, vv2)), texnum, typ))

    verts = []
    for i in range(num_verts):
        val = struct.unpack("<I", anim_data[i * 4 : i * 4 + 4])[0]
        x = sign_extend(val & 0x7FF, 11)
        y = sign_extend((val >> 11) & 0x7FF, 11)
        z = sign_extend((val >> 22) & 0x3FF, 10)
        verts.append((float(x), float(y), float(z)))
    return np.array(verts, dtype=np.float32), tris


def sign_extend(v, bits):
    sign = 1 << (bits - 1)
    return (v ^ sign) - sign


def load_texture(path):
    img = Image.open(path).convert("RGBA")
    return img


def rot_matrix(pitch, yaw, roll):
    # #exec MESH ORIGIN uses 256 units per full turn.
    p = pitch * math.pi * 2.0 / 256.0
    y = yaw * math.pi * 2.0 / 256.0
    r = roll * math.pi * 2.0 / 256.0
    cp, sp = math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)
    cr, sr = math.cos(r), math.sin(r)
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]], dtype=np.float32)
    rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]], dtype=np.float32)
    ry = np.array([[cr, 0, sr], [0, 1, 0], [-sr, 0, cr]], dtype=np.float32)
    return rz @ ry @ rx


def sample_texture(tex, u, v):
    if tex is None:
        return np.array([180, 190, 196, 255], dtype=np.float32)
    x = int(max(0, min(tex.width - 1, u / 255.0 * (tex.width - 1))))
    y = int(max(0, min(tex.height - 1, v / 255.0 * (tex.height - 1))))
    return np.array(tex.getpixel((x, y)), dtype=np.float32)


def profile_basis(points):
    centered = points - points.mean(axis=0)
    cov = centered.T @ centered / max(len(centered) - 1, 1)
    values, vectors = np.linalg.eigh(cov)

    screen_x = vectors[:, np.argmax(values)]
    if screen_x[0] < 0:
        screen_x = -screen_x

    up = np.array([0.0, 0.0, 1.0], dtype=np.float32)
    if abs(float(np.dot(screen_x, up))) > 0.88:
        up = np.array([0.0, 1.0, 0.0], dtype=np.float32)

    view = np.cross(screen_x, up)
    view_len = np.linalg.norm(view)
    if view_len < 0.0001:
        view = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    else:
        view = view / view_len

    screen_y = np.cross(view, screen_x)
    screen_y = screen_y / np.linalg.norm(screen_y)
    return screen_x.astype(np.float32), screen_y.astype(np.float32), view.astype(np.float32)


def render_weapon(defn, size=128):
    out_name, label, package, aniv_rel, data_rel, texture_map, origin_rot, extra_yaw = defn
    base = ROOT / package
    verts, tris = read_mesh(base / aniv_rel, base / data_rel)
    textures = {k: load_texture(base / v) for k, v in texture_map.items() if (base / v).is_file()}

    r = rot_matrix(origin_rot[1], origin_rot[0] + extra_yaw, origin_rot[2])
    world_pts = verts @ r.T
    screen_x, screen_y, view = profile_basis(world_pts)
    pts = np.stack((world_pts @ screen_x, world_pts @ screen_y, world_pts @ view), axis=1)
    pts[:, 2] -= pts[:, 2].mean()

    min_xy = pts[:, :2].min(axis=0)
    max_xy = pts[:, :2].max(axis=0)
    span = np.maximum(max_xy - min_xy, 1.0)
    scale = (size * 0.82) / max(span)
    center = (min_xy + max_xy) * 0.5
    xy = (pts[:, :2] - center) * scale + np.array([size * 0.5, size * 0.50])
    z = pts[:, 2]

    rgba = np.zeros((size, size, 4), dtype=np.float32)
    depth = np.full((size, size), -1.0e9, dtype=np.float32)

    light = np.array([-0.25, -0.35, 0.90], dtype=np.float32)
    light = light / np.linalg.norm(light)

    tri_order = sorted(tris, key=lambda t: float(z[list(t[0])].mean()))
    for idxs, uvs, texnum, typ in tri_order:
        p = xy[list(idxs)]
        zz = z[list(idxs)]
        minx = max(0, int(math.floor(p[:, 0].min())))
        maxx = min(size - 1, int(math.ceil(p[:, 0].max())))
        miny = max(0, int(math.floor(p[:, 1].min())))
        maxy = min(size - 1, int(math.ceil(p[:, 1].max())))
        if maxx < minx or maxy < miny:
            continue
        a = p[0]
        b = p[1]
        c = p[2]
        denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denom) < 0.001:
            continue
        v3 = pts[list(idxs)]
        n = np.cross(v3[1] - v3[0], v3[2] - v3[0])
        nl = np.linalg.norm(n)
        if nl > 0:
            n = n / nl
        shade = max(0.44, min(1.20, 0.78 + float(np.dot(n, light)) * 0.28))
        tex = textures.get(texnum) or textures.get(0) or textures.get(1) or textures.get(2)
        for yy in range(miny, maxy + 1):
            for xx in range(minx, maxx + 1):
                px = xx + 0.5
                py = yy + 0.5
                w0 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / denom
                w1 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / denom
                w2 = 1.0 - w0 - w1
                if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                    continue
                dz = w0 * zz[0] + w1 * zz[1] + w2 * zz[2]
                if dz <= depth[yy, xx]:
                    continue
                u = w0 * uvs[0][0] + w1 * uvs[1][0] + w2 * uvs[2][0]
                v = w0 * uvs[0][1] + w1 * uvs[1][1] + w2 * uvs[2][1]
                col = sample_texture(tex, u, v)
                col[:3] *= shade
                col[:3] = np.clip(col[:3], 0, 255)
                col[3] = 255
                rgba[yy, xx] = col
                depth[yy, xx] = dz

    img = Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), "RGBA")
    img = trim_and_fit(img, size)
    return out_name, label, img


def trim_and_fit(img, size):
    alpha = img.getchannel("A")
    bbox = alpha.getbbox()
    if not bbox:
        return img
    cropped = img.crop(bbox)
    if cropped.height > cropped.width * 1.18:
        cropped = cropped.rotate(-90, expand=True, resample=Image.Resampling.BICUBIC)
    cropped.thumbnail((size - 6, size - 6), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    shadow = Image.new("RGBA", cropped.size, (0, 0, 0, 0))
    shadow.putalpha(cropped.getchannel("A").filter(ImageFilter.GaussianBlur(1.2)))
    canvas.alpha_composite(shadow, ((size - cropped.width) // 2 + 1, (size - cropped.height) // 2 + 2))
    canvas.alpha_composite(cropped, ((size - cropped.width) // 2, (size - cropped.height) // 2))
    return canvas


def write_xui(path, image):
    image = image.convert("RGBA")
    pixels = bytearray()
    for r, g, b, a in image.getdata():
        pixels += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    with open(path, "wb") as f:
        f.write(b"XUI0")
        f.write(struct.pack("<II", image.width, image.height))
        f.write(pixels)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rendered = []
    for weapon in WEAPONS:
        out_name, label, img = render_weapon(weapon)
        write_xui(OUT_DIR / out_name, img)
        img.save(OUT_DIR / (out_name.replace(".xui", ".png")))
        rendered.append((label, img))
        print("Wrote", OUT_DIR / out_name)

    contact = Image.new("RGBA", (4 * 164, 4 * 156), (12, 14, 18, 255))
    draw = ImageDraw.Draw(contact)
    for i, (label, img) in enumerate(rendered):
        x = (i % 4) * 164
        y = (i // 4) * 156
        contact.alpha_composite(img, (x + 18, y + 6))
        draw.text((x + 4, y + 136), label, fill=(220, 228, 235, 255))
    contact.save(OUT_DIR / "weapon_mesh_contact.png")


if __name__ == "__main__":
    main()
