#!/usr/bin/env bash
# DOS 3.3 file contents (add/extract), file type names on DOS 3.3 and ProDOS,
# T/S list chains, and WOZ1 output being written as WOZ2.
#
# The on-disk layout expected here was taken from files written by real DOS
# 3.3 under sa2 (see PLAN_APPLE_DOS33_FILES.md B-1): B = address(2) +
# length(2) + body, A/I = length(2) + body, T ends at the first $00, and every
# T/S list sector holds the file-relative number of its first sector at +5.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_dos33files.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
same() { cmp -s "$1" "$2" || fail "$3: $1 differs from $2"; pass; }
rc_of() {
  set +e
  "$@" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc -lt 128 ]] || { cat "$WORK/out.log" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}
# DOS 3.3 catalog reader (independent of rdedisktool): prints
# name type sectors first-bytes(hex) ts-offsets
dosdump() {
  python3 -I - "$1" "$2" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
want = sys.argv[2]
sec = lambda t, s: d[(t * 16 + s) * 256:(t * 16 + s + 1) * 256]
v = sec(17, 0)
t, s = v[1], v[2]
while (t, s) != (0, 0):
    c = sec(t, s)
    for e in range(7):
        x = c[0x0B + e * 35:0x0B + e * 35 + 35]
        if x[0] in (0, 0xFF):
            continue
        name = bytes(b & 0x7F for b in x[3:33]).decode().rstrip()
        if name != want:
            continue
        lt, ls, offs, first = x[0], x[1], [], None
        while (lt, ls) != (0, 0):
            ts = sec(lt, ls)
            offs.append(ts[5] | ts[6] << 8)
            if first is None:
                first = sec(ts[12], ts[13])[:6].hex()
            lt, ls = ts[1], ts[2]
        print(f"type={x[2]:02x} sectors={x[33] | x[34] << 8} first={first} offsets={','.join(map(str, offs))}")
    t, s = c[1], c[2]
EOF
}

python3 -I -c '
import random, sys
r = random.Random(7)
w = lambda n, b: open(sys.argv[1] + "/" + n, "wb").write(b)
w("body.bin", bytes(r.randrange(1, 256) for _ in range(203)))
w("zeros.bin", bytes([0x41]) * 100 + bytes(50))
w("looks_like_hdr.bin", bytes([0x00, 0x03, 0x05, 0x00, 1, 2, 3, 4, 5]))
w("text.txt", bytes(c | 0x80 for c in b"HELLO\rWORLD\r"))
w("big.bin", bytes(r.randrange(256) for _ in range(32000)))
' "$WORK"

"$RDEDISKTOOL" create "$WORK/d.do" -f do --fs dos33 >/dev/null

# 1. B: header added, body extracted back; default address $2000 with a warning
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/body.bin" BADDR --type B --addr 0x0803 >/dev/null
[[ "$(dosdump "$WORK/d.do" BADDR)" == "type=04 sectors=2 first=0308cb00"* ]] || fail "B header (addr \$0803, len 203)"; pass
"$RDEDISKTOOL" extract "$WORK/d.do" BADDR "$WORK/x_baddr" >/dev/null
same "$WORK/body.bin" "$WORK/x_baddr" "B body"
"$RDEDISKTOOL" extract --raw "$WORK/d.do" BADDR "$WORK/x_baddr.raw" >/dev/null
[[ $(stat -c %s "$WORK/x_baddr.raw") == 256 ]] || fail "--raw B = whole data sector"; pass

[[ $(rc_of "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/body.bin" NOTYPE) == 0 ]] || fail "add without --type"
grep -q "using \$2000" "$WORK/out.log" || fail "default address warning"; pass
[[ "$(dosdump "$WORK/d.do" NOTYPE)" == "type=04 sectors=2 first=0020cb00"* ]] || fail "default type B at \$2000"; pass

# data that merely looks like a header is still the body
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/looks_like_hdr.bin" BHDR --type B --addr 0x0300 >/dev/null
"$RDEDISKTOOL" extract "$WORK/d.do" BHDR "$WORK/x_bhdr" >/dev/null
same "$WORK/looks_like_hdr.bin" "$WORK/x_bhdr" "header-like body kept"

# 2. A/I: 2-byte length
for t in A I; do
  "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/body.bin" "P$t" --type "$t" >/dev/null
  [[ "$(dosdump "$WORK/d.do" "P$t")" == *" first=cb00"* ]] || fail "$t length header"; pass
  "$RDEDISKTOOL" extract "$WORK/d.do" "P$t" "$WORK/x_$t" >/dev/null
  same "$WORK/body.bin" "$WORK/x_$t" "$t body"
done

# 3. T: type code 0, read up to the first $00
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/text.txt" TX --type T >/dev/null
[[ "$(dosdump "$WORK/d.do" TX)" == "type=00 "* ]] || fail "--type T stored as T"; pass
"$RDEDISKTOOL" extract "$WORK/d.do" TX "$WORK/x_tx" >/dev/null
same "$WORK/text.txt" "$WORK/x_tx" "T body"
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/zeros.bin" TZ --type T) == 0 ]] || fail "T with \$00"
grep -q "sequential reads stop there" "$WORK/out.log" || fail "T with \$00 warns"; pass

