#!/usr/bin/env bash
# Apple II 800K ProDOS in a 2MG container (.2mg, -f 800mg).
#
# Header layout from MAME (src/lib/formats/ap_dsk35.cpp, apple_2mg_format)
# and AppleWin (source/DiskImageHelper.h, Header2IMG): 64 bytes, little
# endian; 0x0C format (1 = ProDOS order), 0x10 flags (bit 31 = locked),
# 0x14 blocks, 0x18/0x1C data offset/length, 0x20/0x24 comment,
# 0x28/0x2C creator data. Python builds and reads the headers here; file
# contents are checked with tests/tools/a2_prodos_ref.py.
#   - 800po -> 800mg writes a plain header; 800mg -> 800po gives the data back
#   - changing files rewrites only the data range: header, comment, creator
#     data and trailing bytes stay byte-identical
#   - a locked image (bit 31) can be read but not changed
#   - DOS order, nibble, other block counts, bad ranges: refused with a reason
# Optional: real A2 DeskTop 1.5 800K .po/.2mg in A2_REAL_800K_DIR.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
A2_REAL_800K_DIR="${A2_REAL_800K_DIR:-$TOOL_ROOT/../resource/AppleII/disk35}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_apple_2mg.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
ref() { python3 -I -B "$REF" "$@"; }
rde() { "$RDEDISKTOOL" "$@"; }
refcheck() { ref check "$1" >/dev/null || { ref check "$1"; fail "$2"; }; }

# mk2mg <data.po> <out.2mg> key=value...   (header fields, comment=, cdata=, tail=)
cat >"$WORK/mk2mg.py" <<'EOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
f = dict(creator='TEST', hsize=64, ver=1, fmt=1, flags=0, blocks=1600, doff=64,
         dlen=len(data), comment='', cdata='', tail='', coff=None, xoff=None)
for kv in sys.argv[3:]:
    k, v = kv.split('=', 1)
    f[k] = v if k in ('creator', 'comment', 'cdata', 'tail') else int(v, 0)
body = bytearray(b'\0' * f['doff'])
body[f['doff']:] = data
cm, cd = f['comment'].encode(), f['cdata'].encode()
coff = f['coff'] if f['coff'] is not None else (len(body) if cm else 0)
body += cm
xoff = f['xoff'] if f['xoff'] is not None else (len(body) if cd else 0)
body += cd + f['tail'].encode()
struct.pack_into('<4s4sHHIIIIIIIII', body, 0, b'2IMG', f['creator'].encode(), f['hsize'],
                 f['ver'], f['fmt'], f['flags'], f['blocks'], f['doff'], f['dlen'],
                 coff, len(cm), xoff, len(cd))
