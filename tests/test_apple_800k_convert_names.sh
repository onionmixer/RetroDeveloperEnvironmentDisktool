#!/usr/bin/env bash
# convert / create: output names of 800K images and the -f value of convert.
#
# 800K images are recognised only by extension + size (.po 819,200 B; .2mg
# with a 2IMG header), so an 800po written as .2mg/.img or an 800mg written
# as .po could not be opened again (measured before the fix: info said
# "Unknown", or took the 2MG file for a 140K .po). Such names are refused.
# convert -f used to be case-sensitive and silently fell back to the output
# extension for a value it did not know; it now takes the names of create
# (any case) plus the older aliases, and refuses anything else.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_800k_names.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }
refused() {   # refused <reason> <output> <rdedisktool args...>
  local why=$1 out=$2; shift 2
  rm -f "$out"
  rde "$@" >/dev/null 2>"$WORK/err" && fail "accepted: $*"
  grep -q "$why" "$WORK/err" || { cat "$WORK/err"; fail "refused for another reason: $*"; }
  [[ ! -e "$out" ]] || fail "refused command left $out"
}
fmt_of() { rde info "$1" | sed -n 's/^Format: //p'; }

rde create "$WORK/a.po" -f 800po --fs prodos -n A >/dev/null
rde convert "$WORK/a.po" "$WORK/a.2mg" >/dev/null
rde create "$WORK/s.po" --fs prodos -n S >/dev/null

# --- E1: names that would not open again
refused "must be named \*.2mg" "$WORK/b.po"  convert "$WORK/a.2mg" "$WORK/b.po" -f 800mg
refused "must be named \*.po"  "$WORK/c.2mg" convert "$WORK/a.po" "$WORK/c.2mg" -f 800po
refused "must be named \*.po"  "$WORK/d.img" convert "$WORK/a.po" "$WORK/d.img" -f 800po
refused "must be named \*.2mg" "$WORK/e.po"  create "$WORK/e.po" -f 800mg --fs prodos
refused "must be named \*.po"  "$WORK/f.dsk" create "$WORK/f.dsk" -f 800po
refused "no extension"         "$WORK/g"     create "$WORK/g" -f 800po
pass
# accepted names, any case, open as the format written
rde convert "$WORK/a.po" "$WORK/h.2MG" >/dev/null || fail "convert -> .2MG"
rde convert "$WORK/a.2mg" "$WORK/i.PO" -f 800po >/dev/null || fail "convert -> .PO -f 800po"
rde create "$WORK/j.Po" -f 800po --fs prodos -n J >/dev/null || fail "create .Po"
[[ $(fmt_of "$WORK/h.2MG") == "Apple II ProDOS 800K (2MG)" ]] || fail "h.2MG format"
[[ $(fmt_of "$WORK/i.PO") == "Apple II ProDOS 800K" && $(fmt_of "$WORK/j.Po") == "Apple II ProDOS 800K" ]] \
  || fail "i.PO/j.Po format"
cmp -s "$WORK/a.po" "$WORK/i.PO" || fail "a.2mg -> i.PO data"; pass

# --- E2: convert -f
rde convert "$WORK/a.po" "$WORK/k.2mg" -f 800MG >/dev/null || fail "-f 800MG"
[[ $(fmt_of "$WORK/k.2mg") == "Apple II ProDOS 800K (2MG)" ]] || fail "-f 800MG format"
rde convert "$WORK/s.po" "$WORK/l.do" -f Do >/dev/null || fail "-f Do"
[[ $(fmt_of "$WORK/l.do") == "Apple II DOS Order" ]] || fail "-f Do format: $(fmt_of "$WORK/l.do")"
rde convert "$WORK/l.do" "$WORK/m.po" -f PRODOS >/dev/null || fail "-f PRODOS (alias)"
cmp -s "$WORK/s.po" "$WORK/m.po" || fail "-f PRODOS data"; pass
refused "Unknown disk format: bogus"  "$WORK/n.2mg" convert "$WORK/a.po" "$WORK/n.2mg" -f bogus
refused "Unknown disk format: 800pox" "$WORK/o.po"  convert "$WORK/a.po" "$WORK/o.po" -f 800pox
pass
# a wrong case of a real name is that name (ProDOS order, 140K), not the
# extension's format: refused because .2mg belongs to another format
refused "Apple II ProDOS Order output named \*.2mg" "$WORK/p.2mg" convert "$WORK/a.po" "$WORK/p.2mg" -f PO
pass

echo "PASS test_apple_800k_convert_names ($CHECKS checks)"
