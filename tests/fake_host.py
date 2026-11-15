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

from net_handshake import (BASE_VERSION, NETWORK_VERSION, recv_hello,
                           send_welcome)


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
    ap.add_argument("--welcome-base-version", default=BASE_VERSION,
                    help="base game version advertised in MSG_WELCOME")
    ap.add_argument("--no-map", action="store_true",
                    help="send only the welcome block and keep the socket open")
    ap.add_argument("--mod-mismatch", action="store_true",
                    help="send a host mod list the client must reject")
    ap.add_argument("--plugin-mismatch", action="store_true",
                    help="send a host plugin list the client must reject")
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
        _, hello_version, hello_base, hello_mods, hello_plugins = recv_hello(conn)
        print("hello: %d %s mods=%d plugins=%d"
              % (hello_version, hello_base, len(hello_mods),
                 len(hello_plugins)), flush=True)
        if hello_version != NETWORK_VERSION:
            raise SystemExit("unexpected hello version: %d" % hello_version)

        if args.mod_mismatch or args.plugin_mismatch:
            mods = [("fake_mod", "1.0.0", 1)] if args.mod_mismatch else []
            plugins = ([("fake_plugin", "1.0.0", 1)]
                       if args.plugin_mismatch else [])
            send_welcome(conn, version=args.welcome_version,
                         base=args.welcome_base_version, mods=mods,
                         plugins=plugins)
            print("mismatched welcome block sent", flush=True)
            time.sleep(args.wait)
            return

        send_welcome(conn, version=args.welcome_version,
                     base=args.welcome_base_version)
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
