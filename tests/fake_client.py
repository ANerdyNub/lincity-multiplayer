#!/usr/bin/env python3
"""Minimal LinCity multiplayer client for the audit repro harness.

Reads the welcome line, the 8-byte compressed-map header, the map blob, and
the OK line, sends zero or more protocol lines, then reads broadcasts for a
short settle period.  Expected build/bulldoze/MFLAGS broadcasts can be
asserted, and forbidden build broadcasts can be rejected.
"""
import argparse
import socket
import struct
import sys
import time

MSG_BUILD = 66
MSG_BULLDOZE = 68
MSG_MFLAGS = 70
MSG_HELLO = 72
MSG_WELCOME = 87
MSG_DISCONN = 88
NETWORK_VERSION = 2


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


def parse_values(spec, count, label):
    parts = spec.split(",")
    if len(parts) != count:
        raise SystemExit("%s expects %d comma-separated integers: %s"
                         % (label, count, spec))
    try:
        return tuple(int(p, 10) for p in parts)
    except ValueError:
        raise SystemExit("%s has a non-integer value: %s" % (label, spec))


def add_values(values, spec, count, label):
    values.add(parse_values(spec, count, label))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5123)
    ap.add_argument("--payload", action="append", default=[],
                    help="protocol line to send after the map; repeatable")
    ap.add_argument("--delay", type=float, default=0.2,
                    help="seconds between payloads")
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--hello-version", type=int, default=NETWORK_VERSION,
                    help="protocol version sent in MSG_HELLO")
    ap.add_argument("--expect-disconnect", action="store_true",
                    help="expect MSG_HELLO rejection instead of a map")
    ap.add_argument("--wait", type=float, default=2.0,
                    help="seconds to keep reading broadcasts after the last payload")
    ap.add_argument("--expect-build", action="append", default=[],
                    metavar="X,Y,TYPE",
                    help="require a matching MSG_BUILD broadcast")
    ap.add_argument("--forbid-build", action="append", default=[],
                    metavar="X,Y,TYPE",
                    help="fail if a matching MSG_BUILD broadcast appears")
    ap.add_argument("--expect-bulldoze", action="append", default=[],
                    metavar="X,Y",
                    help="require a matching MSG_BULLDOZE broadcast")
    ap.add_argument("--expect-mflags", action="append", default=[],
                    metavar="X,Y,FLAGS",
                    help="require a matching merged MSG_MFLAGS broadcast")
    args = ap.parse_args()

    expected_builds = set()
    forbidden_builds = set()
    expected_bulldozes = set()
    expected_mflags = set()
    for spec in args.expect_build:
        add_values(expected_builds, spec, 3, "--expect-build")
    for spec in args.forbid_build:
        add_values(forbidden_builds, spec, 3, "--forbid-build")
    for spec in args.expect_bulldoze:
        add_values(expected_bulldozes, spec, 2, "--expect-bulldoze")
    for spec in args.expect_mflags:
        add_values(expected_mflags, spec, 3, "--expect-mflags")

    sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    sock.settimeout(args.timeout)
    try:
        sock.sendall(("%d %d\n" % (MSG_HELLO, args.hello_version)).encode("ascii"))
        if args.expect_disconnect:
            line = recv_line(sock)
            print("hello rejection: %s"
                  % line.decode("ascii", "replace").strip(), flush=True)
            parts = line.split()
            if len(parts) != 1 or int(parts[0]) != MSG_DISCONN:
                raise SystemExit("expected MSG_DISCONN, got %r" % line)
            print("PASS: host rejected the client hello before a map", flush=True)
            return

        welcome = recv_line(sock)
        print("welcome: %s" % welcome.decode("ascii", "replace").strip(), flush=True)
        welcome_values = [int(p) for p in welcome.split()]
        if (len(welcome_values) != 3
                or welcome_values[0] != MSG_WELCOME
                or welcome_values[1] != 100
                or welcome_values[2] != NETWORK_VERSION):
            raise SystemExit("unexpected welcome: %r" % welcome)

        hdr = recv_exact(sock, 8)
        usz, csz = struct.unpack("<II", hdr)
        print("map header: usz=%d csz=%d" % (usz, csz), flush=True)
        if csz > 64 * 1024 * 1024:
            raise SystemExit("refusing implausible csz=%d" % csz)
        recv_exact(sock, csz)
        print("map received", flush=True)

        ok = recv_line(sock)
        print("ok: %s" % ok.decode("ascii", "replace").strip(), flush=True)

        for payload in args.payload:
            print("sending: %s" % payload, flush=True)
            sock.sendall(payload.encode("ascii") + b"\n")
            time.sleep(args.delay)

        seen_builds = set()
        seen_bulldozes = set()
        seen_mflags = set()
        pending = b""
        deadline = time.monotonic() + args.wait
        sock.settimeout(0.1)
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                parts = line.split()
                if len(parts) < 2:
                    continue
                try:
                    values = [int(p, 10) for p in parts]
                except ValueError:
                    continue
                code = values[0]
                if code == MSG_BUILD and len(values) >= 4:
                    seen_builds.add(tuple(values[1:4]))
                elif code == MSG_BULLDOZE and len(values) >= 3:
                    seen_bulldozes.add(tuple(values[1:3]))
                elif code == MSG_MFLAGS and len(values) >= 4:
                    seen_mflags.add(tuple(values[1:4]))

        if seen_builds:
            print("build broadcasts: %s" % sorted(seen_builds), flush=True)
        if seen_bulldozes:
            print("bulldoze broadcasts: %s" % sorted(seen_bulldozes), flush=True)
        if seen_mflags:
            print("mflags broadcasts: %s" % sorted(seen_mflags), flush=True)

        missing = []
        for item in sorted(expected_builds - seen_builds):
            missing.append("MSG_BUILD %d,%d,%d" % item)
        for item in sorted(expected_bulldozes - seen_bulldozes):
            missing.append("MSG_BULLDOZE %d,%d" % item)
        for item in sorted(expected_mflags - seen_mflags):
            missing.append("MSG_MFLAGS %d,%d,%d" % item)
        if missing:
            raise SystemExit("missing expected broadcast(s): %s"
                             % ", ".join(missing))

        for item in sorted(forbidden_builds & seen_builds):
            raise SystemExit("forbidden MSG_BUILD broadcast: %d,%d,%d" % item)

        print("PASS: expected broadcasts observed", flush=True)
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
