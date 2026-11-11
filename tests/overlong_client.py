#!/usr/bin/env python3
"""Overlong-line client for the host input-buffer policy test.

Reads the map, sends more than NETWORK_BUF_SIZE bytes without a newline, and
requires the host to close this client.  The host must remain alive.
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
        recv_exact(sock, csz)
        print("ok: %s" % recv_line(sock).decode("ascii", "replace").strip(),
              flush=True)

        print("sending 5000 bytes without newline", flush=True)
        sock.sendall(b"A" * 5000)
        sock.settimeout(5.0)
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            except ConnectionResetError:
                print("PASS: host reset the overlong-line client", flush=True)
                return
            if not data:
                print("PASS: host disconnected the overlong-line client",
                      flush=True)
                return
            print("draining %d buffered bytes before EOF" % len(data),
                  flush=True)
        raise SystemExit("host kept the overlong-line client connected")
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("overlong client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
