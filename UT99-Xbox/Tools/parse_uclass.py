#!/usr/bin/env python
"""Parse a UE1 (UT99) .u package file.

Modes:
  python parse_uclass.py --list-native <Package.u>
      List every class with CLASS_Native set, plus its PropertiesSize.
      Use this to find native classes that need IMPLEMENT_CLASS + force-link.

  python parse_uclass.py <Package.u> <ClassName>
      Dump the Children property chain for a single class.
      Use this to compare field order against C++ struct declarations.

Reference: UT99 source — UnObj.cpp UObject::Serialize, UnClass.cpp
UStruct::Serialize/Link, UnProp.cpp UProperty::Serialize, FPropertyTag.

UClass body layout (v69/v436):
  UField:  SuperField(ci) Next(ci)
  UStruct: ScriptText(ci) Children(ci) FriendlyName(ci)
           Bytecode(ci-count + N bytes) PropertiesSize(DWORD)
  UState:  ProbeMask(QWORD) IgnoreMask(QWORD) LabelTableOffset(WORD) StateFlags(DWORD)
  UClass:  ClassFlags(DWORD)  ← CLASS_Native = 0x00000004
"""

from __future__ import print_function

import struct
import sys


# ── Compact-int decoder ──────────────────────────────────────────────────────
# UT99 source: Core/Src/UnObj.cpp:3638 (operator<< FCompactIndex).
# First byte: bit 7 = sign, bit 6 = continuation, bits 0..5 = value (6 low bits).
# Each subsequent byte: bit 7 = continuation, bits 0..6 = value (7 bits).

def read_compact_int(buf, offs):
    b = buf[offs]
    offs += 1
    sign = b & 0x80
    cont = b & 0x40
    val = b & 0x3F
    if cont:
        shift = 6
        while True:
            b = buf[offs]
            offs += 1
            val |= (b & 0x7F) << shift
            shift += 7
            if not (b & 0x80):
                break
    if sign:
        val = -val
    return val, offs


def read_byte(buf, offs):
    return buf[offs], offs + 1


def read_word(buf, offs):
    return struct.unpack_from('<H', buf, offs)[0], offs + 2


def read_dword(buf, offs):
    return struct.unpack_from('<I', buf, offs)[0], offs + 4


# ── FPropertyTag skipper ─────────────────────────────────────────────────────
# UT99 source: Core/Src/UnClass.cpp:96 (operator<< FPropertyTag).
# Tag-stream layout: <Tag><Tag>...<Tag with Name=NAME_None>
# Each tag (when Name != NAME_None):
#   Name        : compact int (FName index)
#   Info        : 1 byte  -- bits 0..3 = Type, bits 4..6 = SizeCode, bit 7 = ArrayFlag
#   ItemName    : compact int (only if Type == NAME_StructProperty == 10)
#   Size        : 0/1/2/4 bytes depending on SizeCode
#   ArrayIndex  : 0/1/2/4 bytes (only if (Info & 0x80) and Type != BoolProperty)
#   Data        : Size bytes

NAME_NONE             = 0
NAME_BoolProperty     = 3
NAME_StructProperty   = 10

SIZE_FROM_CODE = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}


def skip_tagged_properties(buf, offs, pkg):
    """Skip the SerializeTaggedProperties prefix that UObject::Serialize emits
    for any non-UClass UObject export.  Returns the offset past the terminator.

    Note: the terminator is "any name in the package's name table whose string
    is 'None'" — not literal name index 0.  At runtime the engine maps all such
    package-local indices to the single global FName 'None' before comparing."""
    while True:
        name_idx, offs = read_compact_int(buf, offs)
        if pkg.name(name_idx) == 'None':
            break
        info, offs = read_byte(buf, offs)
        type_code = info & 0x0F
        if type_code == NAME_StructProperty:
            _item_name, offs = read_compact_int(buf, offs)  # struct type name
        size_code = info & 0x70
        if size_code in SIZE_FROM_CODE:
            size = SIZE_FROM_CODE[size_code]
        elif size_code == 0x50:
            size, offs = read_byte(buf, offs)
        elif size_code == 0x60:
            size, offs = read_word(buf, offs)
        elif size_code == 0x70:
            size, offs = read_dword(buf, offs)
        else:
            raise ValueError('Unknown size code 0x%02X' % size_code)
        # Array index encoding (variable: 1, 2, or 4 bytes)
        if (info & 0x80) and type_code != NAME_BoolProperty:
            b, offs = read_byte(buf, offs)
            if (b & 0x80) == 0:
                pass  # array_index = b
            elif (b & 0xC0) == 0x80:
                _c, offs = read_byte(buf, offs)
            else:
                _c, offs = read_byte(buf, offs)
                _d, offs = read_byte(buf, offs)
                _e, offs = read_byte(buf, offs)
        # BoolProperty stores its value in the array-flag bit; Size byte is still
        # read and described as 1, but no value bytes follow.
        if type_code == NAME_BoolProperty:
            # Don't skip the Size bytes for bools; the value is in Info bit 7.
            pass
        else:
            offs += size
    return offs


