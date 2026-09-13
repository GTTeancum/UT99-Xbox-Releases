"""Validate measured pawn rotation, including every port and stick-test phase."""
import argparse, json, re, statistics
from pathlib import Path

def collect(path,players):
    text=path.read_text(errors='replace')
    pattern=r'XSENS slot=(\d+) players=(\d+) phase=(\d+) seconds=([\d.]+) samples=(\d+) yawDPS=([\d.-]+) pitchDPS=([\d.-]+)'
    # Preserve separate equal-valued windows; only remove repeated ring records.
    lines=list(dict.fromkeys(line for line in text.splitlines() if 'XSENS slot=' in line))
    rows=[]
    # The native XSENS "players" field counts allocated viewports. Split
    # mode retains four objects, including inactive dummies in a 2P game.
    if players > 1:
        active=set(map(int,re.findall(r'XSPLIT configured viewports=\d+ activeMask=0x[0-9A-Fa-f]+ activePlayers=(\d+)',text)))
        if active != {players}:
            raise ValueError('Missing or mismatched active-player configuration')
    expected=[298.5984 * .9999,149.2992 * .9999,149.2992 * .9999,151.875 * .9999]
    for line in lines:
        m=re.search(pattern,line)
        if not m: continue
        slot,n,phase=map(int,m.groups()[:3]); seconds=float(m[4]); samples=int(m[5])
        yaw,pitch=map(float,m.groups()[5:])
        if n!=(1 if players==1 else 4) or not 1<=slot<=players or phase not in range(4):
            raise ValueError('Unexpected player count or phase: '+line)
        rate=pitch if phase==3 else yaw
        if seconds<4 or samples<1 or abs(rate/expected[phase]-1)>.02:
            raise ValueError('Measured rotation outside 2% tolerance: '+line)
        if abs(yaw if phase==3 else pitch)>1:
            raise ValueError('Cross-axis movement: '+line)
        rows.append(dict(slot=slot,allocatedViewports=n,phase=phase,seconds=seconds,samples=samples,
                         simulationFPS=samples/seconds,yawDPS=yaw,pitchDPS=pitch))
    if {(r['slot'],r['phase']) for r in rows}!={(s,p) for s in range(1,players+1) for p in range(4)}:
        raise ValueError('Missing player/phase measurements')
    return dict(players=players,windows=len(rows),source=str(path),measurements=rows,
        simulationFPSRange=[min(r['simulationFPS'] for r in rows),max(r['simulationFPS'] for r in rows)],
        phaseMeans=[statistics.mean(r['pitchDPS'] if p==3 else r['yawDPS'] for r in rows if r['phase']==p) for p in range(4)])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--players',type=int,required=True);p.add_argument('--output',type=Path)
    a=p.parse_args();d=collect(a.log,a.players)
    if a.output:a.output.write_text(json.dumps(d,indent=2)+'\n')
    print('PASS',d['players'],'players;',d['windows'],'windows; DPS',d['phaseMeans'],'simulation FPS',d['simulationFPSRange'])
