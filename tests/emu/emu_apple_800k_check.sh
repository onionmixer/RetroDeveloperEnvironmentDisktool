#!/usr/bin/env bash
# Manual check (not part of the tests/test_*.sh run): Apple II 800K ProDOS images
# written by rdedisktool, used by real ProDOS on an Apple //c Plus (ROM 05) in
# sa2 - its built-in 3.5" drive (slot 5, drive 1).
#   1. A2 DeskTop 1.5 (800K, optional, resource/AppleII/disk35) with a file
#      added by rdedisktool boots from the 3.5" drive; A2 DeskTop writes its
#      settings (creates /A2.DESKTOP/LOCAL) on it; afterwards rdedisktool and
#      the independent reader (tests/tools/a2_prodos_ref.py) read the same
#      volume, the added file is intact. Screenshots: desk.png, open.png (the
#      volume window, showing the added file) in the run directory.
#   2. ProDOS 2.4.3 booted from 5.25" (diskwork/bootdisk/AppleII/ProDOS_2_4_3.po),
#      a volume made by `create -f 800po` / `-f 800mg` in the 3.5" drive:
#      Bitsy Bye lists it (S5,D1:/BIGDISK with the file rdedisktool added).
#   3. BASIC.SYSTEM: SAVE a program to it, CAT shows both files and the block
#      counts (expected: python); the image ProDOS wrote reads the same with
#      rdedisktool and the independent reader; the 2MG header is unchanged.
# Uses a2run.sh (isolated sa2, A2RUN_MODEL=iicp). Exit 0 = all match,
# 1 = mismatch, 3 = not judged (sa2/Xvfb/ROM/boot disk missing).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL_ROOT="$(cd "$HERE/../.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$TOOL_ROOT/tests/tools/a2_prodos_ref.py"
PRODOS="$PROJECT_ROOT/diskwork/bootdisk/AppleII/ProDOS_2_4_3.po"
A2D="${A2D:-$PROJECT_ROOT/resource/AppleII/disk35/A2DeskTop-1.5-en_800k.po}"
export IIC_ROM="${IIC_ROM:-$PROJECT_ROOT/resource/AppleII/rom/iicp_rom05.bin}"

skip() { echo "SKIP emu_apple_800k_check: $* (not judged)"; exit 3; }
[[ -x "$RDEDISKTOOL" ]] || skip "missing $RDEDISKTOOL"
[[ -f "$PRODOS" ]] || skip "missing $PRODOS"
[[ -f "$IIC_ROM" ]] || skip "missing //c Plus ROM $IIC_ROM"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_emu_800k.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT
export A2RUN_WORK="$WORK" A2RUN_MODEL=iicp

FAILS=0 CHECKS=0
judge() { CHECKS=$((CHECKS + 1)); if [[ $1 == 0 ]]; then echo "  ok   $2"; else echo "  FAIL $2"; FAILS=$((FAILS + 1)); fi; }
run() {   # run <5.25 image> <boot-seconds> [steps] (A2RUN_DISK35 set by the caller) -> run dir
  local out rc
  out=$(WAIT=8 "$HERE/a2run.sh" "$@" 2>"$WORK/a2run.err"); rc=$?
  if [[ $rc == 3 ]]; then cat "$WORK/a2run.err" >&2; skip "a2run could not start an isolated sa2"; fi
  [[ $rc == 0 ]] || { cat "$WORK/a2run.err" >&2; echo ""; return 1; }
  echo "$out"
}
# same_read <image> <path> <expected file>: rdedisktool and the reference read the same bytes
same_read() {
  rm -f "$WORK/a.out" "$WORK/b.out"
  "$RDEDISKTOOL" extract "$1" "$2" "$WORK/a.out" >/dev/null 2>&1 &&
    python3 -I "$REF" cat "$1" "/$2" "$WORK/b.out" && cmp -s "$WORK/a.out" "$WORK/b.out" &&
    { [[ -z "${3:-}" ]] || cmp -s "$WORK/a.out" "$3"; }
}
printf 'ADDED BY RDEDISKTOOL\r' >"$WORK/note.txt"

