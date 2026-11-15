#!/usr/bin/env python3
"""Protocol-4 HELLO/WELCOME block helpers for the network harness.

A handshake is a header line followed by a mod list, a plugin list, and an
``end`` marker.  The real game sends the same shape from ``src/network.c``.
"""
MSG_HELLO = 72
MSG_WELCOME = 87
NETWORK_VERSION = 4
BASE_VERSION = "1.15.0"


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


def send_hello(sock, version=NETWORK_VERSION, base=BASE_VERSION, mods=(),
               plugins=()):
    send_handshake(sock, MSG_HELLO, version, base, mods, plugins)


def send_welcome(sock, world=100, version=NETWORK_VERSION,
                 base=BASE_VERSION, mods=(), plugins=()):
    send_handshake(sock, MSG_WELCOME, version, base, mods, plugins, world)


def send_handshake(sock, code, version, base, mods, plugins, world=None):
    if code == MSG_WELCOME:
        header = "%d %d %d %s\n" % (code, world, version, base)
    else:
        header = "%d %d %s\n" % (code, version, base)
    lines = [header, "mods %d\n" % len(mods)]
    lines.extend("mod %s %s %d\n" % entry for entry in mods)
    lines.append("plugins %d\n" % len(plugins))
    lines.extend("plugin %s %s %d\n" % entry for entry in plugins)
    lines.append("end\n")
    sock.sendall("".join(lines).encode("ascii"))


def recv_hello(sock):
    return recv_handshake(sock, MSG_HELLO)


def recv_welcome(sock):
    return recv_handshake(sock, MSG_WELCOME)


def recv_handshake(sock, expected_code):
    header = recv_line(sock).decode("ascii", "replace").strip()
    fields = header.split()
    if expected_code == MSG_WELCOME:
        if len(fields) != 4 or fields[0] != str(expected_code):
            raise SystemExit("bad welcome header: %r" % header)
        world, version, base = int(fields[1]), int(fields[2]), fields[3]
    else:
        if len(fields) != 3 or fields[0] != str(expected_code):
            raise SystemExit("bad hello header: %r" % header)
        world, version, base = None, int(fields[1]), fields[2]

    fields = recv_line(sock).decode("ascii", "replace").split()
    if len(fields) != 2 or fields[0] != "mods":
        raise SystemExit("bad mod count: %r" % (fields,))
    mods = []
    for _ in range(int(fields[1])):
        fields = recv_line(sock).decode("ascii", "replace").split()
        if len(fields) != 4 or fields[0] != "mod":
            raise SystemExit("bad mod entry: %r" % (fields,))
        mods.append((fields[1], fields[2], int(fields[3])))

    fields = recv_line(sock).decode("ascii", "replace").split()
    if len(fields) != 2 or fields[0] != "plugins":
        raise SystemExit("bad plugin count: %r" % (fields,))
    plugins = []
    for _ in range(int(fields[1])):
        fields = recv_line(sock).decode("ascii", "replace").split()
        if len(fields) != 4 or fields[0] != "plugin":
            raise SystemExit("bad plugin entry: %r" % (fields,))
        plugins.append((fields[1], fields[2], int(fields[3])))

    if recv_line(sock).strip() != b"end":
        raise SystemExit("handshake has no end marker")
    return world, version, base, mods, plugins
