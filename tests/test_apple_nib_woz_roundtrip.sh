#!/usr/bin/env bash
# Apple II NIB/WOZ writer: DSK -> NIB/WOZ -> DSK must be byte-identical, and the
# NIB/WOZ files must decode correctly with an independent reader
# (tools/a2_nibref.py: exact RWTS 6-and-2 encoding, physical sector order,
# 10-bit self-sync, WOZ INFO/TMAP conventions).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
NIBREF="$SCRIPT_DIR/tools/a2_nibref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_nibwoz.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
nibref() { python3 -I "$NIBREF" "$@"; }
same() { cmp -s "$1" "$2" || fail "$3: $1 differs from $2"; pass; }
mkblob() { head -c "$2" /dev/urandom >"$1"; }

python3 -I "$NIBREF" make-dsk "$WORK/rand1.dsk" 1
python3 -I "$NIBREF" make-dsk "$WORK/rand2.dsk" 2
python3 -I "$NIBREF" make-id-dsk "$WORK/ids.dsk"

# 1. Round trip + independent decode of raw sector images
for src in rand1 rand2 ids; do
  for fmt in woz nib nb2; do
    "$RDEDISKTOOL" convert "$WORK/$src.dsk" "$WORK/$src.$fmt" -f "$fmt" >/dev/null \
      || fail "convert $src -> $fmt"
    pass
    chk=$fmt; [[ $fmt == nb2 ]] && chk=nib
    nibref "check-$chk" "$WORK/$src.$fmt" "$WORK/$src.dsk" --standard >"$WORK/check.log" \
      || { cat "$WORK/check.log" >&2; fail "independent check of $src.$fmt"; }
    pass
    "$RDEDISKTOOL" convert "$WORK/$src.$fmt" "$WORK/$src.$fmt.do" -f do >/dev/null \
      || fail "convert $src.$fmt -> do"
    same "$WORK/$src.dsk" "$WORK/$src.$fmt.do" "round trip $src via $fmt"
  done
done

# 2. Filesystem operations on NIB/WOZ give the same sectors as on a .do image
mkblob "$WORK/blob.bin" 7000
for fs in dos33 prodos; do
  for fmt in do woz nib nb2; do
    "$RDEDISKTOOL" create "$WORK/fs_$fs.$fmt" -f "$fmt" --fs "$fs" -n TEST >/dev/null \
      || fail "create $fmt --fs $fs"
    "$RDEDISKTOOL" add "$WORK/fs_$fs.$fmt" "$WORK/blob.bin" BLOB >/dev/null \
      || fail "add to $fmt ($fs)"
    "$RDEDISKTOOL" info "$WORK/fs_$fs.$fmt" | grep -q "File System: $( [[ $fs == dos33 ]] && echo 'DOS 3.3' || echo ProDOS)" \
      || fail "filesystem detection on $fmt ($fs)"
    pass
    "$RDEDISKTOOL" extract "$WORK/fs_$fs.$fmt" BLOB "$WORK/out_${fs}_$fmt.bin" >/dev/null \
      || fail "extract from $fmt ($fs)"
    same "$WORK/blob.bin" "$WORK/out_${fs}_$fmt.bin" "extract $fmt ($fs)"
  done
  for fmt in woz nib nb2; do
    "$RDEDISKTOOL" convert "$WORK/fs_$fs.$fmt" "$WORK/fs_$fs.$fmt.do" -f do >/dev/null
    same "$WORK/fs_$fs.do" "$WORK/fs_$fs.$fmt.do" "$fs image written through $fmt"
    chk=$fmt; [[ $fmt == nb2 ]] && chk=nib
    nibref "check-$chk" "$WORK/fs_$fs.$fmt" "$WORK/fs_$fs.do" --standard >"$WORK/check.log" \
      || { cat "$WORK/check.log" >&2; fail "independent check of written $fs.$fmt"; }
    pass
  done
done

# NB2 really is 35 x 6384 bytes
[[ $(stat -c %s "$WORK/rand1.nb2") == 223440 ]] || fail "NB2 size"; pass

# 3. WOZ container fields written for a new image
nibref woz-info "$WORK/rand1.woz" >"$WORK/info.txt"
grep -q "^version=2 info_version=2 crc_ok=1$" "$WORK/info.txt" || fail "WOZ2/INFO v2/CRC"; pass
grep -q "^creator='rdedisktool *'$" "$WORK/info.txt" || fail "creator space padded"; pass
grep -q "^compat=0x0000 largest=13$" "$WORK/info.txt" || fail "compat/largest"; pass
grep -q "^tmap=0,0,-,1,1,1,-,2,2,2,-,3$" "$WORK/info.txt" || fail "TMAP adjacent quarter tracks"; pass

echo "PASS test_apple_nib_woz_roundtrip ($CHECKS checks)"