open(sys.argv[2], 'wb').write(body)
EOF
mk2mg() { python3 -I "$WORK/mk2mg.py" "$@"; }
# outside <a> <b>: bytes outside the 2MG data range are identical (same file size)
outside() {
  python3 -I - "$1" "$2" <<'EOF'
import struct, sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
o = struct.unpack_from('<I', a, 0x18)[0]
sys.exit(0 if len(a) == len(b) and a[:o] == b[:o] and a[o + 819200:] == b[o + 819200:] else 1)
EOF
}
datapart() { python3 -I -c 'import struct, sys
d = open(sys.argv[1], "rb").read(); o = struct.unpack_from("<I", d, 0x18)[0]
open(sys.argv[2], "wb").write(d[o:o + 819200])' "$1" "$2"; }

echo hello >"$WORK/h.txt"
head -c 150000 /dev/urandom >"$WORK/big.bin"
rde create "$WORK/base.po" -f 800po --fs prodos -n MGTEST >/dev/null || fail "create 800po"
rde add "$WORK/base.po" "$WORK/big.bin" BIG >/dev/null || fail "add BIG"

# --- 800po -> 800mg: header written as planned, data unchanged, and back
rde convert "$WORK/base.po" "$WORK/conv.2mg" >/dev/null || fail "convert .po -> .2mg"
python3 -I - "$WORK/conv.2mg" <<'EOF' || fail "new 2MG header"
import struct, sys
d = open(sys.argv[1], 'rb').read()
h = struct.unpack_from('<4s4sHHIIIIIIIII', d, 0)
want = (b'2IMG', b'RDET', 64, 1, 1, 0, 1600, 64, 819200, 0, 0, 0, 0)
sys.exit(0 if h == want and len(d) == 64 + 819200 and d[0x30:0x40] == bytes(16) else 1)
EOF
pass
datapart "$WORK/conv.2mg" "$WORK/conv.data"
cmp -s "$WORK/base.po" "$WORK/conv.data" || fail ".po -> .2mg changed the data"; pass
rde convert "$WORK/conv.2mg" "$WORK/back.po" >/dev/null || fail "convert .2mg -> .po"
cmp -s "$WORK/base.po" "$WORK/back.po" || fail ".2mg -> .po changed the data"; pass
rde convert "$WORK/base.po" "$WORK/explicit.2mg" -f 800mg >/dev/null || fail "convert -f 800mg"
cmp -s "$WORK/conv.2mg" "$WORK/explicit.2mg" || fail "-f 800mg differs from .2mg inference"; pass
rde info "$WORK/conv.2mg" | grep -q "^Format: Apple II ProDOS 800K (2MG)" || fail "info format"; pass

# --- a 2MG with comment, creator data, flags and trailing bytes: only data changes
mk2mg "$WORK/base.po" "$WORK/meta.2mg" flags=0x1FE comment="a comment" cdata="CREATOR-DATA" tail="TAIL"
cp "$WORK/meta.2mg" "$WORK/meta0.2mg"
rde add "$WORK/meta.2mg" "$WORK/h.txt" HELLO >/dev/null || fail "add to 2MG"
rde mkdir "$WORK/meta.2mg" SUB >/dev/null || fail "mkdir on 2MG"
rde add "$WORK/meta.2mg" "$WORK/h.txt" SUB/IN >/dev/null || fail "add SUB/IN"
rde delete "$WORK/meta.2mg" BIG >/dev/null || fail "delete BIG"
outside "$WORK/meta0.2mg" "$WORK/meta.2mg" || fail "bytes outside the data range changed"; pass
refcheck "$WORK/meta.2mg" "reference: 2MG volume inconsistent after changes"; pass
for p in HELLO SUB/IN; do
  ref cat "$WORK/meta.2mg" "$p" "$WORK/exp"
  rde extract "$WORK/meta.2mg" "$p" "$WORK/got" >/dev/null || fail "extract $p"
  cmp -s "$WORK/h.txt" "$WORK/exp" && cmp -s "$WORK/exp" "$WORK/got" || fail "$p contents"
done; pass
rde info -v "$WORK/meta.2mg" >"$WORK/minfo.txt"
grep -q "^2MG Creator: TEST" "$WORK/minfo.txt" && grep -q "^2MG Comment: 9 bytes" "$WORK/minfo.txt" \
  && grep -q "^2MG Creator Data: 12 bytes" "$WORK/minfo.txt" || { cat "$WORK/minfo.txt"; fail "info -v 2MG fields"; }
pass
rde convert "$WORK/meta.2mg" "$WORK/meta.po" >/dev/null 2>"$WORK/cv.err" || fail "convert meta -> .po"
grep -q "not carried over" "$WORK/cv.err" || fail "no warning about dropped 2MG data"
datapart "$WORK/meta.2mg" "$WORK/meta.data"
cmp -s "$WORK/meta.data" "$WORK/meta.po" || fail "meta .2mg -> .po data"; pass
rde convert "$WORK/conv.2mg" "$WORK/plain.po" >/dev/null 2>"$WORK/cv.err" || fail "convert plain"
if grep -q "not carried over" "$WORK/cv.err"; then fail "warning without 2MG data"; fi; pass

# --- data length 0 (AppleWin: then blocks x 512) is read
mk2mg "$WORK/base.po" "$WORK/len0.2mg" dlen=0
ref cat "$WORK/base.po" BIG "$WORK/big.exp"
rde extract "$WORK/len0.2mg" BIG "$WORK/big.got" >/dev/null || fail "extract from length-0 2MG"
cmp -s "$WORK/big.exp" "$WORK/big.got" || fail "length-0 2MG data"; pass

# --- data offset 128 (64 padding bytes after the header): data read from there,
# written back there, padding kept
mk2mg "$WORK/base.po" "$WORK/off128.2mg" doff=128
python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); d[64:128] = b"P" * 64
open(p, "wb").write(d)' "$WORK/off128.2mg"
cp "$WORK/off128.2mg" "$WORK/off128_0.2mg"
rde extract "$WORK/off128.2mg" BIG "$WORK/o.got" >/dev/null && cmp -s "$WORK/big.exp" "$WORK/o.got" \
  || fail "data at offset 128"
