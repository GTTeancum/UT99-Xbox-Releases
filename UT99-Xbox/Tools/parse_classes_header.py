#!/usr/bin/env python
"""Parse a UT99 EngineClasses.h-style auto-generated C++ header and extract
each class's field list in declaration order.

Output is JSON-lines: one record per class, with keys
    file, class, super, fields: [{name, type, array_dim, is_bitfield}, ...]

Usage: python parse_classes_header.py <out.jsonl> <header.h> [header.h ...]
"""

from __future__ import print_function

import json
import os
import re
import sys


# Field declaration patterns we recognize inside a class body.
# Order matters — try BITFIELD before generic.

CLASS_RE = re.compile(
    # NOTE: require leading 'class' at column 0 (no stripping) so we don't
    # match `    class Foo* bar;` field declarations.  We also require
    # inheritance (`: public Bar`) which all real engine class definitions have.
    r'^class\s+(?:[A-Z_]+_API\s+)?([A-Za-z_][A-Za-z_0-9]*)'
    r'\s*:\s*public\s+([A-Za-z_][A-Za-z_0-9:]*)'
)

# `BITFIELD bName:1;` or `BITFIELD bName:1 GCC_PACK(...);`
BITFIELD_RE = re.compile(
    r'^\s*BITFIELD\s+([A-Za-z_][A-Za-z_0-9]*)\s*:\s*1\s*(?:GCC_PACK\([^)]*\))?\s*;'
)

# `Type FieldName;` or `Type FieldName[N];` or `Type FieldName GCC_PACK(...);`
# Type can be: basic (INT, FLOAT, BYTE, FName, FString, FStringNoInit, FVector,
# FRotator, FCoords, FPlane, FColor, FScale, DWORD, _WORD, ANSICHAR, UNICHAR,
# QWORD, SQWORD, SBYTE, BITFIELD), or `class Foo*`, or struct names with
# leading 'F' / 'A' / 'U'.
FIELD_RE = re.compile(
    r'^\s*'
    r'(?P<type>'
        r'(?:class\s+)?'                               # optional class keyword
        r'(?:[A-Za-z_][A-Za-z_0-9]*)'                  # base type name
        r'(?:\s*<\s*[^>]+\s*>)?'                       # optional template args
        r'(?:\s*\*)?'                                  # optional pointer
    r')'
    r'\s+'
    r'(?P<name>[A-Za-z_][A-Za-z_0-9]*)'                # field name
    r'(?:\s*\[\s*(?P<dim>\d+)\s*\])?'                  # optional array dim
    r'(?:\s*GCC_PACK\([^)]*\))?'                       # optional GCC_PACK
    r'\s*;'
)

# Things we DO NOT want to capture as fields
SKIP_RE_LIST = [
    re.compile(r'^\s*//'),                       # comment line
    re.compile(r'^\s*public\s*:'),
    re.compile(r'^\s*private\s*:'),
    re.compile(r'^\s*protected\s*:'),
    re.compile(r'^\s*friend\b'),
    re.compile(r'^\s*typedef\b'),
    re.compile(r'^\s*enum\b'),
    re.compile(r'^\s*virtual\b'),
    re.compile(r'^\s*static\b'),
    re.compile(r'^\s*inline\b'),
    re.compile(r'^\s*explicit\b'),
    re.compile(r'^\s*operator\b'),
    re.compile(r'^\s*~'),
    re.compile(r'^\s*DECLARE_'),
    re.compile(r'^\s*IMPLEMENT_'),
    re.compile(r'^\s*NO_DEFAULT_CONSTRUCTOR'),
    re.compile(r'^\s*#'),                        # preprocessor
]

# Tokens that, if present in the captured "type" position, mean it's a method,
# return type, or other non-field declaration we should skip.
NON_TYPE_FIRST_WORDS = {
    'return', 'if', 'else', 'while', 'for', 'switch', 'case', 'default',
    'goto', 'break', 'continue',
}

