"""Validate and summarize paired 4P presentation runs without menu/load timing."""
import argparse
import json
from pathlib import Path
import statistics

from summarize_frame_benchmark import summarize


def collect(plan_path, partial=False):
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    rows = []
    for run in plan['runs']:
        directory = Path(run['evidence']) / ('01_' + run['map'])
        summary_path = directory / 'summary.json'
        if partial and not summary_path.exists():
            continue
        summary = json.loads(summary_path.read_text(encoding='utf-8'))
        identity = json.loads((directory/'combat_identity.json').read_text(encoding='utf-8'))
        if not summary.get('ok') or not summary.get('combatProofComplete'):
            raise ValueError('Failed combat run: ' + str(directory))
        if summary.get('dashboardVideoProof') or summary.get('screenshots'):
            raise ValueError('Transition/capture timing is not a gameplay baseline: ' + str(directory))
        if (identity['players'] != 4 or identity['bots'] != 8 or identity['seconds'] != 90
                or identity['rasterMode'] != 'optimized' or identity['auditOverhead']
                or identity['files']['xbe']['sha256'] != plan['xbeSha256']):
            raise ValueError('Mismatched test setup: ' + str(directory))
        log = directory/'xemu_ram_log_accumulated.txt'
        raw = log.read_text(errors='replace')
        flags = '0x00000050' if run['wide'] else '0x00000040'
        region = 'region=80,0 240x240' if run['wide'] else 'region=0,0 320x240'
        if 'XVIDEO created flags='+flags not in raw or region not in raw:
            raise ValueError('Wrong scan mode or viewport layout: ' + str(directory))
        timing = summarize(log, 20000, 75000, True)
        if not timing['wallFPS'] or timing['wallFPS']['windows'] < 5:
            raise ValueError('Insufficient steady 4P timing: ' + str(directory))
        rows.append(dict(map=run['map'], wide=run['wide'], evidence=str(directory),
                         identity=identity, eepromSha256=run['eepromSha256'], timings=timing,
                         minAvailableMiB=summary['minAvailKB']/1024,
                         meanFPS=timing['wallFPS']['mean'], lowestWindowFPS=timing['wallFPS']['min']))
    pairs=[]
    for arena in dict.fromkeys(run['map'] for run in plan['runs']):
        pair={row['wide']:row for row in rows if row['map']==arena}
        if len(pair)!=2:
            if partial: continue
            raise ValueError('Missing pair: '+arena)
        for key in ('xbe','symbols','map'):
            if pair[False]['identity']['files'][key]['sha256'] != pair[True]['identity']['files'][key]['sha256']:
                raise ValueError('Different '+key+' in pair: '+arena)
        a,b=pair[False]['meanFPS'],pair[True]['meanFPS']
        pairs.append(dict(map=arena,standardFPS=a,pillarboxedFPS=b,
                          differenceFPS=b-a,percentDifference=100*(b-a)/a))
    result=dict(xbeSha256=plan['xbeSha256'], complete=len(rows)==len(plan['runs']),
                combatWindowSeconds=[20,75], players=4,bots=8,scan='480p',runs=rows,pairs=pairs,
                note='Xemu measurements, not retail Xbox FPS. Dynamic combat and host load differ; one run per map/mode does not isolate a causal performance gain. Lowest window is about five seconds, not a 1% low.')
    if pairs:
        result['equalMapMean']={key:statistics.mean(pair[key] for pair in pairs)
                                for key in ('standardFPS','pillarboxedFPS')}
    return result


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan',type=Path)
    parser.add_argument('--partial',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=collect(args.plan,args.partial)
    if args.output:
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    for row in result['runs']:
        print('%s %-8s %.2f FPS, lowest window %.2f, available %.2f MiB' %
              (row['map'],'wide' if row['wide'] else '4:3',row['meanFPS'],row['lowestWindowFPS'],row['minAvailableMiB']))
    print('Complete:',result['complete'],'paired means:',result.get('equalMapMean'))
