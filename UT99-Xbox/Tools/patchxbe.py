#!/usr/bin/env python
# patchxbe.py - Patch PE subsystem to Xbox (14) then run imagebld
# Usage: patchxbe.py <exe_path> <xbe_out_path>

import sys
import os
import struct
import shutil
import subprocess

exe_path = sys.argv[1]
xbe_path = sys.argv[2]

# Read the EXE
with open(exe_path, 'rb') as f:
    data = bytearray(f.read())

# Find PE header offset at 0x3C
pe_offset = struct.unpack_from('<I', data, 0x3C)[0]

# Verify PE signature
if data[pe_offset:pe_offset+4] != b'PE\0\0':
    print("ERROR: Not a valid PE file")
    sys.exit(1)

# Subsystem field is at PE header + 4 (sig) + 20 (COFF) + 68 (optional header offset to subsystem)
# Optional header starts at pe_offset + 24
# Subsystem is at offset 68 within optional header
subsystem_offset = pe_offset + 24 + 68
old_subsystem = struct.unpack_from('<H', data, subsystem_offset)[0]
print("Original subsystem: %d" % old_subsystem)

# Patch to Xbox subsystem (14)
struct.pack_into('<H', data, subsystem_offset, 14)
print("Patched subsystem to: 14 (Xbox)")

# Write patched EXE to a temp file
temp_exe = exe_path + '.xbox.tmp'
with open(temp_exe, 'wb') as f:
    f.write(data)

# Run imagebld
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

print("Running: " + ' '.join(cmd))
result = subprocess.call(cmd)

# Clean up temp file
os.remove(temp_exe)

if result != 0:
    print("imagebld failed with code %d" % result)
    sys.exit(result)

print("XBE created: " + xbe_path)
