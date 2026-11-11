#!/usr/bin/env python3
"""Read-only client for the multi-client P8b repro.

Reads the map, then drains broadcasts for --duration seconds without sending
anything.  A stalled second client must not stop this one from being served.
"""
import argparse
import socket
import struct
import sys
import time


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("eof after %d/%d bytes" % (len(buf), n))
        buf += chunk
    return buf


def recv_line(sock):
    buf = b""
    while not buf.endswith(b"\n"):
        buf += recv_exact(sock, 1)
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5123)
    ap.add_argument("--duration", type=float, default=35.0)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--min-messages", type=int, default=50)
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    try:
        sock.sendall(b"72 2\n")
        print("welcome: %s" % recv_line(sock).decode("ascii", "replace").strip(),
              flush=True)
        hdr = recv_exact(sock, 8)
        usz, csz = struct.unpack("<II", hdr)
        print("map header: usz=%d csz=%d" % (usz, csz), flush=True)
        if csz > 64 * 1024 * 1024:
            raise SystemExit("refusing implausible csz=%d" % csz)
        recv_exact(sock, csz)
        print("ok: %s" % recv_line(sock).decode("ascii", "replace").strip(),
              flush=True)

        sock.settimeout(0.5)
        deadline = time.monotonic() + args.duration
        received_bytes = 0
        received_lines = 0
        while time.monotonic() < deadline:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                raise SystemExit("host closed the normal client early")
            received_bytes += len(data)
            received_lines += data.count(b"\n")

        print("normal client received_bytes=%d received_messages=%d"
              % (received_bytes, received_lines), flush=True)
        if received_lines < args.min_messages:
            raise SystemExit(
                "host stopped serving the normal client: %d messages < %d"
                % (received_lines, args.min_messages))
        print("PASS: normal client kept being served", flush=True)
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("normal client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
