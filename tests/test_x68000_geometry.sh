#!/usr/bin/env bash
# X68000 2HD geometry, sector numbering and BPB size.
#
# A 2HD disk is 77 cylinders x 2 heads x 8 sectors (numbered 1-8) x 1024 bytes
# = 1,232 sectors / 1,261,568 bytes; a real Human68k disk's BPB says 1,232.
# Before the fix: geometry said 154 tracks x 2 sides (info showed twice the size,
# `create --fs human68k` wrote BPB total 2,464), XDF<->DIM convert dropped the
# 8th sector of every track (exit 0), `dump` could not reach sector 8, and an add
# beyond the real space "succeeded" while overwriting other files.
# Expected values are computed here (python), not taken from rdedisktool.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REAL_HUMAN="${REAL_HUMAN:-$TOOL_ROOT/../diskwork/bootdisk/x68000/HUMAN302.XDF}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_x68_geom.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rc_of() {
  set +e
  "$@" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc -lt 128 ]] || { cat "$WORK/out.log" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}
py() { python3 -I -c "$@"; }
bpb_total() { py 'import sys,struct; print(struct.unpack_from("<H", open(sys.argv[1],"rb").read(), 0x13)[0])' "$1"; }
# free bytes from the BPB and FAT, counting only clusters inside the image
free_bytes() {
  py 'import sys,struct
d = open(sys.argv[1], "rb").read()
bps, spc, res, nf, root, tot = struct.unpack_from("<HBHBHH", d, 0x0B)
spf = struct.unpack_from("<H", d, 0x16)[0]
total = min(tot, len(d) // bps)
first = res + nf * spf + (root * 32 + bps - 1) // bps
clusters = (total - first) // spc
fat = d[res * bps:(res + spf) * bps]
def ent(n):
    o = n * 3 // 2
    v = fat[o] | fat[o + 1] << 8
    return v >> 4 if n & 1 else v & 0xFFF
print(sum(1 for n in range(2, clusters + 2) if ent(n) == 0) * spc * bps)' "$1"
}

SIZE=$(py 'print(77 * 2 * 8 * 1024)')
SECTORS=$(py 'print(77 * 2 * 8)')
[[ $SIZE == 1261568 && $SECTORS == 1232 ]] || fail "reference arithmetic"

# --- random XDF -> DIM -> XDF: every byte back (was: 154 sectors lost)
py 'import random,sys; r = random.Random(68); open(sys.argv[1], "wb").write(bytes(r.randrange(256) for _ in range(1261568)))' "$WORK/r.xdf"
[[ $(rc_of "$RDEDISKTOOL" convert "$WORK/r.xdf" "$WORK/r.dim" -f dim) == 0 ]] || { cat "$WORK/out.log" >&2; fail "xdf->dim"; }
! grep -q "Failed to copy" "$WORK/out.log" || fail "xdf->dim lost sectors"; pass
py 'import sys
x = open(sys.argv[1], "rb").read(); d = open(sys.argv[2], "rb").read()
sys.exit(0 if d[256:256 + len(x)] == x else 1)' "$WORK/r.xdf" "$WORK/r.dim" || fail "DIM data area != XDF"
pass
[[ $(rc_of "$RDEDISKTOOL" convert "$WORK/r.dim" "$WORK/r2.xdf" -f xdf) == 0 ]] || fail "dim->xdf"
cmp -s "$WORK/r.xdf" "$WORK/r2.xdf" || fail "XDF -> DIM -> XDF not byte-identical"; pass

# --- dump reaches sectors 1-8 (offsets computed here)
for spec in "0 0 1" "0 0 8" "0 1 8" "76 1 8" "40 0 5"; do
  set -- $spec
  "$RDEDISKTOOL" dump "$WORK/r.xdf" -t "$1" --side "$2" -s "$3" >"$WORK/dump.txt" || fail "dump $spec"
  want=$(py 'import sys
c, h, s = map(int, sys.argv[2:5]); d = open(sys.argv[1], "rb").read()
o = ((c * 2 + h) * 8 + (s - 1)) * 1024
print(" ".join("%02X" % b for b in d[o:o + 8]))' "$WORK/r.xdf" "$1" "$2" "$3")
  got=$(awk '/^000000/ { print $2, $3, $4, $5, $6, $7, $8, $9; exit }' "$WORK/dump.txt")
  [[ "$got" == "$want" ]] || fail "dump $spec: got '$got', want '$want'"
  pass
done
for spec in "-s 0" "-s 9" "-t 77 -s 1" "-t 0 --side 2 -s 1"; do
  [[ $(rc_of "$RDEDISKTOOL" dump "$WORK/r.xdf" -t 0 $spec) != 0 ]] || fail "dump $spec accepted"; pass
done

# --- created disk: real size, BPB like a real Human68k disk
for fmt in xdf dim; do
  "$RDEDISKTOOL" create "$WORK/n.$fmt" -f "$fmt" --fs human68k -n GEOM >"$WORK/create.txt"
  grep -q "Geometry: 77 tracks, 2 side(s), 8 sectors/track, 1024 bytes/sector" "$WORK/create.txt" || fail "$fmt create geometry"
  "$RDEDISKTOOL" info "$WORK/n.$fmt" >"$WORK/info.txt"
  grep -q "Total Size: $SIZE bytes" "$WORK/info.txt" || fail "$fmt info size"; pass
done
[[ $(bpb_total "$WORK/n.xdf") == "$SECTORS" ]] || fail "created BPB total $(bpb_total "$WORK/n.xdf")"; pass
grep -q "Free Space: $(free_bytes "$WORK/n.xdf") bytes" <("$RDEDISKTOOL" info "$WORK/n.xdf") || fail "free space"; pass
if [[ -f "$REAL_HUMAN" ]]; then
  py 'import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
sys.exit(0 if a[0x0B:0x1C] == b[0x0B:0x1C] else 1)' "$WORK/n.xdf" "$REAL_HUMAN" || fail "BPB differs from the real Human68k disk"
  pass
fi

# --- fill the disk: an add past the real end is refused and changes nothing
head -c 200000 /dev/urandom >"$WORK/big.bin"
head -c 150000 /dev/urandom >"$WORK/mid.bin"
for i in 1 2 3 4 5 6; do "$RDEDISKTOOL" add "$WORK/n.xdf" "$WORK/big.bin" "F$i.BIN" >/dev/null; done
[[ $(free_bytes "$WORK/n.xdf") -lt 150000 ]] || fail "test setup: disk not full enough"
cp "$WORK/n.xdf" "$WORK/n0.xdf"
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/n.xdf" "$WORK/mid.bin" G.BIN) != 0 ]] || fail "add past the end accepted"
grep -q "Not enough space" "$WORK/out.log" || { cat "$WORK/out.log" >&2; fail "refused for another reason"; }
cmp -s "$WORK/n.xdf" "$WORK/n0.xdf" || fail "refused add changed the image"; pass
for i in 1 2 3 4 5 6; do
  "$RDEDISKTOOL" extract "$WORK/n.xdf" "F$i.BIN" "$WORK/x" >/dev/null
  cmp -s "$WORK/x" "$WORK/big.bin" || fail "F$i damaged"; pass
done

# --- disk written by an older rdedisktool (BPB total 2,464): clamp, report, repair
"$RDEDISKTOOL" create "$WORK/old.xdf" -f xdf --fs human68k -n OLD >/dev/null
for i in 1 2 3; do "$RDEDISKTOOL" add "$WORK/old.xdf" "$WORK/big.bin" "F$i.BIN" >/dev/null; done
py 'import sys,struct
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); struct.pack_into("<H", d, 0x13, 2464)
open(p, "wb").write(d)' "$WORK/old.xdf"
"$RDEDISKTOOL" info "$WORK/old.xdf" >"$WORK/info.txt" 2>&1
grep -q "BPB total sectors 2464 exceed the image (1232 sectors)" "$WORK/info.txt" || fail "no warning for oversized BPB"; pass
grep -q "Free Space: $(free_bytes "$WORK/old.xdf") bytes" "$WORK/info.txt" || fail "free space not limited to the image"; pass
[[ $(rc_of "$RDEDISKTOOL" validate "$WORK/old.xdf") != 0 ]] || fail "validate accepted oversized BPB"
grep -q "exceed the image" "$WORK/out.log" || fail "validate reason"; pass
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/old.xdf" "$WORK/mid.bin" G.BIN) == 0 ]] || { cat "$WORK/out.log" >&2; fail "add on old disk"; }
[[ $(bpb_total "$WORK/old.xdf") == "$SECTORS" ]] || fail "BPB not repaired ($(bpb_total "$WORK/old.xdf"))"; pass
[[ $(rc_of "$RDEDISKTOOL" validate "$WORK/old.xdf") == 0 ]] || fail "validate after repair"; pass
for i in 4 5 6 7; do "$RDEDISKTOOL" add "$WORK/old.xdf" "$WORK/big.bin" "F$i.BIN" >/dev/null 2>&1 || true; done
for i in 1 2 3; do
  "$RDEDISKTOOL" extract "$WORK/old.xdf" "F$i.BIN" "$WORK/x" >/dev/null
  cmp -s "$WORK/x" "$WORK/big.bin" || fail "old disk: F$i damaged after filling"; pass
done
"$RDEDISKTOOL" extract "$WORK/old.xdf" G.BIN "$WORK/x" >/dev/null
cmp -s "$WORK/x" "$WORK/mid.bin" || fail "old disk: G damaged"; pass

echo "PASS test_x68000_geometry ($CHECKS checks)"