rde add "$WORK/off128.2mg" "$WORK/h.txt" HELLO >/dev/null || fail "add to offset-128 2MG"
outside "$WORK/off128_0.2mg" "$WORK/off128.2mg" || fail "offset-128 2MG: bytes outside data changed"
refcheck "$WORK/off128.2mg" "reference: offset-128 2MG inconsistent"; pass

# --- locked (flags bit 31): readable, not changeable
mk2mg "$WORK/base.po" "$WORK/lock.2mg" flags=0x80000000
cp "$WORK/lock.2mg" "$WORK/lock0.2mg"
rde list "$WORK/lock.2mg" | grep -q "^BIG " || fail "list locked 2MG"
rde info "$WORK/lock.2mg" | grep -q "^Write Protected: Yes" || fail "locked 2MG not shown write protected"
rde extract "$WORK/lock.2mg" BIG "$WORK/lk.got" >/dev/null && cmp -s "$WORK/big.exp" "$WORK/lk.got" \
  || fail "extract from locked 2MG"
for op in "add $WORK/lock.2mg $WORK/h.txt NEW" "delete $WORK/lock.2mg BIG" "mkdir $WORK/lock.2mg D" \
          "rename $WORK/lock.2mg BIG BIG2"; do
  read -r -a a <<<"$op"
  rde "${a[@]}" >/dev/null 2>"$WORK/lk.err" && fail "locked 2MG: ${a[0]} accepted"
  grep -q "write protected" "$WORK/lk.err" || { cat "$WORK/lk.err"; fail "locked 2MG: ${a[0]} refused for another reason"; }
done
cmp -s "$WORK/lock0.2mg" "$WORK/lock.2mg" || fail "locked 2MG changed"; pass

# --- refused headers: each with its reason, file untouched
bad() {   # bad <reason-regex> <mk2mg fields...>
  local why=$1; shift
  mk2mg "$WORK/base.po" "$WORK/bad.2mg" "$@"
  cp "$WORK/bad.2mg" "$WORK/bad0.2mg"
  if rde list "$WORK/bad.2mg" >/dev/null 2>"$WORK/bad.err"; then fail "accepted: $*"; fi
  grep -Eq "$why" "$WORK/bad.err" || { cat "$WORK/bad.err"; fail "refused for another reason: $*"; }
  cmp -s "$WORK/bad0.2mg" "$WORK/bad.2mg" || fail "refused image changed: $*"
}
bad "DOS 3.3 sector order" fmt=0
bad "nibble data" fmt=2
bad "280 blocks" blocks=280
bad "data length 819712" dlen=819712
bad "header size 32" hsize=32
bad "header version 2" ver=2
bad "comment range" comment="x" coff=100
bad "creator data range" cdata="y" xoff=8
pass
# data offset past the end: header claims offset 1024 but the file is shorter
python3 -I - "$WORK/conv.2mg" "$WORK/short.2mg" <<'EOF'
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read()); struct.pack_into('<I', d, 0x18, 1024)
open(sys.argv[2], 'wb').write(d)
EOF
rde list "$WORK/short.2mg" >/dev/null 2>"$WORK/bad.err" && fail "data past end accepted"
grep -q "outside the file" "$WORK/bad.err" || { cat "$WORK/bad.err"; fail "data past end: reason"; }; pass
printf 'NOT2MG' >"$WORK/nomagic.2mg"; head -c 819200 /dev/zero >>"$WORK/nomagic.2mg"
if rde list "$WORK/nomagic.2mg" >/dev/null 2>&1; then fail ".2mg without 2IMG accepted"; fi; pass

