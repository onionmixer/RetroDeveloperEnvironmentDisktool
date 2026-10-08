#!/usr/bin/env bash
# ProDOS: sparse files and per-directory file_count.
#
# Expected values come from tests/tools/a2_prodos_ref.py (independent ProDOS
# reader / writer, ProDOS 8 Technical Reference Appendix B):
#   - a zero pointer in an index block is an unallocated block that reads as
#     512 zero bytes and keeps its place in the file. Before the fix the reader
#     dropped such blocks (later data moved forward, the file came out short):
#     real ProDOS 2.4.3 PRODOS extracted as 16,896 of 17,128 bytes.
#   - file_count of a directory counts that directory's own active entries.
#     Before the fix validate compared the whole tree's count with the volume
#     header and warned on every disk with files in a subdirectory.
# Optional: workspace ProDOS 2.4.3 disk (diskwork/bootdisk/AppleII/ProDOS_2_4_3.po).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
PRODOS243="${PRODOS243:-$TOOL_ROOT/../diskwork/bootdisk/AppleII/ProDOS_2_4_3.po}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_prodos_sparse.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
ref() { python3 -I -B "$REF" "$@"; }

# --- sparse files written by the reference, read by rdedisktool
"$RDEDISKTOOL" create "$WORK/s.po" -f po --fs prodos -n SPARSE >/dev/null
ref add-sparse "$WORK/s.po" HOLE hole 1 "$WORK/HOLE.exp"
ref add-sparse "$WORK/s.po" TAIL tail 2 "$WORK/TAIL.exp"
ref add-sparse "$WORK/s.po" MASTER master 3 "$WORK/MASTER.exp"
ref check "$WORK/s.po" >/dev/null || fail "reference wrote an inconsistent disk"
for f in HOLE TAIL MASTER; do
  "$RDEDISKTOOL" extract "$WORK/s.po" "$f" "$WORK/$f.got" >/dev/null || fail "extract $f"
  cmp -s "$WORK/$f.exp" "$WORK/$f.got" || fail "$f: extracted bytes differ from the reference"
  pass
done
"$RDEDISKTOOL" validate "$WORK/s.po" | grep -q "0 error(s), 0 warning(s)" || fail "validate sparse disk"; pass
cp "$WORK/s.po" "$WORK/d.po"
for f in HOLE TAIL MASTER; do "$RDEDISKTOOL" delete "$WORK/d.po" "$f" >/dev/null || fail "delete $f"; done
ref check "$WORK/d.po" >/dev/null || { ref check "$WORK/d.po"; fail "disk inconsistent after deleting sparse files"; }
pass

# --- per-directory file_count
"$RDEDISKTOOL" create "$WORK/c.po" -f po --fs prodos -n COUNT >/dev/null
echo x >"$WORK/x.txt"
"$RDEDISKTOOL" add "$WORK/c.po" "$WORK/x.txt" ROOTFILE >/dev/null
"$RDEDISKTOOL" mkdir "$WORK/c.po" SUB >/dev/null
for i in 1 2 3; do "$RDEDISKTOOL" add "$WORK/c.po" "$WORK/x.txt" "SUB/F$i" >/dev/null; done
ref check "$WORK/c.po" >/dev/null || fail "reference: subdirectory disk inconsistent"
"$RDEDISKTOOL" validate "$WORK/c.po" | grep -q "0 error(s), 0 warning(s)" || fail "validate warns on a consistent subdirectory disk"; pass
patch_count() {   # patch_count <image> <out> root|sub <delta>
  python3 -I -B - "$REF" "$@" <<'EOF'
import sys, struct, shutil
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
src, out, where, delta = sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
d = bytearray(open(src, 'rb').read()); v = A.Volume(bytes(d))
key = 2 if where == 'root' else next(e['key'] for e in v.entries(2) if e['path'] == '/SUB')
o = key * 512 + 0x25
struct.pack_into('<H', d, o, struct.unpack_from('<H', d, o)[0] + delta)
open(out, 'wb').write(d)
EOF
}
patch_count "$WORK/c.po" "$WORK/bad_root.po" root 1
"$RDEDISKTOOL" validate "$WORK/bad_root.po" | grep -q "File count mismatch: header says 3, found 2 \[Volume header\]" \
  || { "$RDEDISKTOOL" validate "$WORK/bad_root.po"; fail "wrong root file_count not reported"; }
pass
patch_count "$WORK/c.po" "$WORK/bad_sub.po" sub 1
"$RDEDISKTOOL" validate "$WORK/bad_sub.po" | grep -q "File count mismatch: header says 4, found 3 \[SUB\]" \
  || { "$RDEDISKTOOL" validate "$WORK/bad_sub.po"; fail "wrong subdirectory file_count not reported"; }
pass

# --- optional: real ProDOS 2.4.3 disk (PRODOS is sparse)
if [[ -f "$PRODOS243" ]]; then
  cp "$PRODOS243" "$WORK/p.po"
  n=0
  while IFS=$'\t' read -r path storage _; do
    [[ $storage == 1 || $storage == 2 || $storage == 3 ]] || continue
    ref cat "$WORK/p.po" "$path" "$WORK/exp.bin"
    "$RDEDISKTOOL" extract "$WORK/p.po" "${path#/}" "$WORK/got.bin" >/dev/null || fail "extract $path"
    cmp -s "$WORK/exp.bin" "$WORK/got.bin" || fail "ProDOS 2.4.3 $path differs from the reference"
    n=$((n + 1))
  done < <(ref ls "$WORK/p.po")
  [[ $n -gt 0 ]] || fail "no files compared"; pass
  "$RDEDISKTOOL" validate "$WORK/p.po" | grep -q "0 error(s), 0 warning(s)" || fail "validate ProDOS 2.4.3"; pass
else
  echo "  (skip: $PRODOS243 missing; not judged)"
fi

echo "PASS test_apple_prodos_sparse ($CHECKS checks)"