# Method-like patterns: <type> name (...)
METHOD_RE = re.compile(r'^\s*[A-Za-z_:][A-Za-z_:0-9*<>\s]*\s+[A-Za-z_][A-Za-z_0-9]*\s*\(')


def is_skip_line(line):
    if METHOD_RE.match(line):
        return True
    for pat in SKIP_RE_LIST:
        if pat.match(line):
            return True
    return False


def parse_header(path):
    """Parse one header file. Yield class records.

    Strategy: line-based scan. Track when we enter a class body (after `class
    Foo : public Bar {`), accumulate fields until matching close brace, then
    emit the class. Brace counting handles nested struct/enum.
    """
    classes = []
    with open(path, 'r', encoding='latin-1') as f:
        lines = f.read().split('\n')

    i = 0
    n = len(lines)
    label = os.path.basename(path)
    while i < n:
        line = lines[i]
        m = CLASS_RE.match(line)
        if not m:
            i += 1
            continue
        cls_name = m.group(1)
        super_name = m.group(2)
        # Find the opening '{' (could be on same line or next line)
        # Then accumulate until matching '}'
        depth = 0
        in_body = False
        body_started = False
        # Look forward for '{'
        j = i
        while j < n:
            depth += lines[j].count('{')
            depth -= lines[j].count('}')
            if '{' in lines[j]:
                body_started = True
            if body_started and depth == 0:
                break
            j += 1
        if j >= n:
            i += 1
            continue

        # Now scan lines (i+1 .. j) for fields.  Use a depth counter to stay at
        # depth-1 (the class's own scope, not nested braces).
        fields = []
        scope = 0
        for k in range(i, j + 1):
            l = lines[k]
            # Count braces incrementally
            opens = l.count('{')
            closes = l.count('}')
            # Process line's content: only consider lines whose START is at
            # scope==1 (inside the class but not inside a nested brace block).
            # For lines that open or close braces mid-line, we approximate.
            line_start_scope = scope + opens  # after processing opens
            # If the line has no braces and we're at scope 1, it's a candidate
            if scope == 0 and opens == 0:
                pass  # not yet inside the class
            elif scope == 1 and opens == 0 and closes == 0:
                # Pure content inside class body.
                # Try BITFIELD first, then FIELD, then explicit-skip patterns.
                # We can't apply skip patterns before field detection because
                # METHOD_RE matches `BITFIELD bStatic:1 GCC_PACK(INT_ALIGNMENT);`
                # (the GCC_PACK call looks like a method invocation).
                bf = BITFIELD_RE.match(l)
                if bf:
                    fields.append({
                        'name': bf.group(1),
                        'type': 'BITFIELD',
                        'array_dim': 1,
                        'is_bitfield': True,
                    })
                else:
                    fm = FIELD_RE.match(l)
                    is_field = False
                    if fm:
                        t = fm.group('type').strip()
                        t_first = t.split()[0] if t else ''
                        if t_first not in NON_TYPE_FIRST_WORDS:
                            fields.append({
                                'name': fm.group('name'),
                                'type': t,
                                'array_dim': int(fm.group('dim')) if fm.group('dim') else 1,
                                'is_bitfield': False,
                            })
                            is_field = True
                    if not is_field:
                        # Not a field — fall through to explicit skip patterns
                        # (for completeness, no action needed since we only
                        # collect fields here).
                        pass
            scope = scope + opens - closes
            if scope < 0:
                scope = 0

        classes.append({
            'file': label,
            'class': cls_name,
            'super': super_name,
            'fields': fields,
        })
        i = j + 1
    return classes


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    out_path = sys.argv[1]
    paths = sys.argv[2:]
    with open(out_path, 'w') as out:
        for p in paths:
            print('Parsing %s...' % p, file=sys.stderr)
            for cls in parse_header(p):
                out.write(json.dumps(cls) + '\n')
    print('Wrote %s' % out_path, file=sys.stderr)


if __name__ == '__main__':
    main()
