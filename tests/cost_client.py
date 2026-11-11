#!/usr/bin/env python3
"""Check host-authoritative build cost and no-credit rejection.

The host starts a new city with balance 0.  A parkland (type 91, cost 1000,
GROUP_PARKLAND_NO_CREDIT) must be rejected without changing the balance; a
market (type 71, cost 100) must be accepted and charge exactly 100.  Remote
bulldoze of that market must charge the market's own bulldoze cost (100),
not a UI selection cost.  An unchanged or wrongly charged balance, a wrong
cost after bulldoze, or a broadcast for the rejected parkland fails.
"""
import argparse
import socket
import struct
import sys
import time

MSG_BUILD = 66
MSG_BULLDOZE = 68
MSG_MONEY = 77


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
    sock.settimeout(args.timeout)
    try:
        sock.sendall(b"72 2\n")
        print("welcome: %s"
              % recv_line(sock).decode("ascii", "replace").strip(),
              flush=True)
        usz, csz = struct.unpack("<II", recv_exact(sock, 8))
        if csz > 64 * 1024 * 1024:
            raise SystemExit("refusing implausible csz=%d" % csz)
        recv_exact(sock, csz)
        print("ok: %s" % recv_line(sock).decode("ascii", "replace").strip(),
              flush=True)

        pending = b""
        money = None
        builds = set()
        bulldozes = set()

        def pump(duration, done=None):
            nonlocal pending, money
            deadline = time.monotonic() + duration
            sock.settimeout(0.1)
            while time.monotonic() < deadline:
                try:
                    chunk = sock.recv(65536)
                except socket.timeout:
                    if done is not None and done():
                        return True
                    continue
                if not chunk:
                    raise EOFError("host closed the cost client")
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    parts = line.split()
                    if len(parts) < 2:
                        continue
                    try:
                        code = int(parts[0])
                        values = [int(p) for p in parts[1:]]
                    except ValueError:
                        continue
                    if code == MSG_MONEY:
                        money = values[0]
                    elif code == MSG_BUILD and len(values) >= 3:
                        builds.add((values[0], values[1], values[2]))
                    elif code == MSG_BULLDOZE and len(values) >= 2:
                        bulldozes.add((values[0], values[1]))
                if done is not None and done():
                    return True
            return done is None or done()

        if not pump(2.0, lambda: money is not None):
            raise SystemExit("no initial MSG_MONEY")
        print("initial money=%d" % money, flush=True)
        if money != 0:
            raise SystemExit("expected initial money 0, got %d" % money)

        # No-credit group: cost 1000 > balance 0, so reject.
        sock.sendall(b"66 4 4 91\n")
        pump(1.5)
        print("after parkland: money=%d builds=%s"
              % (money, sorted(builds)), flush=True)
        if money != 0:
            raise SystemExit("rejected parkland changed money to %d" % money)
        if (4, 4, 91) in builds:
            raise SystemExit("host broadcast the rejected parkland build")

        # Normal group: cost 100, credit-building is unchanged, and the
        # immediate money broadcast must report -100.
        sock.sendall(b"66 2 2 71\n")
        if not pump(3.0, lambda: money == -100 and (2, 2, 71) in builds):
            raise SystemExit(
                "market did not charge 100 (money=%s builds=%s)"
                % (money, sorted(builds)))
        if (4, 4, 91) in builds:
            raise SystemExit("rejected parkland appeared after the market")

        # Bulldoze cost comes from the target market group, not the host's
        # selected module.  Market build cost 100 + bulldoze cost 100 -> -200.
        sock.sendall(b"68 2 2 0\n")
        if not pump(3.0, lambda: money == -200 and (2, 2) in bulldozes):
            raise SystemExit(
                "market bulldoze did not charge its group cost "
                "(money=%s bulldozes=%s)" % (money, sorted(bulldozes)))
        print("final money=%d builds=%s bulldozes=%s"
              % (money, sorted(builds), sorted(bulldozes)), flush=True)
        print("PASS: host charged the requested market and bulldoze costs",
              flush=True)
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError) as exc:
        print("cost client error: %s" % exc, file=sys.stderr)
        sys.exit(1)
