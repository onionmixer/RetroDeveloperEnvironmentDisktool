#!/usr/bin/env python3
"""Print the Apple II 40-column text page 1 ($0400-$07FF) of the sa2 running
in this network namespace (debug server port 64504; reads of 256 bytes)."""
import json
import urllib.request


def read(addr, n):
    url = "http://127.0.0.1:64504/api/read?addr=%%24%04X&len=%d" % (addr, n)
    return json.load(urllib.request.urlopen(url, timeout=3))["bytes"]


mem = []
for a in range(0x400, 0x800, 0x100):
    mem += read(a, 0x100)
assert len(mem) == 0x400, len(mem)
for row in range(24):
    base = (row % 8) * 0x80 + (row // 8) * 0x28
    s = ""
    for c in mem[base:base + 40]:
        c &= 0x7F
        if c < 0x20:
            c += 0x40
        s += chr(c)
    print("%2d|%s|" % (row, s.rstrip()))
