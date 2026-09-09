"""Convert the Project Torlan HaloUT assets into a UE1 skeletal package.

Run against an extracted HaloUT_Allfiles_In_One.1.zip, the command-line
UE Viewer exporter, and an isolated UT99 UCC installation. No GUI is used.
The source archive has MD5 d1c998d07f1b7cbfcd3b0d88ead016a7.
"""
from pathlib import Path
import argparse
import json
import re
import shutil
import struct
import subprocess

from PIL import Image
from normalize_halout_meshes import normalize


def chunks(path):
    data = path.read_bytes()
    pos = 0
    result = {}
    while pos < len(data):
        name, kind, size, count = struct.unpack_from('<20s3i', data, pos)
        pos += 32
        if size < 0 or count < 0 or pos + size * count > len(data):
            raise ValueError('Invalid ActorX chunk')
        result[name.rstrip(b'\0').decode()] = (kind, size, count, data[pos:pos + size * count])
        pos += size * count
    return result


def preserve_rifle_display_materials(path):
    """Translate Halo AR_ui FinalBlend/self-illumination to UE1 materials.

    The source displayfb2, compass and number layers disable ZWrite and
    blend over the housing. Opaque PSK defaults hide the underlying digits.
    UE1's translucent, unlit material is the available emissive blend.
    """
    data = bytearray(path.read_bytes())
    pos = 0
    while pos < len(data):
        name, kind, size, count = struct.unpack_from('<20s3i', data, pos)
        pos += 32
        if name.rstrip(b'\0') == b'MATT0000':
            if size != 88 or count != 6:
                raise ValueError('Unexpected assault rifle material layout')
            for material in range(1, 6):
                # ActorX uses MTT bits, not runtime EPolyFlags: translucent
                # type 2 plus unlit 16. UCC translates these on import.
                struct.pack_into('<I', data, pos + material*size + 68, 18)
        pos += size*count
    path.write_bytes(data)


def preserve_animation_keys(path):
    """Override UE Viewer's zero key-retention hint before UCC digestion.

    IMPORTSEQS reads KeyReduction from each ANIMINFO row, overriding the
    command's COMPRESS value. Zero reduces moving tracks to endpoints.
    One retains motion keys (UCC still removes redundant keys).
    """
    data = bytearray(path.read_bytes())
    pos = 0
    while pos < len(data):
        name, kind, size, count = struct.unpack_from('<20s3i', data, pos)
        pos += 32
        if size < 0 or count < 0 or pos + size*count > len(data):
            raise ValueError('Invalid ActorX chunk')
        if name.rstrip(b'\0') == b'ANIMINFO':
            if size != 168:
                raise ValueError('Unexpected ActorX animation metadata size')
            for row in range(count):
                struct.pack_into('<f', data, pos + row*size + 144, 1.0)
        pos += size*count
    path.write_bytes(data)