# 4. S: DOS keeps no length -> all data sectors, nothing trimmed
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/zeros.bin" SZ --type S >/dev/null
"$RDEDISKTOOL" extract "$WORK/d.do" SZ "$WORK/x_sz" >/dev/null
python3 -I -c 'import sys; a=open(sys.argv[1],"rb").read(); b=open(sys.argv[2],"rb").read(); sys.exit(0 if len(b)==256 and b[:150]==a and not any(b[150:]) else 1)' \
  "$WORK/zeros.bin" "$WORK/x_sz" || fail "S keeps trailing zeros (whole sector)"; pass

# 5. --raw add stores the DOS bytes unchanged; bad header rejected
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/x_baddr.raw" RAWB --type B --raw >/dev/null
"$RDEDISKTOOL" extract "$WORK/d.do" RAWB "$WORK/x_rawb" >/dev/null
same "$WORK/body.bin" "$WORK/x_rawb" "raw B round trip"
python3 -I -c 'import sys; open(sys.argv[1],"wb").write(bytes([0,8,0xff,0x7f,1,2,3]))' "$WORK/badhdr.bin"
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/badhdr.bin" BAD --type B --raw) != 0 ]] || fail "raw B with short data accepted"
grep -q "header length exceeds" "$WORK/out.log" || fail "raw header message"; pass

# 6. Types not available on DOS 3.3; ProDOS names map to DOS types
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/d.do" "$WORK/body.bin" SYSF --type SYS) != 0 ]] || fail "SYS on DOS 3.3 accepted"
grep -q "not available on DOS 3.3" "$WORK/out.log" || fail "SYS message"; pass
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/text.txt" TXTN --type TXT >/dev/null
[[ "$(dosdump "$WORK/d.do" TXTN)" == "type=00 "* ]] || fail "TXT -> T on DOS 3.3"; pass

# 7. More than 122 data sectors: second T/S list holds offset 122; body intact
"$RDEDISKTOOL" add "$WORK/d.do" "$WORK/big.bin" BIG --type B --addr 0x0800 >/dev/null
[[ "$(dosdump "$WORK/d.do" BIG)" == "type=04 sectors=128 "*" offsets=0,122" ]] || fail "T/S list chain offsets"; pass
"$RDEDISKTOOL" extract "$WORK/d.do" BIG "$WORK/x_big" >/dev/null
same "$WORK/big.bin" "$WORK/x_big" "big B body"

# 8. Sparse file: a (0,0) hole inside the T/S list reads as a zero sector
python3 -I - "$WORK/d.do" <<'EOF'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
off = lambda t, s: (t * 16 + s) * 256
v = d[off(17, 0):off(17, 0) + 256]
t, s, done = v[1], v[2], False
while (t, s) != (0, 0) and not done:
    c = d[off(t, s):off(t, s) + 256]
    for e in range(7):
        x = c[0x0B + e * 35:0x0B + e * 35 + 35]
        if x[0] not in (0, 0xFF) and bytes(b & 0x7F for b in x[3:33]).decode().rstrip() == 'BIG':
            ts = off(x[0], x[1])
            d[ts + 12 + 2 * 5:ts + 12 + 2 * 5 + 2] = b'\0\0'     # drop data sector 5
            done = True
    t, s = c[1], c[2]
if not done:
    sys.exit('BIG not found in catalog')
open(p, 'wb').write(d)
EOF
"$RDEDISKTOOL" extract --raw "$WORK/d.do" BIG "$WORK/x_big_hole" >/dev/null
python3 -I -c '
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
raw = bytes([0, 8, 0x00, 0x7d]) + a
ok = len(b) == 126 * 256 and b[5*256:6*256] == bytes(256) and b[:5*256] == raw[:5*256] and b[6*256:len(raw)] == raw[6*256:]
sys.exit(0 if ok else 1)' "$WORK/big.bin" "$WORK/x_big_hole" || fail "hole kept in place"; pass

# 9. ProDOS: T/TXT/$04 are TXT, B is BIN, S has no ProDOS equivalent
"$RDEDISKTOOL" create "$WORK/p.po" -f po --fs prodos -n TYPES >/dev/null
for t in T TXT '$04' B; do
  "$RDEDISKTOOL" add "$WORK/p.po" "$WORK/text.txt" "F$(echo "$t" | tr -dc 'A-Z0-9')" --type "$t" >/dev/null
done
python3 -I - "$WORK/p.po" <<'EOF' || fail "ProDOS type codes"
import sys
d = open(sys.argv[1], 'rb').read()
b = d[2 * 512:3 * 512]
types = {}
for i in range(1, 13):
    e = b[4 + i * 0x27:4 + (i + 1) * 0x27]
    if e[0] >> 4:
        types[e[1:1 + (e[0] & 15)].decode()] = e[0x10]
