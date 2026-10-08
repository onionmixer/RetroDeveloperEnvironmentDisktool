#!/usr/bin/env bash
# Manual check (not part of the tests/test_*.sh run): WOZ / NIB / NB2 images
# written by rdedisktool boot on an Apple //e Enhanced in sa2 and behave like
# the DSK they came from.
#   1. DOS 3.3 (workspace diskwork/bootdisk/AppleII/dos33.dsk): boot + CATALOG
#      screen of each format == screen of the DSK
#   2. DOS writes: "SAVE T" on each format, image converted back to .do == the
#      DSK after the same SAVE
#   3. ProDOS (ProDOS_2_4_3.po): boot screen of each format == screen of the PO
#   4. VTOC bitmap: a file added by rdedisktool on a partially used track
#      survives a real DOS BSAVE that allocates on the same track (D2)
#   5. DOS 3.2 (13 sectors, optional images in resource/AppleII/dos32): a file
#      rdedisktool added to a .d13 is read by real DOS 3.2 (BLOAD), and a real
#      DOS 3.2 BSAVE next to it leaves it intact with a consistent bitmap
#   6. DOS 3.2 on 13-sector NIB / WOZ rdedisktool wrote sectors to: BLOAD and
#      BSAVE next to it on a NIB (D2), boot + BLOAD from the System Master WOZ
#   7. DOS 3.2: a file written into never-written sectors (address fields
#      only, real Utility.nib), a created volume (create --fs dos32 -> NIB) and
#      a real master converted .d13 -> WOZ (booted) - BLOAD / BSAVE / CATALOG
#   8. WOZ 2.1 FLUX (optional Applesauce sample, even tracks incl. track 0 as
#      FLUX; sa2 itself does not read FLUX): converted to .po by rdedisktool,
#      it boots to the ProDOS User's Disk menu and runs FILER
# Uses a2run.sh (isolated sa2; never touches the user's display).
# Exit 0 = all match, 1 = mismatch, 3 = not judged (sa2/Xvfb/images missing).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL_ROOT="$(cd "$HERE/../.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
DOS33="$PROJECT_ROOT/diskwork/bootdisk/AppleII/dos33.dsk"
PRODOS="$PROJECT_ROOT/diskwork/bootdisk/AppleII/ProDOS_2_4_3.po"

skip() { echo "SKIP emu_apple_boot_check: $* (not judged)"; exit 3; }
[[ -x "$RDEDISKTOOL" ]] || skip "missing $RDEDISKTOOL"
[[ -f "$DOS33" ]] || skip "missing $DOS33"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_emu_check.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT
export A2RUN_WORK="$WORK"

FAILS=0 CHECKS=0
judge() { CHECKS=$((CHECKS + 1)); if [[ $1 == 0 ]]; then echo "  ok   $2"; else echo "  FAIL $2"; FAILS=$((FAILS + 1)); fi; }
run() {   # run <image> <boot-seconds> [steps] -> prints run dir; rc 3 = environment
  local out rc
  out=$("$HERE/a2run.sh" "$@" 2>"$WORK/a2run.err"); rc=$?
  if [[ $rc == 3 ]]; then cat "$WORK/a2run.err" >&2; skip "a2run could not start an isolated sa2"; fi
  [[ $rc == 0 ]] || { cat "$WORK/a2run.err" >&2; echo ""; return 1; }
  echo "$out"
}

for f in woz nib nb2; do
  "$RDEDISKTOOL" convert "$DOS33" "$WORK/dos.$f" -f "$f" >/dev/null || { echo "convert $f failed"; exit 1; }
done

echo "1. DOS 3.3 boot + CATALOG"
ref=$(run "$DOS33" 15 type:CATALOG) || exit 1
for f in woz nib nb2; do
  r=$(run "$WORK/dos.$f" 15 type:CATALOG)
  python3 -I "$HERE/cmpscreen.py" "$ref/screen.txt" "$r/screen.txt" 20 >"$WORK/cmp.txt"; judge $? "$f CATALOG screen == DSK"