# --- only 800K <-> 800K
rde create "$WORK/small.po" --fs prodos -n S >/dev/null
for out in "x.do:only to 800po or 800mg" "-f po x.po:only to 800po or 800mg" "-f mac_img x.img:Cross-platform"; do
  why=${out#*:}; read -r -a a <<<"${out%%:*}"
  o=${a[-1]}; unset 'a[-1]'
  rde convert "${a[@]}" "$WORK/conv.2mg" "$WORK/$o" >/dev/null 2>"$WORK/cv.err" && fail "convert 2mg -> ${out%%:*} accepted"
  grep -q "$why" "$WORK/cv.err" || { cat "$WORK/cv.err"; fail "2mg -> ${out%%:*}: reason"; }
  [[ ! -e "$WORK/$o" ]] || fail "rejected convert left $o"
done
rde convert "$WORK/small.po" "$WORK/up.2mg" >/dev/null 2>"$WORK/cv.err" && fail "140K -> .2mg accepted"
grep -q "hold 800K" "$WORK/cv.err" || { cat "$WORK/cv.err"; fail "140K -> .2mg: reason"; }
[[ ! -e "$WORK/up.2mg" ]] || fail "rejected 140K -> .2mg left a file"; pass
rde create "$WORK/d.2mg" -f 800mg --fs dos33 >/dev/null 2>"$WORK/cv.err" && fail "dos33 on 800mg accepted"
grep -q "not compatible" "$WORK/cv.err" || fail "dos33 on 800mg: reason"; pass

# --- optional: real A2 DeskTop 1.5 .2mg
real="$A2_REAL_800K_DIR/A2DeskTop-1.5-en_800k.2mg"
realpo="$A2_REAL_800K_DIR/A2DeskTop-1.5-en_800k.po"
if [[ -f "$real" && -f "$realpo" ]]; then
  cp "$real" "$WORK/r.2mg"
  rde convert "$WORK/r.2mg" "$WORK/r.po" >/dev/null || fail "real .2mg -> .po"
  cmp -s "$WORK/r.po" "$realpo" || fail "real .2mg -> .po differs from the bundled .po"; pass
  n=0
  while IFS=$'\t' read -r path storage _; do
    [[ $storage == 1 || $storage == 2 || $storage == 3 ]] || continue
    ref cat "$WORK/r.2mg" "$path" "$WORK/exp.bin"
    rde extract "$WORK/r.2mg" "${path#/}" "$WORK/got.bin" >/dev/null || fail "extract $path"
    cmp -s "$WORK/exp.bin" "$WORK/got.bin" || fail "A2 DeskTop $path differs from the reference"
    n=$((n + 1))
  done < <(ref ls "$WORK/r.2mg")
  [[ $n -gt 100 ]] || fail "only $n files compared"; pass
  rde --bootdisk-mode strict add "$WORK/r.2mg" "$WORK/h.txt" NOTE.TXT >/dev/null || fail "strict add on real .2mg"
  outside "$real" "$WORK/r.2mg" || fail "real .2mg header changed"
  refcheck "$WORK/r.2mg" "reference: real .2mg inconsistent after add"; pass
else
  echo "  (skip: $real missing; not judged)"
fi

echo "PASS test_apple_800k_2mg ($CHECKS checks)"
