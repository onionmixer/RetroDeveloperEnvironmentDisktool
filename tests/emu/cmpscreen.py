#!/usr/bin/env python3
"""cmpscreen.py <a.txt> <b.txt> <min-nonblank-rows>: compare two text pages
printed by textpage.py. The blinking cursor ($7F) counts as a space. Exit 0
only when equal AND the first page has at least <min> non-blank rows (two
blank screens must not count as a match)."""
import sys


def page(path):
    rows = []
    for line in open(path, encoding="latin-1"):
        line = line.rstrip("\n")
        if len(line) > 3 and line[2] == "|":
            assert line.endswith("|"), line
            rows.append(line[:-1].replace("\x7f", " ").rstrip())
    return rows


a, b = page(sys.argv[1]), page(sys.argv[2])
nonblank = sum(1 for r in a if r[3:].strip())
print(("SAME" if a == b else "DIFF"), f"rows={len(a)}/{len(b)} nonblank={nonblank}")
for x, y in zip(a, b):
    if x != y:
        print("  <", x)
        print("  >", y)
sys.exit(0 if a == b and len(a) == 24 and nonblank >= int(sys.argv[3]) else 1)
