#!/usr/bin/env bash
# `repair`: correct, in one go, what older rdedisktool versions wrote wrong
# (before, only the next add / delete / rename corrected it on the way).
#   DOS 3.3 / 3.2: sectors the VTOC, catalog or a file uses that the bitmap
#     shows as free (older versions reversed the bit order of a bitmap byte)
#     -> marked used; nothing else changes. Sectors marked used that nothing
#     refers to are only reported (they may hold a DOS image or hidden data).
#   Human68k: BPB total sectors larger than the image (older create wrote
#     2,464 on a 1,232-sector disk) -> the image size; only those bytes change.
#   A damaged catalog / T-S list (loop) is refused, the image unchanged.
#   --dry-run reports and writes nothing; a disk with nothing to repair is
#   left alone (also a boot disk in strict mode).
# Expected values come from independent python readers (tests/tools/a2_nibref.py
# for DOS 3.3, the DOS 3.2 layout of test_apple_d13_write.sh, the BPB offsets
# of a real Human68k disk), never from rdedisktool.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_nibref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_repair.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rc_of() { set +e; "$@" >"$WORK/out.log" 2>&1; local rc=$?; set -e; echo "$rc"; }
py() { python3 -I -B "$WORK/r.py" "$SCRIPT_DIR/tools" "$@"; }

cat >"$WORK/r.py" <<'EOF'
# r.py <tools> <cmd> ...
import struct, sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A
cmd, a = sys.argv[2], sys.argv[3:]
rd = lambda p: open(p, 'rb').read()

def d32_free(d, t, s):
    v = d[17 * 13 * 256:17 * 13 * 256 + 256]
    return (((v[0x38 + 4 * t] << 8) | v[0x39 + 4 * t]) >> (s + 3)) & 1

def d32_in_use(d):
    """{(t, s)} the VTOC, catalog chain and every live file use (DOS 3.2 layout)."""
    sec = lambda t, s: d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
    used, v = {(17, 0)}, sec(17, 0)
    cur, seen = (v[1], v[2]), set()
    while cur != (0, 0) and cur not in seen:
        seen.add(cur); used.add(cur); c = sec(*cur)
        for i in range(7):
            e = c[0x0B + 35 * i:0x0B + 35 * (i + 1)]
            if e[0] in (0, 0xFF):
                continue
            ts, ls = (e[0], e[1]), set()
            while ts != (0, 0) and ts not in ls:
                ls.add(ts); used.add(ts); l = sec(*ts)
                used |= {(l[0x0C + 2 * k], l[0x0D + 2 * k]) for k in range(122)} - {(0, 0)}
                ts = (l[1], l[2])
        cur = (c[1], c[2])
    return used

if cmd == 'dos33-expect':              # before.do: <fixes> <unreferenced>
    d = rd(a[0]); used = set(A.dos33_in_use(d))
    fixes = sum(1 for k in used if A.bm_free(d, *k))
    unref = sum(1 for t in range(35) for s in range(16)
                if t > 2 and t != 17 and not A.bm_free(d, t, s) and (t, s) not in used)
    print(fixes, unref)
elif cmd == 'dos33-after':             # before after: only referenced-but-free bits cleared, nothing else
    b, d = rd(a[0]), rd(a[1]); used = set(A.dos33_in_use(b))
    for t in range(35):
        for s in range(16):
            want = A.bm_free(b, t, s) and (t, s) not in used
            assert A.bm_free(d, t, s) == want, 'T%d S%d' % (t, s)
    o = 17 * 16 * 256 + 0x38
    diff = [i for i in range(len(b)) if b[i] != d[i]]
    assert diff and all(o <= i < o + 4 * 35 for i in diff), 'bytes outside the bitmap changed'
elif cmd == 'dos33-free-used':          # img: mark the sectors of the first file free (as an old tool left them)
    d = bytearray(rd(a[0])); used = A.dos33_in_use(bytes(d))
    victim = sorted(k for k, o in used.items() if 'CATALOG' not in o and k != (17, 0))[:3]
    for t, s in victim:
        b, k = A.bm_pos(s, False)
        d[17 * 16 * 256 + 0x38 + 4 * t + b] |= 1 << k
    open(a[0], 'wb').write(d)
    print(len(victim))
elif cmd == 'loop':                     # img: catalog sector 17/14 links back to 17/15
    d = bytearray(rd(a[0])); o = (17 * 16 + 14) * 256
    d[o + 1], d[o + 2] = 17, 15
    open(a[0], 'wb').write(d)
elif cmd == 'bad-ts':                   # img: first catalog entry's T/S list on track 40 (off the disk)
    d = bytearray(rd(a[0])); o = (17 * 16 + 15) * 256 + 0x0B
    d[o] = 40
    open(a[0], 'wb').write(d)
elif cmd == 'd32-free-used':            # img: the file's sectors shown free (bit s+3)
    d = bytearray(rd(a[0]))
    used = d32_in_use(bytes(d)) - {(17, s) for s in range(13)}
    for t, s in used:
        o = 17 * 13 * 256 + 0x38 + 4 * t
        w = ((d[o] << 8) | d[o + 1]) | (1 << (s + 3))
        d[o], d[o + 1] = w >> 8, w & 0xFF
    open(a[0], 'wb').write(d)
    print(len(used))
