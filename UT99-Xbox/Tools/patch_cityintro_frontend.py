#!/usr/bin/env python
"""Patch a staged CityIntro.unr so frontend load uses a light GameInfo default.

This intentionally patches only deployment/staging copies. It rewrites the
compact object reference for CityIntro's LevelInfo.DefaultGameType from
Botpack.UTIntro to Engine.GameInfo.
"""

from __future__ import print_function

import os
import sys


OFFSET = 33273
OLD = b"\xdc\x02"
NEW = b"\xf4\x01"


def main(argv):
    if len(argv) != 2:
        print("usage: patch_cityintro_frontend.py <CityIntro.unr>", file=sys.stderr)
        return 2

    path = argv[1]
    with open(path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < OFFSET + len(OLD):
        print("file too small: %s" % path, file=sys.stderr)
        return 1

    current = bytes(data[OFFSET:OFFSET + len(OLD)])
    if current == NEW:
        print("CityIntro frontend patch already applied: %s" % path)
        return 0
    if current != OLD:
        print(
            "unexpected bytes at 0x%X in %s: got %s expected %s"
            % (OFFSET, path, current.hex(), OLD.hex()),
            file=sys.stderr,
        )
        return 1

    data[OFFSET:OFFSET + len(OLD)] = NEW
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    print("Patched CityIntro frontend DefaultGameType: %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
