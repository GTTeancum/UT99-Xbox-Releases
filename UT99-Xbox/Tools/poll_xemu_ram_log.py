#!/usr/bin/env python
from __future__ import print_function

import argparse
import io
import os
import re
import socket
import struct
import sys
import time


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD_ROOT = os.path.join(REPO_ROOT, "UT99-Xbox", "build_cli")
DEFAULT_MAP = os.path.join(BUILD_ROOT, "release", "UnrealTournament.map")
DEFAULT_XBE = os.path.join(BUILD_ROOT, "release", "default.xbe")
DEFAULT_OUT = os.path.join(BUILD_ROOT, "xemu_ram_logs")
MIRROR_BYTES = 65536
MAGIC0 = 0x55395439
MAGIC1 = 0x4D41524C


def strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def connect_monitor(port, timeout):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            sock = socket.socket()
            sock.settimeout(1.5)
            sock.connect(("127.0.0.1", port))
            time.sleep(0.1)
            try:
                sock.recv(65536)
            except Exception:
                pass
            return sock
        except Exception as exc:
            last = exc
            try:
                sock.close()
            except Exception:
                pass
            time.sleep(0.25)
    raise RuntimeError("monitor port %d not ready: %s" % (port, last))


def monitor_cmd(sock, command, wait=0.25):
    sock.sendall((command + "\r\n").encode("ascii"))
    time.sleep(wait)
    data = b""
    sock.settimeout(0.8)
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    except Exception:
        pass
    return strip_ansi(data.decode("utf-8", errors="replace"))


def parse_words(text):
    words = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        _addr, values = line.split(":", 1)
        for token in re.findall(r"\b(?:0x)?[0-9a-fA-F]{8}\b", values):
            try:
                words.append(int(token, 16))
            except ValueError:
                pass
    return words


def read_words(sock, va, words, phys_delta):
    command = "x"
    addr = va
    if phys_delta is not None:
        command = "xp"
        addr = va - phys_delta
    reply = monitor_cmd(sock, "%s/%dwx 0x%08x" % (command, words, addr), 0.25)
    return parse_words(reply)


def read_u32(sock, va, phys_delta):
    words = read_words(sock, va, 1, phys_delta)
    return words[0] if words else None


