#!/usr/bin/env python3
"""Bake a UE Viewer MD5 skeletal export into a native UE1 vertex mesh.

UE Viewer is used as the authoritative UE1 skeletal decoder. Its MD5 exports
contain a standard skinned mesh and one MD5 animation per sequence. This tool
evaluates those animations offline and writes the James/Unreal ``_d.3d`` and
``_a.3d`` files consumed by the stock UT99 mesh importer.
"""

from __future__ import annotations

import argparse
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]


@dataclass(frozen=True)
class Joint:
    name: str
    parent: int
    position: Vec3
    orientation: Quat


@dataclass(frozen=True)
class Weight:
    joint: int
    bias: float
    position: Vec3


@dataclass(frozen=True)
class Vertex:
    uv: tuple[float, float]
    first_weight: int
    weight_count: int


@dataclass(frozen=True)
class Surface:
    vertices: tuple[Vertex, ...]
    triangles: tuple[tuple[int, int, int], ...]
    weights: tuple[Weight, ...]


@dataclass(frozen=True)
class Md5Mesh:
    joints: tuple[Joint, ...]
    surfaces: tuple[Surface, ...]


@dataclass(frozen=True)
class Md5Animation:
    name: str
    frame_rate: float
    joint_names: tuple[str, ...]
    parents: tuple[int, ...]
    frames: tuple[tuple[tuple[Vec3, Quat], ...], ...]


@dataclass(frozen=True)
class SequenceInfo:
    name: str
    start_frame: int
    frame_count: int
    frame_rate: float


_FLOAT = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


def _extract_block(text: str, keyword: str, start: int = 0) -> tuple[str, int]:
    match = re.search(rf"\b{re.escape(keyword)}\s*\{{", text[start:])
    if not match:
        raise ValueError(f"missing {keyword} block")
    opening = start + match.end() - 1
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index], index + 1
    raise ValueError(f"unterminated {keyword} block")


def _parse_quat(x: float, y: float, z: float) -> Quat:
    w = -math.sqrt(max(0.0, 1.0 - x * x - y * y - z * z))
    return _quat_normalize((x, y, z, w))


def _quat_normalize(q: Quat) -> Quat:
    length = math.sqrt(sum(value * value for value in q))
    if length <= 1.0e-12:
        raise ValueError("zero-length quaternion")
    return tuple(value / length for value in q)  # type: ignore[return-value]


def _quat_mul(a: Quat, b: Quat) -> Quat:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def _rotate(q: Quat, point: Vec3) -> Vec3:
    qx, qy, qz, qw = q
    px, py, pz = point
    tx = 2.0 * (qy * pz - qz * py)
    ty = 2.0 * (qz * px - qx * pz)
    tz = 2.0 * (qx * py - qy * px)
    return (
        px + qw * tx + (qy * tz - qz * ty),
        py + qw * ty + (qz * tx - qx * tz),
        pz + qw * tz + (qx * ty - qy * tx),
    )


def _add(a: Vec3, b: Vec3) -> Vec3:
    return a[0] + b[0], a[1] + b[1], a[2] + b[2]


