#!/usr/bin/env bash
# Boot disk safe-add protection across Apple II sector orders.
#
# ProDOS boot blocks 0-1 are ProDOS sectors 0-3 of track 0 (physical 0,2,4,6).
# .po numbers sectors in ProDOS order; .do/.nib/.woz in DOS 3.3 order, where
# the same sectors are DOS 0,14,13,12. The safe-add snapshot must look at the
# real boot blocks in every format:
#   positive: a normal add (which rewrites the bitmap, block 6) succeeds
#   negative: an add that writes block 1 (corrupted directory pointer) is
#             rejected for block 1 itself ("linear sector 2") and nothing is saved
# DOS 3.3 protects track 0 only; files on tracks 1-2 of a disk formatted by
# this tool must stay writable when the disk is treated as a boot disk.
# Disks are synthetic (no Apple code): dummy PRODOS / BASIC.SYSTEM files make
# the boot disk detection fire.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
TOOLS="$SCRIPT_DIR/tools"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_bootorder.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rc_of() {
  set +e
  "$@" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc -lt 128 ]] || { cat "$WORK/out.log" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}
# ProDOS blocks 0-1 (1024 bytes) of any Apple format, read without rdedisktool
bootblocks() {
  python3 -I - "$TOOLS" "$1" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A
p = sys.argv[2]
phys = [A.PRODOS_L2P[s] for s in range(4)]           # ProDOS sectors 0-3
if p.endswith('.po'):
    out = open(p, 'rb').read()[:1024]
elif p.endswith('.do'):
    d = open(p, 'rb').read()
    out = b''.join(d[A.DOS_P2L[q] * 256:(A.DOS_P2L[q] + 1) * 256] for q in phys)
else:
    if p.endswith('.nib'):
        tr = list(open(p, 'rb').read()[:6656])
        secs = A.parse_stream(tr + tr, 0)
    else:
        bits = A.woz_track(A.read_woz(p), 0)
        secs = A.parse_stream([v for v, _ in A.lss(bits, 2)], 0)
    out = b''.join(secs[q]['data'] for q in phys)
sys.stdout.write(out.hex())
EOF
}

echo x >"$WORK/f.txt"

# --- synthetic ProDOS boot disk (SUB is made before it counts as a boot disk)
"$RDEDISKTOOL" create "$WORK/base.po" -f po --fs prodos -n BOOTORDER >/dev/null
"$RDEDISKTOOL" mkdir "$WORK/base.po" SUB >/dev/null
"$RDEDISKTOOL" add "$WORK/base.po" "$WORK/f.txt" PRODOS --type SYS >/dev/null
"$RDEDISKTOOL" add "$WORK/base.po" "$WORK/f.txt" BASIC.SYSTEM --type SYS >/dev/null
# recognisable boot blocks (the formatter leaves them zero)
python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
d[0:1024] = bytes((i * 7 + 1) & 0xFF for i in range(1024))
open(p, "wb").write(d)' "$WORK/base.po"
"$RDEDISKTOOL" info "$WORK/base.po" -v | grep -q "BootDisk: yes" || fail "synthetic disk is a boot disk"; pass

# corrupted copy: SUB's key block pointer -> block 1 (block 1 = copy of SUB's key block)
cp "$WORK/base.po" "$WORK/neg.po"
python3 -I - "$WORK/neg.po" <<'EOF'
import struct, sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
for i in range(1, 13):
    o = 2 * 512 + 4 + i * 0x27
    e = d[o:o + 0x27]
    if e[0] >> 4 == 0xD and e[1:1 + (e[0] & 15)] == b'SUB':
        key = struct.unpack('<H', e[0x11:0x13])[0]
        d[512:1024] = d[key * 512:(key + 1) * 512]
        d[o + 0x11:o + 0x13] = struct.pack('<H', 1)
        break
else:
    sys.exit('SUB not found')
