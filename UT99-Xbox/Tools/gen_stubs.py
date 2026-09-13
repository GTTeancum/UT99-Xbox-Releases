#!/usr/bin/env python
"""Generate C++ DECLARE_CLASS / IMPLEMENT_CLASS stubs for native classes in a .u package.

Usage:
  python gen_stubs.py <Package.u> [PackageName [source_root]]

PackageName defaults to the filename stem (e.g. BotPack for BotPack.u).
source_root defaults to two levels up from Tools/ (repo root).

Output: two files in the current directory --
  <PackageName>Stubs.h    -- DECLARE_CLASS stubs (include in a private header)
  <PackageName>Stubs.cpp  -- GPackage redirect + IMPLEMENT_PACKAGE + IMPLEMENT_CLASS stubs

Also prints #pragma comment(linker, ...) lines for EngineForceLinks.cpp.

The stubs use NO_DEFAULT_CONSTRUCTOR and no native member variables --
they exist purely to satisfy autoclass<Name> linker symbols and allow
UClass::Bind() to succeed. If a class has script properties, those are
handled by the .u property table; C++ native members would need separate work.

Parent-class substitution:
  When a gap class's C++ parent is not visible in the standard Engine headers,
  not in the KNOWN_PREFIX table, and not itself a gap class being stubbed here,
  the parent is substituted with UObject. This satisfies IMPLEMENT_CLASS (which
  calls Super::StaticClass()) without requiring stubs for non-native intermediaries.
  Runtime class hierarchy is always read from the .u file; C++ stubs are
  only needed for linker symbol and static-init chain satisfaction.

GPackage redirect:
  Each generated .cpp opens with #undef GPackage / #define GPackage GPackage_<Pkg>
  to avoid multiply-defined 'GPackage' linker errors when multiple stub libs are
  linked into the same EXE.

Prefix convention (TEXT(#TClass)+1 strips exactly one leading char):
  - Actor-hierarchy classes get 'A' prefix  -> autoclassABot etc.
  - Object-hierarchy classes get 'U' prefix -> autoclassUUTConsole etc.
  The prefix is determined by walking up the parent chain to Actor or Object.

Reference: Core/Inc/UnObjBas.h IMPLEMENT_CLASS macro.
"""

from __future__ import print_function

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_uclass import Package, read_uclass_flags


def _find_implement_classes(root):
    """Scan source tree; return set of bare class names found in IMPLEMENT_CLASS(X)."""
    found = set()
    for dirpath, dirnames, files in os.walk(root):
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


CLASS_NATIVE = 0x00000004

# -- Known Engine/Core class prefix table -------------------------------------
# .u in-package names (no A/U prefix) -> C++ prefix ('A' for Actor, 'U' for Object)
KNOWN_PREFIX = {
    # A-prefix (Actor hierarchy) -- all have real C++ IMPLEMENT_CLASS in Engine/Core
    'Actor': 'A', 'Pawn': 'A', 'PlayerPawn': 'A', 'Spectator': 'A',
    'GameInfo': 'A', 'HUD': 'A', 'Mutator': 'A',
    'Info': 'A', 'StatLog': 'A', 'StatLogFile': 'A',
    'Inventory': 'A', 'Weapon': 'A', 'Pickup': 'A', 'Ammo': 'A',
    'Projectile': 'A', 'Effects': 'A', 'Fragment': 'A', 'Carcass': 'A',
    'Decoration': 'A', 'Mover': 'A',
    'NavigationPoint': 'A', 'PathNode': 'A', 'InventorySpot': 'A',
    'Trigger': 'A', 'Triggers': 'A',
    'Light': 'A', 'Brush': 'A',
    'ZoneInfo': 'A', 'WarpZoneInfo': 'A', 'SkyZoneInfo': 'A', 'LevelInfo': 'A',
    'ReplicationInfo': 'A', 'PlayerReplicationInfo': 'A', 'GameReplicationInfo': 'A',
    'InterpolationPoint': 'A', 'Keypoint': 'A',
    'Camera': 'A', 'Scout': 'A',
    # MapList, Spectator, MessagingSpectator are in EngineStubs.h (generated)
    # and are always included before any downstream package stubs.
    'MapList': 'A', 'MapListReplicationInfo': 'A',
    'Spectator': 'A', 'MessagingSpectator': 'A',
    'TriggerMarker': 'A', 'ButtonMarker': 'A', 'WarpZoneMarker': 'A',
    'LiftCenter': 'A', 'LiftExit': 'A',
    'Menu': 'A', 'SavedMove': 'A',
    'PlayerStart': 'A', 'Decal': 'A', 'DecalInfo': 'A',
    'InternetInfo': 'A', 'SpawnNotify': 'A',
    # U-prefix (Object hierarchy) -- all have real C++ IMPLEMENT_CLASS in Engine/Core
    'Object': 'U', 'Field': 'U', 'Struct': 'U', 'Function': 'U', 'Class': 'U',
    'Console': 'U', 'Player': 'U', 'Viewport': 'U', 'Client': 'U',
    'Engine': 'U', 'GameEngine': 'U',
    'Canvas': 'U', 'Animation': 'U', 'Primitive': 'U', 'Mesh': 'U',
    'LodMesh': 'U', 'Sound': 'U', 'Music': 'U',
    'Texture': 'U', 'Bitmap': 'U', 'Palette': 'U', 'Font': 'U',
    'ScriptedTexture': 'U', 'FractalTexture': 'U', 'FireTexture': 'U',
    'IceTexture': 'U', 'WaterTexture': 'U', 'WaveTexture': 'U', 'WetTexture': 'U',
    'NetDriver': 'U', 'NetConnection': 'U', 'Channel': 'U',
    'Input': 'U', 'Level': 'U', 'LevelBase': 'U', 'Model': 'U',
    'RenderBase': 'U', 'RenderDevice': 'U', 'RenderIterator': 'U',
    'Window': 'U',    # UWindow base
    # IpDrv hierarchy (declared in IpDrv/Inc/IpDrvClasses.h; A-prefix actors).
    # Recorded here so descendant packages (IpServer, UBrowser) emit the
    # correct prefix when referencing these as parents.
    'InternetInfo': 'A', 'InternetLink': 'A',
    'UdpLink': 'A', 'TcpLink': 'A',
}

