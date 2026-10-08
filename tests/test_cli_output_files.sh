#!/usr/bin/env bash
# create / convert / extract: what may be written where.
#
# Measured before the fix (all exit 0):
#   extract a.po HELLO a.po      -> the image became a 6-byte file
#   convert a.po a.po -f do      -> DOS-order data under the .po name, unreadable
#   convert a.po m.do -f po, convert ... m.nib -f do, create c.do -f po
#                                -> images that open as another format, unreadable
#   convert / extract replaced existing files without a word
# Now: an output that is the input image itself is refused; an output whose
# extension belongs to another format is refused (.dsk / .img / no extension /
# unknown extensions are left alone); overwriting an existing file prints a
# warning (decided: warn, not refuse).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_cli_outputs.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }
refused() {   # refused <reason> <path that must not appear> <args...>
  local why=$1 out=$2; shift 2
  rde "$@" >"$WORK/o" 2>"$WORK/err" && fail "accepted: $*"
  grep -q "$why" "$WORK/err" || { cat "$WORK/err"; fail "refused for another reason: $*"; }
  [[ -z "$out" || ! -e "$out" ]] || fail "refused command left $out"
}
fmt_of() { rde info "$1" 2>/dev/null | sed -n 's/^Format: //p'; }

cd "$WORK"
rde create a.po --fs prodos -n A >/dev/null
echo hello >h.txt
rde add a.po h.txt HELLO >/dev/null
cp a.po a0.po

# --- output = input image
refused "is the input image itself" "" extract a.po HELLO a.po
cmp -s a0.po a.po || fail "extract onto the image changed it"
refused "is the input image itself" "" extract a.po HELLO "$WORK/a.po"
ln -s a.po link.po
refused "is the input image itself" "" extract a.po HELLO link.po
refused "is the input image itself" "" convert a.po a.po -f do
refused "is the input image itself" "" convert a.po ./a.po
cmp -s a0.po a.po || fail "convert onto the image changed it"; pass

# --- extensions that belong to another format
refused "output named \*.do would be opened as Apple II DOS Order"   m.do   convert a.po m.do -f po
refused "output named \*.nib would be opened as Apple II Nibble"     m.nib  convert a.po m.nib -f do
refused "output named \*.do would be opened as Apple II DOS Order"   c.do   create c.do -f po --fs prodos
refused "output named \*.xdf would be opened as X68000 XDF"          x.xdf  create x.xdf -f msxdsk
refused "output named \*.woz would be opened as Apple II WOZ"        w.woz  convert a.po w.woz -f nib
refused "output named \*.do would be opened as Apple II DOS Order"   M.DO   convert a.po M.DO -f po
pass
# allowed: matching, shared (.dsk/.img), upper case, unknown extension
rde convert a.po ok.DO -f do >/dev/null || fail "convert -> .DO"
[[ $(fmt_of ok.DO) == "Apple II DOS Order" ]] || fail ".DO reopened as $(fmt_of ok.DO)"
rde convert a.po ok.dsk -f do >/dev/null || fail "convert -> .dsk -f do"
rde convert ok.DO ok.woz -f woz1 >/dev/null 2>&1 || fail "convert -> .woz -f woz1 (written as WOZ2)"
rde create m.dsk -f msxdsk --fs msxdos >/dev/null || fail "create .dsk -f msxdsk"
rde create mac.img -f mac_img --fs hfs -n M >/dev/null || fail "create .img -f mac_img"
rde create raw.bin -f po >/dev/null || fail "create unknown extension"
rde list ok.dsk | grep -q "^HELLO " || fail "ok.dsk unreadable"
# shared names hold other formats too; content decides when they are opened
rde create x.xdf -f xdf --fs human68k -n X >/dev/null
rde convert x.xdf x68.dsk -f xdf >/dev/null || fail "convert xdf -> .dsk"
[[ $(fmt_of x68.dsk) == "X68000 XDF" ]] || fail "x68.dsk reopened as $(fmt_of x68.dsk)"
rde create msx.img -f msxdsk --fs msxdos >/dev/null || fail "create msx .img"
[[ $(fmt_of msx.img) == "MSX DSK" ]] || fail "msx.img reopened as $(fmt_of msx.img)"; pass

# --- overwriting: a warning, then the new content
echo old >out.txt
rde extract a.po HELLO out.txt >/dev/null 2>err.txt || fail "extract over a file"
grep -q "Overwriting existing file: out.txt" err.txt || { cat err.txt; fail "no overwrite warning (extract)"; }
cmp -s h.txt out.txt || fail "extract did not replace the file"
rde extract a.po HELLO fresh.txt >/dev/null 2>err.txt || fail "extract to a new file"
if grep -q "Overwriting" err.txt; then fail "warning for a new file"; fi
echo junk >ex.do
rde convert a.po ex.do -f do >/dev/null 2>err.txt || fail "convert over a file"
grep -q "Overwriting existing file: ex.do" err.txt || { cat err.txt; fail "no overwrite warning (convert)"; }
cmp -s ok.DO ex.do || fail "convert did not replace the file"
# a directory as output: no overwrite warning (nothing is overwritten); the write fails
mkdir -p outdir
rde extract a.po HELLO outdir >/dev/null 2>err.txt && fail "extract into a directory path accepted"
if grep -q "Overwriting" err.txt; then cat err.txt; fail "overwrite warning for a directory"; fi
pass
# AppleDouble: data file and ._ sidecar
echo mac >m.txt
rde add mac.img m.txt Note >/dev/null
mkdir -p ad
rde extract mac.img Note --apple-double ad/Note.txt >/dev/null 2>err.txt || fail "apple-double extract"
if grep -q "Overwriting" err.txt; then fail "apple-double: warning for new files"; fi
rde extract mac.img Note --apple-double ad/Note.txt >/dev/null 2>err.txt || fail "apple-double extract again"
grep -q "Overwriting existing file: ad/Note.txt" err.txt && grep -q "Overwriting existing file: ad/._Note.txt" err.txt \
  || { cat err.txt; fail "apple-double: no warning for data file and sidecar"; }
pass

echo "PASS test_cli_output_files ($CHECKS checks)"
