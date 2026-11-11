#!/usr/bin/env python3
"""Slow partial-line client for the host DoS repro.

Reads the map, then sends one byte of an action line every --interval seconds
without ever sending a newline, while continuously draining host broadcasts.
The host must keep ticking and must not disconnect this client.
"""
import argparse
import socket
import struct
import sys
import threading
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
    ap.add_argument("--interval", type=float, default=4.0)
    ap.add_argument("--duration", type=float, default=30.0)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--min-messages", type=int, default=50)
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    stop = threading.Event()
    sender_error = []

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
        ok = recv_line(sock)
        print("ok: %s" % ok.decode("ascii", "replace").strip(), flush=True)

        def send_partial_line():
            try:
                for byte in b"66 1 1 71":
                    if stop.wait(args.interval):
                        return
                    sock.sendall(bytes([byte]))
            except OSError as exc:
                sender_error.append(str(exc))

        sender = threading.Thread(target=send_partial_line, daemon=True)
        sender.start()
        sock.settimeout(0.5)
        started = time.monotonic()
        deadline = started + args.duration
        received_bytes = 0
        received_lines = 0

        while time.monotonic() < deadline:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                raise SystemExit("host closed the partial-line client early")
            received_bytes += len(data)
            received_lines += data.count(b"\n")

        stop.set()
        sender.join(timeout=1.0)
        elapsed = time.monotonic() - started
        print("received_bytes=%d received_messages=%d elapsed=%.1f"
              % (received_bytes, received_lines, elapsed), flush=True)
        if sender_error:
            raise SystemExit("partial-line sender failed: %s" % sender_error[0])
        if received_lines < args.min_messages:
            raise SystemExit(
                "host stalled or disconnected: %d messages < %d"
                % (received_lines, args.min_messages))
        print("PASS: host kept ticking during the slow partial line", flush=True)
    finally:
        stop.set()
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("slow-line client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