done

echo "2. DOS writes (SAVE)"
export WAIT=8
steps=("type:10 PRINT 12345" "type:SAVE T" "type:CATALOG" "wait:4")
refw=$(run "$DOS33" 15 "${steps[@]}") || exit 1
cmp -s "$DOS33" "$refw/d1.dsk" && { echo "  FAIL DSK was not written (SAVE did not happen)"; exit 1; }
for f in woz nib nb2; do
  r=$(run "$WORK/dos.$f" 15 "${steps[@]}")
  "$RDEDISKTOOL" convert "$r/d1.$f" "$WORK/back_$f.do" -f do >/dev/null
  cmp -s "$refw/d1.dsk" "$WORK/back_$f.do"; judge $? "$f after SAVE == DSK after SAVE"
done
unset WAIT

if [[ -f "$PRODOS" ]]; then
  echo "3. ProDOS boot"
  pref=$(run "$PRODOS" 20) || exit 1
  for f in woz nib nb2; do
    "$RDEDISKTOOL" convert "$PRODOS" "$WORK/pro.$f" -f "$f" >/dev/null
    r=$(run "$WORK/pro.$f" 20)
    python3 -I "$HERE/cmpscreen.py" "$pref/screen.txt" "$r/screen.txt" 3 >"$WORK/cmp.txt"; judge $? "$f ProDOS boot screen == PO"
  done
else
  echo "3. ProDOS boot: SKIP ($PRODOS missing; not judged)"
fi

echo "4. Real DOS allocates next to a file added by rdedisktool"
"$RDEDISKTOOL" create "$WORK/bm.do" -f do --fs dos33 >/dev/null
head -c 2500 /dev/urandom >"$WORK/bm.bin"
"$RDEDISKTOOL" add "$WORK/bm.do" "$WORK/bm.bin" NEW --type B --addr 0x2000 >/dev/null
# DOS searches from the track after the last allocated one: make that track 18
python3 -I -c 'import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read()); o = 17 * 16 * 256
d[o + 0x30], d[o + 0x31] = 19, 0xFF
open(p, "wb").write(d)' "$WORK/bm.do"
export WAIT=8
r=$(D2SRC="$WORK/bm.do" run "$DOS33" 15 'type:BSAVE X,A$2000,L$400,D2' "wait:4")
unset WAIT
if cmp -s "$WORK/bm.do" "$r/d2.do"; then
  judge 1 "D2 was not written (BSAVE did not happen)"
else
  python3 -I "$TOOL_ROOT/tests/tools/a2_nibref.py" dos33-check "$WORK/bm.do" "$r/d2.do" X >"$WORK/cmp.txt"
  judge $? "real DOS BSAVE on track 18 leaves NEW intact, bitmap consistent"
fi

echo "5. Real DOS 3.2 reads a file rdedisktool added to a 13-sector disk and writes next to it"
DOS32_DIR="${DOS32_DIR:-$PROJECT_ROOT/resource/AppleII/dos32}"
if [[ -f "$DOS32_DIR/DOS 3.2 System Master.woz" && -f "$DOS32_DIR/Apple DOS 3.2 Utility.d13" ]]; then
  cp "$DOS32_DIR/Apple DOS 3.2 Utility.d13" "$WORK/u.d13"
  python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(52).randrange(256) for _ in range(2500)))' "$WORK/u.bin"
  "$RDEDISKTOOL" add "$WORK/u.d13" "$WORK/u.bin" NEW32 --type B --addr 0x2000 >/dev/null
  # DOS searches from the track after the last allocated one: make that NEW32's track
  python3 -I - "$WORK/u.d13" <<'EOF'
import sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read()); v = 17 * 13 * 256
sec = lambda t, s: d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
c, t = sec(d[v + 1], d[v + 2]), None
while t is None:
    for i in range(7):
        e = c[0x0B + i * 0x23:0x0B + (i + 1) * 0x23]
        if bytes(x & 0x7F for x in e[3:33]).decode().strip() == 'NEW32':
            t = e[0]
    if t is None:
        c = sec(c[1], c[2])
d[v + 0x30], d[v + 0x31] = t + 1, 0xFF
open(p, 'wb').write(d)
EOF
  # sa2 does not take .d13: drive 2 gets a 13-sector NIB from the independent encoder
  python3 -I "$TOOL_ROOT/tests/tools/a2_nibref.py" make-13 nib "$WORK/u.d13" "$WORK/u.nib"
  export WAIT=8
  r=$(D2SRC="$WORK/u.nib" run "$DOS32_DIR/DOS 3.2 System Master.woz" 25 'type:BLOAD NEW32,D2' \
      'mem:2000,9C4' 'type:BSAVE X32,A$4000,L$600,D2' 'wait:4' 'mem:4000,600')
  unset WAIT
  if [[ -z "$r" ]] || cmp -s "$WORK/u.nib" "$r/d2.nib"; then
    judge 1 "D2 was not written (BSAVE did not happen)"
  else
    cmp -s "$WORK/u.bin" <(head -c 2500 "$r/mem_2000.bin")
    judge $? "real DOS 3.2 BLOAD of the file rdedisktool added == original"
    python3 -I - "$TOOL_ROOT/tests/tools" "$r/d2.nib" "$WORK/u.d13" "$WORK/after.d13" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1]); import a2_nibref as A
nib, before = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
d = bytearray(35 * 13 * 256)
for t in range(35):
    f = A.parse_stream13(list(nib[t * 6656:(t + 1) * 6656]) * 2, t)
    if len(f) != 13:
        sys.exit('track %d: %d sectors' % (t, len(f)))
    for p, s in f.items():
        d[(t * 13 + p) * 256:(t * 13 + p + 1) * 256] = s['data']
open(sys.argv[4], 'wb').write(d)
sec = lambda t, s: d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
v = sec(17, 0)
free = lambda t, s: (((v[0x38 + 4 * t] << 8) | v[0x39 + 4 * t]) >> (s + 3)) & 1
owner, cur, seen = {}, (v[1], v[2]), set()
while cur != (0, 0) and cur not in seen:
    seen.add(cur); owner.setdefault(cur, []).append('catalog'); c = sec(*cur)
    for i in range(7):
        e = c[0x0B + i * 0x23:0x0B + (i + 1) * 0x23]
        if e[0] in (0, 0xFF):
            continue
        ts, ls = (e[0], e[1]), set()
        while ts != (0, 0) and ts not in ls:
            ls.add(ts); owner.setdefault(ts, []).append(i); l = sec(*ts)
            for k in range(122):
                p = (l[0x0C + 2 * k], l[0x0D + 2 * k])
                if p != (0, 0):
                    owner.setdefault(p, []).append(i)
            ts = (l[1], l[2])
    cur = (c[1], c[2])
if any(len(o) > 1 for o in owner.values()) or any(k[0] >= 3 and free(*k) for k in owner) or \
        bytes(d[:3 * 13 * 256]) != before[:3 * 13 * 256]:
    sys.exit('shared sector, used sector marked free, or tracks 0-2 changed')
EOF
    rc=$?
    if [[ $rc == 0 ]]; then
      "$RDEDISKTOOL" extract "$WORK/after.d13" NEW32 "$WORK/new32.out" >/dev/null && cmp -s "$WORK/u.bin" "$WORK/new32.out" &&
        "$RDEDISKTOOL" extract "$WORK/after.d13" X32 "$WORK/x32.out" >/dev/null && cmp -s "$r/mem_4000.bin" "$WORK/x32.out"
      rc=$?
    fi
    judge $rc "real DOS 3.2 BSAVE next to it: both files intact, bitmap consistent, tracks 0-2 kept"
  fi
