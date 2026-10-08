#!/usr/bin/env bash
# DOS 3.3: a catalog or track/sector list whose link leads back to a sector
# already read must stop with an error, not loop.
#
# Before: list/add/delete/rename ran forever on a catalog cycle; delete ran
# forever on a T/S list cycle; extract silently followed a T/S list cycle 560
# times and validate printed thousands of warnings. Now each command fails with
# "chain loops back to T.. S.." and the image is not changed; validate reports
# one error. Damaged copies are made with python from a disk rdedisktool made
# (.do = DOS order, sector (t, s) at (t * 16 + s) * 256).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_dos33_loops.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
run() {   # run <args...>: rc of rdedisktool (20 s limit), output in $WORK/out
  set +e
  timeout 20 "$RDEDISKTOOL" "$@" >"$WORK/out" 2>&1
  local rc=$?
  set -e
  [[ $rc != 124 ]] || fail "hung (killed after 20 s): $*"
  [[ $rc -lt 128 ]] || { cat "$WORK/out" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}

"$RDEDISKTOOL" create "$WORK/base.do" --fs dos33 >/dev/null
head -c 3000 /dev/urandom >"$WORK/big.bin"
"$RDEDISKTOOL" add "$WORK/base.do" "$WORK/big.bin" BIG --type B --addr 0x2000 >/dev/null 2>&1
"$RDEDISKTOOL" add "$WORK/base.do" "$WORK/big.bin" OTHER --type B --addr 0x2000 >/dev/null 2>&1
"$RDEDISKTOOL" validate "$WORK/base.do" | grep -q "0 error(s)" || fail "base disk does not validate"

# patch <out> catalog|tslist: prints "T S" of the sector the loop returns to
patch() {
  python3 -I - "$WORK/base.do" "$1" "$2" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
sec = lambda t, s: (t * 16 + s) * 256
v = sec(17, 0)
first = (d[v + 1], d[v + 2])
c0 = sec(*first)
if sys.argv[3] == 'catalog':          # second catalog sector links back to the first
    c1 = sec(d[c0 + 1], d[c0 + 2])
    d[c1 + 1], d[c1 + 2] = first
    print(*first)
else:                                  # BIG's T/S list links to itself
    for i in range(7):
        o = c0 + 0x0B + i * 0x23
        if bytes(x & 0x7F for x in d[o + 3:o + 33]).decode().strip() == 'BIG':
            t, s = d[o], d[o + 1]
            d[sec(t, s) + 1], d[sec(t, s) + 2] = t, s
            print(t, s)
            break
    else:
        sys.exit('BIG not found')
open(sys.argv[2], 'wb').write(d)
EOF
}

read -r t s < <(patch "$WORK/cat.do" catalog)
cp "$WORK/cat.do" "$WORK/cat0.do"
for op in "list" "add:$WORK/big.bin NEW --type B --addr 0x2000" "delete:OTHER" "rename:OTHER OTHER2"; do
  cmd=${op%%:*}; rest=; [[ $op == *:* ]] && rest=${op#*:}
  read -r -a extra <<<"$rest"
  [[ $(run "$cmd" "$WORK/cat.do" "${extra[@]}") != 0 ]] || fail "catalog loop: $cmd passed"
  grep -q "catalog chain loops back to T$t S$s" "$WORK/out" || { cat "$WORK/out"; fail "catalog loop: $cmd message"; }
  cmp -s "$WORK/cat0.do" "$WORK/cat.do" || fail "catalog loop: $cmd changed the image"
done; pass
[[ $(run validate "$WORK/cat.do") != 0 ]] || fail "catalog loop: validate passed"; pass

read -r t s < <(patch "$WORK/ts.do" tslist)
cp "$WORK/ts.do" "$WORK/ts0.do"
for op in "extract:BIG $WORK/x.bin" "extract:--raw BIG $WORK/xr.bin" "delete:BIG"; do
  cmd=${op%%:*}; read -r -a extra <<<"${op#*:}"
  if [[ ${extra[0]} == --raw ]]; then args=(--raw "$WORK/ts.do" "${extra[@]:1}"); else args=("$WORK/ts.do" "${extra[@]}"); fi
  [[ $(run "$cmd" "${args[@]}") != 0 ]] || fail "T/S loop: $cmd ${extra[*]} passed"
  grep -q "track/sector list chain loops back to T$t S$s" "$WORK/out" || { cat "$WORK/out"; fail "T/S loop: $cmd message"; }
  cmp -s "$WORK/ts0.do" "$WORK/ts.do" || fail "T/S loop: $cmd changed the image"
done; pass
[[ $(run validate "$WORK/ts.do") != 0 ]] || fail "T/S loop: validate passed"
grep -q "T/S list chain loops back to T$t/S$s" "$WORK/out" || { cat "$WORK/out"; fail "T/S loop: validate message"; }
grep -q "Summary: 1 error(s)" "$WORK/out" || { grep Summary "$WORK/out"; fail "T/S loop: validate should report one error"; }
pass
# the other file is still readable
[[ $(run extract "$WORK/ts.do" OTHER "$WORK/o.bin") == 0 ]] && cmp -s "$WORK/big.bin" "$WORK/o.bin" || fail "OTHER unreadable"; pass

echo "PASS test_apple_dos33_loops ($CHECKS checks)"
