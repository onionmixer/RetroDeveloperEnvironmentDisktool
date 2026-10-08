#!/usr/bin/env bash
# DOS 3.2 (13 sectors): create a volume, convert .d13 <-> 13-sector NIB/NB2/WOZ,
# keep VTOC bytes rdedisktool does not model, search with any VTOC direction.
#
# Expected values (independent of rdedisktool):
#   - create: the VTOC / catalog real DOS 3.2 INIT wrote (System Master booted
#     in sa2, INIT HELLO on a blank disk, read with tests/tools/a2_nibref.py):
#     VTOC 00: 02 11 0C 02 .. 06: FE .. 27: 7A .. 30: 12 01 .. 34: 23 0D 00 01,
#     tracks 0-2 and 17 in use, catalog 17/12 -> .. -> 17/1. HELLO had taken
#     two sectors of track 18 (30: 12); before it the search starts at 17 (+1).
#     The DOS image on tracks 0-2 is not written (they stay marked in use).
#   - convert to NIB/WOZ: the track layout of the Applesauce capture "DOS 3.2
#     System Master.woz" (a2_nibref.track_units13_real; checked here against
#     that capture bit for bit when it is present): 16 nine-bit syncs, then
#     per sector in physical order 0,10,7,4,1,11,8,5,2,12,9,6,3 address field
#     + DE AA EB, 14 syncs, data field + DE AA EB, 28 syncs.
#   - VTOC: add keeps bytes outside the modelled fields (DOS 3.2 masters have
#     02 in byte 0, DOS 3.1 masters values in 04-2E; before, add zeroed them).
#   - VTOC allocation direction 0 (synthetic disks, other tools): a file is
#     still added (before: "Failed to write").
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_nibref.py"
A2_REAL_D13_DIR="${A2_REAL_D13_DIR:-$TOOL_ROOT/../resource/AppleII/dos32}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_d13_create.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }
py() { python3 -I -B "$WORK/c.py" "$SCRIPT_DIR/tools" "$@"; }

cat >"$WORK/c.py" <<'EOF'
# c.py <tools> <cmd> ...
import sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A
cmd, a = sys.argv[2], sys.argv[3:]
sec = lambda d, t, s: d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]

def woz_tracks(p):
    w = A.read_woz(p)
    return w, [A.woz_track(w, t) for t in range(35)]

if cmd == 'expected-create':                     # out.d13
    d = bytearray(35 * 13 * 256)
    v = bytearray(256)                           # measured VTOC (INIT), 30: 11 = before HELLO
    for off, val in {0x00: 0x02, 0x01: 0x11, 0x02: 0x0C, 0x03: 0x02, 0x06: 0xFE, 0x27: 0x7A,
                     0x30: 0x11, 0x31: 0x01, 0x34: 0x23, 0x35: 0x0D, 0x36: 0x00, 0x37: 0x01}.items():
        v[off] = val
    for t in range(35):
        v[0x38 + 4 * t:0x3A + 4 * t] = b'\x00\x00' if t in (0, 1, 2, 17) else b'\xff\xf8'
    d[17 * 13 * 256:17 * 13 * 256 + 256] = v
    for s in range(1, 13):
        if s > 1:
            o = (17 * 13 + s) * 256
            d[o + 1], d[o + 2] = 17, s - 1
    open(a[0], 'wb').write(d)
elif cmd == 'ref':                               # kind d13 out: a2_nibref real layout
    sys.exit(A.make_13(a[0], open(a[1], 'rb').read(), a[2]))
elif cmd == 'same-woz':                          # x.woz y.woz: every track bit-identical, boot format 2
    (wx, x), (wy, y) = woz_tracks(a[0]), woz_tracks(a[1])
    bad = [t for t in range(35) if x[t] != y[t]]
    assert not bad, 'tracks differ: %s' % bad[:5]
    assert wx['info'][38] == wy['info'][38] == 2, 'boot sector format'
    assert wx['tmap'] == wy['tmap'], 'TMAP'