else
  echo "  (skip 5: DOS 3.2 images missing in $DOS32_DIR; not judged)"
fi

echo "6. Real DOS 3.2 on 13-sector NIB / WOZ images rdedisktool wrote sectors to"
if [[ -f "$DOS32_DIR/DOS 3.2 System Master.woz" && -f "$DOS32_DIR/Apple DOS 3.2 Utility.d13" ]]; then
  # 6a: NIB from the independent encoder, file added by rdedisktool on the NIB
  # itself; the VTOC is then pointed at that file's track (re-encoded with
  # a2_nibref) so the real BSAVE allocates next to it
  python3 -I "$TOOL_ROOT/tests/tools/a2_nibref.py" make-13 nib "$DOS32_DIR/Apple DOS 3.2 Utility.d13" "$WORK/n6.nib"
  "$RDEDISKTOOL" add "$WORK/n6.nib" "$WORK/u.bin" NEW32 --type B --addr 0x2000 >/dev/null
  python3 -I - "$TOOL_ROOT/tests/tools" "$WORK/n6.nib" "$WORK/n6.d13" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1]); import a2_nibref as A
p = sys.argv[2]; nib = bytearray(open(p, 'rb').read())
d = bytearray(35 * 13 * 256); fields = {}
for t in range(35):
    for s, f in A.parse_stream13(list(nib[t * 6656:(t + 1) * 6656]) * 2, t).items():
        d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256] = f['data']; fields[(t, s)] = f['dp']
sec = lambda t, s: d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
v = 17 * 13 * 256; c, t = sec(d[v + 1], d[v + 2]), None
while t is None:
    for i in range(7):
        e = c[0x0B + i * 0x23:0x0B + (i + 1) * 0x23]
        if bytes(x & 0x7F for x in e[3:33]).decode().strip() == 'NEW32':
            t = e[0]
    if t is None:
        c = sec(c[1], c[2])
d[v + 0x30], d[v + 0x31] = t + 1, 0xFF
raw = A.encode53(bytes(sec(17, 0)))
o = 17 * 6656
for j, x in enumerate(raw):
    nib[o + (fields[(17, 0)] + 3 + j) % 6656] = x
open(p, 'wb').write(nib); open(sys.argv[3], 'wb').write(d)
EOF
  export WAIT=8
  r=$(D2SRC="$WORK/n6.nib" run "$DOS32_DIR/DOS 3.2 System Master.woz" 25 'type:BLOAD NEW32,D2' \
      'mem:2000,9C4' 'type:BSAVE X32,A$4000,L$600,D2' 'wait:4' 'mem:4000,600')
  unset WAIT
  if [[ -z "$r" ]] || cmp -s "$WORK/n6.nib" "$r/d2.nib"; then
    judge 1 "6a: D2 was not written (BSAVE did not happen)"
  else
    cmp -s "$WORK/u.bin" <(head -c 2500 "$r/mem_2000.bin")
    judge $? "6a: real DOS 3.2 BLOAD of a file rdedisktool added to a 13-sector NIB == original"
    "$RDEDISKTOOL" extract "$r/d2.nib" NEW32 "$WORK/n6new.out" >/dev/null && cmp -s "$WORK/u.bin" "$WORK/n6new.out" &&
      "$RDEDISKTOOL" extract "$r/d2.nib" X32 "$WORK/n6x.out" >/dev/null && cmp -s "$r/mem_4000.bin" "$WORK/n6x.out" &&
      "$RDEDISKTOOL" validate "$r/d2.nib" >"$WORK/n6v.txt" 2>&1
    judge $? "6a: real DOS 3.2 BSAVE next to it: both files read back, validate clean"
  fi
  # 6b: the Applesauce System Master capture (write-protect flag cleared on a
  # copy), file added by rdedisktool, then booted: the boot tracks are intact
  # and DOS 3.2 loads the file
  python3 -I - "$DOS32_DIR/DOS 3.2 System Master.woz" "$WORK/m6.woz" <<'EOF'
