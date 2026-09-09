"""Summarize steady timing windows from a native Xbox RAM log.

Use identical map, players, camera and settings for comparisons. Guest-time
limits exclude boot/loading and screenshot startup from the reported window.
"""
import argparse
import json
from pathlib import Path
import re
import statistics

def summarize(path, start, end):
    series = {key: [] for key in ('meanDrawMS', 'meanWorkMS', 'wallFPS')}
    allocations = []
    warning_count = 0
    for line in path.read_text(errors='replace').splitlines():
        timestamp = re.search(r'\bt=(\d+) ', line)
        if not timestamp or not start <= int(timestamp[1]) <= end:
            continue
        warning_count += 'ScriptWarning' in line
        for key in series:
            match = re.search(r'\b' + key + r'=([\d.]+)', line)
            if match:
                series[key].append(float(match[1]))
        match = re.search(r'SMOKE tick=(\d+).*heapTotalKB=(\d+)', line)
        if match:
            allocations.append(tuple(map(int, match.groups())))
    result = {'log': str(path.resolve()), 'guestWindowMS': [start, end],
              'observedScriptWarnings': warning_count}
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
    args = parser.parse_args()
    result = [summarize(p, args.start_ms, args.end_ms) for p in args.log]
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(text + '\n')
    print(text)

if __name__ == '__main__':
    main()