echo "1. A2 DeskTop (800K) with a file added by rdedisktool boots from the //c Plus 3.5\" drive"
if [[ -f "$A2D" ]]; then
  cp "$A2D" "$WORK/a2d.po"
  "$RDEDISKTOOL" --bootdisk-mode warn add "$WORK/a2d.po" "$WORK/note.txt" RDE.NOTE --type TXT >/dev/null 2>&1
  judge $? "rdedisktool add on an A2 DeskTop copy"
  r=$(A2RUN_DISK35="$WORK/a2d.po" run "$PRODOS" 60 'shot:desk' 'key:Tab' 'key:alt+o' 'wait:8' 'shot:open')
  if [[ -z "$r" ]]; then
    judge 1 "sa2 run"
  else
    # A2 DeskTop creates LOCAL (its settings) on the volume it booted from
    "$RDEDISKTOOL" list "$r/d35.po" 2>/dev/null | grep -q "^LOCAL " &&
      ! "$RDEDISKTOOL" list "$WORK/a2d.po" 2>/dev/null | grep -q "^LOCAL "
    judge $? "A2 DeskTop booted and wrote its settings (LOCAL created) on the 3.5\" volume"
    same_read "$r/d35.po" RDE.NOTE "$WORK/note.txt" && python3 -I "$REF" check "$r/d35.po" >/dev/null &&
      "$RDEDISKTOOL" validate "$r/d35.po" 2>/dev/null | grep -q "Status: Valid"
    judge $? "after A2 DeskTop: RDE.NOTE intact, rdedisktool = reference reader, both checks clean"
    echo "       screenshots: $r/desk.png $r/open.png"
  fi
else
  echo "  (skip 1: $A2D missing; not judged)"
fi

echo "2./3. ProDOS 2.4.3 (5.25\") with a volume made by rdedisktool in the 3.5\" drive"
USED=$(python3 -I -c 'print(2 + 4 + 1 + 1 + 1)')    # boot, volume dir, bitmap, HELLO.TXT, S7PROG
FREE=$(python3 -I -c 'print(1600 - (2 + 4 + 1 + 1 + 1))')
for fmt in 800po 800mg; do
  ext=po; [[ $fmt == 800mg ]] && ext=2mg
  img="$WORK/big.$ext"
  "$RDEDISKTOOL" create "$img" -f "$fmt" --fs prodos -n BIGDISK >/dev/null &&
    "$RDEDISKTOOL" add "$img" "$WORK/note.txt" HELLO.TXT --type TXT >/dev/null
  judge $? "$fmt: create + add"
  r=$(A2RUN_DISK35="$img" run "$PRODOS" 40 'key:Tab')
  [[ -n "$r" ]] && grep -q "S5,D1:/BIGDISK" "$r/screen.txt" && grep -q "T HELLO.TXT" "$r/screen.txt"
  judge $? "$fmt: ProDOS (Bitsy Bye) lists S5,D1:/BIGDISK and HELLO.TXT"
  r=$(A2RUN_DISK35="$img" run "$PRODOS" 40 'key:Down Down Down' 'key:Return' 'wait:6' \
      'type:10 PRINT "S7 SAVED BY PRODOS"' 'type:SAVE /BIGDISK/S7PROG' 'wait:4' 'type:CAT /BIGDISK')
  if [[ -z "$r" ]]; then
    judge 1 "$fmt: sa2 run"
    continue
  fi
  grep -q "HELLO.TXT  *TXT  *1 " "$r/screen.txt" && grep -q "S7PROG  *BAS  *1 " "$r/screen.txt" &&
    grep -Eq "BLOCKS FREE: *$FREE +BLOCKS USED: *$USED\b" "$r/screen.txt"
  judge $? "$fmt: BASIC SAVE, CAT shows both files, $FREE free / $USED used"
  same_read "$r/d35.$ext" S7PROG && same_read "$r/d35.$ext" HELLO.TXT "$WORK/note.txt" &&
    python3 -I "$REF" check "$r/d35.$ext" >/dev/null &&
    "$RDEDISKTOOL" validate "$r/d35.$ext" 2>/dev/null | grep -q "Status: Valid"
  judge $? "$fmt: image ProDOS wrote: rdedisktool = reference reader, both checks clean"
  if [[ $fmt == 800mg ]]; then
    cmp -s <(head -c 64 "$img") <(head -c 64 "$r/d35.2mg")
    judge $? "800mg: 2MG header unchanged after ProDOS wrote"
  fi
done

if [[ $FAILS == 0 ]]; then
  echo "PASS emu_apple_800k_check ($CHECKS checks)"
  exit 0
fi
echo "FAIL emu_apple_800k_check ($FAILS of $CHECKS)"
exit 1