elif cmd == 'decode':                            # img out.d13: independent reader (all 455 sectors)
    p = a[0]
    if p.endswith('.woz'):
        trs = [[v for v, _ in A.lss(b, 2)] for b in woz_tracks(p)[1]]
    else:
        d = open(p, 'rb').read(); n = len(d) // 35
        trs = [list(d[t * n:(t + 1) * n]) * 2 for t in range(35)]
    out = bytearray(35 * 13 * 256)
    for t, nl in enumerate(trs):
        f = A.parse_stream13(nl, t)
        assert len(f) == 13, 'track %d: %d sectors' % (t, len(f))
        for s, x in f.items():
            out[(t * 13 + s) * 256:(t * 13 + s + 1) * 256] = x['data']
    open(a[1], 'wb').write(out)
elif cmd == 'vtoc-kept':                         # before after.d13|.dsk spt: bytes outside the model unchanged
    spt = int(a[2]); b, d = open(a[0], 'rb').read(), open(a[1], 'rb').read()
    o = 17 * spt * 256
    model = {1, 2, 3, 6, 0x27, 0x30, 0x31, 0x34, 0x35, 0x36, 0x37} | set(range(0x38, 0x38 + 4 * 35))
    diff = [i for i in range(256) if i not in model and b[o + i] != d[o + i]]
    assert not diff, 'VTOC bytes changed: %s' % [hex(i) for i in diff]
    assert any(b[o + i] for i in range(256) if i not in model), 'nothing to keep (bad fixture)'
elif cmd == 'set-alloc':                         # img track dir
    d = bytearray(open(a[0], 'rb').read()); o = 17 * 13 * 256
    d[o + 0x30], d[o + 0x31] = int(a[1]), int(a[2]) & 0xFF
    open(a[0], 'wb').write(d)
EOF

# --- create: byte-identical to what real DOS 3.2 INIT writes (minus the DOS image)
rde create "$WORK/c.d13" -f d13 --fs dos32 >/dev/null || fail "create --fs dos32"
py expected-create "$WORK/exp.d13"
cmp -s "$WORK/exp.d13" "$WORK/c.d13" || fail "created DOS 3.2 volume != measured INIT layout"
rde info "$WORK/c.d13" | grep -q "File System: DOS 3.2" || fail "created volume not DOS 3.2"
want=$(python3 -I -c 'print(31 * 13 * 256)')
rde info "$WORK/c.d13" | grep -q "Free Space: $want bytes" || fail "free space != $want"
pass
# only d13 + dos32 go together
for bad in "x.d13 -f d13 --fs dos33" "x.do -f do --fs dos32" "x.nib -f nib --fs dos32" "x.po -f po --fs dos32"; do
  # shellcheck disable=SC2086
  set -- $bad
  rde create "$WORK/$1" "${@:2}" >"$WORK/out" 2>&1 && fail "create $bad accepted"
  grep -q "not compatible" "$WORK/out" || { cat "$WORK/out"; fail "create $bad: reason"; }
  [[ ! -e "$WORK/$1" ]] || fail "create $bad wrote a file"
done; pass

# --- the reference layout is the real capture (checks a2_nibref against hardware)
SM="$A2_REAL_D13_DIR/DOS 3.2 System Master.woz"
if [[ -f "$SM" ]]; then
  py decode "$SM" "$WORK/sm.d13"
  py ref realwoz "$WORK/sm.d13" "$WORK/sm_ref.woz"
  py same-woz "$SM" "$WORK/sm_ref.woz" || fail "a2_nibref real layout != the capture"
  pass
  # rdedisktool: capture -> .d13 -> WOZ gives the capture's bits back
  rde convert "$SM" "$WORK/sm_rde.d13" -f d13 >/dev/null || fail "capture -> d13"
  cmp -s "$WORK/sm.d13" "$WORK/sm_rde.d13" || fail "capture -> d13 != independent reader"
  rde convert "$WORK/sm_rde.d13" "$WORK/sm_rde.woz" -f woz >/dev/null || fail "d13 -> woz"
  py same-woz "$SM" "$WORK/sm_rde.woz" || fail "d13 -> woz != the capture"
  pass
else
  echo "  (skip: $SM missing; not judged)"
fi

