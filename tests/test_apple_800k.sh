#!/usr/bin/env bash
# Apple II 3.5" 800K ProDOS-order image (-f 800po, .po of 819,200 bytes).
#
# Expected values come from tests/tools/a2_prodos_ref.py (independent ProDOS
# reader, ProDOS 8 Technical Reference Appendix B; its index-block layout was
# settled on a real 800K volume). Python computes block numbers and sizes.
#   - create gives 1600 blocks, bitmap at block 6, every block past the
#     system area free; files are written past block 280 (the 140K limit).
#   - only .po of exactly 819,200 bytes is 800K; the same bytes as .img/.dsk
#     are not opened as Apple II, other .po sizes are not 800K.
#   - 800K and 5.25" (and Macintosh) images do not convert into each other.
# Optional: real A2 DeskTop 1.5 800K image in A2_REAL_800K_DIR (not
# redistributable, never committed); every file is compared with the reference.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_prodos_ref.py"
A2_REAL_800K_DIR="${A2_REAL_800K_DIR:-$TOOL_ROOT/../resource/AppleII/disk35}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_apple_800k.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
ref() { python3 -I -B "$REF" "$@"; }
rde() { "$RDEDISKTOOL" "$@"; }
refcheck() { ref check "$1" >/dev/null || { ref check "$1"; fail "$2"; }; }
size() { stat -c %s "$1"; }

# --- create
rde create "$WORK/v.po" -f 800po --fs prodos -n BIG >/dev/null || fail "create 800po"
[[ $(size "$WORK/v.po") == 819200 ]] || fail "800po size $(size "$WORK/v.po")"; pass
ref info "$WORK/v.po" >"$WORK/info.txt"
grep -qx "volume.total_blocks=1600" "$WORK/info.txt" || fail "total_blocks"
grep -qx "volume.bitmap_pointer=6" "$WORK/info.txt" || fail "bitmap_pointer"
grep -qx "volume.name=BIG" "$WORK/info.txt" || fail "volume name"; pass
refcheck "$WORK/v.po" "fresh 800K volume inconsistent (bitmap must cover 1600 blocks)"; pass
rde info "$WORK/v.po" >"$WORK/rinfo.txt"
grep -q "^Format: Apple II ProDOS 800K" "$WORK/rinfo.txt" || fail "info format"
grep -q "^File System: ProDOS" "$WORK/rinfo.txt" || fail "info file system"; pass
# free space = (1600 - blocks 0..6) * 512
want=$(python3 -I -c 'print((1600 - 7) * 512)')
grep -q "^Free space: $want bytes" <(rde list "$WORK/v.po") || fail "free space on an empty volume"; pass

# --- add seedling / sapling / tree, subdirectory, rename, delete
python3 -I - "$WORK" <<'EOF'
import random, sys
r = random.Random(800)
for name, n in (('seed', 100), ('sap', 50000), ('tree', 300000), ('sub', 20000), ('gone', 70000)):
    open(sys.argv[1] + '/' + name + '.bin', 'wb').write(bytes(r.randrange(256) for _ in range(n)))
