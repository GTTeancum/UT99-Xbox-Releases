#!/usr/bin/env python3
"""Capture UT99 Xbox UDP debug mirror output from XEMU and hardware."""

from __future__ import print_function

import argparse
import datetime
import os
import socket


def default_output_path():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    xbox_dir = os.path.dirname(script_dir)
    return os.path.join(xbox_dir, "build_cli", "release", "systemlink_udp_debug.log")


def main():
    parser = argparse.ArgumentParser(description="Listen for UT99 Xbox UDP debug packets.")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", default=14099, type=int)
    parser.add_argument("--out", default=default_output_path())
    args = parser.parse_args()

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))

    print("Listening on {0}:{1} -> {2}".format(args.host, args.port, args.out))
    with open(args.out, "a", buffering=1) as log:
        log.write("\n--- listener started {0} on {1}:{2} ---\n".format(
            datetime.datetime.now().isoformat(timespec="seconds"),
            args.host,
            args.port,
        ))
        while True:
            data, addr = sock.recvfrom(4096)
            text = data.decode("utf-8", "replace").rstrip("\r\n")
            stamp = datetime.datetime.now().isoformat(timespec="milliseconds")
            line = "{0} {1}:{2} {3}".format(stamp, addr[0], addr[1], text)
            print(line)
            log.write(line + "\n")


if __name__ == "__main__":
    main()
