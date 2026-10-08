#!/usr/bin/env bash
# Boot disk safe-add on Apple II 800K ProDOS images (-f 800po).
#
# ProDOS boot blocks 0-1 are the first 1024 bytes: on an 800K image those are
# 512-byte sectors 0-1. Block 2 is the volume directory and changes on every
# add. Before the fix the ProDOS range was sectors 0-3 (the 256-byte 5.25"
# layout), so on 800K blocks 2-3 were protected too and every add to a boot
# disk failed ("linear sector 2"), e.g. on A2 DeskTop.
#   positive: a normal add succeeds, blocks 0-1 unchanged, block 2 changed
#   negative: an add that writes block 1 (corrupted directory pointer) is
#             rejected for block 1 ("linear sector 1"), nothing saved
#   control:  --force-bootdisk lets that add through and only block 1 changes
# Expected file contents come from tests/tools/a2_prodos_ref.py.
# Optional: real A2 DeskTop 1.5 800K image in A2_REAL_800K_DIR (never committed).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
A2_REAL_800K_DIR="${A2_REAL_800K_DIR:-$TOOL_ROOT/../resource/AppleII/disk35}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_boot800k.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
ref() { python3 -I -B "$REF" "$@"; }
rc_of() {
  set +e
  "$@" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc -lt 128 ]] || { cat "$WORK/out.log" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}