# ── Package parser ───────────────────────────────────────────────────────────

class Package(object):
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        self._read_summary()
        self._read_names()
        self._read_imports()
        self._read_exports()

    def _read_summary(self):
        d = self.data
        tag = struct.unpack_from('<I', d, 0)[0]
        assert tag == 0x9E2A83C1, 'Bad package tag: %#x' % tag
        self.file_version = struct.unpack_from('<H', d, 4)[0]
        self.licensee_version = struct.unpack_from('<H', d, 6)[0]
        self.package_flags = struct.unpack_from('<I', d, 8)[0]
        self.name_count = struct.unpack_from('<I', d, 12)[0]
        self.name_offset = struct.unpack_from('<I', d, 16)[0]
        self.export_count = struct.unpack_from('<I', d, 20)[0]
        self.export_offset = struct.unpack_from('<I', d, 24)[0]
        self.import_count = struct.unpack_from('<I', d, 28)[0]
        self.import_offset = struct.unpack_from('<I', d, 32)[0]

    def _read_names(self):
        # v69 names: <BYTE length> <ANSI bytes incl. null> <DWORD flags>
        # (not compact int — names predate the compact form)
        self.names = []
        offs = self.name_offset
        for _ in range(self.name_count):
            length = self.data[offs]
            offs += 1
            s = self.data[offs:offs + length - 1].decode('latin-1', errors='replace')
            offs += length
            flags = struct.unpack_from('<I', self.data, offs)[0]
            offs += 4
            self.names.append((s, flags))

    def _read_imports(self):
        self.imports = []
        offs = self.import_offset
        for _ in range(self.import_count):
            class_pkg, offs = read_compact_int(self.data, offs)
            class_name, offs = read_compact_int(self.data, offs)
            pkg_idx, offs = read_dword(self.data, offs)
            obj_name, offs = read_compact_int(self.data, offs)
            if pkg_idx & 0x80000000:
                pkg_idx -= 0x100000000
            self.imports.append({
                'class_pkg': class_pkg, 'class_name': class_name,
                'pkg_idx': pkg_idx, 'name_idx': obj_name,
            })

    def _read_exports(self):
        self.exports = []
        offs = self.export_offset
        for _ in range(self.export_count):
            class_idx, offs = read_compact_int(self.data, offs)
            super_idx, offs = read_compact_int(self.data, offs)
            pkg_idx, offs = read_dword(self.data, offs)
            obj_name, offs = read_compact_int(self.data, offs)
            obj_flags, offs = read_dword(self.data, offs)
            ser_size, offs = read_compact_int(self.data, offs)
            ser_offset = 0
            if ser_size > 0:
                ser_offset, offs = read_compact_int(self.data, offs)
            self.exports.append({
                'class_idx': class_idx, 'super_idx': super_idx,
                'pkg_idx': pkg_idx, 'name_idx': obj_name,
                'flags': obj_flags, 'size': ser_size, 'offset': ser_offset,
            })

    def name(self, idx):
        if 0 <= idx < len(self.names):
            return self.names[idx][0]
        return '<bad name %d>' % idx

    def export_name(self, ref):
        """Return name of export[ref-1] (1-indexed) or '<NULL>' for 0."""
        if ref == 0:
            return '<NULL>'
        if ref > 0 and ref <= len(self.exports):
            return self.name(self.exports[ref - 1]['name_idx'])
        return '<bad ref %d>' % ref

    def export_class_name(self, ex):
        """Return class name of an export (which class created it)."""
        ci = ex['class_idx']
        if ci == 0:
            return 'Class'  # The export IS a UClass
        if ci > 0:
            return self.name(self.exports[ci - 1]['name_idx'])
        return self.name(self.imports[-ci - 1]['name_idx'])


# ── UClass / UProperty header readers ────────────────────────────────────────

