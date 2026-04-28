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

imagebld = r'C:\XDK\xbox\bin\imagebld.exe'
map_path = exe_path.replace('.exe', '.map')

cmd = [imagebld, '/IN:' + temp_exe, '/OUT:' + xbe_path]
if os.path.exists(map_path):
    cmd.append('/MAP:' + map_path)
cmd.append('/TESTNAME:UnrealTournament')
cmd.append('/TESTID:0x4C410001')
cmd.append('/STACK:0x40000')
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

# ── Step 3: Inject D3D8 + XGRAPHC library version entries ────────────────
# CXBX-Reloaded only activates D3D8 HLE when it finds D3D8/XGRAPHC entries
# in the XBE library version table. Since d3d8-xbox.lib is statically linked,
# imagebld doesn't know to add these. We patch them in post-hoc.

print("Injecting D3D8/XGRAPHC library version entries...")

with open(xbe_path, 'rb') as f:
    xbe = bytearray(f.read())

base_addr = struct.unpack_from('<I', xbe, 0x104)[0]

# Read current library version table info
lib_count  = struct.unpack_from('<I', xbe, 0x160)[0]
lib_va     = struct.unpack_from('<I', xbe, 0x164)[0]
lib_offset = lib_va - base_addr  # File offset of the table

print("  Current library count: %d" % lib_count)
print("  Library table VA: 0x%08X (file offset 0x%X)" % (lib_va, lib_offset))

# Read existing entries to get the build number
if lib_count > 0:
    first_entry = xbe[lib_offset:lib_offset+16]
    existing_build = struct.unpack_from('<H', first_entry, 12)[0]
else:
    existing_build = 5849
print("  Using build number: %d" % existing_build)

# Build the two new 16-byte library version entries
# Format: 8-char name (padded with nulls) + WORD major + WORD minor + WORD build + WORD flags
def make_lib_entry(name, build, flags=0x4001):
    name_bytes = name.encode('ascii')[:8].ljust(8, b'\x00')
    return name_bytes + struct.pack('<HHHH', 1, 0, build, flags)

d3d8_entry    = make_lib_entry('D3D8',    existing_build)
xgraphc_entry = make_lib_entry('XGRAPHC', existing_build)

# Strategy: append the new entries right after the existing table,
# then update the count. This works because imagebld leaves padding
# or we can safely extend into the space after the table.
#
# First check what's after the current table
table_end = lib_offset + lib_count * 16
bytes_after = xbe[table_end:table_end+32]
print("  Bytes after table: %s" % bytes_after.hex())

# We need 32 bytes (2 entries). Append at end of file and update pointers.
# Actually, the safest approach: append new entries to end of XBE file,
# then rewrite the header to point to a new combined table there.

# Build complete new library table: existing entries + D3D8 + XGRAPHC
new_entries = bytearray()
for i in range(lib_count):
    off = lib_offset + i * 16
    new_entries += xbe[off:off+16]
new_entries += d3d8_entry
new_entries += xgraphc_entry
new_count = lib_count + 2

# Align file size to 4 bytes before appending
while len(xbe) % 4 != 0:
    xbe.append(0)

# New table goes at end of file
new_table_file_offset = len(xbe)
new_table_va = new_table_file_offset + base_addr

# Append the new table
xbe += new_entries

# Update header: count and table pointer
struct.pack_into('<I', xbe, 0x160, new_count)
struct.pack_into('<I', xbe, 0x164, new_table_va)

# Update kernel and XAPI library version pointers to point into new table
# XAPI is entry[0], kernel is the XBOXKRNL entry
for i in range(new_count):
    off = new_table_file_offset + i * 16
    name = xbe[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
    entry_va = new_table_va + i * 16
    if name.startswith('XAPILIB'):
        struct.pack_into('<I', xbe, 0x16C, entry_va)
        print("  XAPI lib version -> entry[%d] VA 0x%08X" % (i, entry_va))
    elif name == 'XBOXKRNL':
        struct.pack_into('<I', xbe, 0x168, entry_va)
        print("  Kernel lib version -> entry[%d] VA 0x%08X" % (i, entry_va))

print("  New library count: %d" % new_count)
print("  New table VA: 0x%08X (file offset 0x%X)" % (new_table_va, new_table_file_offset))

# Print final table
for i in range(new_count):
    off = new_table_file_offset + i * 16
    name = xbe[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
    major, minor, build, flags = struct.unpack_from('<HHHH', xbe, off + 8)
    print("  [%d] %-8s v%d.%d.%d flags=0x%04X" % (i, name, major, minor, build, flags))

with open(xbe_path, 'wb') as f:
    f.write(xbe)

print("XBE patched successfully.")
