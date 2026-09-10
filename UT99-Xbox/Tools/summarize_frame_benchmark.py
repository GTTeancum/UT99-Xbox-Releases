"""Summarize steady timing windows from a native Xbox RAM log.

Use identical map, players, camera and settings for comparisons. Guest-time
limits exclude boot/loading and screenshot startup from the reported window.
"""
import argparse
import json
from pathlib import Path
import re
import statistics

def summarize(path, start, end, combat_relative=False):
    lines = path.read_text(errors='replace').splitlines()
    anchor = 0
    if combat_relative:
        starts = []
        for line in lines:
            match = re.search(r'\bt=(\d+) .*XCOMBAT elapsed=([\d.]+)', line)
            if match:
                starts.append(int(match[1])-float(match[2])*1000)
        if not starts:
            raise ValueError('No combat-start evidence in %s' % path)
        anchor = statistics.median(set(starts))
    series = {key: [] for key in ('meanDrawMS', 'meanWorkMS', 'wallFPS')}
    allocations = []
    profile = []
    world_profile = []
    view_profile = []
    hud_profile = []
    canvas_profile = []
    draw_profile = []
    visibility_profile = []
    raster_profiles = [[], []]
    warning_count = 0
    seen_lines = set()
    for line in lines:
        timestamp = re.search(r'\bt=(\d+) ', line)
        if not timestamp or not start <= int(timestamp[1])-anchor <= end:
            continue
        # A ring snapshot can repeat complete records when its byte overlap
        # was interrupted by an in-flight log write. Count each record once.
        if line in seen_lines:
            continue
        seen_lines.add(line)
        warning_count += 'ScriptWarning' in line
        match = re.search(r'XPROFILE samples=(\d+) totalMS=([\d.]+) levelMS=([\d.]+) clientMS=([\d.]+) otherMS=([\d.]+)',line)
        if match:
            profile.append(tuple(map(float,match.groups())))
        match = re.search(r'XPROFILEWORLD views=(\d+) occludeMS=([\d.]+) drawMS=([\d.]+)',line)
        if match:
            world_profile.append(tuple(map(float,match.groups())))
        match = re.search(r'XPROFILEVIEW views=(\d+) preMS=([\d.]+) audioMS=([\d.]+) worldMS=([\d.]+) hudMS=([\d.]+) unlockMS=([\d.]+) finishMS=([\d.]+)',line)
        if match:
            view_profile.append(tuple(map(float,match.groups())))
        match = re.search(r'XPROFILECANVAS views=(\d+) calls=(\d+) nativeMS=([\d.]+)',line)
        if match:
            views, calls, native_ms = map(float, match.groups())
            if views:
                canvas_profile.append((views, native_ms, calls/views))
        match = re.search(r'XPROFILEHUD views=(\d+) playerMS=([\d.]+) consoleMS=([\d.]+) nativeMS=([\d.]+) flashMS=([\d.]+)',line)
        if match:
            hud_profile.append(tuple(map(float,match.groups())))
        match = re.search(r'XRASTERPAIR legacy=([01]) views=(\d+) setupMS=([\d.]+) bspMS=([\d.]+)',line)
        if match:
            raster_profiles[int(match[1])].append(tuple(map(float,match.groups()[1:])))
        match = re.search(r'XPROFILEVIS views=(\d+) setupMS=([\d.]+) bspMS=([\d.]+)',line)
        if match:
            visibility_profile.append(tuple(map(float,match.groups())))
        match = re.search(r'XPROFILEDRAW views=(\d+) surfacesMS=([\d.]+) actorsMS=([\d.]+)',line)
        if match:
            draw_profile.append(tuple(map(float,match.groups())))
        for key in series:
            match = re.search(r'\b' + key + r'=([\d.]+)', line)
            if match:
                series[key].append(float(match[1]))
        match = re.search(r'SMOKE tick=(\d+).*heapTotalKB=(\d+)', line)
        if match:
            allocations.append(tuple(map(int, match.groups())))
    result = {'log': str(path.resolve()), 'guestWindowMS': [start, end],
              'observedScriptWarnings': warning_count}
    if combat_relative:
        result.pop('guestWindowMS')
        result.update(combatWindowMS=[start,end],combatStartGuestMS=anchor)
    for label, rows, keys in (
            ('engineProfile', profile, ('totalMS','levelMS','clientMS','otherMS')),
            ('worldProfilePerView',world_profile,('occludeMS','drawMS')),
            ('viewProfilePerView',view_profile,('preMS','audioMS','worldMS','hudMS','unlockMS','finishMS')),
            ('canvasProfilePerView',canvas_profile,('nativeMS','calls')),
            ('hudProfilePerView',hud_profile,('playerMS','consoleMS','nativeMS','flashMS')),
            ('drawProfilePerView',draw_profile,('surfacesMS','actorsMS')),
            ('visibilityProfilePerView',visibility_profile,('setupMS','bspMS')),
            ('rasterOptimizedPerView',raster_profiles[0],('setupMS','bspMS')),
            ('rasterLegacyPerView',raster_profiles[1],('setupMS','bspMS'))):
        count=sum(row[0] for row in rows)
        result[label] = dict(samples=int(count),windows=len(rows),
            **{key:sum(row[0]*row[i+1] for row in rows)/count for i,key in enumerate(keys)}) if count else None
    for key, values in series.items():
        result[key] = {'windows': len(values), 'mean': statistics.mean(values),
                       'median': statistics.median(values), 'min': min(values),
                       'max': max(values)} if values else None
    if len(allocations) > 1 and allocations[-1][0] > allocations[0][0]:
        result['allocatedKBPerTick'] = ((allocations[-1][1] - allocations[0][1]) /
                                        (allocations[-1][0] - allocations[0][0]))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path, nargs='+')
    parser.add_argument('--start-ms', type=int, default=40000)
    parser.add_argument('--end-ms', type=int, default=95000)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--combat-relative', action='store_true',
                        help='Interpret window limits relative to observed combat start')
    args = parser.parse_args()
    result = [summarize(p, args.start_ms, args.end_ms, args.combat_relative) for p in args.log]
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(text + '\n')
    print(text)

if __name__ == '__main__':
    main()
