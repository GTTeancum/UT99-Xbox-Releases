#!/usr/bin/env python
# patchxbe.py - Patch PE subsystem to Xbox (14), run imagebld, then inject
# D3D8/XGRAPHC library version entries so CXBX-Reloaded enables D3D8 HLE.
#
# Usage: patchxbe.py <exe_path> <xbe_out_path>

import sys
import os
import struct
import shutil
import subprocess

exe_path = sys.argv[1]
xbe_path = sys.argv[2]

# ── Step 1: Patch PE subsystem to Xbox (14) ──────────────────────────────

with open(exe_path, 'rb') as f:
    data = bytearray(f.read())

pe_offset = struct.unpack_from('<I', data, 0x3C)[0]

if data[pe_offset:pe_offset+4] != b'PE\0\0':
    print("ERROR: Not a valid PE file")
    sys.exit(1)

subsystem_offset = pe_offset + 24 + 68
old_subsystem = struct.unpack_from('<H', data, subsystem_offset)[0]
print("Original subsystem: %d" % old_subsystem)

struct.pack_into('<H', data, subsystem_offset, 14)
print("Patched subsystem to: 14 (Xbox)")

temp_exe = exe_path + '.xbox.tmp'
with open(temp_exe, 'wb') as f:
    f.write(data)

# ── Step 2: Run imagebld ─────────────────────────────────────────────────

imagebld = r'C:\XDK_5558\XDK\xbox\bin\imagebld.exe'
map_path = exe_path.replace('.exe', '.map')

cmd = [imagebld, '/IN:' + temp_exe, '/OUT:' + xbe_path]
if os.path.exists(map_path):
    cmd.append('/MAP:' + map_path)
cmd.append('/TESTNAME:UnrealTournament')
cmd.append('/TESTID:0x4C410001')
# UE1 GC mark phase (UObject::SerializeRootSet -> FArchiveTagUsed::operator<<)
# recurses through every reachable object's property graph.  With ~16K objects
# loaded after CityIntro the recursion can go many hundreds of frames deep,
# and the previous 0x40000 (256KB) stack overflowed silently and hung the
# engine right at "Collecting garbage".  Bump to 1MB to match the PC build.
cmd.append('/STACK:0x100000')
cmd.append('/DEBUG')
cmd.append('/TESTMEDIATYPES:0xFFFFFFFF')

# Title icon — 128x128 BMP embedded as XBE SaveImage
icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'xbe_icon.bmp')
if os.path.exists(icon_path):
    cmd.append('/TITLEIMAGE:' + icon_path)
    print("Using title icon: " + icon_path)

print("Running: " + ' '.join(cmd))
result = subprocess.call(cmd)

os.remove(temp_exe)

if result != 0:
    print("imagebld failed with code %d" % result)
    sys.exit(result)

print("XBE created: " + xbe_path)

# ── Step 3: Inspect the library version table imagebld produced ──────────
# Earlier this script appended an "extra" lib table to the end of the XBE
# file and rewrote dwLibraryVersionsAddr to point at it. That was needed when
# we were on XDK 5849 whose imagebld doesn't stamp D3D8/XGRAPHC entries.
# But the appended table sat at a file offset *past every section's raw
# data*, so the VA it pointed at landed in unmapped memory.  At runtime
# CXBX-R read garbage from that VA, decided the XBE has zero recognisable
# libraries, and installed no HLE patches — explaining why every D3D call
# returned S_OK but nothing reached the display.
#
# With the XDK 5558 toolchain, imagebld already produces a complete and
# correct library version table inside the XBE header region (file offset
# ~0x604, VA ~0x10604).  This script must NOT touch it.  Verify only.

print("Inspecting library version table (imagebld output, no patching)...")

with open(xbe_path, 'rb') as f:
    xbe = bytearray(f.read())

base_addr = struct.unpack_from('<I', xbe, 0x104)[0]
lib_count = struct.unpack_from('<I', xbe, 0x160)[0]
lib_va    = struct.unpack_from('<I', xbe, 0x164)[0]
lib_offset = lib_va - base_addr

print("  Library count: %d" % lib_count)
print("  Table VA: 0x%08X (file offset 0x%X)" % (lib_va, lib_offset))
for i in range(lib_count):
    off = lib_offset + i * 16
    name = xbe[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
    major, minor, build, flags = struct.unpack_from('<HHHH', xbe, off + 8)
    print("  [%d] %-8s v%d.%d.%d flags=0x%04X" % (i, name, major, minor, build, flags))

# Quick sanity: make sure the table is inside a section that gets memory-
# mapped at runtime.  XBE header region (file 0..first-section-raw) is
# always mapped, so a table living there is safe.
n_sect = struct.unpack_from('<I', xbe, 0x11C)[0]
sh = struct.unpack_from('<I', xbe, 0x120)[0] - base_addr
first_raw = min(struct.unpack_from('<I', xbe, sh + i*0x38 + 0x10)[0] for i in range(n_sect))
if lib_offset >= first_raw:
    print("  WARNING: lib table at file 0x%X is past first section raw 0x%X — runtime VA may be unmapped!" % (lib_offset, first_raw))
else:
    print("  OK: lib table lives in mapped XBE-header region (file 0x%X < first section raw 0x%X)" % (lib_offset, first_raw))

print("Inspecting section names for OpenJKDF2 display parity...")
section_names = []
for i in range(n_sect):
    h = sh + i * 0x38
    name_va = struct.unpack_from('<I', xbe, h + 0x14)[0]
    name_offset = name_va - base_addr
    end = xbe.find(b'\x00', name_offset)
    section_names.append(xbe[name_offset:end].decode('ascii', errors='replace'))
print("  Sections: " + ", ".join(section_names))
for required in ("D3D", "D3DX", "DSOUND", "XGRPH", "XPP"):
    if required not in section_names:
        print("  WARNING: missing section %s (OpenJKDF2 baseline has it)" % required)

with open(xbe_path, 'wb') as f:
    f.write(xbe)

print("XBE patched successfully.")
