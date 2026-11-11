#!/usr/bin/env python3
"""Connect and send no HELLO.  The host must time it out without using a slot."""
import argparse
import socket
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5123)
    ap.add_argument("--duration", type=float, default=10.0)
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=5.0)
    print("silent client connected; not sending HELLO", flush=True)
    sock.settimeout(1.0)
    deadline = time.monotonic() + args.duration
    try:
        while time.monotonic() < deadline:
            try:
                if not sock.recv(1):
                    print("silent client disconnected by host", flush=True)
                    return
            except socket.timeout:
                continue
        raise SystemExit("host did not reject the silent client")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
