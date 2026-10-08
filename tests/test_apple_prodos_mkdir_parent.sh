#!/usr/bin/env bash
# ProDOS mkdir: subdirectory header parent fields, and volume names.
#
# ProDOS 8 Technical Reference B.2.3/B.2.4: parent_pointer is the directory
# block that holds the subdirectory's entry, parent_entry_number its entry
# number within that block, where the first entry of a key block is the
# header. On A2 DeskTop 1.5 (8 subdirectories) the number is slot + 1 in
# every case. Before the fix mkdir wrote the directory's key block and a
# 0-based count over the whole directory (0 for the first entry, 13 for the
# first entry of the second block); ProDOS uses these fields to find the
# entry it updates. Checked with tests/tools/a2_prodos_ref.py check, plus the
# exact block / number of each edge case computed here.
# Header bytes $14-$1B, the header version and the parent entry's version /
# access are written as ProDOS 2.4.3 CREATE writes them (measured in MAME
# apple2cp: reserved 75 24 00 C3 27 0D 00 00, version $24, entry access $E3).
# Volume names (create -n) follow the file name rules (2.1): 1-15 of A-Z,
# 0-9, '.', starting with a letter; before the fix they were cut to 15 and
# any character was kept.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_prodos_mkdir.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }
# parent <image.po> <dir path>: "<parent_pointer> <parent_entry_number>" from the header
parent() {
  python3 -I -B - "$REF" "$1" "$2" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
data, _ = A.load(sys.argv[2]); v = A.Volume(data)
e = next(e for e in v.entries(2) if e['path'] == sys.argv[3])
h = v.block(e['key'])
print(h[0x27] | h[0x28] << 8, h[0x29])
EOF
}
# reserved <image.po>: every subdirectory matches what ProDOS 2.4.3 CREATE wrote in
# MAME: header $20-$22 = 24 00 C3, $14-$1B = 75 24 00 C3 27 0D 00 00; its entry in
# the parent: version $24, min_version 0, access $E3
reserved() {
  python3 -I -B - "$REF" "$1" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
data, _ = A.load(sys.argv[2]); v = A.Volume(data)
n = 0
for e in v.entries(2):
    if e['storage'] == 0xD:
        h = v.block(e['key'])
        b = v.block(e['dir_block'])
        o = next(4 + i * 0x27 for i in range(13)
                 if b[4 + i * 0x27] >> 4 == 0xD and (b[4 + i * 0x27 + 0x11] | b[4 + i * 0x27 + 0x12] << 8) == e['key'])
        got = (h[0x20:0x23], h[0x14:0x1C], b[o + 0x1C:o + 0x1F])
        want = (bytes.fromhex('2400c3'), bytes.fromhex('752400c3270d0000'), bytes.fromhex('2400e3'))
        if got != want:
            sys.exit('%s: header %s reserved %s entry %s, want %s %s %s' % (
                (e['path'],) + tuple(x.hex() for x in got) + tuple(x.hex() for x in want)))
        n += 1
sys.exit(0 if n else 'no subdirectory')
EOF
}
echo x >"$WORK/x"

run_case() {   # run_case <label> <create args...>
  local n=$1; shift
  local img="$WORK/$n.po"
  rde create "$img" "$@" --fs prodos -n CASE >/dev/null || fail "$n: create"
  rde mkdir "$img" FIRST >/dev/null || fail "$n: mkdir FIRST"          # root slot 1
  rde mkdir "$img" FIRST/INNER >/dev/null || fail "$n: mkdir INNER"    # slot 1 of FIRST
  for i in $(seq 2 12); do rde add "$img" "$WORK/x" "F$i" >/dev/null; done   # root slots 2-12
  rde mkdir "$img" B3S0 >/dev/null || fail "$n: mkdir B3S0"            # block 3 slot 0
  rde mkdir "$img" B3S1 >/dev/null || fail "$n: mkdir B3S1"            # block 3 slot 1
  rde delete "$img" F5 >/dev/null || fail "$n: delete F5"              # frees root slot 5
  rde mkdir "$img" REUSE >/dev/null || fail "$n: mkdir REUSE"          # root slot 5 again
  python3 -I -B "$REF" check "$img" >/dev/null || { python3 -I -B "$REF" check "$img"; fail "$n: reference check"; }
  pass
  local inner_key
  inner_key=$(python3 -I -B "$REF" ls "$img" | awk -F'\t' '$1=="/FIRST"{print $6}')
  for want in "/FIRST 2 2" "/FIRST/INNER $inner_key 2" "/B3S0 3 1" "/B3S1 3 2" "/REUSE 2 6"; do
    read -r p b e <<<"$want"
    [[ "$(parent "$img" "$p")" == "$b $e" ]] || fail "$n: $p parent = $(parent "$img" "$p"), want $b $e"
  done
  pass
  rde validate "$img" | grep -q "0 error(s)" || fail "$n: validate"; pass
  reserved "$img" || fail "$n: subdirectory header / entry differ from ProDOS CREATE"; pass
}
run_case p140 -f po
run_case p800 -f 800po
# the same disk in DOS order: convert the 140K result and read it back
rde convert "$WORK/p140.po" "$WORK/p140.do" -f do >/dev/null || fail "convert -> .do"
rde mkdir "$WORK/p140.do" DOSORD >/dev/null || fail "mkdir on .do"
rde convert "$WORK/p140.do" "$WORK/p140b.po" -f po >/dev/null || fail "convert .do -> .po"
python3 -I -B "$REF" check "$WORK/p140b.po" >/dev/null || fail ".do mkdir: reference check"
[[ "$(parent "$WORK/p140b.po" /DOSORD)" == "3 3" ]] || fail ".do mkdir parent = $(parent "$WORK/p140b.po" /DOSORD)"; pass
reserved "$WORK/p140b.po" || fail ".do mkdir: header / entry differ from ProDOS CREATE"; pass

# --- volume names
for n in "1ABC" "MY VOL" "A_B" "ABCDEFGHIJKLMNOP" "A/B"; do
  for fmt in po 800po; do
    rm -f "$WORK/v.po"
    rde create "$WORK/v.po" -f "$fmt" --fs prodos -n "$n" >/dev/null 2>"$WORK/err" && fail "$fmt: volume name '$n' accepted"
    grep -q "Invalid filename" "$WORK/err" || { cat "$WORK/err"; fail "$fmt '$n': refused for another reason"; }
    [[ ! -e "$WORK/v.po" ]] || fail "$fmt '$n': refused create left a file"
  done
done; pass
for pair in "abcdefghijklmno:ABCDEFGHIJKLMNO" "a.1:A.1" "Z:Z"; do
  rm -f "$WORK/v.po"
  rde create "$WORK/v.po" -f po --fs prodos -n "${pair%%:*}" >/dev/null || fail "volume name '${pair%%:*}' refused"
  python3 -I -B "$REF" info "$WORK/v.po" | grep -qx "volume.name=${pair##*:}" || fail "volume name '${pair%%:*}'"
done; pass

echo "PASS test_apple_prodos_mkdir_parent ($CHECKS checks)"