# --- convert: random sector data, every format = the reference, and back
python3 -I -c 'import random, sys
r = random.Random(13)
open(sys.argv[1], "wb").write(bytes(r.randrange(256) for _ in range(116480)))' "$WORK/rnd.d13"
for k in nib nb2 woz; do
  rde convert "$WORK/rnd.d13" "$WORK/rnd.$k" -f "$k" >/dev/null || fail "d13 -> $k"
  py ref "real$k" "$WORK/rnd.d13" "$WORK/ref.$k"
  if [[ $k == woz ]]; then
    py same-woz "$WORK/ref.woz" "$WORK/rnd.woz" || fail "d13 -> woz != reference"
  else
    cmp -s "$WORK/ref.$k" "$WORK/rnd.$k" || fail "d13 -> $k != reference"
  fi
  py decode "$WORK/rnd.$k" "$WORK/dec_$k.d13"
  cmp -s "$WORK/rnd.d13" "$WORK/dec_$k.d13" || fail "$k: independent reader != source"
  rde convert "$WORK/rnd.$k" "$WORK/back_$k.d13" -f d13 >/dev/null || fail "$k -> d13"
  cmp -s "$WORK/rnd.d13" "$WORK/back_$k.d13" || fail "$k -> d13 not byte-identical"
  pass
done
# a created volume, converted, takes a file on every format and reads it back
python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(5).randrange(256) for _ in range(5000)))' "$WORK/f.bin"
for k in d13 nib woz; do
  img="$WORK/vol.$k"
  if [[ $k == d13 ]]; then cp "$WORK/c.d13" "$img"; else rde convert "$WORK/c.d13" "$img" -f "$k" >/dev/null; fi
  rde add "$img" "$WORK/f.bin" FILE --type B --addr 0x2000 >/dev/null || fail "$k: add on a created volume"
  rde extract "$img" FILE "$WORK/got_$k" >/dev/null && cmp -s "$WORK/f.bin" "$WORK/got_$k" || fail "$k: extract"
  [[ $k == d13 ]] || { py decode "$img" "$WORK/vol_$k.d13"; cmp -s "$WORK/vol.d13" "$WORK/vol_$k.d13" || fail "$k: sectors != the .d13 path"; }
  pass
done

# --- VTOC bytes outside the modelled fields survive add
for m in "Apple DOS 3.2 Utility.d13" "Apple DOS 3.1 Master.d13"; do
  f="$A2_REAL_D13_DIR/$m"
  if [[ -f "$f" ]]; then
    cp "$f" "$WORK/v.d13"
    rde add "$WORK/v.d13" "$WORK/f.bin" VTOCTEST --type B --addr 0x2000 >/dev/null || fail "$m: add"
    py vtoc-kept "$f" "$WORK/v.d13" 13 || fail "$m: VTOC bytes lost"
    pass
  else
    echo "  (skip: $f missing; not judged)"
  fi
done
DOS33="$TOOL_ROOT/../diskwork/bootdisk/AppleII/dos33.dsk"
if [[ -f "$DOS33" ]]; then
  cp "$DOS33" "$WORK/v.dsk"
  rde --bootdisk-mode off add "$WORK/v.dsk" "$WORK/f.bin" VTOCTEST --type B --addr 0x2000 >/dev/null || fail "dos33: add"
  py vtoc-kept "$DOS33" "$WORK/v.dsk" 16 || fail "dos33: VTOC byte 0 lost"
  pass
else
  echo "  (skip: $DOS33 missing; not judged)"
fi

# --- VTOC allocation direction 0: still searched (upwards)
mkdir -p "$WORK/exp"
python3 -I -B "$REF" make-d13-fs "$WORK/dir0.d13" 7 "$WORK/exp"
py set-alloc "$WORK/dir0.d13" 17 0
rde add "$WORK/dir0.d13" "$WORK/f.bin" DIRZERO --type B --addr 0x2000 >/dev/null || fail "direction 0: add"
rde extract "$WORK/dir0.d13" DIRZERO "$WORK/dz" >/dev/null && cmp -s "$WORK/f.bin" "$WORK/dz" || fail "direction 0: extract"
pass

echo "PASS test_apple_d13_create_convert ($CHECKS checks)"