blk() { python3 -I -c 'import sys
b = int(sys.argv[2]); sys.stdout.write(open(sys.argv[1], "rb").read()[b * 512:(b + 1) * 512].hex())' "$1" "$2"; }
# every file of the volume, read by the reference: "<path> <sha256>" per line
snapshot() {
  python3 -I -B - "$REF" "$1" <<'EOF'
import hashlib, sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
data, _ = A.load(sys.argv[2]); v = A.Volume(data)
for e in v.entries(2):
    if e['storage'] in (1, 2, 3):
        print(e['path'], hashlib.sha256(v.read(e)).hexdigest())
EOF
}

echo x >"$WORK/f.txt"

# --- synthetic 800K ProDOS boot disk (SUB is made before it counts as a boot disk)
"$RDEDISKTOOL" create "$WORK/base.po" -f 800po --fs prodos -n BOOT800 >/dev/null
"$RDEDISKTOOL" mkdir "$WORK/base.po" SUB >/dev/null
"$RDEDISKTOOL" add "$WORK/base.po" "$WORK/f.txt" PRODOS --type SYS >/dev/null
"$RDEDISKTOOL" add "$WORK/base.po" "$WORK/f.txt" BASIC.SYSTEM --type SYS >/dev/null
# recognisable boot blocks (the formatter leaves them zero)
python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
d[0:1024] = bytes((i * 7 + 1) & 0xFF for i in range(1024))
open(p, "wb").write(d)' "$WORK/base.po"
"$RDEDISKTOOL" info "$WORK/base.po" -v | grep -q "BootDisk: yes" || fail "synthetic disk is a boot disk"; pass
ref check "$WORK/base.po" >/dev/null || fail "reference: base disk inconsistent"

# --- positive
cp "$WORK/base.po" "$WORK/pos.po"
b0=$(blk "$WORK/pos.po" 0); b1=$(blk "$WORK/pos.po" 1); b2=$(blk "$WORK/pos.po" 2)
[[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/pos.po" "$WORK/f.txt" NOTE.TXT) == 0 ]] \
  || { cat "$WORK/out.log" >&2; fail "normal add on 800K boot disk rejected"; }
grep -q "Bootdisk safe-add verification enabled (profile=prodos)" "$WORK/out.log" || fail "safe-add did not run"; pass
[[ $(blk "$WORK/pos.po" 0) == "$b0" && $(blk "$WORK/pos.po" 1) == "$b1" ]] || fail "boot blocks changed"; pass
[[ $(blk "$WORK/pos.po" 2) != "$b2" ]] || fail "block 2 unchanged (the add did not touch the directory?)"; pass
ref check "$WORK/pos.po" >/dev/null || { ref check "$WORK/pos.po"; fail "reference: inconsistent after add"; }
ref cat "$WORK/pos.po" NOTE.TXT "$WORK/note.ref"
cmp -s "$WORK/f.txt" "$WORK/note.ref" || fail "added file"; pass
# warn mode takes the same safe-add path
cp "$WORK/base.po" "$WORK/warn.po"
[[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode warn add "$WORK/warn.po" "$WORK/f.txt" NOTE.TXT) == 0 ]] \
  || { cat "$WORK/out.log" >&2; fail "warn-mode add on 800K boot disk rejected"; }
pass

# --- negative: SUB's key block pointer -> block 1 (block 1 = copy of SUB's key block)
cp "$WORK/base.po" "$WORK/neg.po"
python3 -I - "$WORK/neg.po" <<'EOF'
import struct, sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
for i in range(1, 13):
    o = 2 * 512 + 4 + i * 0x27
    e = d[o:o + 0x27]
    if e[0] >> 4 == 0xD and e[1:1 + (e[0] & 15)] == b'SUB':
        key = struct.unpack('<H', e[0x11:0x13])[0]
        d[512:1024] = d[key * 512:(key + 1) * 512]
        d[o + 0x11:o + 0x13] = struct.pack('<H', 1)
        break
else:
    sys.exit('SUB not found')
open(p, 'wb').write(d)
EOF
cp "$WORK/neg.po" "$WORK/negtry.po"
[[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/negtry.po" "$WORK/f.txt" SUB/NEW.TXT) != 0 ]] \
  || fail "add writing block 1 accepted"
grep -q "protected sector changed by add operation (linear sector 1)" "$WORK/out.log" \
  || { cat "$WORK/out.log" >&2; fail "rejected for the wrong sector"; }
pass
cmp -s "$WORK/neg.po" "$WORK/negtry.po" || fail "rejected add changed the image"; pass

# --- control: forced, the same add changes block 1 and not block 0
cp "$WORK/neg.po" "$WORK/negforce.po"
n0=$(blk "$WORK/negforce.po" 0); n1=$(blk "$WORK/negforce.po" 1)
[[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/negforce.po" "$WORK/f.txt" SUB/NEW.TXT) == 0 ]] \
  || fail "forced add"
[[ $(blk "$WORK/negforce.po" 0) == "$n0" && $(blk "$WORK/negforce.po" 1) != "$n1" ]] \
  || fail "forced add should change block 1 only"
pass

# --- optional: real A2 DeskTop 1.5 800K boot volume
real="$A2_REAL_800K_DIR/A2DeskTop-1.5-en_800k.po"
if [[ -f "$real" ]]; then
  cp "$real" "$WORK/real.po"
  "$RDEDISKTOOL" info "$WORK/real.po" -v | grep -q "BootDisk: yes" || fail "A2 DeskTop is a boot disk"
  snapshot "$WORK/real.po" >"$WORK/real.before"
  r0=$(blk "$WORK/real.po" 0); r1=$(blk "$WORK/real.po" 1)
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/real.po" "$WORK/f.txt" NOTE.TXT) == 0 ]] \
    || { cat "$WORK/out.log" >&2; fail "add on A2 DeskTop rejected"; }
  pass
  [[ $(blk "$WORK/real.po" 0) == "$r0" && $(blk "$WORK/real.po" 1) == "$r1" ]] || fail "A2 DeskTop boot blocks changed"
  ref check "$WORK/real.po" >/dev/null || { ref check "$WORK/real.po"; fail "reference: A2 DeskTop inconsistent after add"; }
  snapshot "$WORK/real.po" | grep -v '^/NOTE.TXT ' >"$WORK/real.after"
  cmp -s "$WORK/real.before" "$WORK/real.after" || fail "A2 DeskTop files changed by the add"
  [[ $(wc -l <"$WORK/real.before") -gt 100 ]] || fail "too few A2 DeskTop files compared"
  pass
else
  echo "  (skip: $real missing; not judged)"
fi

echo "PASS test_bootdisk_guard_prodos_800k ($CHECKS checks)"