def read_bytes(sock, va, byte_count, phys_delta):
    raw = bytearray()
    offset = 0
    while offset < byte_count:
        chunk = min(2048, byte_count - offset)
        words = read_words(sock, va + offset, (chunk + 3) // 4, phys_delta)
        if not words:
            break
        for word in words:
            raw.extend((
                word & 0xff,
                (word >> 8) & 0xff,
                (word >> 16) & 0xff,
                (word >> 24) & 0xff,
            ))
        offset += chunk
    return bytes(raw[:byte_count])


def read_xbe_sections(xbe_path):
    with open(xbe_path, "rb") as handle:
        data = handle.read()
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    table = struct.unpack_from("<I", data, 0x120)[0] - base
    sections = {}
    for index in range(count):
        header = table + index * 0x38
        va = struct.unpack_from("<I", data, header + 0x04)[0]
        size = struct.unpack_from("<I", data, header + 0x08)[0]
        name_va = struct.unpack_from("<I", data, header + 0x14)[0]
        name_off = name_va - base
        name_end = data.find(b"\x00", name_off)
        if name_off < 0 or name_end < 0:
            continue
        name = data[name_off:name_end].decode("ascii", errors="replace")
        sections[name] = (va, size)
    return sections


def read_map_segment_sections(map_path):
    segments = {}
    pattern = re.compile(r"^\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+[0-9a-fA-F]+H\s+(\S+)\s+")
    with io.open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                segment = int(match.group(1), 16)
                segments.setdefault(segment, set()).add(match.group(2))
    return segments


def section_for_segment(section_names):
    if ".bss" in section_names or ".data" in section_names:
        return ".data"
    if ".rdata" in section_names:
        return ".rdata"
    for name in section_names:
        if name.startswith(".text"):
            return ".text"
    for name in section_names:
        if name in ("D3D", "D3DX", "DSOUND", "XGRPH", "XNET", "XONLINE", "XPP", "DOLBY", "XON_RD"):
            return name
    return None


def resolve_symbol(map_path, xbe_sections, map_segments, symbol):
    names = [symbol]
    if not symbol.startswith("_"):
        names.append("_" + symbol)
    else:
        names.append(symbol[1:])

    patterns = [
        re.compile(r"\b([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+%s\b\s+([0-9a-fA-F]{8})\b" % re.escape(name))
        for name in names
    ]
    with io.open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            for pattern in patterns:
                match = pattern.search(line)
                if match:
                    segment = int(match.group(1), 16)
                    offset = int(match.group(2), 16)
                    pe_va = int(match.group(3), 16)
                    section_name = section_for_segment(map_segments.get(segment, set()))
                    if xbe_sections and section_name:
                        section = xbe_sections.get(section_name)
                        if section:
                            return section[0] + offset
                    return pe_va
    raise RuntimeError("symbol not found in map: %s" % symbol)


def resolve_symbols(map_path, xbe_path):
    required = [
        "g_XboxDebugMirrorMagic0",
        "g_XboxBootPhase",
        "g_XboxLogWriteCount",
        "g_XboxHeartbeatCount",
        "g_XboxLastLogTick",
        "g_XboxLogMirrorWriteOffset",
        "g_XboxLogMirrorWrapped",
        "g_XboxDebugMirrorMagic1",
        "g_XboxLogMirror",
    ]
    xbe_sections = read_xbe_sections(xbe_path) if xbe_path else None
    map_segments = read_map_segment_sections(map_path)
    return dict((name, resolve_symbol(map_path, xbe_sections, map_segments, name)) for name in required)


def choose_phys_delta(sock, symbols, requested):
    if requested == "0":
        return None
    if requested != "auto":
        return int(requested, 0)

    magic_va = symbols["g_XboxDebugMirrorMagic0"]
    if read_u32(sock, magic_va, None) == MAGIC0:
        return None

    candidates = [
        0x2a4000,
        0x287000,
        0x286000,
        0x285000,
        0x284000,
        0x283000,
        0x282000,
        0x281000,
        0x280000,
        0x264000,
    ]
    for delta in candidates:
        if read_u32(sock, magic_va, delta) == MAGIC0:
            return delta

    raise RuntimeError("could not resolve virtual-to-physical delta for RAM log symbols")


def decode_mirror(raw, offset=0, wrapped=0):
    if not raw:
        return ""

    if wrapped and offset is not None and 0 <= offset < len(raw):
        # The game writes a circular C string and leaves the terminator at the
        # current write offset. Rebuild chronological text before trimming.
        raw = raw[offset + 1:] + raw[:offset]

    raw = raw.replace(b"\x00", b"")
    return raw.decode("ascii", errors="replace")


def poll_port(port, symbols, args):
    sock = connect_monitor(port, args.timeout)
    try:
        phys_delta = choose_phys_delta(sock, symbols, args.phys_delta)
        values = {
            "boot_phase": read_u32(sock, symbols["g_XboxBootPhase"], phys_delta),
            "writes": read_u32(sock, symbols["g_XboxLogWriteCount"], phys_delta),
            "heartbeats": read_u32(sock, symbols["g_XboxHeartbeatCount"], phys_delta),
            "last_tick": read_u32(sock, symbols["g_XboxLastLogTick"], phys_delta),
            "offset": read_u32(sock, symbols["g_XboxLogMirrorWriteOffset"], phys_delta),
            "wrapped": read_u32(sock, symbols["g_XboxLogMirrorWrapped"], phys_delta),
            "magic0": read_u32(sock, symbols["g_XboxDebugMirrorMagic0"], phys_delta),
            "magic1": read_u32(sock, symbols["g_XboxDebugMirrorMagic1"], phys_delta),
        }
        raw = read_bytes(sock, symbols["g_XboxLogMirror"], MIRROR_BYTES, phys_delta)
        text = decode_mirror(raw, values.get("offset") or 0, values.get("wrapped") or 0)
        return phys_delta, values, text
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description="Poll UT99 Xbox RAM log mirror from XEMU HMP monitors.")
    parser.add_argument("--ports", default="4477", help="Comma-separated HMP monitor ports, e.g. 4478,4479.")
    parser.add_argument("--map", default=DEFAULT_MAP, help="UnrealTournament.map path from the same XBE build.")
    parser.add_argument("--xbe", default=DEFAULT_XBE, help="default.xbe path from the same build; used to translate linker-map symbols to runtime VAs.")
    parser.add_argument("--out-dir", default=DEFAULT_OUT, help="Folder for per-port RAM log dumps.")
    parser.add_argument("--phys-delta", default="auto", help="'auto', '0' for virtual x/ reads, or a hex VA-physical delta.")
    parser.add_argument("--timeout", type=float, default=8.0, help="Seconds to wait for each monitor port.")
    args = parser.parse_args()

    if not os.path.exists(args.map):
        print("Map file not found: %s" % args.map, file=sys.stderr)
        return 2
    if args.xbe and not os.path.exists(args.xbe):
        print("XBE file not found: %s" % args.xbe, file=sys.stderr)
        return 2

    if not os.path.isdir(args.out_dir):
        os.makedirs(args.out_dir)

    symbols = resolve_symbols(args.map, args.xbe)
    ports = [int(part.strip()) for part in args.ports.split(",") if part.strip()]
    if not ports:
        print("No monitor ports supplied.", file=sys.stderr)
        return 2

    status = 0
    for port in ports:
        try:
            phys_delta, values, text = poll_port(port, symbols, args)
            tag = "port%d" % port
            out_path = os.path.join(args.out_dir, tag + "_ut99_ram_log.txt")
            with io.open(out_path, "w", encoding="utf-8", errors="replace") as handle:
                handle.write("port=%d\n" % port)
                handle.write("map=%s\n" % os.path.abspath(args.map))
                if args.xbe:
                    handle.write("xbe=%s\n" % os.path.abspath(args.xbe))
                handle.write("phys_delta=%s\n" % ("virtual" if phys_delta is None else "0x%08X" % phys_delta))
                for key in ("boot_phase", "writes", "heartbeats", "last_tick", "offset", "wrapped", "magic0", "magic1"):
                    value = values.get(key)
                    if value is None:
                        handle.write("%s=None\n" % key)
                    elif key.startswith("magic") or key == "boot_phase":
                        handle.write("%s=0x%08X\n" % (key, value))
                    else:
                        handle.write("%s=%u\n" % (key, value))
                handle.write("\n")
                handle.write(text)
                if text and not text.endswith("\n"):
                    handle.write("\n")

            print("port %d: phase=0x%08X writes=%s hb=%s mirror=%s" % (
                port,
                values["boot_phase"] or 0,
                values["writes"],
                values["heartbeats"],
                out_path,
            ))
        except Exception as exc:
            status = 1
            print("port %d: ERROR: %s" % (port, exc), file=sys.stderr)

    return status


if __name__ == "__main__":
    sys.exit(main())
