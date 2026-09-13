"""Compare completed legacy/optimized combat runs from the same binary.

Matching metadata does not make combat deterministic or control host load.
"""
import argparse
import json
from pathlib import Path

from summarize_frame_benchmark import summarize


def compare(legacy, optimized, start=20000, end=75000):
    identities = []
    timings = []
    for directory, mode in ((legacy, 'legacy'), (optimized, 'optimized')):
        identity = json.loads((directory / 'combat_identity.json').read_text())
        summary = json.loads((directory / 'summary.json').read_text())
        if identity.get('rasterMode') != mode or identity.get('auditOverhead') is not False:
            raise ValueError('Wrong mode or audit overhead: %s' % directory)
        if not summary.get('ok') or not summary.get('combatProofComplete'):
            raise ValueError('Incomplete or failed combat: %s' % directory)
        for field in ('screenshots', 'cameraScreenshots', 'lightingScreenshots',
                      'traversalScreenshots', 'stateScreenshots'):
            if summary.get(field):
                raise ValueError('Gameplay capture overhead: %s' % directory)
        if (summary.get('rasterMode') != mode or summary.get('rasterAuditOverhead') is not False
                or summary.get('rasterUpdatesVerifiedAtLeast') or summary.get('rasterQueriesVerifiedAtLeast')):
            raise ValueError('Unexpected raster audit evidence: %s' % directory)
        identities.append(identity)
        timings.append(summarize(directory / 'xemu_ram_log_accumulated.txt', start, end, True))
    for field in ('players', 'bots', 'seconds', 'profiling'):
        if identities[0][field] != identities[1][field]:
            raise ValueError('Mismatched %s' % field)
    for field in ('xbe', 'symbols', 'map'):
        if identities[0]['files'][field]['sha256'] != identities[1]['files'][field]['sha256']:
            raise ValueError('Mismatched %s hash' % field)
    metrics = {}
    for group, key in (('meanWorkMS', 'mean'), ('wallFPS', 'mean'),
                       ('visibilityProfilePerView', 'setupMS'),
                       ('visibilityProfilePerView', 'bspMS')):
        rows = [timing.get(group) for timing in timings]
        if any(not row or row.get('windows', 0) < 5 for row in rows):
            raise ValueError('Insufficient %s windows' % group)
        before, after = [row[key] for row in rows]
        metrics[group + '.' + key] = dict(legacy=before, optimized=after,
            difference=after-before, percentChange=100*(after-before)/before if before else None)
    return dict(legacy=str(legacy.resolve()), optimized=str(optimized.resolve()),
        combatWindowMS=[start, end], metrics=metrics, timings=timings,
        caveat='Metadata matched; combat and host load are not controlled. Repeat before attributing a gain.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('legacy', type=Path)
    parser.add_argument('optimized', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(compare(args.legacy, args.optimized), indent=2) + '\n'
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    else:
        print(result)


if __name__ == '__main__':
    main()