import struct, sys, zlib
d = bytearray(open(sys.argv[1], 'rb').read())
o = d.index(b'INFO') + 8
d[o + 2] = 0
struct.pack_into('<I', d, 8, zlib.crc32(bytes(d[12:])) & 0xFFFFFFFF)
open(sys.argv[2], 'wb').write(bytes(d))
EOF
  head -c 400 "$WORK/u.bin" >"$WORK/m6.bin"
  "$RDEDISKTOOL" add "$WORK/m6.woz" "$WORK/m6.bin" NEW6B --type B --addr 0x2000 >/dev/null
  export WAIT=8
  r=$(run "$WORK/m6.woz" 25 'type:BLOAD NEW6B' 'mem:2000,190')
  unset WAIT
  if [[ -z "$r" ]]; then
    judge 1 "6b: sa2 run failed"
  else
    cmp -s "$WORK/m6.bin" "$r/mem_2000.bin"
    judge $? "6b: boot from the WOZ rdedisktool wrote to, BLOAD of the added file == original"
  fi
else
  echo "  (skip 6: DOS 3.2 images missing in $DOS32_DIR; not judged)"
fi

echo "7. DOS 3.2: never-written sectors, a created volume, a converted master"
if [[ -f "$DOS32_DIR/DOS 3.2 System Master.woz" && -f "$DOS32_DIR/Apple DOS 3.2 Utility.nib" &&
      -f "$DOS32_DIR/Apple DOS 3.2 Utility.d13" ]]; then
  # 7a: real Utility.nib - its free sectors have address fields only (INIT);
  # rdedisktool writes the data fields, real DOS 3.2 reads them and writes on
  cp "$DOS32_DIR/Apple DOS 3.2 Utility.nib" "$WORK/u7.nib"
  python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(71).randrange(256) for _ in range(3000)))' "$WORK/u7.bin"
  "$RDEDISKTOOL" add "$WORK/u7.nib" "$WORK/u7.bin" NEW7 --type B --addr 0x2000 >/dev/null
  export WAIT=8
  r=$(D2SRC="$WORK/u7.nib" run "$DOS32_DIR/DOS 3.2 System Master.woz" 25 'type:BLOAD NEW7,D2' \
      'mem:2000,BB8' 'type:BSAVE X7,A$4000,L$800,D2' 'wait:4' 'mem:4000,800')
  unset WAIT
  if [[ -z "$r" ]] || cmp -s "$WORK/u7.nib" "$r/d2.nib"; then
    judge 1 "7a: D2 was not written (BSAVE did not happen)"
  else
    cmp -s "$WORK/u7.bin" "$r/mem_2000.bin"
    judge $? "7a: BLOAD of a file rdedisktool wrote into never-written sectors == original"
    "$RDEDISKTOOL" extract "$r/d2.nib" NEW7 "$WORK/n7.out" >/dev/null && cmp -s "$WORK/u7.bin" "$WORK/n7.out" &&
      "$RDEDISKTOOL" extract "$r/d2.nib" X7 "$WORK/x7.out" >/dev/null && cmp -s "$r/mem_4000.bin" "$WORK/x7.out" &&
      "$RDEDISKTOOL" validate "$r/d2.nib" >/dev/null 2>&1
    judge $? "7a: real DOS 3.2 BSAVE next to it: both files read back, validate clean"
  fi
  # 7b: create --fs dos32 -> NIB, file added by rdedisktool, used in D2
  "$RDEDISKTOOL" create "$WORK/c7.d13" -f d13 --fs dos32 >/dev/null &&
    "$RDEDISKTOOL" convert "$WORK/c7.d13" "$WORK/c7.nib" -f nib >/dev/null &&
    "$RDEDISKTOOL" add "$WORK/c7.nib" "$WORK/u7.bin" NEW7 --type B --addr 0x2000 >/dev/null
  export WAIT=8
  r=$(D2SRC="$WORK/c7.nib" run "$DOS32_DIR/DOS 3.2 System Master.woz" 25 'type:CATALOG,D2' \
      'type:BLOAD NEW7,D2' 'mem:2000,BB8' 'type:BSAVE X7,A$4000,L$800,D2' 'wait:4' 'mem:4000,800')
  unset WAIT
  if [[ -z "$r" ]] || cmp -s "$WORK/c7.nib" "$r/d2.nib"; then
    judge 1 "7b: D2 was not written (BSAVE did not happen)"
  else
    grep -q "DISK VOLUME 254" "$r/screen.txt" && grep -q "NEW7" "$r/screen.txt" &&
      cmp -s "$WORK/u7.bin" "$r/mem_2000.bin"
    judge $? "7b: created DOS 3.2 volume (NIB): CATALOG lists the file, BLOAD == original"
    "$RDEDISKTOOL" extract "$r/d2.nib" X7 "$WORK/x7b.out" >/dev/null && cmp -s "$r/mem_4000.bin" "$WORK/x7b.out" &&
      "$RDEDISKTOOL" extract "$r/d2.nib" NEW7 "$WORK/n7b.out" >/dev/null && cmp -s "$WORK/u7.bin" "$WORK/n7b.out"
    judge $? "7b: real DOS 3.2 BSAVE on it: both files read back"
  fi
  # 7c: real Utility.d13 converted to WOZ, file added, booted from it
  "$RDEDISKTOOL" convert "$DOS32_DIR/Apple DOS 3.2 Utility.d13" "$WORK/u7.woz" -f woz >/dev/null &&
    "$RDEDISKTOOL" add "$WORK/u7.woz" "$WORK/u7.bin" NEW7 --type B --addr 0x2000 >/dev/null
  export WAIT=8
  r=$(run "$WORK/u7.woz" 25 'type:BLOAD NEW7' 'mem:2000,BB8')
  unset WAIT
  [[ -n "$r" ]] && cmp -s "$WORK/u7.bin" "$r/mem_2000.bin"
  judge $? "7c: boot from Utility.d13 converted to WOZ, BLOAD of the added file == original"
