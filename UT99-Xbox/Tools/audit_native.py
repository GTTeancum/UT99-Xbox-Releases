#!/usr/bin/env python
"""Cross-reference .u native classes against C++ IMPLEMENT_CLASS and force-link pragmas.

Usage:
  python audit_native.py <Package.u> [source_root]

source_root defaults to two levels up from Tools/ (i.e. the repo root).
Scans all .cpp/.c files under source_root for IMPLEMENT_CLASS(X) and all
EngineForceLinks.cpp files for /include:_autoclassX pragmas.

Output: one row per native class in the .u file, showing whether
IMPLEMENT_CLASS and a force-link pragma exist in the C++ source.
Any row marked <-- GAP needs attention before the package will load.

Run this against every package that loads at boot:
  python audit_native.py System\\Engine.u
  python audit_native.py System\\Core.u
  python audit_native.py System\\Fire.u
  python audit_native.py System\\Render.u
  python audit_native.py System\\UTMenu.u
  python audit_native.py System\\BotPack.u
  python audit_native.py System\\UWindow.u

Reference: UT99 source -- UObject::Bind() in Core/Src/UnObj.cpp uses
CLASS_Native (0x00000004) in ClassFlags to decide whether to look up
_autoclass<Name> symbols at package-load time.
"""

from __future__ import print_function

import os
import re
import sys

# Pull Package + read_uclass_flags from parse_uclass.py in same directory.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_uclass import Package, read_uclass_flags

CLASS_NATIVE = 0x00000004


# ── Source scanners ───────────────────────────────────────────────────────────

def find_implement_classes(root):
    """Walk root and return a set of class names found in IMPLEMENT_CLASS(X) macros."""
    found = set()
    for dirpath, dirnames, files in os.walk(root):
        # Skip build output directories to avoid false positives from object caches.
        dirnames[:] = [d for d in dirnames if d.lower() not in ('build', 'obj', '.git')]
        for fname in files:
            if not fname.lower().endswith(('.cpp', '.c')):
                continue
            path = os.path.join(dirpath, fname)
            try:
                txt = open(path, 'r', errors='replace').read()
            except Exception:
                continue
            for m in re.finditer(r'\bIMPLEMENT_CLASS\s*\(\s*(\w+)\s*\)', txt):
                found.add(m.group(1))
    return found


def find_force_links(root):
    """Walk root and return a set of class names found in /include:_autoclassX pragmas
    inside any file named EngineForceLinks.cpp."""
    found = set()
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d.lower() not in ('build', 'obj', '.git')]
        for fname in files:
            if fname != 'EngineForceLinks.cpp':
                continue
            path = os.path.join(dirpath, fname)
            try:
                txt = open(path, 'r', errors='replace').read()
            except Exception:
                continue
            for m in re.finditer(r'_autoclass(\w+)', txt):
                found.add(m.group(1))
    return found


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    u_path = sys.argv[1]
    if len(sys.argv) >= 3:
        root = sys.argv[2]
    else:
        # Default: two levels up from Tools/ → repo root
        root = os.path.normpath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

    # Load package
    try:
        pkg = Package(u_path)
    except Exception as e:
        print('ERROR: cannot open %s: %s' % (u_path, e), file=sys.stderr)
        sys.exit(1)

    if pkg.file_version > 70:
        print('WARNING: package version %d is not UT99 v69 — UClass body layout '
              'may differ; results unreliable.' % pkg.file_version, file=sys.stderr)

    # Scan C++ source
    impl  = find_implement_classes(root)
    links = find_force_links(root)

    # Collect native classes from the .u file
    native = []   # list of (name, ClassFlags)
    parse_errors = []
    for i, ex in enumerate(pkg.exports):
        if ex['class_idx'] != 0 or ex['size'] == 0:
            continue  # not a UClass export
        name = pkg.name(ex['name_idx'])
        try:
            cf = read_uclass_flags(pkg, ex)
            if cf & CLASS_NATIVE:
                native.append((name, cf))
        except Exception as e:
            parse_errors.append((i, name, str(e)))

    # Print report header
    pkg_base = os.path.basename(u_path)
    print('=' * 85)
    print('Package : %s' % u_path)
    print('Version : %d.%d    Names: %d    Imports: %d    Exports: %d' % (
        pkg.file_version, pkg.licensee_version,
        pkg.name_count, pkg.import_count, pkg.export_count))
    print('Native classes found: %d    Parse errors: %d' % (len(native), len(parse_errors)))
    print('Source root : %s' % root)
    print('=' * 85)
    print()

    if parse_errors:
        print('PARSE ERRORS (these classes could not be fully read):')
        for idx, name, err in parse_errors:
            print('  export[%d] %s: %s' % (idx, name, err))
        print()

    if not native:
        print('No native classes found in this package.')
        return

    # Column layout
    W_NAME  = max(40, max(len(n) for n, _ in native) + 2)
    header = '%-*s  %-15s  %s' % (W_NAME, 'Class', 'IMPLEMENT_CLASS', 'ForceLink')
    print(header)
    print('-' * len(header))

    def _in_impl(name):
        # .u names have no prefix; C++ uses A/U prefix. Try all variants.
        return any(n in impl for n in (name, 'A'+name, 'U'+name))

    def _in_links(name):
        return any(n in links for n in (name, 'A'+name, 'U'+name))

    gaps = 0
    for name, cf in sorted(native, key=lambda x: x[0]):
        has_impl  = 'OK'      if _in_impl(name)  else 'MISSING'
        has_link  = 'OK'      if _in_links(name) else 'MISSING'
        is_gap    = not (_in_impl(name) and _in_links(name))
        marker    = '  <-- GAP' if is_gap else ''
        print('%-*s  %-15s  %-15s%s' % (W_NAME, name, has_impl, has_link, marker))
        if is_gap:
            gaps += 1

    print()
    if gaps == 0:
        print('OK — %d native class(es), 0 gaps.' % len(native))
    else:
        print('GAPS: %d of %d native class(es) are missing IMPLEMENT_CLASS or ForceLink.' % (
            gaps, len(native)))
        print()
        print('For each gap:')
        print('  1. Add DECLARE_CLASS stub in appropriate *Classes.h or engine header')
        print('  2. Add IMPLEMENT_CLASS(ClassName) in appropriate .cpp')
        print('  3. Add #pragma comment(linker, "/include:_autoclassClassName") in EngineForceLinks.cpp')


if __name__ == '__main__':
    main()