EOF
rde add "$WORK/v.po" "$WORK/seed.bin" SEED >/dev/null || fail "add SEED"
rde add "$WORK/v.po" "$WORK/sap.bin" SAP >/dev/null || fail "add SAP"
rde add "$WORK/v.po" "$WORK/gone.bin" GONE >/dev/null || fail "add GONE"
rde add "$WORK/v.po" "$WORK/tree.bin" TREE >/dev/null || fail "add TREE"
rde mkdir "$WORK/v.po" DIR >/dev/null || fail "mkdir DIR"
rde add "$WORK/v.po" "$WORK/sub.bin" DIR/SUBF >/dev/null || fail "add DIR/SUBF"
refcheck "$WORK/v.po" "volume inconsistent after add"; pass
rde rename "$WORK/v.po" SAP SAPR >/dev/null || fail "rename SAP"
rde delete "$WORK/v.po" GONE >/dev/null || fail "delete GONE"
refcheck "$WORK/v.po" "volume inconsistent after rename/delete"; pass
ref ls "$WORK/v.po" | cut -f1,2 >"$WORK/ls.txt"
printf '/SEED\t1\n/SAPR\t2\n/TREE\t3\n/DIR\t13\n/DIR/SUBF\t2\n' | sort >"$WORK/ls.want"
sort "$WORK/ls.txt" | cmp -s - "$WORK/ls.want" || { cat "$WORK/ls.txt"; fail "entries/storage types"; }; pass
for pair in SEED:seed SAPR:sap TREE:tree DIR/SUBF:sub; do
  p=${pair%%:*}; f=${pair##*:}
  ref cat "$WORK/v.po" "$p" "$WORK/$f.ref"
  cmp -s "$WORK/$f.bin" "$WORK/$f.ref" || fail "$p: reference reads other bytes"
  rde extract "$WORK/v.po" "$p" "$WORK/$f.got" >/dev/null || fail "extract $p"
  cmp -s "$WORK/$f.bin" "$WORK/$f.got" || fail "$p: extracted bytes differ"
done; pass
# blocks past the 140K limit are really in use (otherwise this test proves nothing)
python3 -I -B - "$REF" "$WORK/v.po" <<'EOF' || fail "no file block at or above 280"
import sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
data, _ = A.load(sys.argv[2]); v = A.Volume(data)
used = [n for e in v.entries(2) if e['storage'] in (1, 2, 3)
        for part in v.file_blocks(e) for n in part if n]
sys.exit(0 if max(used) >= 280 and sum(n >= 280 for n in used) > 100 else 1)
EOF
pass
rde validate "$WORK/v.po" | grep -q "0 error(s), 0 warning(s)" || fail "validate"; pass

# --- dump: (track, side, sector) = block (track*2+side)*10+sector
# (a TREE data block on side 1 past block 280: random bytes, found nowhere else)
python3 -I -B - "$REF" "$WORK/v.po" >"$WORK/dump.want" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1].rsplit('/', 1)[0]); import a2_prodos_ref as A
data, _ = A.load(sys.argv[2]); v = A.Volume(data)
e = next(e for e in v.entries(2) if e['path'] == '/TREE')
b = next(n for n in v.file_blocks(e)[1] if n >= 280 and (n % 20) // 10 == 1)
print(b // 20, (b % 20) // 10, b % 10); print(v.block(b).hex())
EOF
read -r t s c < <(head -1 "$WORK/dump.want")
rde dump "$WORK/v.po" -t "$t" --side "$s" -s "$c" >"$WORK/dump.txt" || fail "dump"
python3 -I - "$WORK/dump.txt" "$(sed -n 2p "$WORK/dump.want")" <<'EOF' || fail "dump bytes differ from the block"
import re, sys
got = ''.join(''.join(m.group(1).split()) for m in
              (re.match(r'^[0-9A-F]{6}  ((?:[0-9A-F]{2} ?){8} ((?:[0-9A-F]{2} ?){8}))', l)
               for l in open(sys.argv[1])) if m)
sys.exit(0 if got.lower() == sys.argv[2] else 1)
EOF
pass

# --- detection: extension + exact size only
cp "$WORK/v.po" "$WORK/same.img"; cp "$WORK/v.po" "$WORK/same.dsk"
for f in same.img same.dsk; do
  if rde info "$WORK/$f" 2>&1 | grep -q "Apple II"; then fail "$f opened as Apple II"; fi
done; pass
python3 -I - "$WORK/v.po" "$WORK" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
open(sys.argv[2] + '/short.po', 'wb').write(d[:-512])
open(sys.argv[2] + '/long.po', 'wb').write(d + bytes(512))
EOF
for f in short.po long.po; do
  if rde info "$WORK/$f" 2>&1 | grep -q "800K"; then fail "$f taken as 800K"; fi
  if rde list "$WORK/$f" >/dev/null 2>&1; then fail "$f listed"; fi
done; pass
rde create "$WORK/plain.po" --fs prodos -n SMALL >/dev/null || fail "create .po without -f"
[[ $(size "$WORK/plain.po") == 143360 ]] || fail ".po without -f is not 140K"; pass

# --- create guards
if rde create "$WORK/g1.po" -f 800po -g 35:1:16:256 >/dev/null 2>&1; then fail "-g 35:1:16:256 accepted"; fi
if rde create "$WORK/g2.po" -f 800po -g 80:2:9:512 >/dev/null 2>&1; then fail "-g 80:2:9:512 accepted"; fi
rde create "$WORK/g3.po" -f 800po --fs dos33 >/dev/null 2>"$WORK/g3.err" && fail "dos33 on 800po accepted"
grep -q "Filesystem 'dos33' is not compatible" "$WORK/g3.err" || { cat "$WORK/g3.err"; fail "dos33 refused for another reason"; }
[[ ! -e "$WORK/g1.po" && ! -e "$WORK/g2.po" && ! -e "$WORK/g3.po" ]] || fail "rejected create left a file"
rde create "$WORK/g4.po" -f 800po -g 80:2:10:512 >/dev/null || fail "-g 80:2:10:512 refused"
[[ $(size "$WORK/g4.po") == 819200 ]] || fail "g4 size"; pass

# --- convert: 800K only to 800K
rde convert "$WORK/v.po" "$WORK/copy.po" >/dev/null || fail "convert 800po -> .po"
cmp -s "$WORK/v.po" "$WORK/copy.po" || fail "800po -> .po changed the data"; pass
for out in "x.do:only to 800po" "x.nib:only to 800po" "x.woz:only to 800po" "x.d13:only to 800po" \
           "-f po x2.po:only to 800po" "-f mac_img x.img:Cross-platform" "-f msxdsk x.dsk:Cross-platform"; do
  why=${out#*:}; read -r -a a <<<"${out%%:*}"
  o=${a[-1]}; unset 'a[-1]'
  rde convert "${a[@]}" "$WORK/v.po" "$WORK/$o" >/dev/null 2>"$WORK/cv.err" && fail "convert 800po -> ${out%%:*} accepted"
  grep -q "$why" "$WORK/cv.err" || { cat "$WORK/cv.err"; fail "convert -> ${out%%:*} refused for another reason"; }
  [[ ! -e "$WORK/$o" ]] || fail "rejected convert -> $o left a file"
done; pass
rde convert "$WORK/plain.po" "$WORK/up.po" -f 800po >/dev/null 2>"$WORK/cv.err" && fail "140K -> 800po accepted"
grep -q "800po/800mg hold 800K" "$WORK/cv.err" || { cat "$WORK/cv.err"; fail "140K -> 800po refused for another reason"; }
[[ ! -e "$WORK/up.po" ]] || fail "rejected 140K -> 800po left a file"; pass

# --- ProDOS only: a DOS 3.3 VTOC where DOS 3.3 would look (T17 S0) is not mounted
rde create "$WORK/vt.po" -f 800po >/dev/null || fail "create blank 800po"
python3 -I - "$WORK/vt.po" <<'EOF'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
o = (17 * 2 + 0) * 10 * 512            # track 17, side 0, sector 0
# catalog at T17 S5 (exists here, all zeros = empty catalog): a DOS 3.3 reader would mount it
d[o + 1], d[o + 2], d[o + 3], d[o + 6], d[o + 0x34], d[o + 0x35] = 17, 5, 3, 254, 35, 16
open(p, 'wb').write(d)
EOF
rde list "$WORK/vt.po" >/dev/null 2>&1 && fail "DOS 3.3 mounted on an 800K image"; pass

# --- optional: real A2 DeskTop 1.5 800K volume
real="$A2_REAL_800K_DIR/A2DeskTop-1.5-en_800k.po"
if [[ -f "$real" ]]; then
  cp "$real" "$WORK/real.po"
  n=0
  while IFS=$'\t' read -r path storage _; do
    [[ $storage == 1 || $storage == 2 || $storage == 3 ]] || continue
    ref cat "$WORK/real.po" "$path" "$WORK/exp.bin"
    rde extract "$WORK/real.po" "${path#/}" "$WORK/got.bin" >/dev/null || fail "extract $path"
    cmp -s "$WORK/exp.bin" "$WORK/got.bin" || fail "A2 DeskTop $path differs from the reference"
    n=$((n + 1))
  done < <(ref ls "$WORK/real.po")
  [[ $n -gt 100 ]] || fail "only $n files compared"; pass
  cmp -s "$real" "$WORK/real.po" || fail "reading changed the image"; pass
else
  echo "  (skip: $real missing; not judged)"
fi

echo "PASS test_apple_800k ($CHECKS checks)"
