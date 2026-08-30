#!/usr/bin/env python3
"""Probe the game's InitPAD buffer over the psx-runtime TCP debug server.

Twisted Metal 2 uses the old BIOS-polled pad interface (InitPAD/StartPAD) and
the BIOS fills a buffer at 0x801B2D80:

    +0 status (0xFF = no pad connected)   +1 pad id (0x41 digital / 0x73 analog)
    +2..+3 buttons (active low)           +4..+7 stick axes (analog only)

With an analog pad the driver marks the pad absent on ~half the polls, so the
game discards those frames and input looks dead. Run this against a live
`--debug-port 4370` session to see which mode is on the wire and how stable
the status byte is. See docs/DISC_NOTES.md.
"""

import collections
import json
import socket
import time

HOST, PORT = "127.0.0.1", 4370
def cmd(**kw):
    s = socket.create_connection((HOST, PORT), timeout=60)
    f = s.makefile("rwb")
    f.write((json.dumps(kw) + "\n").encode()); f.flush()
    buf = b""
    while True:
        chunk = f.readline()
        if not chunk: break
        buf += chunk
        try:
            r = json.loads(buf); s.close(); return r
        except json.JSONDecodeError:
            continue
    s.close(); raise SystemExit("no reply for %r" % kw.get("cmd"))

def buf12():
    return bytes.fromhex(cmd(id=1, cmd="read_ram", addr=0x801B2D80, len=12)["hex"])

tr = cmd(id=9, cmd="sio_trace", count=12)
ids = sorted({e["rx"] for e in tr["entries"] if e["tx"] == "0x42"})
print("pad id on the wire: %s" % (ids or "(not in window)"))

cmd(id=2, cmd="clear_input")
stat = collections.Counter(); pid = collections.Counter()
N = 60
for _ in range(N):
    b = buf12()
    stat["%02X" % b[0]] += 1
    pid["%02X" % b[1]] += 1
    time.sleep(0.05)
print("over %d samples of the InitPAD buffer @0x801B2D80:" % N)
print("  byte[0] status : %s" % dict(stat))
print("  byte[1] pad id : %s" % dict(pid))
print("  (status 0xFF = 'no pad connected' in the InitPAD buffer convention)")
