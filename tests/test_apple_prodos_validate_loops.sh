#!/usr/bin/env bash
# ProDOS: validate checks every allocated block, corrupt chains do not hang,
# Total Space is the space files can use.
#
# Damaged copies of a disk made by rdedisktool are built here with python; the
# independent reader tests/tools/a2_prodos_ref.py `check` must report each
# damage too (so the test really runs on a broken disk).
#   validate: a block in use but marked free is an error for every kind of
#     block — volume directory blocks 3-5, index / master blocks, subdirectory
#     blocks (before: only the key blocks and data blocks were counted); a
#     block marked used but referenced by nothing is a warning; a block
#     referenced twice is a warning also when it is an index block
#   loops: a directory next pointer leading back to a block already read, or a
#     subdirectory pointing at an ancestor, made list/add/validate run forever
#     (or recurse until the stack ran out); now they stop with an error and
#     the image is not changed
#   Total Space = (total blocks - 2 boot - 4 volume directory - bitmap blocks) x 512
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_prodos_validate.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }
# refcheck <image>: the reference's findings (it exits 1 when it finds any)
refcheck() { python3 -I -B "$REF" check "$1" || true; }
# run <seconds> <args...>: rc of rdedisktool, output in $WORK/out; 124 = hung
run() {
  local t=$1; shift
  set +e
  timeout "$t" "$RDEDISKTOOL" "$@" >"$WORK/out" 2>&1
  local rc=$?
  set -e
  [[ $rc != 124 ]] || fail "hung (killed after ${t}s): $*"
  [[ $rc -lt 128 ]] || { cat "$WORK/out" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}

# patch <in> <out> <what>: damaged copy, prints the block number involved
cat >"$WORK/patch.py" <<'EOF'
import struct, sys
sys.path.insert(0, sys.argv[1]); import a2_prodos_ref as A
src, out, what = sys.argv[2], sys.argv[3], sys.argv[4]
d = bytearray(open(src, 'rb').read()); v = A.Volume(bytes(d))
ent = {e['path']: e for e in v.entries(2)}
def bit(n, free):
    o = v.bitmap_pointer * 512 + n // 8
    d[o] = d[o] | (0x80 >> n % 8) if free else d[o] & ~(0x80 >> n % 8)
def setw(o, val): struct.pack_into('<H', d, o, val)
def slot_of(e):
    b = e['dir_block']
    return next(b * 512 + 4 + i * 0x27 for i in range(13)
                if d[b * 512 + 4 + i * 0x27] >> 4 == e['storage'] and
                struct.unpack_from('<H', d, b * 512 + 4 + i * 0x27 + 0x11)[0] == e['key'])
n = None
if what == 'free_voldir':   n = 4; bit(n, True)
elif what == 'free_index':  n = ent['/P']['key']; bit(n, True)
elif what == 'free_master': n = ent['/T']['key']; bit(n, True)
elif what == 'free_tindex': n = v.index(ent['/T']['key'], 128)[0]; bit(n, True)
elif what == 'free_subdir': n = ent['/D']['key']; bit(n, True)
elif what == 'orphan':
    n = next(b for b in range(v.total_blocks - 1, 0, -1) if v.bitmap_free(b)); bit(n, False)
elif what == 'dup_index':   # S (seedling) now uses P's index block as its data block
    n = ent['/P']['key']; old = ent['/S']['key']; setw(slot_of(ent['/S']) + 0x11, n); bit(old, True)
elif what == 'loop_voldir': n = 3; setw(5 * 512 + 2, 3)          # block 5 -> 3
elif what == 'loop_subdir': n = ent['/D']['key']; setw(n * 512 + 2, n)   # D's key -> itself
elif what == 'loop_ancestor':  # D/E's key pointer -> D's key block (an ancestor)
    n = ent['/D']['key']; setw(slot_of(ent['/D/E']) + 0x11, n)
else:
    sys.exit('unknown ' + what)
open(out, 'wb').write(d)
print(n)
EOF
patch() { python3 -I -B "$WORK/patch.py" "$SCRIPT_DIR/tools" "$@"; }

build_base() {   # build_base <image> <format>
  rde create "$1" -f "$2" --fs prodos -n VAL >/dev/null
  head -c 100    /dev/urandom >"$WORK/s.bin"
  head -c 1000   /dev/urandom >"$WORK/p.bin"   # sapling: 2 data + 1 index block
  head -c 132000 /dev/urandom >"$WORK/t.bin"   # tree: 258 data + 2 index + 1 master (fits 140K)
  rde add "$1" "$WORK/s.bin" S >/dev/null
  rde add "$1" "$WORK/p.bin" P >/dev/null
  rde add "$1" "$WORK/t.bin" T >/dev/null
  rde mkdir "$1" D >/dev/null
  rde mkdir "$1" D/E >/dev/null
  rde add "$1" "$WORK/s.bin" D/X >/dev/null
  python3 -I -B "$REF" check "$1" >/dev/null || fail "$2: base disk inconsistent"
  rde validate "$1" | grep -q "0 error(s), 0 warning(s)" || fail "$2: base disk does not validate clean"
}
build_base "$WORK/base.po" po; pass
build_base "$WORK/base800.po" 800po; pass

# --- validate: every kind of block
for case in free_voldir free_index free_master free_tindex free_subdir; do
  n=$(patch "$WORK/base.po" "$WORK/$case.po" "$case")
  refcheck "$WORK/$case.po" | grep -q "block $n in use" || fail "$case: reference did not see the damage"
  [[ $(run 20 validate "$WORK/$case.po") != 0 ]] || fail "$case: validate passed"
  grep -q "Block $n is used but marked free in bitmap" "$WORK/out" || { cat "$WORK/out"; fail "$case: block $n not reported"; }
done; pass
n=$(patch "$WORK/base.po" "$WORK/orphan.po" orphan)
refcheck "$WORK/orphan.po" | grep -q "block $n marked used but not referenced" || fail "orphan: reference"
run 20 validate "$WORK/orphan.po" >/dev/null
grep -q "1 block(s) marked used in bitmap but not referenced: $n" "$WORK/out" || { cat "$WORK/out"; fail "orphan block $n not reported"; }
pass
n=$(patch "$WORK/base.po" "$WORK/dup.po" dup_index)
refcheck "$WORK/dup.po" | grep -q "block $n shared by" || fail "dup: reference"
run 20 validate "$WORK/dup.po" >/dev/null
grep -q "Block $n referenced multiple times" "$WORK/out" || { cat "$WORK/out"; fail "shared index block $n not reported"; }
pass

# --- loops: stop with an error, leave the image alone
for case in loop_voldir loop_subdir; do
  n=$(patch "$WORK/base.po" "$WORK/$case.po" "$case")
  cp "$WORK/$case.po" "$WORK/$case.0.po"
  path=; [[ $case == loop_subdir ]] && path=D
  [[ $(run 20 list "$WORK/$case.po" $path) != 0 ]] || fail "$case: list passed"
  grep -q "directory chain loops back to block $n" "$WORK/out" || { cat "$WORK/out"; fail "$case: list message"; }
  dest=NEW; [[ $case == loop_subdir ]] && dest=D/NEW
  [[ $(run 20 add "$WORK/$case.po" "$WORK/s.bin" "$dest") != 0 ]] || fail "$case: add passed"
  cmp -s "$WORK/$case.0.po" "$WORK/$case.po" || fail "$case: refused add changed the image"
  [[ $(run 20 validate "$WORK/$case.po") != 0 ]] || fail "$case: validate passed"
  grep -q "loops back to block $n" "$WORK/out" || { cat "$WORK/out"; fail "$case: validate message"; }
done; pass
n=$(patch "$WORK/base.po" "$WORK/anc.po" loop_ancestor)
[[ $(run 20 validate "$WORK/anc.po") != 0 ]] || fail "ancestor loop: validate passed"
grep -q "Directory loops back to block $n" "$WORK/out" || { cat "$WORK/out"; fail "ancestor loop: validate message"; }
# safe-add walks every directory: on a boot disk the loop must end in an error
rde --bootdisk-mode off add "$WORK/anc.po" "$WORK/s.bin" PRODOS --type SYS >/dev/null
cp "$WORK/anc.po" "$WORK/anc.0.po"
[[ $(run 60 --bootdisk-mode strict add "$WORK/anc.po" "$WORK/s.bin" NOTE) != 0 ]] || fail "ancestor loop: safe-add passed"
grep -q "nest deeper than 128 levels" "$WORK/out" || { cat "$WORK/out"; fail "ancestor loop: safe-add message"; }
cmp -s "$WORK/anc.0.po" "$WORK/anc.po" || fail "ancestor loop: refused add changed the image"
pass

# --- Total Space
for pair in "base.po:280" "base800.po:1600"; do
  want=$(python3 -I -c 'import sys; n = int(sys.argv[1]); print((n - 2 - 4 - (n + 4095) // 4096) * 512)' "${pair#*:}")
  rde info "$WORK/${pair%%:*}" | grep -qx "Total Space: $want bytes" || fail "${pair%%:*}: Total Space (want $want)"
done; pass

echo "PASS test_apple_prodos_validate_loops ($CHECKS checks)"
