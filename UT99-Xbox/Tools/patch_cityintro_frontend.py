#!/usr/bin/env python
"""Patch a staged CityIntro.unr so frontend load uses a light GameInfo default.

This intentionally patches only deployment/staging copies. It rewrites the
compact object reference for CityIntro's LevelInfo.DefaultGameType from
Botpack.UTIntro to Engine.GameInfo.

It also clears the LevelInfo.Song hard reference to Uttitle.  CityIntro uses a
native Xbox stream for menu music, so keeping the UMX object reference only
costs memory on the frontend.
"""

from __future__ import print_function

import os
import sys


GAME_TYPE_OFFSET = 33273
GAME_TYPE_OLD = b"\xdc\x02"
GAME_TYPE_NEW = b"\xf4\x01"

SONG_OFFSET = 33268
SONG_OLD = b"\xd8\x02"
# CompactIndex zero with a continuation byte; this preserves the original
# two-byte property payload length while decoding as None.
SONG_NEW = b"\x40\x00"


def main(argv):
    if len(argv) != 2:
        print("usage: patch_cityintro_frontend.py <CityIntro.unr>", file=sys.stderr)
        return 2

    path = argv[1]
    with open(path, "rb") as f:
        data = bytearray(f.read())

    patches = [
        ("DefaultGameType", GAME_TYPE_OFFSET, GAME_TYPE_OLD, GAME_TYPE_NEW),
        ("Song", SONG_OFFSET, SONG_OLD, SONG_NEW),
    ]

    max_end = max(offset + len(old) for _name, offset, old, _new in patches)
    if len(data) < max_end:
        print("file too small: %s" % path, file=sys.stderr)
        return 1

    changed = False
    for label, offset, old, new in patches:
        current = bytes(data[offset:offset + len(old)])
        if current == new:
            print("CityIntro %s patch already applied: %s" % (label, path))
            continue
        if current != old:
            print(
                "unexpected bytes for %s at 0x%X in %s: got %s expected %s"
                % (label, offset, path, current.hex(), old.hex()),
                file=sys.stderr,
            )
            return 1
        data[offset:offset + len(old)] = new
        changed = True

    if not changed:
        print("CityIntro frontend patch already applied: %s" % path)
        return 0
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    print("Patched CityIntro frontend DefaultGameType/Song: %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
