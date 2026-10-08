#!/usr/bin/env bash
# Blank Apple II 800K ProDOS volumes (create -f 800po / 800mg --fs prodos).
#
# Every byte of the new volume is compared with an image python builds from
# the ProDOS 8 Technical Reference, Appendix B (B.2.2 volume directory
# header, bit map; Figure B-12 dates) and from a real 800K ProDOS volume
# (A2 DeskTop 1.5: directory blocks 2-5, bit map at block 6, bit map bits
# past the last block are 0):
#   blocks 0-1  zero (no boot loader: bootable volumes are out of scope)
#   blocks 2-5  volume directory, prev/next 0-3, 2-4, 3-5, 4-0, no entries
#   header      $F0|len, name upper case, reserved 0 (as on a real ProDOS
#               2.4.3 disk), creation = local time of the run, version 0,
#               min_version 0, access $C3, $27, $0D, file_count 0,
#               bit_map_pointer 6, total_blocks 1600
#   block 6     bits of blocks 0-6 used (0), 7-1599 free (1), the rest 0
#   blocks 7-   zero
# 800mg holds the same blocks after its 64-byte header.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_apple_800k_create.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }

# expect <image> <data-offset> <name> <t-before> <t-after>: exit 0 = every byte as expected
cat >"$WORK/expect.py" <<'EOF'
import struct, sys, time
img, off, name, t0, t1 = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
d = open(img, 'rb').read()[off:off + 1600 * 512]
if len(d) != 1600 * 512:
    sys.exit('data is %d bytes' % len(d))
def stamp(t):
    tm = time.localtime(t)
    return struct.pack('<HH', (tm.tm_year % 100) << 9 | tm.tm_mon << 5 | tm.tm_mday,
                       tm.tm_hour << 8 | tm.tm_min)
stamps = {stamp(t) for t in range(t0, t1 + 1)}
got_stamp = d[2 * 512 + 0x1C:2 * 512 + 0x20]
if got_stamp not in stamps:
    sys.exit('creation %s not one of %s' % (got_stamp.hex(), sorted(s.hex() for s in stamps)))
exp = bytearray(1600 * 512)
for n, (prev, nxt) in zip(range(2, 6), ((0, 3), (2, 4), (3, 5), (4, 0))):
    struct.pack_into('<HH', exp, n * 512, prev, nxt)
h = 2 * 512
exp[h + 4] = 0xF0 | len(name)
exp[h + 5:h + 5 + len(name)] = name.encode()
exp[h + 0x1C:h + 0x20] = got_stamp
exp[h + 0x20], exp[h + 0x21], exp[h + 0x22], exp[h + 0x23], exp[h + 0x24] = 0, 0, 0xC3, 0x27, 0x0D
struct.pack_into('<HHH', exp, h + 0x25, 0, 6, 1600)
for blk in range(1600):                       # bit map: 1 = free, high bit first
    if blk >= 7:
        exp[6 * 512 + blk // 8] |= 0x80 >> (blk % 8)
if bytes(exp) != d:
    bad = [i for i in range(len(d)) if d[i] != exp[i]]
    sys.exit('%d bytes differ, first at block %d offset %d: got %02x want %02x'
             % (len(bad), bad[0] // 512, bad[0] % 512, d[bad[0]], exp[bad[0]]))
EOF
check_new() {   # check_new <format> <file> <-n arg> <expected name> <data offset>
  local fmt=$1 f=$2 n=$3 want=$4 off=$5 t0 t1
  rm -f "$f"
  t0=$(date +%s)
  if [[ -n "$n" ]]; then
    "$RDEDISKTOOL" create "$f" -f "$fmt" --fs prodos -n "$n" >/dev/null || fail "create $fmt -n '$n'"
  else
    "$RDEDISKTOOL" create "$f" -f "$fmt" --fs prodos >/dev/null || fail "create $fmt (no name)"
  fi
  t1=$(date +%s)
  python3 -I "$WORK/expect.py" "$f" "$off" "$want" "$t0" "$t1" || fail "$fmt '$n': volume bytes"
  python3 -I -B "$REF" check "$f" >/dev/null || fail "$fmt '$n': reference check"
}

check_new 800po "$WORK/a.po" NEWVOL NEWVOL 0; pass
[[ $(stat -c %s "$WORK/a.po") == 819200 ]] || fail "800po size"; pass
check_new 800mg "$WORK/a.2mg" NEWVOL NEWVOL 64; pass
[[ $(stat -c %s "$WORK/a.2mg") == 819264 ]] || fail "800mg size"; pass
python3 -I - "$WORK/a.2mg" <<'EOF' || fail "800mg header"
import struct, sys
d = open(sys.argv[1], 'rb').read()
sys.exit(0 if struct.unpack_from('<4s4sHHIIIIIIIII', d, 0) ==
         (b'2IMG', b'RDET', 64, 1, 1, 0, 1600, 64, 819200, 0, 0, 0, 0) and d[0x30:0x40] == bytes(16) else 1)
EOF
pass
# names: lower case is stored in upper case, default BLANK, 15 characters allowed
check_new 800po "$WORK/b.po" "my.disk" MY.DISK 0; pass
check_new 800po "$WORK/c.po" "" BLANK 0; pass
check_new 800po "$WORK/d.po" "abcdefghijklmno" ABCDEFGHIJKLMNO 0; pass

# a file round trip on the new volume
head -c 70000 /dev/urandom >"$WORK/f.bin"
"$RDEDISKTOOL" add "$WORK/a.2mg" "$WORK/f.bin" FILE >/dev/null || fail "add"
python3 -I -B "$REF" cat "$WORK/a.2mg" FILE "$WORK/f.ref"
cmp -s "$WORK/f.bin" "$WORK/f.ref" || fail "reference reads other bytes"
python3 -I -B "$REF" check "$WORK/a.2mg" >/dev/null || fail "reference check after add"; pass

echo "PASS test_apple_800k_create ($CHECKS checks)"
