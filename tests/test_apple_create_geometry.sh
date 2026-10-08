#!/usr/bin/env bash
# Apple II `create -g`: only geometries the image classes can load back.
#
# .do/.po load only 143,360 bytes (DO also 116,480 = 13 sectors), NIB/WOZ read
# 35 tracks (16 sectors, or 13: DOS 3.2 tracks, since 2026-10-08). Before the fix `create` accepted any -g and wrote images that
# `info`/`add` then refused ("Invalid file size") or read partly.
#   refused: create fails and writes no file
#   accepted: the created image opens again, has the expected size and
#             sectors per track (sizes computed here, not taken from the tool)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_creategeom.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }

refused() {   # refused <fmt> <geometry> [--fs x]
  local f="$WORK/r_$1_${2//:/_}.$1"
  set +e
  "$RDEDISKTOOL" create "$f" -f "$1" -g "$2" "${@:3}" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc != 0 ]] || fail "$1 -g $2 ${*:3}: accepted"
  grep -q "can only be created as 35 tracks" "$WORK/out.log" || { cat "$WORK/out.log" >&2; fail "$1 -g $2: refused for another reason"; }
  [[ ! -e "$f" ]] || fail "$1 -g $2: refused but wrote $f"
  pass
}
accepted() {  # accepted <fmt> <geometry or -> <bytes> <spt> [--fs x]
  local f="$WORK/a_$1_${2//:/_}.$1" g=()
  [[ $2 != - ]] && g=(-g "$2")
  "$RDEDISKTOOL" create "$f" -f "$1" "${g[@]}" "${@:5}" >/dev/null || fail "$1 ${g[*]}: refused"
  [[ $(stat -c %s "$f") == "$3" ]] || fail "$1 ${g[*]}: size $(stat -c %s "$f"), want $3"
  "$RDEDISKTOOL" info "$f" >"$WORK/info.txt" || fail "$1 ${g[*]}: created image does not open"
  grep -q "Sectors/Track: $4" "$WORK/info.txt" || fail "$1 ${g[*]}: not $4 sectors/track"
  pass
}

S16=$(python3 -I -c 'print(35 * 16 * 256)')
S13=$(python3 -I -c 'print(35 * 13 * 256)')
NIB=$(python3 -I -c 'print(35 * 6656)')
NB2=$(python3 -I -c 'print(35 * 6384)')

for fmt in do po nib nb2 woz; do
  refused "$fmt" 40:1:16:256
  refused "$fmt" 80:2:16:256
  refused "$fmt" 35:1:16:512
  refused "$fmt" 35:2:16:256
done
refused po 280:1:16:256 --fs prodos
refused do 40:1:16:256 --fs dos33
refused po 35:1:13:256
refused d13 40:1:13:256
refused nib 40:1:13:256

accepted do  -           "$S16" 16
accepted do  35:1:16:256 "$S16" 16 --fs dos33
accepted po  35:1:16:256 "$S16" 16 --fs prodos
accepted do  35:1:13:256 "$S13" 13
accepted d13 -           "$S13" 13
accepted nib 35:1:16:256 "$NIB" 16
accepted nb2 -           "$NB2" 16
accepted nib 35:1:13:256 "$NIB" 13
accepted nb2 35:1:13:256 "$NB2" 13
# WOZ size depends on the bit stream: check that it opens as 35 x 16
"$RDEDISKTOOL" create "$WORK/a.woz" -f woz -g 35:1:16:256 >/dev/null || fail "woz 35:1:16:256 refused"
"$RDEDISKTOOL" info "$WORK/a.woz" >"$WORK/info.txt" || fail "created woz does not open"
grep -q "Tracks: 35" "$WORK/info.txt" && grep -q "Sectors/Track: 16" "$WORK/info.txt" || fail "woz geometry"
pass
"$RDEDISKTOOL" create "$WORK/a13.woz" -f woz -g 35:1:13:256 >/dev/null || fail "woz 35:1:13:256 refused"
"$RDEDISKTOOL" info "$WORK/a13.woz" >"$WORK/info.txt" || fail "created 13-sector woz does not open"
grep -q "Tracks: 35" "$WORK/info.txt" && grep -q "Sectors/Track: 13" "$WORK/info.txt" || fail "13-sector woz geometry"
pass

echo "PASS test_apple_create_geometry ($CHECKS checks)"