else
  echo "  (skip 7: DOS 3.2 images missing in $DOS32_DIR; not judged)"
fi

echo "8. WOZ 2.1 FLUX tracks read by rdedisktool: the converted disk boots"
FLUX_DEFAULT="$PROJECT_ROOT/resource/AppleII/woz_flux/ProDOS User's Disk - Disk 1, Side A.woz"
FLUX_WOZ="${FLUX_WOZ:-$FLUX_DEFAULT}"
if [[ -f "$FLUX_WOZ" ]]; then
  "$RDEDISKTOOL" convert "$FLUX_WOZ" "$WORK/flux.po" -f po >/dev/null
  judge $? "8: FLUX WOZ -> .po, every sector read"
  export WAIT=10
  r=$(run "$WORK/flux.po" 30)
  r2=$(run "$WORK/flux.po" 30 'type:F')
  unset WAIT
  [[ -n "$r" && -n "$r2" ]] && grep -q "PRODOS USER'S DISK" "$r/screen.txt" &&
    grep -q "PLEASE SELECT ONE OF THE ABOVE" "$r/screen.txt" && grep -q "FILER   VERSION 1.0" "$r2/screen.txt"
  judge $? "8: boots to the ProDOS User's Disk menu, FILER loads"
else
  echo "  (skip 8: $FLUX_WOZ missing; not judged)"
fi

if [[ $FAILS == 0 ]]; then
  echo "PASS emu_apple_boot_check ($CHECKS checks)"
  exit 0
fi
echo "FAIL emu_apple_boot_check ($FAILS of $CHECKS)"
exit 1