def join_actorx_sequences(original, sequences, parts, rate=30.0):
    """Join complete motions on one clock, preserving their authored durations."""
    import math
    import numpy as np
    from scipy.spatial.transform import Rotation, Slerp
    kind, key_size, key_count, key_data = original['ANIMKEYS']
    if key_size != 32:
        raise ValueError('Expected ActorX animation keys')
    rows = [sequences[name] for name in parts]
    bones = struct.unpack_from('<i', rows[0], 128)[0]
    clips = []
    total = 0.0
    for row in rows:
        if struct.unpack_from('<i', row, 128)[0] != bones:
            raise ValueError('Cannot join animations with different track counts')
        first, frames = struct.unpack_from('<ii', row, 160)
        fps = struct.unpack_from('<f', row, 152)[0]
        if frames < 1 or fps <= 0:
            raise ValueError('Invalid source animation duration')
        clips.append((total, first, frames, fps))
        total += frames / fps
    count = int(math.ceil(total * rate - 1e-6))
    # An exact duration also matters when the source rates do not divide 30.
    output_rate = count / total
    output = bytearray()
    for frame in range(count):
        seconds = frame / output_rate
        start, first, frames, fps = next(clip for clip in reversed(clips) if seconds >= clip[0])
        position = min((seconds-start)*fps, frames-1)
        a, b = int(position), min(int(position)+1, frames-1)
        alpha = position-a
        for bone in range(bones):
            ka = struct.unpack_from('<8f', key_data, ((first+a)*bones+bone)*key_size)
            kb = struct.unpack_from('<8f', key_data, ((first+b)*bones+bone)*key_size)
            p = np.array(ka[:3])*(1-alpha) + np.array(kb[:3])*alpha
            qa, qb = np.array(ka[3:7]), np.array(kb[3:7])
            qa = qa if qa@qa > 1e-6 else np.array([0,0,0,1])
            qb = qb if qb@qb > 1e-6 else np.array([0,0,0,1])
            q = Slerp([0,1], Rotation.from_quat([qa,qb]))([alpha]).as_quat()[0]
            output += struct.pack('<8f', *p, *q, 1.0)
    row = bytearray(rows[0])
    struct.pack_into('<i', row, 140, count*bones)
    struct.pack_into('<ff', row, 148, float(count), output_rate)
    struct.pack_into('<ii', row, 160, key_count//bones, count)
    original['ANIMKEYS'] = kind, key_size, key_count+count*bones, key_data+output
    return bytes(row)


def elite_animations(source, destination, soldier_source, mesh_source):
    """Retain Halo motion while supplying names used by UT99's player/bot code."""
    original = chunks(source)
    _, size, count, info = original['ANIMINFO']
    if size != 168:
        raise ValueError('Expected ActorX AnimInfoBinary records (168 bytes)')
    sequences = {}
    for i in range(count):
        row = info[i * size:(i + 1) * size]
        sequences[row[:64].split(b'\0')[0].decode()] = row
    sequences['EliteJumpFull'] = join_actorx_sequences(
        original, sequences, ('JumpF_Takeoff', 'JumpF_Mid'))
    names = re.findall(r'#exec MESH SEQUENCE.*?SEQ=(\w+)([^\n]*)', soldier_source)
    aliases = {}
    timings = {}
    deaths = {'Dead1': 'DeathR', 'Dead2': 'DeathF', 'Dead3': 'DeathB',
              'Dead4': 'DeathL', 'Dead7': 'DeathB', 'Dead8': 'DeathF',
              'Dead9': 'DeathL', 'Dead9B': 'DeathL', 'Dead11': 'DeathB'}
    for name, rest in names:
        if name == 'All':
            continue
        group = re.search(r'Group=(\w+)', rest, re.I)
        group = group[1] if group else ''
        target_frames = re.search(r'NUMFRAMES=(\d+)', rest, re.I)
        target_rate = re.search(r'RATE=([0-9.]+)', rest, re.I)
        if target_frames and target_rate and int(target_frames[1]) > 1:
            timings[name] = (int(target_frames[1]), float(target_rate[1]))
        if name.startswith('DeathEnd'):
            motion = deaths[{'DeathEnd': 'Dead1', 'DeathEnd2': 'Dead2', 'DeathEnd3': 'Dead3'}[name]]
        elif name.startswith('Dead'):
            motion = deaths[name]
        elif name in ('GutHit', 'HeadHit', 'LeftHit', 'RightHit'):
            motion = {'GutHit': 'HitF', 'HeadHit': 'Hit_Head', 'LeftHit': 'HitL', 'RightHit': 'HitR'}[name]
        elif name == 'BackRun':
            motion = 'RunB'
        elif name.startswith('Run'):
            motion = 'RunF'
        elif name.startswith('Strafe'):
            motion = 'RunL' if name.endswith('L') else 'RunR'
        elif name.startswith('Walk'):
            motion = 'WalkF'
        elif name.startswith('Duck'):
            motion = 'CrouchF'
        elif name.startswith('Jump'):
            motion = 'EliteJumpFull'
        elif name.startswith('Land'):
            motion = 'JumpF_Land'
        elif name.startswith('Dodge'):
            motion = {'DodgeB': 'DodgeB', 'DodgeF': 'DodgeF', 'DodgeL': 'DodgeL', 'DodgeR': 'DodgeR'}.get(name, 'DodgeF')
        elif name.startswith('Swim'):
            motion = 'SwimF'
        elif name.startswith('Tread'):
            motion = 'Swim_Tread'
        elif name.startswith('Turn'):
            motion = 'TurnL'
        elif name.startswith('Still'):
            motion = 'Rifle_Burst'
        elif name.startswith('Aim'):
            motion = 'Rifle_Aimed'
        elif name in ('CockGun', 'CockGunL'):
            motion = 'Weapon_Switch'
        elif name in ('Chat1', 'Chat2'):
            motion = 'idle_chat'
        elif name in ('Look', 'LookL'):
            motion = 'Idle_Character01'
        elif name in ('Taunt1', 'Victory1', 'Wave', 'WaveL', 'Thrust', 'Flip'):
            motion = {'Taunt1': 'Gesture_Taunt01', 'Victory1': 'gesture_cheer', 'Wave': 'gesture_beckon', 'WaveL': 'gesture_beckon', 'Thrust': 'PThrust', 'Flip': 'Gesture_Taunt02'}[name]
        else:
            motion = 'Idle_Rifle'
        aliases[name] = (motion, group)
    rows = []
    key_kind, key_size, key_count, key_data = original['ANIMKEYS']
    key_data = bytearray(key_data)
    for name, (motion, group) in aliases.items():
        row = bytearray(sequences[motion])
        row[:64] = name.encode().ljust(64, b'\0')
        row[64:128] = group.encode().ljust(64, b'\0')
        if name.startswith(('AimUp', 'AimDn')):
            # UT99 tweens to a single aimed pose. Halo's source has only level
            # aim; rotate the upper-body subtree, retaining the authored grip.
            from halout_aim_pose import aimed_frame
            bones = struct.unpack_from('<i', row, 128)[0]
            first = struct.unpack_from('<i', row, 160)[0]
            posed = aimed_frame(original, chunks(mesh_source), first, 45 if name.startswith('AimUp') else -45)
            new_first = len(key_data) // (key_size * bones)
            key_data += posed
            struct.pack_into('<ii', row, 160, new_first, 1)
            struct.pack_into('<f', row, 148, 1.0)
            struct.pack_into('<i', row, 140, bones)
        if name in timings:
            target_frames, target_rate = timings[name]
            source_frames = struct.unpack_from('<i', row, 164)[0]
            # Preserve UT's cycle duration while retaining every Halo source key.
            struct.pack_into('<f', row, 152, source_frames * target_rate / target_frames)
        if name.startswith('DeathEnd') or name == 'Dead9B':
            # UT99 uses these one-frame sequences for already-dead bodies.
            # ActorX stores FirstRawFrame/NumRawFrames at 160/164 and
            # TrackTime in source frames at 148 (see AnimInfoBinary).
            first, frames = struct.unpack_from('<ii', row, 160)
            struct.pack_into('<ii', row, 160, first + frames - 1, 1)
            struct.pack_into('<f', row, 148, 1.0)
            struct.pack_into('<i', row, 140, struct.unpack_from('<i', row, 128)[0])
        rows.append(bytes(row))
    original['ANIMINFO'] = (original['ANIMINFO'][0], size, len(rows), b''.join(rows))
    original['ANIMKEYS'] = (key_kind, key_size, len(key_data) // key_size, key_data)
    with destination.open('wb') as f:
        for name, (kind, size, count, data) in original.items():
            f.write(struct.pack('<20s3i', name.encode(), kind, size, count))
            f.write(data)
    return aliases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--umodel', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = args.compiler.resolve()
    package = compiler / 'HaloUTXbox'
    export = compiler / 'HaloUTExport'
    for folder in ('Classes', 'Models', 'Textures'):
        (package / folder).mkdir(parents=True, exist_ok=True)
    for archive, obj in [('HaloCreatures', 'Elite'), ('HaloCreatures', 'EliteAni'),
                         ('Halocreaturestex', 'RedEliteComposite'),
                         ('Halocreaturestex', 'SpecOpsEliteComposite'),
                         ('Haloweapons', '')]:
        command = [str(args.umodel.resolve()), '-export', '-sounds', '-path=' + str(args.source.resolve()),
                   '-out=' + str(export), archive]
        if obj:
            command.append(obj)
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    shutil.copy2(export / 'HaloCreatures/SkeletalMesh/Elite.psk', package / 'Models/Elite.psk')
    aliases = elite_animations(export / 'HaloCreatures/MeshAnimation/EliteAni.psa',
                               package / 'Models/Elite.psa',
                               (root / 'Botpack/Classes/TMale2.uc').read_text(),
                               package / 'Models/Elite.psk')
    textures = {'EliteBlue': export / 'Halocreaturestex/Texture/BlueEliteComposite.tga',
                'EliteRed': export / 'Halocreaturestex/Texture/RedEliteComposite.tga',
                'EliteSpecOps': export / 'Halocreaturestex/Texture/SpecOpsEliteComposite.tga'}
    for name, original in [('AssaultSkin', 'Assault'), ('PistolSkin', 'Pistoltex'),
                           ('PlasmaSkin', 'plasmar-tex'), ('HeatGauge', 'heatgauge'),
                           ('Compass', 'compass_plate1'), ('AmmoDisplay', 'display_fp1')]:
        textures[name] = export / 'Haloweapons/Texture' / (original + '.tga')
    for i in range(10):
        textures['Digit' + str(i)] = export / 'Haloweapons/Texture' / ('numbers_plate%d.tga' % (i + 1))
    for i in range(8):
        textures['Compass' + str(i)] = export / 'Haloweapons/Texture' / ('compass_plate%d.tga' % (i + 1))
    for name, original, anim in [('Assault', 'Halo-ar', 'AR-anim'),
                                  ('Pistol', 'Halo-pistol', 'Pistol-anim'),
                                  ('Plasma', 'Halo-PlasmaRifle', 'Plasma-anim')]:
        for suffix in ('1st', '3rd'):
            shutil.copy2(export / 'Haloweapons/SkeletalMesh' / (original + '_' + suffix + '.psk'),
                         package / 'Models' / (name + suffix + '.psk'))
            if name == 'Assault':
                preserve_rifle_display_materials(package / 'Models' / (name + suffix + '.psk'))
        shutil.copy2(export / 'Haloweapons/MeshAnimation' / (anim + '.psa'), package / 'Models' / (name + '.psa'))
    (package / 'Sounds').mkdir(exist_ok=True)
    for name, original in [('AssaultFire', 'fire2_1'), ('PistolFire', 'handgun_fire'), ('PlasmaFire', 'plasmarifle1')]:
        shutil.copy2(export / 'Haloweapons/Sound' / (original + '.wav'), package / 'Sounds' / (name + '.wav'))
    for name in ('Elite', 'Assault', 'Pistol', 'Plasma'):
        preserve_animation_keys(package / 'Models' / (name + '.psa'))
    # P8 and a bounded resolution keep character texture residency small on 64MB.
    for name, source in textures.items():
        image = Image.open(source).convert('RGB')
        image.thumbnail((256, 256), Image.Resampling.LANCZOS)
        image.quantize(colors=256).save(package / 'Textures' / (name + '.pcx'))
    Image.new('P', (2, 2), 0).save(package / 'Textures/Invisible.pcx')
    for source in (root / 'UT99-Xbox/HaloUTXbox/Classes').glob('*.uc'):
        shutil.copy2(source, package / 'Classes' / source.name)
        if source.name == 'EliteAssets.uc':
            # Runtime skeletal sequences come from UAnimation, so notifications
            # must be added there, not to the mesh's replaceable sequence copy.
            soldier = (root / 'Botpack/Classes/TMale2.uc').read_text()
            notices = re.findall(r'#exec MESH NOTIFY MESH=Soldier (.*)', soldier)
            with (package / 'Classes' / source.name).open('a') as handle:
                handle.write('\n// Inherited UT99 footsteps and landing/death events.\n')
                for notice in notices:
                    handle.write('#exec ANIM NOTIFY ANIM=EliteAnims ' + notice + '\n')
    # Use the same script dependencies as the Xbox runtime; keep the compiler's
    # native Core/Engine packages paired with its DLLs.
    for name in ('Botpack.u', 'UnrealShare.u', 'UnrealI.u'):
        shutil.copy2(args.runtime / 'System' / name, compiler / 'System' / name)
    ini = '[Core.System]\nPaths=../System/*.u\n'
    for folder, ext in [('Textures', 'utx'), ('Sounds', 'uax')]:
        ini += 'Paths=' + str(args.runtime.resolve() / folder / ('*.' + ext)) + '\n'
    ini += '\n[Engine.Engine]\nEditorEngine=Editor.EditorEngine\n[Editor.EditorEngine]\n'
    for name in ('Core', 'Engine', 'UnrealShare', 'UnrealI', 'Botpack', 'HaloUTXbox'):
        ini += 'EditPackages=' + name + '\n'
    (compiler / 'System/HaloBuild.ini').write_text(ini)
    output = compiler / 'System/HaloUTXbox.u'
    if output.exists():
        output.unlink()
    result = subprocess.run([str(compiler / 'System/ucc.exe'), 'make', 'ini=HaloBuild.ini', '-unattended'],
                            cwd=compiler / 'System', capture_output=True, text=True)
    (compiler / 'HaloBuild.log').write_text(result.stdout + result.stderr)
    if result.returncode or not output.exists() or '0 error(s)' not in result.stdout:
        raise RuntimeError('HaloUT compile failed; see ' + str(compiler / 'HaloBuild.log'))
    (compiler / 'HaloAnimationMap.json').write_text(json.dumps(aliases, indent=2))
    (compiler / 'HaloMeshConversion.json').write_text(json.dumps(normalize(output), indent=2))
    from audit_animation_import import audit
    pose_audits = [audit(package / 'Models' / (name + '.psa'), output, name + 'Anims')
                   for name in ('Elite', 'Assault', 'Pistol', 'Plasma')]
    (compiler / 'HaloAnimationPoseAudit.json').write_text(json.dumps(pose_audits, indent=2))
    for report in pose_audits:
        # UCC removes nearly redundant keys even at full retention. Bound that
        # error at every source frame; an endpoint-only import fails decisively.
        if report['max_rotation_error_degrees'] > 0.1 or report['max_position_error'] > 0.001:
            raise RuntimeError('Animation import loses source poses: ' + report['animation'])
    from audit_halout_weapon_grips import audit as audit_grips, audit_elite_frame
    (compiler / 'HaloWeaponGripAudit.json').write_text(json.dumps(
        dict(grips=audit_grips(output), eliteIdle=audit_elite_frame(output)), indent=2))
    from audit_halout_firstperson import audit as audit_firstperson
    (compiler / 'HaloFirstPersonAudit.json').write_text(json.dumps(
        audit_firstperson(output, package / 'Classes'), indent=2))
    print(output)


if __name__ == '__main__':
    main()
