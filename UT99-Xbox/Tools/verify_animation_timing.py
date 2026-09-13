"""Measure rendered animation advancement from native guest telemetry."""
import argparse
import json
import pathlib
import re
import statistics

STATE = re.compile(r'XSKELSTATE tick=(\d+).*?seq=(\S+) frame=([0-9.]+) numframes=(\d+) rate=([0-9.]+) found=1 elapsed=([0-9.]+) cycleSeconds=([0-9.]+)(?: speed=1.0 loop=(\d+))?')
LIVE = re.compile(r'UT99XDBG t=(\d+).*?XSKELPLAY tick=(\d+) actor=(.+?) seq=(\S+) frame=([-0-9.]+) normRate=([-0-9.]+) numframes=(\d+) seqRate=([0-9.]+) loop=(\d+)')


def summarize(text, live_time_dilation=1.0):
    states = {}
    seen = set()
    last = None
    for tick, name, frame, count, rate, elapsed, cycle, loop in STATE.findall(text):
        if tick in seen:
            continue
        seen.add(tick)
        frame, count, rate, elapsed = float(frame), int(count), float(rate), float(elapsed)
        row = states.setdefault(name, {'frames': count, 'rate': rate, 'cycleSeconds': float(cycle), 'samples': 0, 'speedRatios': []})
        row['samples'] += 1
        if last and last[0] == name and elapsed > last[2] and count > 1:
            expected = (elapsed-last[2])*rate/count
            if loop == '0' or (not loop and name.startswith('Dead')):
                expected = min(expected, max(0, 1-1/count-last[1]))
            if .02 < expected < .90:
                observed = (frame-last[1]) % 1.0
                row['speedRatios'].append(observed/expected)
        last = name, frame, elapsed
    live = {}
    seen = set()
    last = None
    for ms, tick, actor, name, frame, norm_rate, count, seq_rate, loop in LIVE.findall(text):
        if tick in seen:
            continue
        seen.add(tick)
        ms, frame, norm_rate, count, seq_rate = int(ms), float(frame), float(norm_rate), int(count), float(seq_rate)
        row = live.setdefault(name, {'frames': count, 'sequenceRate': seq_rate, 'speedRatios': [], 'requestedMultipliers': []})
        if count > 1 and norm_rate > 0 and seq_rate > 0:
            row['requestedMultipliers'].append(norm_rate*count/seq_rate)
        if last and last[0] == actor and last[1] == name and frame >= 0 and last[2] >= 0 and norm_rate > 0 and abs(norm_rate-last[4]) < .001:
            expected = (ms-last[3])*.001*norm_rate*live_time_dilation
            if .02 < expected < .90 and (loop == '1' or frame > last[2]):
                row['speedRatios'].append(((frame-last[2]) % 1.0)/expected)
        last = actor, name, frame, ms, norm_rate
    for group in (states, live):
        for row in group.values():
            values = row.pop('speedRatios')
            row['measuredPairs'] = len(values)
            row['medianSpeedRatio'] = statistics.median(values) if values else None
            if 'requestedMultipliers' in row:
                values = row.pop('requestedMultipliers')
                row['medianRequestedMultiplier'] = statistics.median(values) if values else None
    measured = [row for group in (states, live) for row in group.values() if row['measuredPairs'] >= 2]
    return {'states': states, 'live': live, 'liveTimeDilation': live_time_dilation, 'measuredSequences': len(measured),
            'timingPassed': bool(measured) and all(.85 <= row['medianSpeedRatio'] <= 1.15 for row in measured)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--live-time-dilation', type=float, default=1.0,
                        help='Level.TimeDilation for comparison against wall-clock guest timestamps')
    args = parser.parse_args()
    if args.live_time_dilation <= 0:
        parser.error('--live-time-dilation must be positive')
    result = summarize(args.log.read_text(errors='replace'), args.live_time_dilation)
    args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps({'timingPassed': result['timingPassed'], 'measuredSequences': result['measuredSequences'], 'output': str(args.output)}))
