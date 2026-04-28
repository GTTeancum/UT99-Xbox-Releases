#!/usr/bin/env python
"""Dump every UClass's property chain from one or more .u files.

Output is JSON-lines: one line per class, with fields:
    file, class, super, properties: [{name, type, array_dim, prop_flags}, ...]

Usage: python dump_all_classes.py <out.jsonl> <pkg.u> [pkg.u ...]
"""

from __future__ import print_function

import json
import os
import sys

# Reuse parser
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_uclass import (
    Package, read_compact_int, read_byte, read_word, read_dword,
    skip_tagged_properties, NAME_StructProperty, NAME_BoolProperty,
)


def read_uclass_children(pkg, ex):
    o = ex['offset']
    _super, o = read_compact_int(pkg.data, o)
    _next, o = read_compact_int(pkg.data, o)
    _script_text, o = read_compact_int(pkg.data, o)
    children, o = read_compact_int(pkg.data, o)
    return children


def read_uclass_super_name(pkg, ex):
    o = ex['offset']
    super_ref, _ = read_compact_int(pkg.data, o)
    if super_ref == 0:
        return None
    if super_ref > 0:
        return pkg.name(pkg.exports[super_ref - 1]['name_idx'])
    return pkg.name(pkg.imports[-super_ref - 1]['name_idx'])


def read_uproperty_header(pkg, ex):
    o = ex['offset']
    o = skip_tagged_properties(pkg.data, o, pkg)
    _super, o = read_compact_int(pkg.data, o)
    next_ref, o = read_compact_int(pkg.data, o)
    array_dim, o = read_dword(pkg.data, o)
    prop_flags, o = read_dword(pkg.data, o)
    category, o = read_compact_int(pkg.data, o)
    if prop_flags & 0x00000020:  # CPF_Net
        _rep_offset, o = read_word(pkg.data, o)
    return next_ref, array_dim, prop_flags


def read_field_next(pkg, ex):
    o = ex['offset']
    o = skip_tagged_properties(pkg.data, o, pkg)
    _super, o = read_compact_int(pkg.data, o)
    next_ref, o = read_compact_int(pkg.data, o)
    return next_ref


def collect_class(pkg, cls_idx, cls_ex):
    cls_name = pkg.name(cls_ex['name_idx'])
    super_name = read_uclass_super_name(pkg, cls_ex)
    properties = []
    cur = read_uclass_children(pkg, cls_ex)
    seen = set()
    while cur != 0:
        if cur in seen or cur < 0 or cur > len(pkg.exports):
            break
        seen.add(cur)
        ex = pkg.exports[cur - 1]
        name = pkg.name(ex['name_idx'])
        ci = ex['class_idx']
        if ci == 0:
            child_cls = 'Class'
        elif ci > 0:
            child_cls = pkg.name(pkg.exports[ci - 1]['name_idx'])
        else:
            child_cls = pkg.name(pkg.imports[-ci - 1]['name_idx'])
        try:
            if child_cls.endswith('Property'):
                next_ref, array_dim, prop_flags = read_uproperty_header(pkg, ex)
                properties.append({
                    'name': name,
                    'type': child_cls,
                    'array_dim': array_dim,
                    'prop_flags': prop_flags,
                })
            else:
                next_ref = read_field_next(pkg, ex)
            cur = next_ref
        except Exception as e:
            properties.append({'name': name, 'type': child_cls, 'parse_error': str(e)})
            break
    return {
        'class': cls_name,
        'super': super_name,
        'properties': properties,
    }


def dump_pkg(pkg, file_label, out):
    for i, ex in enumerate(pkg.exports):
        if ex['class_idx'] != 0:
            continue
        # Real UClass exports have ClassIndex==0 (the "Class" sentinel)
        cls = collect_class(pkg, i, ex)
        cls['file'] = file_label
        out.write(json.dumps(cls) + '\n')


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    out_path = sys.argv[1]
    paths = sys.argv[2:]
    with open(out_path, 'w') as out:
        for p in paths:
            label = os.path.basename(p)
            print('Parsing %s...' % label, file=sys.stderr)
            try:
                pkg = Package(p)
                dump_pkg(pkg, label, out)
            except Exception as e:
                print('  ERROR parsing %s: %s' % (p, e), file=sys.stderr)
    print('Wrote %s' % out_path, file=sys.stderr)


if __name__ == '__main__':
    main()