# Additional prefix hints for propagation only -- these classes exist in
# descendant packages and are NOT in standard Engine/Core C++ headers.
# They are used by build_prefix_map to propagate A/U to their children but
# safe_parent_cpp() will NOT accept them as valid C++ parent types (it will
# substitute UObject instead if they are not in gap_set or existing_impl).
_PROPAGATION_PREFIX = {
    # BotPack classes (prefix hints so their children get A prefix)
    'Bot': 'A', 'StationaryPawn': 'A',
    'DeathMatchPlus': 'A', 'TeamGamePlus': 'A', 'CTFGame': 'A',
    'TournamentGameInfo': 'A',
    'ChallengeHUD': 'A', 'ChallengeTeamHUD': 'A', 'AssaultHUD': 'A',
    # BotPack classes (U prefix)
    'TournamentConsole': 'U',
    'TournamentPlayer': 'U', 'TournamentMale': 'U', 'TournamentFemale': 'U',
    'TMale1': 'U', 'TMale2': 'U', 'TFemale1': 'U', 'TFemale2': 'U',
    # UWindow classes (U prefix)
    'UWindowWindow': 'U', 'UWindowDialogControl': 'U',
    'UWindowDialogClientWindow': 'U', 'UWindowRootWindow': 'U',
    'UWindowDynamicTextArea': 'U', 'WindowConsole': 'U',
    'UWindowConsoleTextAreaControl': 'U', 'UWindowTabControlTabArea': 'U',
    'UWindowURLTextArea': 'U', 'UWindowHTMLTextArea': 'U',
}


def resolve_parent(pkg, ex):
    """Return (parent_name, parent_is_import) for a class export.
    parent_name is the in-package class name (no C++ prefix).
    Returns ('Object', True) if no parent (root class).
    """
    si = ex['super_idx']
    if si == 0:
        return 'Object', True
    if si > 0 and si <= len(pkg.exports):
        parent_ex = pkg.exports[si - 1]
        return pkg.name(parent_ex['name_idx']), False
    if si < 0:
        imp_idx = -si - 1
        if 0 <= imp_idx < len(pkg.imports):
            return pkg.name(pkg.imports[imp_idx]['name_idx']), True
    return 'Object', True


def build_prefix_map(pkg, class_names, name_to_ex):
    """Build a dict: in-package class name -> C++ prefix ('A' or 'U').
    Uses KNOWN_PREFIX + _PROPAGATION_PREFIX tables plus propagation through
    local inheritance.  class_names is the full list to assign prefixes for.
    name_to_ex maps those names to their export dicts.
    """
    prefix = dict(KNOWN_PREFIX)
    prefix.update(_PROPAGATION_PREFIX)  # add propagation hints before propagating

    # Propagate through local inheritance
    changed = True
    while changed:
        changed = False
        for name in class_names:
            if name in prefix:
                continue
            if name not in name_to_ex:
                continue
            ex = name_to_ex[name]
            parent_name, _ = resolve_parent(pkg, ex)
            if parent_name in prefix:
                prefix[name] = prefix[parent_name]
                changed = True

    # Anything still unresolved -> U prefix (conservative: Object hierarchy)
    for name in class_names:
        if name not in prefix:
            prefix[name] = 'U'

    return prefix