open(p, 'wb').write(d)
EOF

for kind in base neg; do
  for f in do nib woz; do
    "$RDEDISKTOOL" convert "$WORK/$kind.po" "$WORK/$kind.$f" -f "$f" >/dev/null 2>&1 \
      || fail "convert $kind -> $f"
  done
done

for f in po do nib woz; do
  # positive: normal add passes the safe-add check, boot blocks unchanged
  cp "$WORK/base.$f" "$WORK/pos.$f"
  before=$(bootblocks "$WORK/pos.$f")
  [[ ${#before} == 2048 ]] || fail "$f boot block reader"
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/pos.$f" "$WORK/f.txt" NOTE.TXT) == 0 ]] \
    || { cat "$WORK/out.log" >&2; fail "$f: normal add on boot disk rejected"; }
  grep -q "Bootdisk safe-add verification enabled" "$WORK/out.log" || fail "$f: safe-add did not run"; pass
  [[ "$(bootblocks "$WORK/pos.$f")" == "$before" ]] || fail "$f: boot blocks changed"; pass
  "$RDEDISKTOOL" extract "$WORK/pos.$f" NOTE.TXT "$WORK/note_$f" >/dev/null
  cmp -s "$WORK/f.txt" "$WORK/note_$f" || fail "$f: added file"; pass

  # negative: the add writes block 1 -> rejected for block 1, file untouched
  cp "$WORK/neg.$f" "$WORK/negtry.$f"
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/negtry.$f" "$WORK/f.txt" SUB/NEW.TXT) != 0 ]] \
    || fail "$f: add writing block 1 accepted"
  grep -q "protected sector changed by add operation (linear sector 2)" "$WORK/out.log" \
    || { cat "$WORK/out.log" >&2; fail "$f: rejected for the wrong sector"; }
  pass
  cmp -s "$WORK/neg.$f" "$WORK/negtry.$f" || fail "$f: rejected add changed the image"; pass

  # control: with --force-bootdisk the same add goes through and does change block 1
  cp "$WORK/neg.$f" "$WORK/negforce.$f"
  b1=$(bootblocks "$WORK/negforce.$f")
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/negforce.$f" "$WORK/f.txt" SUB/NEW.TXT) == 0 ]] \
    || fail "$f: forced add"
  after=$(bootblocks "$WORK/negforce.$f")
  [[ "${after:0:1024}" == "${b1:0:1024}" && "${after:1024}" != "${b1:1024}" ]] \
    || fail "$f: forced add should change block 1 only"
  pass
done

# --- DOS 3.3: only track 0 is protected. A disk formatted by this tool gives
# tracks 1-2 to files; adds that land there must succeed on a boot disk.
"$RDEDISKTOOL" create "$WORK/dos.do" -f do --fs dos33 >/dev/null
head -c 4096 /dev/urandom >"$WORK/blk.bin"
for i in $(seq 1 20); do
  [[ $(rc_of "$RDEDISKTOOL" --bootdisk-profile dos33 --bootdisk-mode strict add "$WORK/dos.do" "$WORK/blk.bin" "F$i" --type B --addr 0x2000) == 0 ]] \
    || { cat "$WORK/out.log" >&2; fail "DOS add F$i on boot disk"; }
done
grep -q "Bootdisk safe-add verification enabled" "$WORK/out.log" || fail "DOS safe-add did not run"; pass
python3 -I - "$WORK/dos.do" <<'EOF' || fail "no DOS file reached tracks 1-2 (check is vacuous)"
import sys
d = open(sys.argv[1], 'rb').read()
v = d[17 * 16 * 256:17 * 16 * 256 + 256]
used = lambda t: (v[0x38 + 4 * t] << 8 | v[0x39 + 4 * t]) != 0xFFFF
sys.exit(0 if used(1) else 1)
EOF
pass

echo "PASS test_bootdisk_guard_prodos_order ($CHECKS checks)"
