#!/usr/bin/env python3
"""Reuse the OpenJKDF2ogx process-local trigger for Xemu's native PNG writer.

Resolve the screenshot-request flag through the executable's Screenshot/F12
code references. No host input, window enumeration or desktop capture is used.
"""
import argparse
import json
import os
import struct
import sys
import time


_XEMU_SCREENSHOT_FLAG_RVA_CACHE = {}


def pe_sections(data):
    pe_offset = int.from_bytes(data[0x3c:0x40], "little")
    section_count = int.from_bytes(data[pe_offset + 6:pe_offset + 8], "little")
    optional_size = int.from_bytes(data[pe_offset + 20:pe_offset + 22], "little")
    optional_offset = pe_offset + 24
    magic = int.from_bytes(data[optional_offset:optional_offset + 2], "little")
    if magic == 0x20B:
        image_base = int.from_bytes(data[optional_offset + 24:optional_offset + 32], "little")
    else:
        image_base = int.from_bytes(data[optional_offset + 28:optional_offset + 32], "little")
    section_offset = optional_offset + optional_size
    sections = []
    for index in range(section_count):
        offset = section_offset + index * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size = int.from_bytes(data[offset + 8:offset + 12], "little")
        virtual_address = int.from_bytes(data[offset + 12:offset + 16], "little")
        raw_size = int.from_bytes(data[offset + 16:offset + 20], "little")
        raw_offset = int.from_bytes(data[offset + 20:offset + 24], "little")
        sections.append((name, virtual_address, raw_offset, max(virtual_size, raw_size)))
    return image_base, sections


def pe_file_offset_to_va(offset, image_base, sections):
    for _name, virtual_address, raw_offset, size in sections:
        if raw_offset and raw_offset <= offset < raw_offset + size:
            return image_base + virtual_address + (offset - raw_offset)
    return None


def pe_va_to_file_offset(va, image_base, sections):
    rva = va - image_base
    for _name, virtual_address, raw_offset, size in sections:
        if virtual_address <= rva < virtual_address + size:
            return raw_offset + (rva - virtual_address)
    return None


def pe_find_rip_xrefs(data, image_base, sections, target_va):
    text_section = None
    for section in sections:
        if section[0] == ".text":
            text_section = section
            break
    if text_section is None:
        return []

    _name, text_va, text_raw, text_size = text_section
    text = data[text_raw:text_raw + text_size]
    hits = []
    for index in range(0, len(text) - 4):
        disp = struct.unpack_from("<i", text, index)[0]
        va = image_base + text_va + index + 4 + disp
        if va == target_va:
            hits.append(image_base + text_va + index)
    return hits


def xemu_find_screenshot_flag_pointer_rva(xemu_exe):
    cached = _XEMU_SCREENSHOT_FLAG_RVA_CACHE.get(xemu_exe)
    if cached is not None:
        return cached

    with open(xemu_exe, "rb") as f:
        data = f.read()
    image_base, sections = pe_sections(data)

    screenshot_offsets = []
    start = 0
    while True:
        offset = data.find(b"Screenshot\0", start)
        if offset < 0:
            break
        screenshot_offsets.append(offset)
        start = offset + 1

    f12_offsets = []
    start = 0
    while True:
        offset = data.find(b"F12\0", start)
        if offset < 0:
            break
        f12_offsets.append(offset)
        start = offset + 1

    screenshot_xrefs = []
    for offset in screenshot_offsets:
        va = pe_file_offset_to_va(offset, image_base, sections)
        if va is not None:
            screenshot_xrefs.extend(pe_find_rip_xrefs(data, image_base, sections, va))

    f12_xrefs = []
    for offset in f12_offsets:
        va = pe_file_offset_to_va(offset, image_base, sections)
        if va is not None:
            f12_xrefs.extend(pe_find_rip_xrefs(data, image_base, sections, va))

    anchors = []
    for screenshot_xref in screenshot_xrefs:
        for f12_xref in f12_xrefs:
            if abs(screenshot_xref - f12_xref) < 64:
                anchors.append(min(screenshot_xref, f12_xref) - 3)

    text_section = None
    for section in sections:
        if section[0] == ".text":
            text_section = section
            break
    if text_section is None:
        return None

    _name, text_va, text_raw, text_size = text_section
    text_end = text_raw + text_size
    for anchor in anchors:
        anchor_offset = pe_va_to_file_offset(anchor, image_base, sections)
        if anchor_offset is None:
            continue
        search_start = max(text_raw, anchor_offset - 128)
        search_end = min(text_end, anchor_offset + 0x1200)
        for offset in range(search_start, search_end - 12):
            if (data[offset:offset + 3] == b"\x48\x8b\x05" and
                    data[offset + 7:offset + 10] == b"\xc6\x00\x01" and
                    data[offset + 10] == 0xE9):
                pattern_va = pe_file_offset_to_va(offset, image_base, sections)
                disp = struct.unpack_from("<i", data, offset + 3)[0]
                pointer_va = pattern_va + 7 + disp
                pointer_rva = pointer_va - image_base
                _XEMU_SCREENSHOT_FLAG_RVA_CACHE[xemu_exe] = pointer_rva
                return pointer_rva
    return None


