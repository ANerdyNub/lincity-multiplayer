#!/usr/bin/env python3
"""Never-read client for the host blocking-send repro.

Reads the welcome/map/OK, then stops reading for --duration seconds.  At the
end it sends a malformed line, so the host log proves the simulation loop was
still able to process input.  This is the original C4 send-side scenario.
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
    ap.add_argument("--duration", type=float, default=30.0)
    ap.add_argument("--timeout", type=float, default=15.0)
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

        print("not reading for %.0f seconds" % args.duration, flush=True)
        time.sleep(args.duration)
        try:
            sock.sendall(b"999\n")
            print("sent malformed probe", flush=True)
        except OSError as exc:
            print("host closed before probe: %s" % exc, flush=True)
        time.sleep(1.0)
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("never-read client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