def parse_md5mesh(path: Path) -> Md5Mesh:
    text = path.read_text(encoding="utf-8")
    joints_text, cursor = _extract_block(text, "joints")
    joint_pattern = re.compile(
        rf'^\s*"([^"]+)"\s+(-?\d+)\s+\(\s*({_FLOAT})\s+({_FLOAT})\s+({_FLOAT})\s*\)'
        rf'\s+\(\s*({_FLOAT})\s+({_FLOAT})\s+({_FLOAT})\s*\)\s*$',
        re.MULTILINE,
    )
    joints = []
    for match in joint_pattern.finditer(joints_text):
        values = [float(match.group(i)) for i in range(3, 9)]
        joints.append(
            Joint(
                match.group(1),
                int(match.group(2)),
                (values[0], values[1], values[2]),
                _parse_quat(values[3], values[4], values[5]),
            )
        )
    expected_joints = _header_int(text, "numJoints")
    if len(joints) != expected_joints:
        raise ValueError(f"{path}: parsed {len(joints)} of {expected_joints} joints")

    surfaces = []
    while re.search(r"\bmesh\s*\{", text[cursor:]):
        block, cursor = _extract_block(text, "mesh", cursor)
        vertices_by_index: dict[int, Vertex] = {}
        for match in re.finditer(
            rf"^\s*vert\s+(\d+)\s+\(\s*({_FLOAT})\s+({_FLOAT})\s*\)\s+(\d+)\s+(\d+)\s*$",
            block,
            re.MULTILINE,
        ):
            vertices_by_index[int(match.group(1))] = Vertex(
                (float(match.group(2)), float(match.group(3))),
                int(match.group(4)),
                int(match.group(5)),
            )
        triangles_by_index: dict[int, tuple[int, int, int]] = {}
        for match in re.finditer(r"^\s*tri\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", block, re.MULTILINE):
            triangles_by_index[int(match.group(1))] = tuple(int(match.group(i)) for i in range(2, 5))  # type: ignore[assignment]
        weights_by_index: dict[int, Weight] = {}
        for match in re.finditer(
            rf"^\s*weight\s+(\d+)\s+(\d+)\s+({_FLOAT})\s+\(\s*({_FLOAT})\s+({_FLOAT})\s+({_FLOAT})\s*\)\s*$",
            block,
            re.MULTILINE,
        ):
            weights_by_index[int(match.group(1))] = Weight(
                int(match.group(2)),
                float(match.group(3)),
                (float(match.group(4)), float(match.group(5)), float(match.group(6))),
            )
        vertices = _dense_values(vertices_by_index, "vertices", path)
        triangles = _dense_values(triangles_by_index, "triangles", path)
        weights = _dense_values(weights_by_index, "weights", path)
        if len(vertices) != _block_int(block, "numverts"):
            raise ValueError(f"{path}: vertex count mismatch")
        if len(triangles) != _block_int(block, "numtris"):
            raise ValueError(f"{path}: triangle count mismatch")
        if len(weights) != _block_int(block, "numweights"):
            raise ValueError(f"{path}: weight count mismatch")
        surfaces.append(Surface(tuple(vertices), tuple(triangles), tuple(weights)))

    expected_surfaces = _header_int(text, "numMeshes")
    if len(surfaces) != expected_surfaces:
        raise ValueError(f"{path}: parsed {len(surfaces)} of {expected_surfaces} surfaces")
    return Md5Mesh(tuple(joints), tuple(surfaces))


def _dense_values(mapping: dict[int, object], label: str, path: Path) -> list:
    if not mapping:
        return []
    expected = list(range(max(mapping) + 1))
    if sorted(mapping) != expected:
        raise ValueError(f"{path}: non-dense {label}")
    return [mapping[index] for index in expected]