def xemu_process_module_base(process_handle):
    import ctypes

    psapi = ctypes.windll.psapi
    modules = (ctypes.c_void_p * 1024)()
    needed = ctypes.c_ulong()
    if not psapi.EnumProcessModules(process_handle, ctypes.byref(modules),
                                    ctypes.sizeof(modules), ctypes.byref(needed)):
        return None
    return int(modules[0]) if modules[0] else None


def trigger_native_screenshot(pid, xemu_exe, screenshot_dir, timeout, poll_interval=0.1):
    if os.name != "nt":
        return False, "xemu native screenshots require Windows", None

    import ctypes

    os.makedirs(screenshot_dir, exist_ok=True)
    # Xemu names PNGs with second precision. Rapid animation captures may
    # overwrite the previous name, so track writes as well as new filenames.
    before = {}
    for name in os.listdir(screenshot_dir):
        if name.lower().endswith(".png"):
            path = os.path.abspath(os.path.join(screenshot_dir, name))
            stat = os.stat(path)
            before[path] = (stat.st_mtime_ns, stat.st_size)

    pointer_rva = xemu_find_screenshot_flag_pointer_rva(xemu_exe)
    if pointer_rva is None:
        return False, "xemu native screenshot flag path not found", None

    kernel32 = ctypes.windll.kernel32
    process_query_information = 0x0400
    process_vm_operation = 0x0008
    process_vm_read = 0x0010
    process_vm_write = 0x0020
    process = kernel32.OpenProcess(
        process_query_information | process_vm_operation | process_vm_read | process_vm_write,
        False,
        pid,
    )
    if not process:
        return False, "OpenProcess failed pid=%d" % pid, None

    flag_addr = None
    try:
        module_base = xemu_process_module_base(process)
        if module_base is None:
            return False, "module base not found pid=%d" % pid, None

        pointer_addr = module_base + pointer_rva
        pointer_buf = (ctypes.c_ubyte * 8)()
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(process, ctypes.c_void_p(pointer_addr),
                                          pointer_buf, 8, ctypes.byref(transferred)):
            return False, "flag pointer unreadable rva=0x%x" % pointer_rva, None

        flag_addr = int.from_bytes(bytes(pointer_buf), "little")
        one = (ctypes.c_ubyte * 1)(1)
        if not kernel32.WriteProcessMemory(process, ctypes.c_void_p(flag_addr),
                                           one, 1, ctypes.byref(transferred)):
            return False, "flag write failed addr=0x%x" % flag_addr, None
    finally:
        kernel32.CloseHandle(process)

    deadline = time.time() + timeout
    newest = None
    while time.time() < deadline:
        current = []
        for name in os.listdir(screenshot_dir):
            if not name.lower().endswith(".png"):
                continue
            path = os.path.abspath(os.path.join(screenshot_dir, name))
            try:
                stat = os.stat(path)
                if before.get(path) == (stat.st_mtime_ns, stat.st_size):
                    continue
                # The writer creates/truncates the file before finishing it.
                # Do not hand callers a partial PNG just because it exists.
                with open(path, "rb") as capture:
                    if capture.read(8) != b"\x89PNG\r\n\x1a\n":
                        continue
                    capture.seek(-12, os.SEEK_END)
                    if capture.read(12) != b"\0\0\0\0IEND\xaeB`\x82":
                        continue
                current.append(path)
            except OSError:
                continue
        if current:
            newest = max(current, key=lambda path: os.path.getmtime(path))
            return True, "flag_rva=0x%x flag=0x%x" % (pointer_rva, flag_addr or 0), newest
        time.sleep(poll_interval)

    return False, "flag set but no PNG appeared rva=0x%x flag=0x%x" % (pointer_rva, flag_addr or 0), newest


def main():
    parser = argparse.ArgumentParser(description="Trigger XEMU's native screenshot flag.")
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--xemu-exe", required=True)
    parser.add_argument("--screenshot-dir", required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    ok, detail, path = trigger_native_screenshot(
        args.pid,
        os.path.abspath(args.xemu_exe),
        os.path.abspath(args.screenshot_dir),
        args.timeout,
    )
    print(json.dumps({"ok": ok, "detail": detail, "path": path}))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