want = {'FT': 0x04, 'FTXT': 0x04, 'F04': 0x04, 'FB': 0x06}
sys.exit(0 if all(types.get(k) == v for k, v in want.items()) else 1)
EOF
pass
[[ $(rc_of "$RDEDISKTOOL" add "$WORK/p.po" "$WORK/text.txt" FS --type S) != 0 ]] || fail "S on ProDOS accepted"; pass

# 10. WOZ1 output is written as WOZ2
[[ $(rc_of "$RDEDISKTOOL" create "$WORK/w.woz" -f woz1) == 0 ]] || fail "create -f woz1"
grep -q "WOZ1 output is not supported; writing WOZ2" "$WORK/out.log" || fail "woz1 create warning"; pass
grep -q "Format: Apple II WOZ v2" "$WORK/out.log" || fail "woz1 create reports WOZ v2"; pass
[[ $(head -c 4 "$WORK/w.woz") == WOZ2 ]] || fail "create -f woz1 magic"; pass
[[ $(rc_of "$RDEDISKTOOL" convert "$WORK/d.do" "$WORK/c.woz" -f woz1) == 0 ]] || fail "convert -f woz1"
grep -q "writing WOZ2" "$WORK/out.log" && grep -q "WOZ v2" "$WORK/out.log" || fail "woz1 convert messages"; pass
[[ $(head -c 4 "$WORK/c.woz") == WOZ2 ]] || fail "convert -f woz1 magic"; pass
for verb in create convert; do
  if [[ $verb == create ]]; then cmd=(create "$WORK/p.woz" -f woz --force); else cmd=(convert "$WORK/d.do" "$WORK/p.woz" -f woz); fi
  [[ $(rc_of "$RDEDISKTOOL" "${cmd[@]}") == 0 ]] || fail "$verb -f woz"
  ! grep -q "WOZ1" "$WORK/out.log" || fail "$verb -f woz must not mention WOZ1"; pass
  grep -q "WOZ v2" "$WORK/out.log" || fail "$verb -f woz reports WOZ v2"; pass
done

# 11. Only a plausible VTOC makes a disk DOS 3.3 (no fallback onto blank/foreign disks)
"$RDEDISKTOOL" create "$WORK/blank.do" -f do >/dev/null
cp "$WORK/blank.do" "$WORK/blank_before.do"
for cmd in list add; do
  if [[ $cmd == list ]]; then args=(list "$WORK/blank.do"); else args=(add "$WORK/blank.do" "$WORK/body.bin" X --type B --addr 0x0800); fi
  [[ $(rc_of "$RDEDISKTOOL" "${args[@]}") != 0 ]] || { cat "$WORK/out.log" >&2; fail "$cmd on blank image must fail"; }
  grep -q "No DOS 3.3 or ProDOS file system found" "$WORK/out.log" || fail "$cmd on blank image message"; pass
done
cmp -s "$WORK/blank.do" "$WORK/blank_before.do" || fail "blank image changed"; pass

vtoc_patch() {   # <image> <offset> <byte>
  python3 -I -c 'import sys
p, o, v = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
d = bytearray(open(p, "rb").read()); d[17 * 16 * 256 + o] = v; open(p, "wb").write(d)' "$@"
}
"$RDEDISKTOOL" create "$WORK/v.do" -f do --fs dos33 >/dev/null
"$RDEDISKTOOL" add "$WORK/v.do" "$WORK/body.bin" KEEP --type B --addr 0x0800 >/dev/null
cp "$WORK/v.do" "$WORK/v_tracks0.do"; vtoc_patch "$WORK/v_tracks0.do" 0x34 0
[[ $(rc_of "$RDEDISKTOOL" list "$WORK/v_tracks0.do") != 0 ]] || fail "VTOC with 0 tracks accepted"; pass
cp "$WORK/v.do" "$WORK/v_cat0.do"; vtoc_patch "$WORK/v_cat0.do" 0x01 0
[[ $(rc_of "$RDEDISKTOOL" list "$WORK/v_cat0.do") != 0 ]] || fail "VTOC with catalog track 0 accepted"; pass
# volume 0 fails strict detection but is still a valid DOS 3.3 VTOC: keep reading it
cp "$WORK/v.do" "$WORK/v_vol0.do"; vtoc_patch "$WORK/v_vol0.do" 0x06 0
"$RDEDISKTOOL" info "$WORK/v_vol0.do" | grep -q "File System: Unknown" || fail "volume 0 is not strictly detected"; pass
"$RDEDISKTOOL" extract "$WORK/v_vol0.do" KEEP "$WORK/x_keep" >/dev/null || fail "volume 0 disk must stay readable"
same "$WORK/body.bin" "$WORK/x_keep" "file on volume 0 disk"

# a ProDOS-order image named .do: told that the sector order may be wrong
"$RDEDISKTOOL" create "$WORK/pro.po" -f po --fs prodos -n ORDER >/dev/null
cp "$WORK/pro.po" "$WORK/misnamed.do"
[[ $(rc_of "$RDEDISKTOOL" list "$WORK/misnamed.do") != 0 ]] || fail "misnamed image listed"
grep -q "sector order may be wrong" "$WORK/out.log" || fail "misnamed image hint"; pass

echo "PASS test_apple_dos33_files ($CHECKS checks)"
