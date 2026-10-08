#!/usr/bin/env bash
# `list` Type / Attr columns on Apple II disks.
#
# Expected values were measured on the real systems (isolated AppleWin, 2026-10-08):
#   DOS 3.3 CATALOG: type letters T I A B S R for $00 $01 $02 $04 $08 $10; LOCK sets
#     bit 7 ($04 -> $84) and CATALOG shows '*'. ($20/$40 show as A/B in DOS;
#     rdedisktool prints a/b to tell them apart.)
#   ProDOS 2.4.3 CAT: LOCK sets access $21, UNLOCK $E3; '*' (locked) exactly when the
#     write bit $02 is clear ($21 $01 $E1 locked; $E3 $C3 $C2 not). Unnamed types
#     show as $ + two hex digits (e.g. $F1).
# The disks are built by rdedisktool, then type / access bytes are patched with
# python and the columns compared with the table below. MSX / X68000 listings
# must keep their FAT attribute letters.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_listdisp.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
# column <image> <name> -> "TYPE ATTR" from `list` (ATTR may be empty)
column() {
  "$RDEDISKTOOL" list "$1" | awk -v n="$2" '$1 == n { print $3, ($4 == "" ? "-" : $4) }'
}
expect() {   # expect <image> <name> <type> <attr or ->
  local got; got=$(column "$1" "$2")
  [[ "$got" == "$3 $4" ]] || fail "$(basename "$1") $2: got '$got', want '$3 $4'"
  pass
}

echo x >"$WORK/t.txt"

# --- DOS 3.3: catalog type byte (bit 7 = locked)
"$RDEDISKTOOL" create "$WORK/d.do" -f do --fs dos33 >/dev/null
names=(FT FI FA FB FS FR FX FY LB LT)
for n in "${names[@]}"; do "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/t.txt" "$n" --type B --addr 0x2000 >/dev/null 2>&1; done
python3 -I - "$WORK/d.do" <<'EOF'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
want = {'FT': 0x00, 'FI': 0x01, 'FA': 0x02, 'FB': 0x04, 'FS': 0x08, 'FR': 0x10,
        'FX': 0x20, 'FY': 0x40, 'LB': 0x84, 'LT': 0x80}
seen = 0
t, s = d[17 * 4096 + 1], d[17 * 4096 + 2]
while t:
    o0 = (t * 16 + s) * 256
    for e in range(7):
        o = o0 + 0x0B + e * 35
        name = bytes(x & 0x7F for x in d[o + 3:o + 33]).decode().strip()
        if d[o] not in (0, 0xFF) and name in want:
            d[o + 2] = want[name]; seen += 1
    t, s = d[o0 + 1], d[o0 + 2]
assert seen == len(want), seen
open(p, 'wb').write(d)
EOF
expect "$WORK/d.do" FT T -
expect "$WORK/d.do" FI I -
expect "$WORK/d.do" FA A -
expect "$WORK/d.do" FB B -
expect "$WORK/d.do" FS S -
expect "$WORK/d.do" FR R -
expect "$WORK/d.do" FX a -
expect "$WORK/d.do" FY b -
expect "$WORK/d.do" LB B L
expect "$WORK/d.do" LT T L

# --- ProDOS: file type + access byte
"$RDEDISKTOOL" create "$WORK/p.po" -f po --fs prodos -n LISTDISP >/dev/null
"$RDEDISKTOOL" mkdir "$WORK/p.po" SUB >/dev/null
for n in A21 A01 AE1 AE3 AC3 AC2 TBIN TSYS TBAS TF1; do "$RDEDISKTOOL" add "$WORK/p.po" "$WORK/t.txt" "$n" --type TXT >/dev/null; done
python3 -I - "$WORK/p.po" <<'EOF'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
acc = {'A21': 0x21, 'A01': 0x01, 'AE1': 0xE1, 'AE3': 0xE3, 'AC3': 0xC3, 'AC2': 0xC2}
typ = {'TBIN': 0x06, 'TSYS': 0xFF, 'TBAS': 0xFC, 'TF1': 0xF1}
seen = 0; blk = 2
while blk:
    b = blk * 512
    for i in range(13):
        o = b + 4 + i * 0x27
        st, nl = d[o] >> 4, d[o] & 15
        if blk == 2 and i == 0 or st == 0:
            continue
        name = d[o + 1:o + 1 + nl].decode()
        if name in acc: d[o + 0x1E] = acc[name]; seen += 1
        if name in typ: d[o + 0x10] = typ[name]; d[o + 0x1E] = 0xE3; seen += 1
    blk = d[b + 2] | d[b + 3] << 8
assert seen == len(acc) + len(typ), seen
open(p, 'wb').write(d)
EOF
expect "$WORK/p.po" A21 TXT L
expect "$WORK/p.po" A01 TXT L
expect "$WORK/p.po" AE1 TXT L
expect "$WORK/p.po" AE3 TXT -
expect "$WORK/p.po" AC3 TXT -
expect "$WORK/p.po" AC2 TXT -
expect "$WORK/p.po" TBIN BIN -
expect "$WORK/p.po" TSYS SYS -
expect "$WORK/p.po" TBAS BAS -
expect "$WORK/p.po" TF1 '$F1' -
expect "$WORK/p.po" SUB DIR -

# --- other file systems keep FAT attribute letters
"$RDEDISKTOOL" create "$WORK/m.dsk" -f msxdsk --fs msxdos >/dev/null
"$RDEDISKTOOL" add "$WORK/m.dsk" "$WORK/t.txt" A.TXT >/dev/null
python3 -I - "$WORK/m.dsk" <<'EOF'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
root = 7 * 512                      # 720 KB MSX-DOS: boot 1 + 2 FATs x 3 sectors
for o in range(root, root + 7 * 512, 32):
    if d[o:o + 11] == b'A       TXT':
        d[o + 0x0B] = 0x07          # read-only + hidden + system
        break
else:
    sys.exit('entry not found')
open(p, 'wb').write(d)
EOF
expect "$WORK/m.dsk" A.TXT FILE RHS

echo "PASS test_apple_list_display ($CHECKS checks)"
