#!/usr/bin/env bash
# MSX XSA: DSK -> XSA -> DSK must give back every byte.
#
# The compressor's ring holds 8192 bytes of which 254 are look-ahead, so a
# back-reference may reach at most 7938 bytes back. Stale index pointers let it
# pick farther matches (into look-ahead); the decoder then copied other bytes.
# Measured before the fix: a real MSX-DOS 2.3 boot disk came back with 709,120
# bytes changed (from sector 36 on); repeating patterns failed often.
# Expected value = the input itself. Decoded with rdedisktool, and also with
# openMSX's xsa2dsk when XSA2DSK points to it (independent decoder; it pads to
# 512-byte sectors, only the original length is compared).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
BOOTDISKS="${BOOTDISKS:-$TOOL_ROOT/../diskwork/bootdisk/msx}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_msx_xsa.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }

roundtrip() {   # roundtrip <dsk> <label>
  local src=$1 n=$2
  "$RDEDISKTOOL" --bootdisk-mode off convert "$src" "$WORK/$n.xsa" >/dev/null 2>&1 || fail "$n: dsk -> xsa"
  "$RDEDISKTOOL" --bootdisk-mode off convert "$WORK/$n.xsa" "$WORK/$n.back.dsk" -f msxdsk >/dev/null 2>&1 \
    || fail "$n: xsa -> dsk"
  cmp -s "$src" "$WORK/$n.back.dsk" || fail "$n: DSK -> XSA -> DSK changed the data"; pass
  if [[ -n "${XSA2DSK:-}" ]]; then
    "$XSA2DSK" "$WORK/$n.xsa" "$WORK/$n.ox.dsk" >/dev/null || fail "$n: xsa2dsk rejected the XSA"
    python3 -I -c 'import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
sys.exit(0 if b[:len(a)] == a else 1)' "$src" "$WORK/$n.ox.dsk" || fail "$n: openMSX decodes other data"
    pass
  fi
}

# synthetic 720 KB MSX-DOS disks: boot sector + generated contents
"$RDEDISKTOOL" create "$WORK/base.dsk" -f msxdsk --fs msxdos >/dev/null
for kind in repeat7938 repeat8192 runs mixed noise; do
  python3 -I - "$WORK/base.dsk" "$WORK/$kind.dsk" "$kind" <<'EOF'
import random, sys
d = bytearray(open(sys.argv[1], 'rb').read()); kind = sys.argv[3]
r = random.Random(kind)
out = bytearray()
while len(out) < len(d) - 512:
    if kind.startswith('repeat'):
        n = int(kind[6:]); blk = bytes(r.randrange(256) for _ in range(r.randrange(1, 64)))
        out += (blk * (n // len(blk) + 1))[:n]
    elif kind == 'runs':
        out += bytes([r.randrange(256)]) * r.randrange(1, 600)
    elif kind == 'mixed':
        out += r.choice([bytes(r.randrange(256) for _ in range(300)), bytes([r.randrange(256)]) * 500,
                         bytes(out[-8000:-7700]) if len(out) > 8000 else b'x' * 50])
    else:
        out += bytes(r.randrange(256) for _ in range(512))
d[512:] = out[:len(d) - 512]
open(sys.argv[2], 'wb').write(d)
EOF
  roundtrip "$WORK/$kind.dsk" "$kind"
done

# real boot disks, when present in the workspace
for f in msxdos23.dsk msxdos103.dsk; do
  if [[ -f "$BOOTDISKS/$f" ]]; then
    cp "$BOOTDISKS/$f" "$WORK/$f"
    roundtrip "$WORK/$f" "${f%.dsk}"
  else
    echo "  (skip: $BOOTDISKS/$f missing; not judged)"
  fi
done

echo "PASS test_msx_xsa_roundtrip ($CHECKS checks)"
