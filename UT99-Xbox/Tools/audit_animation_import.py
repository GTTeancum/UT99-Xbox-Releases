"""Compare every imported source-frame pose with its ActorX animation keys."""
import argparse
import json
import struct
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation, Slerp
from build_halout import chunks
from read_native_animation import read_animation


def normalized(values):
    values = values.astype(float).copy()
    norms = np.linalg.norm(values, axis=1)
    zero = norms < 1e-8
    values[zero] = (0, 0, 0, 1)
    norms[zero] = 1
    return values / norms[:,None]


def audit(psa, package, animation):
    source = chunks(psa)
    native = read_animation(package, animation)
    size = source['ANIMINFO'][1]
    keys = np.frombuffer(source['ANIMKEYS'][3], dtype='<f4').reshape(-1, 8)
    rows = source['ANIMINFO'][3]
    seqs = {s['name']: (s, m) for s, m in zip(native['sequences'], native['moves'])}
    result = dict(animation=animation, sequences=0, sampled_bone_poses=0,
                  max_position_error=0., max_rotation_error_degrees=0., worst_rotation=None)
    for offset in range(0, len(rows), size):
        row = rows[offset:offset+size]
        name = row[:64].split(b'\0')[0].decode()
        bones = struct.unpack_from('<i', row, 128)[0]
        fps = struct.unpack_from('<f', row, 152)[0]
        first, count = struct.unpack_from('<ii', row, 160)
        seq, move = seqs[name]
        assert seq['frames'] == count and abs(seq['rate']-fps) < 1e-5, name
        assert abs(move['duration']-count/fps) < 1e-4, name
        assert len(move['tracks']) == bones, name
        poses = keys[first*bones:(first+count)*bones].reshape(count, bones, 8)
        times = np.arange(count)/fps
        for bone, track in enumerate(move['tracks']):
            def sample(field):
                values = track[field]
                if len(values) == 1:
                    return np.repeat(values, count, axis=0)
                clock = track['time']
                if len(clock) == 0:
                    clock = np.arange(len(values))*move['duration']/len(values)
                assert len(clock) == len(values), (name, bone, field)
                query = np.clip(times, clock[0], clock[-1])
                if field == 'quat':
                    return Slerp(clock, Rotation.from_quat(normalized(values)))(query).as_quat()
                return np.column_stack([np.interp(query, clock, values[:,j]) for j in range(3)])
            position = sample('position')
            rotation = sample('quat')
            expected = normalized(poses[:,bone,3:7])
            rotation = normalized(rotation)
            errors = np.degrees(2*np.arccos(np.clip(np.abs(np.sum(rotation*expected,axis=1)),0,1)))
            err = float(errors.max())
            if err > result['max_rotation_error_degrees']:
                result['max_rotation_error_degrees'] = err
                result['worst_rotation'] = dict(sequence=name, bone=bone, frame=int(errors.argmax()))
            result['max_position_error'] = max(result['max_position_error'],float(np.linalg.norm(position-poses[:,bone,:3],axis=1).max()))
            result['sampled_bone_poses'] += count
        result['sequences'] += 1
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('psa', type=Path)
    p.add_argument('package', type=Path)
    p.add_argument('animation')
    args = p.parse_args()
    print(json.dumps(audit(args.psa, args.package, args.animation), indent=2))
