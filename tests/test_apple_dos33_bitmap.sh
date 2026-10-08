#!/usr/bin/env bash
# DOS 3.3 VTOC free-sector bitmap: standard bit order and partially used tracks.
#
# Standard layout (47 disks written by real DOS 3.3 agree): track entry byte 0
# bit k = sector 8+k, byte 1 bit k = sector k. Full / empty tracks look the
# same in any bit order, so every disk here has partially used tracks.
# Expected values come from tests/tools/a2_nibref.py (own disk builder and
# catalog walker), never from rdedisktool:
#   - std disk: validate clean, add only takes free sectors and changes only
#     their bits, files intact, delete restores the bitmap byte for byte
#   - disk with the old reversed in-byte order (written by older rdedisktool
#     versions): validate reports it; add marks the in-use sectors used
#     (warning with the exact count) and overwrites nothing
# Optional: the workspace DOS 3.3 master (diskwork/bootdisk/AppleII/dos33.dsk),
# with allocation starting on its partially used track 7.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_nibref.py"
DOS33_MASTER="${DOS33_MASTER:-$TOOL_ROOT/../diskwork/bootdisk/AppleII/dos33.dsk}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_bitmap.XXXXXX")"
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
check() {   # check <before> <after> [new-file]
  python3 -I "$REF" dos33-check "$@" >"$WORK/check.log" || { cat "$WORK/check.log" >&2; return 1; }
}

head -c 2500 /dev/urandom >"$WORK/new.bin"     # 10 data sectors + 1 T/S list
mkdir "$WORK/std" "$WORK/mir"
python3 -I "$REF" make-dos33-partial "$WORK/std.do" 5 std "$WORK/std"
python3 -I "$REF" make-dos33-partial "$WORK/mir.do" 5 mirrored "$WORK/mir"
[[ $(cat "$WORK/std/shown_free") == 0 ]] || fail "reference: standard disk shows in-use sectors free"
MIR_FREE=$(cat "$WORK/mir/shown_free")
[[ $MIR_FREE -gt 0 ]] || fail "reference: reversed disk must show in-use sectors free"

# --- standard disk
"$RDEDISKTOOL" validate "$WORK/std.do" >"$WORK/val.txt" || { cat "$WORK/val.txt" >&2; fail "std: validate"; }
grep -q "0 error(s), 0 warning(s)" "$WORK/val.txt" || { cat "$WORK/val.txt" >&2; fail "std: validate found problems"; }
pass
for f in ALPHA BRAVO CHARLIE DELTA; do
  "$RDEDISKTOOL" extract "$WORK/std.do" "$f" "$WORK/x_$f" >/dev/null
  cmp -s "$WORK/std/$f" "$WORK/x_$f" || fail "std: extract $f"; pass
done

for mode in plain bootdisk; do
  cp "$WORK/std.do" "$WORK/a.do"
  if [[ $mode == plain ]]; then
    rc=$(rc_of "$RDEDISKTOOL" add "$WORK/a.do" "$WORK/new.bin" NEW --type B --addr 0x2000)
  else
    rc=$(rc_of "$RDEDISKTOOL" --bootdisk-profile dos33 --bootdisk-mode strict add "$WORK/a.do" "$WORK/new.bin" NEW --type B --addr 0x2000)
  fi
  [[ $rc == 0 ]] || { cat "$WORK/out.log" >&2; fail "std $mode: add"; }
  ! grep -q "marked them used" "$WORK/out.log" || fail "std $mode: repair warning on a standard disk"
  check "$WORK/std.do" "$WORK/a.do" NEW || fail "std $mode: add"; pass
  for f in ALPHA BRAVO CHARLIE DELTA; do
    "$RDEDISKTOOL" extract "$WORK/a.do" "$f" "$WORK/x_$f" >/dev/null
    cmp -s "$WORK/std/$f" "$WORK/x_$f" || fail "std $mode: $f changed by add"; pass
  done
  "$RDEDISKTOOL" extract "$WORK/a.do" NEW "$WORK/x_new" >/dev/null
  cmp -s "$WORK/new.bin" "$WORK/x_new" || fail "std $mode: extract NEW"; pass
  "$RDEDISKTOOL" validate "$WORK/a.do" | grep -q "0 error(s), 0 warning(s)" || fail "std $mode: validate after add"; pass
done

# the new file must reach a partially used track (otherwise bit order is not tested)
python3 -I - "$REF" "$WORK/std.do" "$WORK/a.do" <<'EOF' || fail "add did not use a partially used track (check is vacuous)"
import sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0])
import a2_nibref as A
a, b = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
mine = {k for k, o in A.dos33_in_use(b).items() if 'NEW' in o}
old = set(A.dos33_in_use(a))
sys.exit(0 if any((t, s2) in old for (t, _) in mine for s2 in range(16)) else 1)
EOF
pass

"$RDEDISKTOOL" delete "$WORK/a.do" NEW >/dev/null
check "$WORK/std.do" "$WORK/a.do" || fail "std: delete did not restore the bitmap"; pass

# --- old reversed in-byte order
"$RDEDISKTOOL" validate "$WORK/mir.do" >"$WORK/val.txt" || true
[[ $(grep -c "is used but marked free in bitmap" "$WORK/val.txt") == "$MIR_FREE" ]] \
  || { cat "$WORK/val.txt" >&2; fail "reversed: validate should report $MIR_FREE sectors"; }
pass
cp "$WORK/mir.do" "$WORK/m.do"
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/m.do" "$WORK/new.bin" NEW --type B --addr 0x2000) == 0 ]] \
  || { cat "$WORK/out.log" >&2; fail "reversed: add"; }
grep -q "Warning: $MIR_FREE sector(s) in use were marked free" "$WORK/out.log" \
  || { cat "$WORK/out.log" >&2; fail "reversed: repair warning with count $MIR_FREE"; }
pass
check "$WORK/mir.do" "$WORK/m.do" NEW || fail "reversed: add overwrote or mis-marked sectors"; pass
for f in ALPHA BRAVO CHARLIE DELTA; do
  "$RDEDISKTOOL" extract "$WORK/m.do" "$f" "$WORK/x_$f" >/dev/null
  cmp -s "$WORK/mir/$f" "$WORK/x_$f" || fail "reversed: $f changed by add"; pass
done
"$RDEDISKTOOL" validate "$WORK/m.do" | grep -q "0 error(s), 0 warning(s)" || fail "reversed: validate after repair"; pass

# --- optional: real DOS 3.3 master, allocation starting on track 7 (S13-15 used)
if [[ -f "$DOS33_MASTER" ]]; then
  "$RDEDISKTOOL" validate "$DOS33_MASTER" | grep -q "0 error(s), 0 warning(s)" || fail "master: validate"; pass
  cp "$DOS33_MASTER" "$WORK/r0.do"
  python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); o = 17 * 16 * 256
d[o + 0x30], d[o + 0x31] = 7, 1
open(p, "wb").write(d)' "$WORK/r0.do"
  cp "$WORK/r0.do" "$WORK/r.do"
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/r.do" "$WORK/new.bin" NEW --type B --addr 0x2000) == 0 ]] \
    || { cat "$WORK/out.log" >&2; fail "master: add on track 7"; }
  check "$WORK/r0.do" "$WORK/r.do" NEW || fail "master: add"; pass
else
  echo "  (skip: $DOS33_MASTER missing; not judged)"
fi

echo "PASS test_apple_dos33_bitmap ($CHECKS checks)"