def topo_sort(class_names, name_to_ex, pkg, include_set=None):
    """Return class_names in topological order (parents before children).
    If include_set is given, only visit nodes in include_set (others are
    treated as already-visited external nodes).
    """
    if include_set is None:
        include_set = set(class_names)
    order = []
    visited = set()

    def visit(name):
        if name in visited or name not in include_set:
            return
        visited.add(name)
        if name in name_to_ex:
            ex = name_to_ex[name]
            parent_name, is_import = resolve_parent(pkg, ex)
            if not is_import and parent_name in include_set:
                visit(parent_name)
        order.append(name)

    for name in class_names:
        visit(name)
    return order


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    u_path = sys.argv[1]
    pkg_name = sys.argv[2] if len(sys.argv) >= 3 else os.path.splitext(
        os.path.basename(u_path))[0]

    # Optional third arg: source root for gap filtering.
    source_root = sys.argv[3] if len(sys.argv) >= 4 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

    pkg = Package(u_path)
    if pkg.file_version > 70:
        print('WARNING: package version %d may not be v69 format.' % pkg.file_version,
              file=sys.stderr)

    # Scan existing IMPLEMENT_CLASS entries so we don't emit duplicates.
    existing_impl = _find_implement_classes(source_root)

    def _already_implemented(in_pkg_name):
        """True if C++ already has IMPLEMENT_CLASS for this name (any prefix variant)."""
        return any(v in existing_impl
                   for v in (in_pkg_name, 'A' + in_pkg_name, 'U' + in_pkg_name))

    # Collect ALL class exports (for parent resolution including non-native classes).
    all_name_to_ex = {}
    for ex in pkg.exports:
        if ex['class_idx'] != 0 or ex['size'] == 0:
            continue
        name = pkg.name(ex['name_idx'])
        all_name_to_ex[name] = ex

    # Collect native UClass exports.
    native_classes = []
    native_name_to_ex = {}
    for ex in pkg.exports:
        if ex['class_idx'] != 0 or ex['size'] == 0:
            continue
        name = pkg.name(ex['name_idx'])
        try:
            cf = read_uclass_flags(pkg, ex)
            if cf & CLASS_NATIVE:
                native_classes.append(name)
                native_name_to_ex[name] = ex
        except Exception:
            pass  # skip unreadable exports

    if not native_classes:
        print('No native classes found in %s.' % u_path)
        return

    # Build prefix map over ALL class exports (so non-native intermediaries help
    # propagate A/U prefix to native descendants).
    prefix_map = build_prefix_map(pkg, list(all_name_to_ex.keys()), all_name_to_ex)

    # Gap classes = native classes with no existing IMPLEMENT_CLASS in the source tree.
    gap_set = set(n for n in native_classes if not _already_implemented(n))
    all_ordered = topo_sort(list(gap_set), native_name_to_ex, pkg, include_set=gap_set)

    skipped = len(native_classes) - len(gap_set)
    if skipped:
        print('# Skipping %d class(es) already implemented in C++ source.' % skipped,
              file=sys.stderr)

    if not gap_set:
        print('# All native classes already implemented. Nothing to generate.')
        return

    def cpp_name(in_pkg_name):
        """Return full C++ name with A/U prefix."""
        p = prefix_map.get(in_pkg_name, 'U')
        return p + in_pkg_name

    def safe_parent_cpp(gap_class_name):
        """Return the C++ parent name to use in the DECLARE_CLASS stub.

        Uses the true parent if it is visible to the C++ compiler:
          - In the KNOWN_PREFIX table (standard Engine/Core headers)
          - A gap class being stubbed in this same file
          - Already implemented in the C++ source tree

        Falls back to UObject for non-native intermediaries and cross-package
        gap classes (whose stubs will be added to the project separately).
        The runtime class hierarchy always comes from the .u file; the C++ parent
        is only needed to satisfy IMPLEMENT_CLASS's Super::StaticClass() call.
        """
        ex = native_name_to_ex[gap_class_name]
        parent_name, is_import = resolve_parent(pkg, ex)

        # Root class (no parent in file) -> UObject
        if parent_name == 'Object' and is_import:
            return 'UObject'

        # Parent in known standard headers?
        if parent_name in KNOWN_PREFIX:
            return cpp_name(parent_name)

        # Parent is another gap class in THIS stub file?
        if parent_name in gap_set:
            return cpp_name(parent_name)

        # Parent already has IMPLEMENT_CLASS in the source tree?
        if _already_implemented(parent_name):
            return cpp_name(parent_name)

        # Unknown/non-native parent -- substitute UObject so the compiler
        # sees a valid Super without needing a bridge declaration.
        # (The real parent chain lives in the .u file at runtime.)
        return 'UObject'

    # -- Generate header -------------------------------------------------------
    h_lines = [
        '// %sStubs.h - auto-generated native class stubs for %s.u' % (pkg_name, pkg_name),
        '// Generated by gen_stubs.py. Do not edit by hand.',
        '// These stubs satisfy autoclass<Name> linker symbols so UClass::Bind() succeeds.',
        '// Native member variables and native functions are NOT included;',
        '// script properties are handled by the .u property table at runtime.',
        '#pragma once',
        '',
    ]

    for name in all_ordered:
        cn  = cpp_name(name)
        par = safe_parent_cpp(name)
        h_lines += [
            'class %s : public %s' % (cn, par),
            '{',
            'public:',
            '    DECLARE_CLASS(%s, %s, 0)' % (cn, par),
            '    NO_DEFAULT_CONSTRUCTOR(%s)' % cn,
            '};',
            '',
        ]

    # -- Generate source -------------------------------------------------------
    # The GPackage redirect MUST precede any includes.  Without it, every stub
    # .cpp that calls IMPLEMENT_PACKAGE would define the same 'GPackage' extern,
    # causing a multiply-defined symbol linker error.

    # Cross-package stub includes: packages that reference gap classes from
    # earlier-generated stub headers need those headers included first.
    # All stub files live in the same directory (PackageStubs/).
    EXTRA_STUB_INCLUDES = {
        # BotPack gap classes include Engine stubs (ASpectator, AMapList, etc.)
        'BotPack': ['EngineStubs.h'],
        # UMenu gap classes inherit from UWindow stubs (UUWindowRootWindow,
        # UWindowConsole, etc.), so the UWindow stubs header MUST precede ours.
        'UMenu':   ['UWindowStubs.h'],
        # UTMenu gap classes reference BotPack stubs (UTournamentConsole->UObject
        # after safe_parent substitution, but include both for completeness).
        'UTMenu':  ['EngineStubs.h', 'BotPackStubs.h', 'UWindowStubs.h', 'UMenuStubs.h'],
        # Server-browser packages (deferred — not boot-critical, but recorded
        # here so future runs of gen_stubs.py emit the right includes).
        'UBrowser':  ['UWindowStubs.h'],
        'UTBrowser': ['UBrowserStubs.h'],
        'IpServer':  ['IpDrvStubs.h'],
    }
    extra_includes = EXTRA_STUB_INCLUDES.get(pkg_name, [])

    cpp_lines = [
        '// %sStubs.cpp - auto-generated native class stubs for %s.u' % (pkg_name, pkg_name),
        '// Generated by gen_stubs.py. Do not edit by hand.',
        '',
        '// Redirect GPackage to a package-specific symbol so that multiple stub',
        '// static libs can coexist in the same link without "already defined" errors.',
        '#undef  GPackage',
        '#define GPackage GPackage_%s' % pkg_name,
        '',
        '#include "XboxLaunchPrivate.h"',
    ]
    for extra_h in extra_includes:
        cpp_lines.append('#include "%s"' % extra_h)
    cpp_lines += [
        '#include "%sStubs.h"' % pkg_name,
        '',
        'IMPLEMENT_PACKAGE(%s);' % pkg_name,
        '',
    ]
    for name in all_ordered:
        cn = cpp_name(name)
        cpp_lines.append('IMPLEMENT_CLASS(%s);' % cn)

    # -- Print force-link pragmas ----------------------------------------------
    print('// -- Add to EngineForceLinks.cpp --')
    print('// %s package' % pkg_name)
    for name in all_ordered:
        cn = cpp_name(name)
        print('#pragma comment(linker, "/include:_autoclass%s")' % cn)
    print()

    # -- Write files -----------------------------------------------------------
    h_path   = pkg_name + 'Stubs.h'
    cpp_path = pkg_name + 'Stubs.cpp'

    with open(h_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(h_lines))
    with open(cpp_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(cpp_lines))

    print('# Written: %s  %s' % (h_path, cpp_path))
    print('# %d native class(es) stubbed (%d already existed, skipped).' % (
        len(gap_set), skipped))


if __name__ == '__main__':
    main()