def _header_int(text: str, name: str) -> int:
    match = re.search(rf"^\s*{re.escape(name)}\s+(\d+)\s*$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return int(match.group(1))


def _header_float(text: str, name: str) -> float:
    match = re.search(rf"^\s*{re.escape(name)}\s+({_FLOAT})\s*$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return float(match.group(1))


def _block_int(text: str, name: str) -> int:
    return _header_int(text, name)


def parse_md5anim(path: Path) -> Md5Animation:
    text = path.read_text(encoding="utf-8")
    frame_count = _header_int(text, "numFrames")
    joint_count = _header_int(text, "numJoints")
    frame_rate = _header_float(text, "frameRate")
    hierarchy_text, _ = _extract_block(text, "hierarchy")
    hierarchy = re.findall(r'^\s*"([^"]+)"\s+(-?\d+)\s+(\d+)\s+(\d+)\s*$', hierarchy_text, re.MULTILINE)
    if len(hierarchy) != joint_count:
        raise ValueError(f"{path}: parsed {len(hierarchy)} of {joint_count} hierarchy entries")
    if any(int(flags) != 63 or int(offset) != index * 6 for index, (_, _, flags, offset) in enumerate(hierarchy)):
        raise ValueError(f"{path}: expected UE Viewer full-component MD5 animation")

    frames_by_index: dict[int, tuple[tuple[Vec3, Quat], ...]] = {}
    for match in re.finditer(r"\bframe\s+(\d+)\s*\{", text):
        frame_index = int(match.group(1))
        opening = match.end() - 1
        block, _ = _extract_open_block(text, opening)
        numbers = [float(value) for value in re.findall(_FLOAT, block)]
        if len(numbers) != joint_count * 6:
            raise ValueError(f"{path}: frame {frame_index} has {len(numbers)} components, expected {joint_count * 6}")
        local_pose = []
        for index in range(joint_count):
            values = numbers[index * 6 : index * 6 + 6]
            local_pose.append(((values[0], values[1], values[2]), _parse_quat(values[3], values[4], values[5])))
        frames_by_index[frame_index] = tuple(local_pose)
    frames = _dense_values(frames_by_index, "frames", path)
    if len(frames) != frame_count:
        raise ValueError(f"{path}: parsed {len(frames)} of {frame_count} frames")
    return Md5Animation(
        path.stem,
        frame_rate,
        tuple(item[0] for item in hierarchy),
        tuple(int(item[1]) for item in hierarchy),
        tuple(frames),
    )


def _extract_open_block(text: str, opening: int) -> tuple[str, int]:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index], index + 1
    raise ValueError("unterminated block")


def _world_pose(animation: Md5Animation, local_pose: Sequence[tuple[Vec3, Quat]]) -> tuple[tuple[Vec3, Quat], ...]:
    world: list[tuple[Vec3, Quat]] = []
    for index, (position, orientation) in enumerate(local_pose):
        parent = animation.parents[index]
        if parent < 0:
            world.append((position, orientation))
            continue
        if parent >= index:
            raise ValueError(f"{animation.name}: joint {index} has non-causal parent {parent}")
        parent_position, parent_orientation = world[parent]
        world.append(
            (
                _add(parent_position, _rotate(parent_orientation, position)),
                _quat_normalize(_quat_mul(parent_orientation, orientation)),
            )
        )
    return tuple(world)


def _skin_frame(mesh: Md5Mesh, animation: Md5Animation, local_pose: Sequence[tuple[Vec3, Quat]]) -> tuple[Vec3, ...]:
    world = _world_pose(animation, local_pose)
    animation_by_name = {name.casefold(): index for index, name in enumerate(animation.joint_names)}
    mesh_to_animation = []
    for joint in mesh.joints:
        animation_index = animation_by_name.get(joint.name.casefold())
        if animation_index is None:
            raise ValueError(f"{animation.name}: missing mesh joint {joint.name!r}")
        mesh_to_animation.append(animation_index)

    result = []
    for surface in mesh.surfaces:
        for vertex in surface.vertices:
            x = y = z = bias_total = 0.0
            end = vertex.first_weight + vertex.weight_count
            if end > len(surface.weights):
                raise ValueError(f"{animation.name}: vertex weight range is out of bounds")
            for weight in surface.weights[vertex.first_weight:end]:
                if weight.joint >= len(mesh_to_animation):
                    raise ValueError(f"{animation.name}: weight references invalid joint {weight.joint}")
                position, orientation = world[mesh_to_animation[weight.joint]]
                wx, wy, wz = _add(position, _rotate(orientation, weight.position))
                x += wx * weight.bias
                y += wy * weight.bias
                z += wz * weight.bias
                bias_total += weight.bias
            if not 0.98 <= bias_total <= 1.02:
                raise ValueError(f"{animation.name}: vertex has weight sum {bias_total:.6f}")
            # Undo UE Viewer's MD5 Y mirror, then its UE1 load-time X mirror.
            result.append((-x, -y, z))
    return tuple(result)


def _pack_mesh_vertex(point: Vec3) -> bytes:
    x, y, z = (int(value) for value in point)
    if not (-1024 <= x <= 1023 and -1024 <= y <= 1023 and -512 <= z <= 511):
        raise ValueError(f"vertex outside packed UE1 range: ({x}, {y}, {z})")
    packed = (x & 0x7FF) | ((y & 0x7FF) << 11) | ((z & 0x3FF) << 22)
    return struct.pack("<I", packed)


def _uv_byte(value: float) -> int:
    return max(0, min(255, int(math.floor(value * 255.0 + 0.5))))


def write_data_file(mesh: Md5Mesh, path: Path) -> tuple[int, int]:
    vertex_offsets = []
    vertex_count = 0
    triangle_count = 0
    for surface in mesh.surfaces:
        vertex_offsets.append(vertex_count)
        vertex_count += len(surface.vertices)
        triangle_count += len(surface.triangles)
    if vertex_count > 65535 or triangle_count > 65535:
        raise ValueError("UE1 .3d count exceeds 16-bit header")

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(struct.pack("<4H10I", triangle_count, vertex_count, 0, 0, *([0] * 10)))
        for material_index, (surface, offset) in enumerate(zip(mesh.surfaces, vertex_offsets)):
            if material_index > 255:
                raise ValueError("material index exceeds byte range")
            for triangle in surface.triangles:
                # MD5 triangle order already includes both mirrors required to
                # return this UE1 package to its source coordinate system.
                indices = tuple(offset + index for index in triangle)
                if any(index > 65535 for index in indices):
                    raise ValueError("triangle index exceeds 16-bit range")
                uv_bytes = []
                for index in triangle:
                    u, v = surface.vertices[index].uv
                    uv_bytes.extend((_uv_byte(u), _uv_byte(v)))
                output.write(struct.pack("<3HBB6BBB", *indices, 0, 0, *uv_bytes, material_index, 0))
    return vertex_count, triangle_count


def write_animation_file(
    mesh: Md5Mesh,
    animations: Sequence[Md5Animation],
    path: Path,
) -> tuple[list[SequenceInfo], tuple[Vec3, Vec3]]:
    vertex_count = sum(len(surface.vertices) for surface in mesh.surfaces)
    frame_count = sum(len(animation.frames) for animation in animations)
    if frame_count > 65535 or vertex_count * 4 > 65535:
        raise ValueError("UE1 .3d animation header exceeds 16-bit range")
    sequences = []
    global_min = [math.inf, math.inf, math.inf]
    global_max = [-math.inf, -math.inf, -math.inf]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(struct.pack("<HH", frame_count, vertex_count * 4))
        start_frame = 0
        for animation in animations:
            sequences.append(SequenceInfo(animation.name, start_frame, len(animation.frames), animation.frame_rate))
            for local_pose in animation.frames:
                points = _skin_frame(mesh, animation, local_pose)
                if len(points) != vertex_count:
                    raise ValueError(f"{animation.name}: skinned vertex count mismatch")
                for point in points:
                    for axis in range(3):
                        global_min[axis] = min(global_min[axis], point[axis])
                        global_max[axis] = max(global_max[axis], point[axis])
                    output.write(_pack_mesh_vertex(point))
            start_frame += len(animation.frames)
    return sequences, (tuple(global_min), tuple(global_max))  # type: ignore[return-value]


def _natural_key(path: Path) -> list[object]:
    return [int(part) if part.isdigit() else part.casefold() for part in re.split(r"(\d+)", path.stem)]


def _parse_vector_option(value: str) -> Vec3:
    parts = [float(part.strip()) for part in value.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected X,Y,Z")
    return parts[0], parts[1], parts[2]


def write_unrealscript(
    path: Path,
    class_name: str,
    mesh_name: str,
    sequences: Sequence[SequenceInfo],
    origin: Vec3,
    rotation: tuple[int, int, int],
    scale: Vec3,
    texture_loads: Iterable[str],
    textures: Iterable[str],
    notifies: Iterable[str],
) -> None:
    lines = [f"class {class_name} extends Object;", ""]
    texture_loads = list(texture_loads)
    lines.extend(texture_loads)
    if texture_loads:
        lines.append("")
    lines.extend(
        [
            f"#exec MESH IMPORT MESH={mesh_name} ANIVFILE=Models/{mesh_name}_a.3d DATAFILE=Models/{mesh_name}_d.3d MLOD=0",
            f"#exec MESH ORIGIN MESH={mesh_name} X={origin[0]:g} Y={origin[1]:g} Z={origin[2]:g} "
            f"YAW={rotation[0]} PITCH={rotation[1]} ROLL={rotation[2]}",
            "",
        ]
    )
    for sequence in sequences:
        lines.append(
            f"#exec MESH SEQUENCE MESH={mesh_name} SEQ={sequence.name} STARTFRAME={sequence.start_frame} "
            f"NUMFRAMES={sequence.frame_count} RATE={sequence.frame_rate:g}"
        )
    lines.extend(
        [
            "",
            f"#exec MESHMAP SCALE MESHMAP={mesh_name} X={scale[0]:g} Y={scale[1]:g} Z={scale[2]:g}",
        ]
    )
    for texture in textures:
        lines.append(texture)
    for notify in notifies:
        lines.append(notify)
    lines.extend(["", "defaultproperties", "{", "}", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def _load_lines(path: Path | None, pattern: str) -> list[str]:
    if path is None:
        return []
    return [line.strip() for line in path.read_text(encoding="latin-1").splitlines() if re.search(pattern, line, re.IGNORECASE)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesh", type=Path, required=True, help="UE Viewer .md5mesh export")
    parser.add_argument("--animations", type=Path, required=True, help="directory containing sequence .md5anim files")
    parser.add_argument("--output", type=Path, required=True, help="output package source directory")
    parser.add_argument("--mesh-name", required=True)
    parser.add_argument("--class-name", default="UTPS2BakedAssets")
    parser.add_argument("--origin", type=_parse_vector_option, default=(0.0, 0.0, 0.0))
    parser.add_argument("--rotation", default="0,0,0", help="YAW,PITCH,ROLL in Unreal import units")
    parser.add_argument("--scale", type=_parse_vector_option, default=(1.0, 1.0, 1.0))
    parser.add_argument("--directives-uc", type=Path, help="class export supplying OBJ LOAD and SETTEXTURE directives")
    parser.add_argument("--notifies-uc", type=Path, help="class export supplying MESH NOTIFY directives")
    parser.add_argument("--only", action="append", default=[], help="only bake this sequence (repeatable)")
    args = parser.parse_args()

    rotation_parts = tuple(int(part.strip()) for part in args.rotation.split(","))
    if len(rotation_parts) != 3:
        parser.error("--rotation expects YAW,PITCH,ROLL")
    mesh = parse_md5mesh(args.mesh)
    animation_paths = sorted(args.animations.glob("*.md5anim"), key=_natural_key)
    if args.only:
        selected = {name.casefold() for name in args.only}
        animation_paths = [path for path in animation_paths if path.stem.casefold() in selected]
        missing = selected - {path.stem.casefold() for path in animation_paths}
        if missing:
            raise ValueError(f"missing requested sequences: {', '.join(sorted(missing))}")
    if not animation_paths:
        raise ValueError("no MD5 animations selected")
    animations = [parse_md5anim(path) for path in animation_paths]

    models = args.output / "Models"
    classes = args.output / "Classes"
    vertex_count, triangle_count = write_data_file(mesh, models / f"{args.mesh_name}_d.3d")
    sequences, bounds = write_animation_file(mesh, animations, models / f"{args.mesh_name}_a.3d")
    texture_loads = _load_lines(args.directives_uc, r"^\s*#exec\s+OBJ\s+LOAD\b")
    texture_lines = _load_lines(args.directives_uc, rf"^\s*#exec\s+MESHMAP\s+SETTEXTURE\s+MESHMAP={re.escape(args.mesh_name)}\b")
    notify_lines = _load_lines(args.notifies_uc, r"^\s*#exec\s+(?:ANIM|MESH)\s+NOTIFY\b")
    sequence_names = {sequence.name.casefold() for sequence in sequences}
    notify_lines = [
        line
        for line in notify_lines
        if (match := re.search(r"\bSEQ=([^\s]+)", line, re.IGNORECASE))
        and match.group(1).casefold() in sequence_names
    ]
    notify_lines = [
        re.sub(
            r"\bANIM=\S+",
            f"MESH={args.mesh_name}",
            re.sub(r"^(\s*#exec)\s+ANIM\s+NOTIFY", r"\1 MESH NOTIFY", line, flags=re.IGNORECASE),
            flags=re.IGNORECASE,
        )
        for line in notify_lines
    ]
    write_unrealscript(
        classes / f"{args.class_name}.uc",
        args.class_name,
        args.mesh_name,
        sequences,
        args.origin,
        rotation_parts,  # type: ignore[arg-type]
        args.scale,
        texture_loads,
        texture_lines,
        notify_lines,
    )
    minimum, maximum = bounds
    print(
        f"BAKE_OK mesh={args.mesh_name} surfaces={len(mesh.surfaces)} vertices={vertex_count} "
        f"triangles={triangle_count} sequences={len(sequences)} frames={sum(item.frame_count for item in sequences)} "
        f"bounds=({minimum[0]:.3f},{minimum[1]:.3f},{minimum[2]:.3f}).."
        f"({maximum[0]:.3f},{maximum[1]:.3f},{maximum[2]:.3f})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
