#!/usr/bin/env python3
"""memdump.py <start-hex> <len-hex> <out>: save main memory of the sa2 running
in this network namespace (debug server, 256-byte reads)."""
import json
import sys
import urllib.request

addr, n, out = int(sys.argv[1], 16), int(sys.argv[2], 16), sys.argv[3]
buf = bytearray()
while len(buf) < n:
    k = min(256, n - len(buf))
    url = "http://127.0.0.1:64504/api/read?addr=%%24%04X&len=%d" % (addr + len(buf), k)
    data = json.load(urllib.request.urlopen(url, timeout=3))["bytes"]
    assert len(data) == k, (len(data), k)
    buf += bytes(data)
open(out, "wb").write(buf)
