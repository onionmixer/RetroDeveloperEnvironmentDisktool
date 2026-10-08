#!/usr/bin/env bash
# Apple II sector numbering across formats: .do/.dsk/.nib/.woz use DOS 3.3
# logical sectors, .po uses ProDOS logical sectors. convert must move every
# sector to the same physical sector, and the DOS 3.3 / ProDOS handlers must
# work on every format.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
NIBREF="$SCRIPT_DIR/tools/a2_nibref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_order.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
nibref() { python3 -I "$NIBREF" "$@"; }
same() { cmp -s "$1" "$2" || fail "$3: $1 differs from $2"; pass; }
mkblob() { head -c "$2" /dev/urandom >"$1"; }
conv() { "$RDEDISKTOOL" convert "$1" "$2" -f "$3" >/dev/null || fail "convert $1 -> $3"; }

# Every sector holds its own (track, DOS sector) id
nibref make-id-dsk "$WORK/ids.do"

# 1. DO -> PO places each sector on the same physical sector
conv "$WORK/ids.do" "$WORK/ids.po" po
nibref order-check po "$WORK/ids.do" "$WORK/ids.po" >/dev/null || fail "DO -> PO mapping"; pass

# 2. Every directed pair through every format returns the original
for a in po nib woz; do
  conv "$WORK/ids.do" "$WORK/a.$a" "$a"
  for b in do po nib woz; do
    [[ $a == "$b" ]] && continue
    conv "$WORK/a.$a" "$WORK/ab_$a.$b" "$b"
    if [[ $b == do ]]; then
      cp "$WORK/ab_$a.$b" "$WORK/ab_$a.$b.do"
    else
      conv "$WORK/ab_$a.$b" "$WORK/ab_$a.$b.do" do
    fi
    same "$WORK/ids.do" "$WORK/ab_$a.$b.do" "do -> $a -> $b -> do"
  done
done
conv "$WORK/a.woz" "$WORK/woz2po.po" po
nibref order-check po "$WORK/ids.do" "$WORK/woz2po.po" >/dev/null || fail "WOZ -> PO mapping"; pass

# 3. DOS 3.3 files survive conversion to every format (catalog chains cross sectors)
mkblob "$WORK/blob.bin" 9000
"$RDEDISKTOOL" create "$WORK/dos.do" -f do --fs dos33 >/dev/null
"$RDEDISKTOOL" add "$WORK/dos.do" "$WORK/blob.bin" BLOB >/dev/null
for f in po nib woz; do
  conv "$WORK/dos.do" "$WORK/dos.$f" "$f"
  "$RDEDISKTOOL" info "$WORK/dos.$f" | grep -q "File System: DOS 3.3" || fail "DOS 3.3 on $f"; pass
  "$RDEDISKTOOL" extract "$WORK/dos.$f" BLOB "$WORK/dos_$f.bin" >/dev/null || fail "extract DOS file from $f"
  same "$WORK/blob.bin" "$WORK/dos_$f.bin" "DOS 3.3 file from $f"
done
# writing DOS 3.3 on a .po image lands on the right physical sectors
mkblob "$WORK/blob2.bin" 2000
"$RDEDISKTOOL" add "$WORK/dos.po" "$WORK/blob2.bin" BLOB2 >/dev/null || fail "add DOS file on po"
"$RDEDISKTOOL" add "$WORK/dos.do" "$WORK/blob2.bin" BLOB2 >/dev/null
conv "$WORK/dos.po" "$WORK/dos_po_back.do" do
same "$WORK/dos.do" "$WORK/dos_po_back.do" "DOS 3.3 write on po"

# 4. ProDOS: all 280 blocks identical after a trip through every format
"$RDEDISKTOOL" create "$WORK/pro.po" -f po --fs prodos -n ORDER >/dev/null
"$RDEDISKTOOL" add "$WORK/pro.po" "$WORK/blob.bin" BLOB >/dev/null
for f in do nib woz; do
  conv "$WORK/pro.po" "$WORK/pro.$f" "$f"
  "$RDEDISKTOOL" info "$WORK/pro.$f" | grep -q "File System: ProDOS" || fail "ProDOS on $f"; pass
  "$RDEDISKTOOL" extract "$WORK/pro.$f" BLOB "$WORK/pro_$f.bin" >/dev/null || fail "extract ProDOS file from $f"
  same "$WORK/blob.bin" "$WORK/pro_$f.bin" "ProDOS file from $f"
  conv "$WORK/pro.$f" "$WORK/pro_$f.po" po
  same "$WORK/pro.po" "$WORK/pro_$f.po" "ProDOS 280 blocks via $f"
done

echo "PASS test_apple_convert_order ($CHECKS checks)"