def read_uclass_children(pkg, ex):
    """For a UClass export, return the Children object reference (head of the
    UField linked list).  UClass exports have Class==UClass::StaticClass so
    UObject::Serialize emits NO bytes (no SerializeTaggedProperties call)."""
    o = ex['offset']
    _super, o = read_compact_int(pkg.data, o)   # UField::SuperField
    _next, o = read_compact_int(pkg.data, o)    # UField::Next
    _script_text, o = read_compact_int(pkg.data, o)  # UStruct::ScriptText
    children, o = read_compact_int(pkg.data, o)      # UStruct::Children
    return children


def read_uclass_flags(pkg, ex):
    """For a UClass export (class_idx==0, size>0), return ClassFlags (DWORD).

    Walks the full UField→UStruct→UState→UClass body layout for v69 packages.
    Source-verified layout (Core/Src/UnClass.cpp):

      UField::Serialize:
        SuperField  (compact int — object ref)
        Next        (compact int — object ref)

      UStruct::Serialize:
        ScriptText  (compact int — object ref)
        Children    (compact int — object ref)
        FriendlyName(compact int — FName index)
        Line        (INT  = 4 bytes)
        TextPos     (INT  = 4 bytes)
        ScriptSize  (INT  = 4 bytes)   ← raw INT, NOT compact int
        <ScriptSize bytes of bytecode>
        [PropertiesSize is NOT stored — computed at runtime by UStruct::Link()]

      UState::Serialize:
        ProbeMask        (QWORD = 8 bytes)
        IgnoreMask       (QWORD = 8 bytes)
        LabelTableOffset (_WORD = 2 bytes)
        StateFlags       (DWORD = 4 bytes)

      UClass::Serialize:
        ClassFlags  (DWORD = 4 bytes)   ← this is what we want

    CLASS_Native  = 0x00000004  — requires C++ IMPLEMENT_CLASS binding.
    CLASS_Config  = 0x00000010  — reads config from .ini at runtime.
    CLASS_Abstract= 0x00000020  — cannot be instantiated directly.

    Raises ValueError if the parse walks past the end of the export data.
    """
    o = ex['offset']
    end = o + ex['size']

    def _ci():
        nonlocal o
        v, o = read_compact_int(pkg.data, o)
        return v

    def _dw():
        nonlocal o
        v, o = read_dword(pkg.data, o)
        return v

    # UField
    _ci()   # SuperField
    _ci()   # Next

    # UStruct
    _ci()   # ScriptText
    _ci()   # Children
    _ci()   # FriendlyName (FName = compact int name-table index)
    _dw()   # Line    (INT, 4 bytes)
    _dw()   # TextPos (INT, 4 bytes)

    script_size = _dw()   # ScriptSize (INT, 4 bytes) — raw INT, not compact int
    if script_size < 0 or o + script_size > end:
        raise ValueError('ScriptSize %d overflows export (size=%d, remaining=%d)'
                         % (script_size, ex['size'], end - o))
    o += script_size      # skip bytecode bytes

    # UState
    o += 8   # ProbeMask        (QWORD)
    o += 8   # IgnoreMask       (QWORD)
    o += 2   # LabelTableOffset (_WORD)
    o += 4   # StateFlags       (DWORD)

    if o + 4 > end:
        raise ValueError('ClassFlags would exceed export bounds')

    # UClass
    class_flags = _dw()   # ClassFlags

    return class_flags


def read_field_next(pkg, ex):
    """For any UField subclass (UProperty, UFunction, UStruct, UEnum, UConst,
    UState), read past the SerializeTaggedProperties prefix and the UField
    SuperField, and return the Next pointer.  This is what we need to walk
    the UStruct's Children chain."""
    o = ex['offset']
    o = skip_tagged_properties(pkg.data, o, pkg)
    _super, o = read_compact_int(pkg.data, o)
    next_ref, o = read_compact_int(pkg.data, o)
    return next_ref


