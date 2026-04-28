#!/usr/bin/env python
"""Compare native UClass layouts across three sources:
    1. binary .u files (the loaded UClass)
    2. v469 SDK headers
    3. our v400 source headers

Output: a per-class diff table.

Usage: python compare_layouts.py <binary.jsonl> <v469.jsonl> <v400.jsonl> <out.txt>
"""

from __future__ import print_function

import json
import os
import sys


def load_jsonl(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def index_classes(records):
    """Build {class_name: [list of records]} for headers (we may have dupes from
    forward-decls); for each class name keep only entries that actually have
    fields (those are the real class body parses)."""
    by_name = {}
    for r in records:
        if not r.get('fields'):
            continue
        # Keep the entry with the most fields (likely the real one)
        existing = by_name.get(r['class'])
        if existing is None or len(r['fields']) > len(existing['fields']):
            by_name[r['class']] = r
    return by_name


def index_binary_classes(records):
    """Index binary classes. Class names sometimes appear in multiple .u files
    when extended; keep the one in the package that defines it (first occurrence
    in the order we parsed)."""
    by_name = {}
    for r in records:
        # Skip non-property children (Functions, Enums, etc.) by filtering.
        # 'properties' field has type ending in 'Property'.
        # But classes may legitimately have 0 properties; keep them all.
        if r['class'] not in by_name:
            by_name[r['class']] = r
    return by_name


def field_names(record, header=False):
    if header:
        return [f['name'] for f in record['fields']]
    else:
        return [p['name'] for p in record.get('properties', []) if p.get('type', '').endswith('Property')]


def diff_class(name, binary, v469, v400):
    """Return human-readable diff for one class."""
    b_props = field_names(binary, header=False) if binary else None
    h469 = field_names(v469, header=True) if v469 else None
    h400 = field_names(v400, header=True) if v400 else None

    out = []
    out.append('===== %s =====' % name)
    out.append('  binary  %s' % ('absent' if b_props is None else '%d props' % len(b_props)))
    out.append('  v469    %s' % ('absent' if h469 is None else '%d fields' % len(h469)))
    out.append('  v400    %s' % ('absent' if h400 is None else '%d fields' % len(h400)))

    # If we have all three, compare orders and contents
    if b_props is not None and h469 is not None:
        # Find first divergence between binary and v469
        m = min(len(b_props), len(h469))
        first_diff = None
        for i in range(m):
            if b_props[i] != h469[i]:
                first_diff = i
                break
        if first_diff is None and len(b_props) == len(h469):
            out.append('  binary == v469 (perfect match, %d items)' % m)
        else:
            if first_diff is None:
                out.append('  binary vs v469: prefix matches (%d items), but lengths differ' % m)
            else:
                out.append('  binary vs v469: DIVERGE at index %d  bin=%r  v469=%r' % (
                    first_diff, b_props[first_diff], h469[first_diff]))
            # Show set differences
            sb, sv = set(b_props), set(h469)
            only_b = sb - sv
            only_v = sv - sb
            if only_b:
                out.append('    in binary, NOT in v469: %s' % sorted(only_b))
            if only_v:
                out.append('    in v469, NOT in binary: %s' % sorted(only_v))

    if b_props is not None and h400 is not None:
        m = min(len(b_props), len(h400))
        first_diff = None
        for i in range(m):
            if b_props[i] != h400[i]:
                first_diff = i
                break
        if first_diff is None and len(b_props) == len(h400):
            out.append('  binary == v400 (perfect match, %d items)' % m)
        else:
            if first_diff is None:
                out.append('  binary vs v400: prefix matches (%d items), but lengths differ' % m)
            else:
                out.append('  binary vs v400: DIVERGE at index %d  bin=%r  v400=%r' % (
                    first_diff, b_props[first_diff], h400[first_diff]))
            sb, sv = set(b_props), set(h400)
            only_b = sb - sv
            only_v = sv - sb
            if only_b:
                out.append('    in binary, NOT in v400: %s' % sorted(only_b)[:20])
            if only_v:
                out.append('    in v400, NOT in binary: %s' % sorted(only_v)[:20])

    return '\n'.join(out)


def summarize(name, binary, v469, v400):
    """One-line per-class summary for the master table."""
    b_props = field_names(binary, header=False) if binary else None
    h469 = field_names(v469, header=True) if v469 else None
    h400 = field_names(v400, header=True) if v400 else None

    def classify(props, header):
        if props is None or header is None:
            return 'N/A'
        if props == header:
            return 'EXACT'
        if len(props) == len(header):
            return 'SAME-LEN-DIFF'
        return 'LEN-DIFF(%+d)' % (len(header) - len(props))

    bn = 0 if b_props is None else len(b_props)
    vn = 0 if h469 is None else len(h469)
    on = 0 if h400 is None else len(h400)
    return '  %-30s bin=%-3d v469=%-3d v400=%-3d v469-vs-bin=%-15s v400-vs-bin=%-15s' % (
        name, bn, vn, on, classify(b_props, h469), classify(b_props, h400))


def main():
    if len(sys.argv) < 5:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    bin_path, v469_path, v400_path, out_path = sys.argv[1:5]

    binary_records = load_jsonl(bin_path)
    v469_records = load_jsonl(v469_path)
    v400_records = load_jsonl(v400_path)

    binary_by_name = index_binary_classes(binary_records)
    v469_by_name = index_classes(v469_records)
    v400_by_name = index_classes(v400_records)

    # Names worth comparing: those that appear in at least one header source
    # (because that's the C++ side we care about — pure-script classes have no
    # struct mismatch issue).
    interesting = set(v469_by_name.keys()) | set(v400_by_name.keys())
    # Only include classes that also appear in the binary
    interesting &= set(binary_by_name.keys())

    # Also try matching with leading-letter stripped (UClass headers have
    # class names like 'AActor', 'UFont', 'ULevel'; binary names are 'Actor',
    # 'Font', 'Level').
    binary_lc = {n.lower(): r for n, r in binary_by_name.items()}

    def find_binary(header_name):
        # Header class 'AActor' -> binary 'Actor'; 'UFont' -> 'Font'
        if header_name in binary_by_name:
            return binary_by_name[header_name]
        if header_name and header_name[0] in 'AU':
            stripped = header_name[1:]
            if stripped in binary_by_name:
                return binary_by_name[stripped]
        return None

    all_header_names = sorted(set(v469_by_name.keys()) | set(v400_by_name.keys()))
    pairs = []
    for hname in all_header_names:
        bin_rec = find_binary(hname)
        if bin_rec is None:
            continue
        v469_rec = v469_by_name.get(hname)
        v400_rec = v400_by_name.get(hname)
        pairs.append((hname, bin_rec, v469_rec, v400_rec))

    with open(out_path, 'w') as out:
        out.write('=== SUMMARY: %d classes compared ===\n' % len(pairs))
        out.write('  Format: bin=<binary-prop-count> v469=<v469-field-count> v400=<v400-field-count>\n')
        out.write('  EXACT = perfect match in name+order\n')
        out.write('  SAME-LEN-DIFF = same count, different names/order\n')
        out.write('  LEN-DIFF(+N) = differ by N fields\n\n')
        for name, b, v, o in pairs:
            out.write(summarize(name, b, v, o) + '\n')
        out.write('\n')
        out.write('=== PER-CLASS DETAIL ===\n')
        for name, b, v, o in pairs:
            out.write(diff_class(name, b, v, o) + '\n\n')
    print('Wrote %s' % out_path, file=sys.stderr)

    # Also print summary stats to stdout
    by_v469 = {'EXACT': 0, 'OTHER': 0, 'NOT-IN-HEADER': 0}
    by_v400 = {'EXACT': 0, 'OTHER': 0, 'NOT-IN-HEADER': 0}
    for name, b, v, o in pairs:
        b_props = field_names(b, header=False)
        h469 = field_names(v, header=True) if v else None
        h400 = field_names(o, header=True) if o else None
        if h469 is None:
            by_v469['NOT-IN-HEADER'] += 1
        elif b_props == h469:
            by_v469['EXACT'] += 1
        else:
            by_v469['OTHER'] += 1
        if h400 is None:
            by_v400['NOT-IN-HEADER'] += 1
        elif b_props == h400:
            by_v400['EXACT'] += 1
        else:
            by_v400['OTHER'] += 1
    print('Total compared: %d' % len(pairs))
    print('v469 vs binary: EXACT=%d  DIFF=%d  NOT-IN-HEADER=%d' % (
        by_v469['EXACT'], by_v469['OTHER'], by_v469['NOT-IN-HEADER']))
    print('v400 vs binary: EXACT=%d  DIFF=%d  NOT-IN-HEADER=%d' % (
        by_v400['EXACT'], by_v400['OTHER'], by_v400['NOT-IN-HEADER']))


if __name__ == '__main__':
    main()