elif cmd == 'd32-after':                # before after
    b, d = rd(a[0]), rd(a[1]); used = d32_in_use(b)
    for t in range(35):
        for s in range(13):
            want = d32_free(b, t, s) and (t, s) not in used
            assert d32_free(d, t, s) == want, 'T%d S%d' % (t, s)
    o = 17 * 13 * 256 + 0x38
    diff = [i for i in range(len(b)) if b[i] != d[i]]
    assert diff and all(o <= i < o + 4 * 35 for i in diff), 'bytes outside the bitmap changed'
elif cmd == 'bpb-after':                # before after total
    b, d = rd(a[0]), rd(a[1])
    assert struct.unpack_from('<H', d, 0x13)[0] == int(a[2]), 'BPB total'
    diff = [i for i in range(len(b)) if b[i] != d[i]]
    assert diff and all(i in (0x13, 0x14) for i in diff), 'bytes other than BPB 13-14 changed: %s' % diff[:5]
EOF

# --- DOS 3.3 disk with the bitmap of an older rdedisktool (bit order reversed)
mkdir -p "$WORK/mir"
python3 -I -B "$REF" make-dos33-partial "$WORK/mir.do" 5 mirrored "$WORK/mir"
read -r FIX UNREF < <(py dos33-expect "$WORK/mir.do")
[[ $FIX -gt 0 && $UNREF -gt 0 ]] || fail "fixture has nothing to repair ($FIX/$UNREF)"
cp "$WORK/mir.do" "$WORK/mir0.do"
[[ $(rc_of "$RDEDISKTOOL" repair --dry-run "$WORK/mir.do") == 0 ]] || { cat "$WORK/out.log"; fail "dry-run"; }
grep -q "^Would repair: $FIX sector(s) in use marked free" "$WORK/out.log" || { cat "$WORK/out.log"; fail "dry-run count != $FIX"; }
grep -q "^Note: $UNREF sector(s) outside tracks 0-2 and 17 are marked used" "$WORK/out.log" || { cat "$WORK/out.log"; fail "note count != $UNREF"; }
cmp -s "$WORK/mir0.do" "$WORK/mir.do" || fail "dry-run changed the image"; pass
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/mir.do") == 0 ]] || { cat "$WORK/out.log"; fail "repair"; }
grep -q "^Repaired: $FIX sector(s)" "$WORK/out.log" || { cat "$WORK/out.log"; fail "repair message"; }
py dos33-after "$WORK/mir0.do" "$WORK/mir.do" || fail "DOS 3.3 bitmap after repair"
python3 -I -B "$REF" dos33-check "$WORK/mir.do" "$WORK/mir.do" >/dev/null || fail "independent check: in-use sectors still free"
for f in ALPHA BRAVO CHARLIE DELTA; do
  rm -f "$WORK/x"; "$RDEDISKTOOL" extract "$WORK/mir.do" "$f" "$WORK/x" >/dev/null && cmp -s "$WORK/x" "$WORK/mir/$f" || fail "$f after repair"
done; pass
cp "$WORK/mir.do" "$WORK/mir1.do"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/mir.do") == 0 ]] && grep -q "^Nothing to repair" "$WORK/out.log" || fail "second repair"
cmp -s "$WORK/mir1.do" "$WORK/mir.do" || fail "second repair changed the image"; pass

# a correct disk: nothing to repair, untouched
mkdir -p "$WORK/std"
python3 -I -B "$REF" make-dos33-partial "$WORK/std.do" 5 std "$WORK/std"
cp "$WORK/std.do" "$WORK/std0.do"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/std.do") == 0 ]] && grep -q "^Nothing to repair" "$WORK/out.log" || { cat "$WORK/out.log"; fail "std: nothing to repair"; }
cmp -s "$WORK/std0.do" "$WORK/std.do" || fail "std: image changed"; pass

# damaged catalog: a loop is refused when the disk is opened; a T/S list
# off the disk is found by the structure check before repairing. Both: exit
# 1, image unchanged (a partial scan is never saved)
cp "$WORK/mir0.do" "$WORK/loop.do"; py loop "$WORK/loop.do"; cp "$WORK/loop.do" "$WORK/loop0.do"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/loop.do") == 1 ]] || { cat "$WORK/out.log"; fail "loop: not refused"; }
grep -q "loops back" "$WORK/out.log" || { cat "$WORK/out.log"; fail "loop: reason"; }
cmp -s "$WORK/loop0.do" "$WORK/loop.do" || fail "loop: image changed"; pass
cp "$WORK/mir0.do" "$WORK/badts.do"; py bad-ts "$WORK/badts.do"; cp "$WORK/badts.do" "$WORK/badts0.do"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/badts.do") == 1 ]] || { cat "$WORK/out.log"; fail "bad T/S: not refused"; }
grep -q "nothing was repaired" "$WORK/out.log" || { cat "$WORK/out.log"; fail "bad T/S: reason"; }
cmp -s "$WORK/badts0.do" "$WORK/badts.do" || fail "bad T/S: image changed"; pass

