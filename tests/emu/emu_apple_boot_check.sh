#!/usr/bin/env bash
# Manual check (not part of the tests/test_*.sh run): WOZ / NIB / NB2 images
# written by rdedisktool boot on an Apple //e Enhanced in sa2 and behave like
# the DSK they came from.
#   1. DOS 3.3 (workspace diskwork/bootdisk/AppleII/dos33.dsk): boot + CATALOG
#      screen of each format == screen of the DSK
#   2. DOS writes: "SAVE T" on each format, image converted back to .do == the
#      DSK after the same SAVE
#   3. ProDOS (ProDOS_2_4_3.po): boot screen of each format == screen of the PO
#   4. VTOC bitmap: a file added by rdedisktool on a partially used track
#      survives a real DOS BSAVE that allocates on the same track (D2)
# Uses a2run.sh (isolated sa2; never touches the user's display).
# Exit 0 = all match, 1 = mismatch, 3 = not judged (sa2/Xvfb/images missing).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL_ROOT="$(cd "$HERE/../.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
DOS33="$PROJECT_ROOT/diskwork/bootdisk/AppleII/dos33.dsk"
PRODOS="$PROJECT_ROOT/diskwork/bootdisk/AppleII/ProDOS_2_4_3.po"

skip() { echo "SKIP emu_apple_boot_check: $* (not judged)"; exit 3; }
[[ -x "$RDEDISKTOOL" ]] || skip "missing $RDEDISKTOOL"
[[ -f "$DOS33" ]] || skip "missing $DOS33"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_emu_check.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT
export A2RUN_WORK="$WORK"

FAILS=0 CHECKS=0
judge() { CHECKS=$((CHECKS + 1)); if [[ $1 == 0 ]]; then echo "  ok   $2"; else echo "  FAIL $2"; FAILS=$((FAILS + 1)); fi; }
run() {   # run <image> <boot-seconds> [steps] -> prints run dir; rc 3 = environment
  local out rc
  out=$("$HERE/a2run.sh" "$@" 2>"$WORK/a2run.err"); rc=$?
  if [[ $rc == 3 ]]; then cat "$WORK/a2run.err" >&2; skip "a2run could not start an isolated sa2"; fi
  [[ $rc == 0 ]] || { cat "$WORK/a2run.err" >&2; echo ""; return 1; }
  echo "$out"
}

for f in woz nib nb2; do
  "$RDEDISKTOOL" convert "$DOS33" "$WORK/dos.$f" -f "$f" >/dev/null || { echo "convert $f failed"; exit 1; }
done

echo "1. DOS 3.3 boot + CATALOG"
ref=$(run "$DOS33" 15 type:CATALOG) || exit 1
for f in woz nib nb2; do
  r=$(run "$WORK/dos.$f" 15 type:CATALOG)
  python3 -I "$HERE/cmpscreen.py" "$ref/screen.txt" "$r/screen.txt" 20 >"$WORK/cmp.txt"; judge $? "$f CATALOG screen == DSK"
done

echo "2. DOS writes (SAVE)"
export WAIT=8
steps=("type:10 PRINT 12345" "type:SAVE T" "type:CATALOG" "wait:4")
refw=$(run "$DOS33" 15 "${steps[@]}") || exit 1
cmp -s "$DOS33" "$refw/d1.dsk" && { echo "  FAIL DSK was not written (SAVE did not happen)"; exit 1; }
for f in woz nib nb2; do
  r=$(run "$WORK/dos.$f" 15 "${steps[@]}")
  "$RDEDISKTOOL" convert "$r/d1.$f" "$WORK/back_$f.do" -f do >/dev/null
  cmp -s "$refw/d1.dsk" "$WORK/back_$f.do"; judge $? "$f after SAVE == DSK after SAVE"
done
unset WAIT

if [[ -f "$PRODOS" ]]; then
  echo "3. ProDOS boot"
  pref=$(run "$PRODOS" 20) || exit 1
  for f in woz nib nb2; do
    "$RDEDISKTOOL" convert "$PRODOS" "$WORK/pro.$f" -f "$f" >/dev/null
    r=$(run "$WORK/pro.$f" 20)
    python3 -I "$HERE/cmpscreen.py" "$pref/screen.txt" "$r/screen.txt" 3 >"$WORK/cmp.txt"; judge $? "$f ProDOS boot screen == PO"
  done
else
  echo "3. ProDOS boot: SKIP ($PRODOS missing; not judged)"
fi

echo "4. Real DOS allocates next to a file added by rdedisktool"
"$RDEDISKTOOL" create "$WORK/bm.do" -f do --fs dos33 >/dev/null
head -c 2500 /dev/urandom >"$WORK/bm.bin"
"$RDEDISKTOOL" add "$WORK/bm.do" "$WORK/bm.bin" NEW --type B --addr 0x2000 >/dev/null
# DOS searches from the track after the last allocated one: make that track 18
python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); o = 17 * 16 * 256
d[o + 0x30], d[o + 0x31] = 19, 0xFF
open(p, "wb").write(d)' "$WORK/bm.do"
export WAIT=8
r=$(D2SRC="$WORK/bm.do" run "$DOS33" 15 'type:BSAVE X,A$2000,L$400,D2' "wait:4")
unset WAIT
if cmp -s "$WORK/bm.do" "$r/d2.do"; then
  judge 1 "D2 was not written (BSAVE did not happen)"
else
  python3 -I "$TOOL_ROOT/tests/tools/a2_nibref.py" dos33-check "$WORK/bm.do" "$r/d2.do" X >"$WORK/cmp.txt"
  judge $? "real DOS BSAVE on track 18 leaves NEW intact, bitmap consistent"
fi

if [[ $FAILS == 0 ]]; then
  echo "PASS emu_apple_boot_check ($CHECKS checks)"
  exit 0
fi
echo "FAIL emu_apple_boot_check ($FAILS of $CHECKS)"
exit 1
