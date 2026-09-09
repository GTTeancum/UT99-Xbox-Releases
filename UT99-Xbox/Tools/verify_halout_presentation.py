"""Isolated Xemu proofs for Halo first-person rendering, portrait and loading.

All actions are implemented by explicit in-game test markers/mutators. This
script never sends host keyboard, mouse or controller input.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
from pathlib import Path
from types import SimpleNamespace
import poll_xemu_ram_log as ram
import xemu_native_screenshot as native

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT/'UT99-Xbox/build_cli'


def run(args):
    evidence = WORK/'halo_presentation_harness'
    evidence.mkdir(exist_ok=True)
    stage = WORK/'halo_presentation_harness_stage'
    shutil.copytree(WORK/'manual_elite_approved',stage,dirs_exist_ok=True)
    for ini_name in ['Default.ini','UnrealTournament.ini']:
        ini = stage/'System'/ini_name
        text = ini.read_text()
        text = re.sub(r'(?m)^InitialBots=.*$', 'InitialBots=0', text)
        if 'InitialBots=' not in text:
            text = text.replace('[Botpack.DeathMatchPlus]', '[Botpack.DeathMatchPlus]\nInitialBots=0')
        ini.write_text(text)
    for path in stage.glob('Xbox*.ini'): path.unlink()
    for name in ['default.xbe','System/HaloUTXbox.u','System/HaloUTXbox.int','MenuAssets/char_haloelite.xui']:
        shutil.copy2(ROOT/'build'/name,stage/name)
    instance = evidence/'instance'; instance.mkdir(exist_ok=True)
    exe = instance/'xemu.exe'
    shutil.copy2(WORK/'elite_takeover_xemu/xemu.exe',exe)
    hdd = instance/'test_hdd.qcow2'
    if not hdd.exists(): shutil.copy2('C:/Games/Emulators/Xemu/UT99Test/HDD/ut99_hdd.qcow2',hdd)
    eeprom = instance/'eeprom.bin'
    shutil.copy2('C:/Games/Emulators/Xemu/EEPROM/eeprom.bin',eeprom)
    # This RVA was resolved by the native helper from this exact executable.
    if hashlib.sha256(exe.read_bytes()).hexdigest() == '7da537938ea2ac09f894186ba793c9ae51dff37c95903b0002273b8d363818b7':
        native._XEMU_SCREENSHOT_FLAG_RVA_CACHE[str(exe)] = 0x13a3ff0
    symbols = ram.resolve_symbols(str(ROOT/'build/UnrealTournament.map'),str(ROOT/'build/default.xbe'))
    specs = [('portrait',None,None)]
    for player,label in [('HaloUTXbox.Elite','elite'),('Botpack.TMale2','soldier')]:
        specs += [(label+'_'+weapon,player,'HaloUTXbox.Halo'+weapon) for weapon in ['AssaultRifle','Pistol','PlasmaRifle']]
    specs += [('elite_Enforcer','HaloUTXbox.Elite','Botpack.Enforcer')]
    if args.cases: specs = [s for s in specs if s[0] in args.cases]
    summaries = []
    for name,player,weapon in specs:
        out = evidence/name; out.mkdir(exist_ok=True)
        captures = out/'native'; captures.mkdir(exist_ok=True)
        for path in stage.glob('Xbox*.ini'): path.unlink()
        if player:
            url = 'DM-Morpheus.unr?Game=Botpack.DeathMatchPlus?Class='+player+'?Team=1?Name=HaloProof?MinPlayers=0?InitialBots=0?FragLimit=0?TimeLimit=0?Mutator=HaloUTXbox.FirstPersonProof'
            (stage/'XboxStartURL.ini').write_text('StartURL='+url+'\n')
            (stage/'System/AttachmentProof.ini').write_text('[HaloUTXbox.AttachmentProof]\nTestWeaponName='+weapon+'\n[HaloUTXbox.FirstPersonProof]\nTestWeaponName='+weapon+'\n')
        else:
            (stage/'XboxHaloPortraitProof.ini').write_text('; Native player setup portrait proof\n')
        iso = instance/'proof.iso'
        if iso.exists(): iso.unlink()
        subprocess.run(['C:/Programming/GitHub/Guitar Hero II/tools/artifacts/extract-xiso.exe','-Q','-m','-c',str(stage),str(iso)],check=True)
        cfg = instance/'proof.toml'
        cfg.write_text("[general]\nshow_welcome=false\nskip_boot_anim=true\nscreenshot_dir='"+str(captures)+"'\n[general.updates]\ncheck=false\n[input]\nauto_bind=false\n[input.bindings]\nport1='keyboard'\nport1_driver='usb-xbox-gamepad'\n[sys.files]\nbootrom_path='C:\\Games\\Emulators\\Xemu\\MCPX\\mcpx_1.0.bin'\nflashrom_path='C:\\Games\\Emulators\\Xemu\\BIOS\\xbox-4627_debug.bin'\neeprom_path='"+str(eeprom)+"'\nhdd_path='"+str(hdd)+"'\ndvd_path='"+str(iso)+"'\n")
        info = subprocess.STARTUPINFO(); info.dwFlags |= subprocess.STARTF_USESHOWWINDOW; info.wShowWindow=0
        print('START '+name,flush=True)
        captured = {}; lines = []; last_write=0; started=time.monotonic(); complete=False
        with (out/'stdout.txt').open('w') as stdout,(out/'stderr.txt').open('w') as stderr:
            proc = subprocess.Popen([str(exe),'-config_path',str(cfg),'-monitor','tcp:127.0.0.1:4477,server,nowait'],cwd=instance,stdout=stdout,stderr=stderr,startupinfo=info)
            try:
                while time.monotonic()-started < 160 and proc.poll() is None:
                    try:
                        _,_,text = ram.poll_port(4477,symbols,SimpleNamespace(timeout=2,phys_delta='auto',tail_bytes=8192))
                    except Exception:
                        time.sleep(.25); continue
                    for line in text.splitlines():
                        match = re.search(r'write=(\d+)',line)
                        if match and int(match[1])>last_write:
                            lines.append(line); last_write=int(match[1])
                    log = '\n'.join(lines)
                    (out/'ram_log.txt').write_text(log)
                    wanted = ['idle','primary','alternate'] if player else ['portrait']
                    for phase in wanted:
                        marker = 'FIRSTPERSONPROOF '+phase if player else 'tex loaded D:\\MenuAssets\\char_haloelite.xui'
                        if phase not in captured and marker in log:
                            ok,detail,path = native.trigger_native_screenshot(proc.pid,str(exe),str(captures),3)
                            if ok:
                                dest=out/(phase+'.png');shutil.copy2(path,dest);captured[phase]=str(dest)
                                print(name+' CAPTURE '+phase,flush=True)
                            else: print(name+' capture error '+detail,flush=True)
                    if not player and 'portrait' in captured:
                        complete=True;break
                    if player and len(captured)==3 and 'FIRSTPERSONPROOF loading-begin' in log:
                        tail=log.split('FIRSTPERSONPROOF loading-begin')[-1]
                        if 'loading silence off' in tail and 'native music stream started' in tail:
                            complete=True;break
                    time.sleep(.2)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:proc.wait(timeout=8)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait()
        log='\n'.join(lines)
        audio_ok=True
        if player:
            tail=log.split('FIRSTPERSONPROOF loading-begin')[-1]
            audio_ok='loading buffers musicPlaying=0 stream=0 activeEffects=0' in tail
            assert weapon in log and player in log, 'Wrong player/weapon identity'
        summary=dict(case=name,completed=complete,audioBuffersStopped=audio_ok,captures=captured,elapsed=time.monotonic()-started)
        summaries.append(summary)
        (evidence/'summary.json').write_text(json.dumps(summaries,indent=2))
        print('RESULT '+json.dumps(summary),flush=True)
        if not complete or not audio_ok:raise RuntimeError('Proof failed: '+name)
    print('ALL_PROOFS_PASSED',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cases',nargs='*')
    run(p.parse_args())