# --- DOS 3.2 (.d13): a file whose sectors the bitmap shows free
python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(42).randrange(256) for _ in range(3000)))' "$WORK/f.bin"
"$RDEDISKTOOL" create "$WORK/v.d13" -f d13 --fs dos32 >/dev/null
"$RDEDISKTOOL" add "$WORK/v.d13" "$WORK/f.bin" FILE --type B --addr 0x2000 >/dev/null
n=$(py d32-free-used "$WORK/v.d13")
cp "$WORK/v.d13" "$WORK/v0.d13"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/v.d13") == 0 ]] && grep -q "^Repaired: $n sector(s)" "$WORK/out.log" || { cat "$WORK/out.log"; fail "d13: repair ($n)"; }
py d32-after "$WORK/v0.d13" "$WORK/v.d13" || fail "d13: bitmap after repair"
"$RDEDISKTOOL" extract "$WORK/v.d13" FILE "$WORK/x" >/dev/null && cmp -s "$WORK/x" "$WORK/f.bin" || fail "d13: file"; pass

# --- Human68k: BPB total 2,464 on a 1,232-sector disk
"$RDEDISKTOOL" create "$WORK/old.xdf" -f xdf --fs human68k -n OLD >/dev/null
"$RDEDISKTOOL" add "$WORK/old.xdf" "$WORK/f.bin" F.BIN >/dev/null
python3 -I -c 'import sys, struct
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); struct.pack_into("<H", d, 0x13, 2464)
open(p, "wb").write(d)' "$WORK/old.xdf"
SECTORS=$(python3 -I -c 'print(77 * 2 * 8)')
cp "$WORK/old.xdf" "$WORK/old0.xdf"
[[ $(rc_of "$RDEDISKTOOL" repair --dry-run "$WORK/old.xdf") == 0 ]] && grep -q "^Would repair: BPB total sectors 2464 (more than the image) -> $SECTORS" "$WORK/out.log" || { cat "$WORK/out.log"; fail "xdf dry-run"; }
cmp -s "$WORK/old0.xdf" "$WORK/old.xdf" || fail "xdf dry-run changed the image"; pass
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/old.xdf") == 0 ]] || { cat "$WORK/out.log"; fail "xdf repair"; }
py bpb-after "$WORK/old0.xdf" "$WORK/old.xdf" "$SECTORS" || fail "xdf: bytes after repair"
[[ $(rc_of "$RDEDISKTOOL" validate "$WORK/old.xdf") == 0 ]] || { cat "$WORK/out.log"; fail "xdf: validate after repair"; }
"$RDEDISKTOOL" extract "$WORK/old.xdf" F.BIN "$WORK/x" >/dev/null && cmp -s "$WORK/x" "$WORK/f.bin" || fail "xdf: file"; pass

# --- other file systems: nothing to repair
"$RDEDISKTOOL" create "$WORK/p.po" -f po --fs prodos -n P >/dev/null
cp "$WORK/p.po" "$WORK/p0.po"
[[ $(rc_of "$RDEDISKTOOL" repair "$WORK/p.po") == 0 ]] && grep -q "^Nothing to repair" "$WORK/out.log" || fail "prodos: nothing to repair"
cmp -s "$WORK/p0.po" "$WORK/p.po" || fail "prodos: image changed"; pass

# --- boot disk (workspace DOS 3.3 master): strict refuses a needed repair,
#     warn repairs with a warning; nothing to repair = no refusal
BOOT="$TOOL_ROOT/../diskwork/bootdisk/AppleII/dos33.dsk"
if [[ -f "$BOOT" ]]; then
  cp "$BOOT" "$WORK/boot.dsk"
  [[ $(rc_of "$RDEDISKTOOL" repair "$WORK/boot.dsk") == 0 ]] && grep -q "^Nothing to repair" "$WORK/out.log" || { cat "$WORK/out.log"; fail "boot: clean disk"; }
  n=$(py dos33-free-used "$WORK/boot.dsk")
  cp "$WORK/boot.dsk" "$WORK/boot0.dsk"
  [[ $(rc_of "$RDEDISKTOOL" repair "$WORK/boot.dsk") == 1 ]] && grep -q "Boot disk protection" "$WORK/out.log" || { cat "$WORK/out.log"; fail "boot: strict"; }
  cmp -s "$WORK/boot0.dsk" "$WORK/boot.dsk" || fail "boot: strict changed the image"
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode warn repair "$WORK/boot.dsk") == 0 ]] && grep -q "^Repaired: $n sector(s)" "$WORK/out.log" || { cat "$WORK/out.log"; fail "boot: warn"; }
  py dos33-after "$WORK/boot0.dsk" "$WORK/boot.dsk" || fail "boot: bitmap after repair"
  pass
else
  echo "  (skip: $BOOT missing; not judged)"
fi

echo "PASS test_repair_older_writes ($CHECKS checks)"
