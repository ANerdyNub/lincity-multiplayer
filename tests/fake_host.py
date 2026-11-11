#!/usr/bin/env python3
"""Minimal LinCity host used by the client-validation repros.

Accepts one client, sends the welcome line and normally a valid 480000-byte
zeroed map, then sends the supplied protocol lines.  It never reads client
actions.  --bad-compressed-size and --partial exercise the P7 receive limits.
"""
import argparse
import socket
import struct
import sys
import time
import zlib

NETWORK_VERSION = 2
MSG_HELLO = 72
MSG_WELCOME = 87


def recv_line(sock):
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            raise EOFError("eof in line")
        buf += chunk
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5123)
    ap.add_argument("--payload", action="append", default=[],
                    help="protocol line to send after the map; repeatable")
    ap.add_argument("--delay", type=float, default=0.2)
    ap.add_argument("--wait", type=float, default=4.0,
                    help="seconds to keep the connection open afterwards")
    ap.add_argument("--bad-compressed-size", action="store_true",
                    help="send a 0xffffffff compressed-size header, no map")
    ap.add_argument("--partial", action="store_true",
                    help="send the last payload without a trailing newline")
    ap.add_argument("--welcome-version", type=int, default=NETWORK_VERSION,
                    help="protocol version advertised in MSG_WELCOME")
    ap.add_argument("--no-map", action="store_true",
                    help="send only the welcome line and keep the socket open")
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(1)
    print("LISTENING", flush=True)
    conn, addr = srv.accept()
    print("accepted %s:%d" % addr, flush=True)
    try:
        conn.settimeout(5.0)
        hello = recv_line(conn)
        print("hello: %s" % hello.decode("ascii", "replace").strip(),
              flush=True)
        hello_values = [int(p) for p in hello.split()]
        if hello_values != [MSG_HELLO, NETWORK_VERSION]:
            raise SystemExit("unexpected hello: %r" % hello)

        # The wire protocol uses numeric message codes: MSG_WELCOME is 87.
        conn.sendall(("%d 100 %d\n"
                      % (MSG_WELCOME, args.welcome_version)).encode("ascii"))
        if args.no_map:
            print("welcome sent; waiting for the client", flush=True)
            time.sleep(args.wait)
            return
        if args.bad_compressed_size:
            conn.sendall(struct.pack("<II", 480000, 0xffffffff))
            print("bad map header sent", flush=True)
            time.sleep(args.wait)
            return
        raw = bytes(480000)
        comp = zlib.compress(raw, 9)
        conn.sendall(struct.pack("<II", len(raw), len(comp)))
        conn.sendall(comp)
        conn.sendall(b"75\n")  # numeric MSG_OK
        # Numeric protocol codes, matching network_send_text("%d ...").
        conn.sendall(b"77 100000000\n")
        conn.sendall(b"80 100\n")
        conn.sendall(b"89 0\n")
        print("map sent", flush=True)
        time.sleep(0.5)
        for i, payload in enumerate(args.payload):
            print("sending: %s" % payload, flush=True)
            data = payload.encode("ascii")
            if args.partial and i == len(args.payload) - 1:
                conn.sendall(data)
                print("sent partial payload", flush=True)
            else:
                conn.sendall(data + b"\n")
            time.sleep(args.delay)
        time.sleep(args.wait)
    finally:
        conn.close()
        srv.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, EOFError) as exc:
        print("fake host error: %s" % exc, file=sys.stderr)
        sys.exit(1)