def read_uproperty_header(pkg, ex):
    """Read a UProperty's header fields (after tagged properties + UField).
    Returns dict with array_dim, prop_flags, category."""
    o = ex['offset']
    o = skip_tagged_properties(pkg.data, o, pkg)
    _super, o = read_compact_int(pkg.data, o)
    next_ref, o = read_compact_int(pkg.data, o)
    array_dim, o = read_dword(pkg.data, o)        # raw INT
    prop_flags, o = read_dword(pkg.data, o)
    category, o = read_compact_int(pkg.data, o)
    rep_offset = None
    if prop_flags & 0x00000020:  # CPF_Net
        rep_offset, o = read_word(pkg.data, o)
    return {
        'next': next_ref, 'array_dim': array_dim,
        'prop_flags': prop_flags, 'category': category,
        'rep_offset': rep_offset,
    }


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    # ── --list-native mode ────────────────────────────────────────────────────
    if sys.argv[1] == '--list-native':
        if len(sys.argv) < 3:
            print('Usage: parse_uclass.py --list-native <Package.u>', file=sys.stderr)
            sys.exit(1)
        pkg = Package(sys.argv[2])
        if pkg.file_version > 70:
            print('WARNING: package version %d is not UT99 v69 — UClass body layout '
                  'may differ; results unreliable.' % pkg.file_version, file=sys.stderr)
        CLASS_NATIVE = 0x00000004
        print('# Package: %s' % sys.argv[2])
        print('# version=%d.%d  names=%d  imports=%d  exports=%d' % (
            pkg.file_version, pkg.licensee_version,
            pkg.name_count, pkg.import_count, pkg.export_count))
        print()
        print('%-40s  %-12s  %s' % ('Class', 'ClassFlags', 'Notes'))
        print('-' * 70)
        found = 0
        errors = 0
        for i, ex in enumerate(pkg.exports):
            if ex['class_idx'] != 0 or ex['size'] == 0:
                continue  # not a UClass export, or has no data
            name = pkg.name(ex['name_idx'])
            try:
                cf = read_uclass_flags(pkg, ex)
                if cf & CLASS_NATIVE:
                    notes = []
                    if cf & 0x00000010: notes.append('Config')
                    if cf & 0x00000020: notes.append('Abstract')
                    if cf & 0x00000040: notes.append('Transient')
                    print('%-40s  0x%08X  %s' % (name, cf, ' '.join(notes)))
                    found += 1
            except Exception as e:
                print('%-40s  # parse error: %s' % (name, e))
                errors += 1
        print()
        print('# %d native class(es) found, %d parse error(s)' % (found, errors))
        sys.exit(0)

    # ── single-class children-chain mode ─────────────────────────────────────
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    pkg = Package(sys.argv[1])
    target = sys.argv[2]

    print('# Package: %s' % sys.argv[1])
    print('# version=%d.%d names=%d imports=%d exports=%d' % (
        pkg.file_version, pkg.licensee_version,
        pkg.name_count, pkg.import_count, pkg.export_count))
    print()

    # Real UClass exports have ClassIndex == 0 (the "I am a Class" sentinel).
    cls_idx, cls_ex = None, None
    for i, ex in enumerate(pkg.exports):
        if pkg.name(ex['name_idx']) == target and ex['class_idx'] == 0:
            cls_idx, cls_ex = i, ex
            break
    if not cls_ex:
        print('ERROR: UClass %r not found' % target, file=sys.stderr)
        sys.exit(2)

    super_ref, _ = read_compact_int(pkg.data, cls_ex['offset'])
    super_name = pkg.export_name(super_ref) if super_ref >= 0 else (
        pkg.name(pkg.imports[-super_ref - 1]['name_idx']))
    print('# class %s at export[%d] (super=%s, file offset=%d, size=%d)' % (
        target, cls_idx, super_name, cls_ex['offset'], cls_ex['size']))
    print()

    children_ref = read_uclass_children(pkg, cls_ex)
    print('# Children chain head: export[%d] = %s' % (
        children_ref - 1 if children_ref else -1,
        pkg.export_name(children_ref)))
    print()

    # Walk chain
    print('%-4s %-22s %-32s %s' % ('idx', 'Class', 'Name', 'Details'))
    print('-' * 100)
    seen = set()
    cur = children_ref
    n = 0
    while cur != 0:
        if cur in seen:
            print('# CYCLE at ref=%d' % cur)
            break
        seen.add(cur)
        if cur < 0 or cur > len(pkg.exports):
            print('# bad ref %d' % cur)
            break
        ex = pkg.exports[cur - 1]
        name = pkg.name(ex['name_idx'])
        cls_name = pkg.export_class_name(ex)
        try:
            if cls_name.endswith('Property'):
                hdr = read_uproperty_header(pkg, ex)
                detail = 'ArrayDim=%d PropFlags=%08X' % (
                    hdr['array_dim'], hdr['prop_flags'])
                next_ref = hdr['next']
            else:
                next_ref = read_field_next(pkg, ex)
                detail = '(%s)' % cls_name
            print('%-4d %-22s %-32s %s' % (n, cls_name, name, detail))
            cur = next_ref
        except Exception as e:
            print('# parse error at export[%d] %s: %s' % (cur - 1, name, e))
            break
        n += 1


if __name__ == '__main__':
    main()
