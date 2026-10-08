#!/usr/bin/env bash
# Writing sectors on non-standard NIB / WOZ tracks: only the data field changes.
#
# DOS 3.3 RWTS writes a sector by finding its address field and rewriting the
# data field (D5 AA AD .. DE AA). rdedisktool now does the same; before, any
# write rebuilt the whole track as a standard track on save, so it refused a
# track with an unreadable sector ("not all 16 sectors are readable") and
# dropped everything non-standard on the tracks it wrote.
# The disks are built with tests/tools/a2_nibref.py (independent encoder /
# Disk II latch reader). On the track the file goes to they carry:
#   - copy-protection-like nibbles in gap 1
#   - one sector with a bad address checksum (unreadable, never written)
#   - weak bits: 40 zero bits in a gap (WOZ)
#   - a timing bit (an extra 0) inside the data field of a written sector (WOZ)
# Expected: the add succeeds; every bit / nibble that changed lies inside the
# data field of a readable sector; the protection nibbles, weak bits and the
# unreadable sector are unchanged (it stays unreadable); the data field with
# the timing bit is written back as plain nibbles (track 1 bit shorter); the
# sectors decode to exactly what the same add gives on a .do copy.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
TOOLS="$SCRIPT_DIR/tools"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_nibwoz_partial.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }

# --- a DOS 3.3 disk and the same add on a .do copy (the expected sectors)
rde create "$WORK/base.do" --fs dos33 >/dev/null
python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(31).randrange(256) for _ in range(1500)))' "$WORK/f.bin"
cp "$WORK/base.do" "$WORK/ctl.do"
rde add "$WORK/ctl.do" "$WORK/f.bin" FILE --type B --addr 0x2000 >/dev/null

cat >"$WORK/fixture.py" <<'EOF'
# fixture.py <tools> <base.do> <ctl.do> <out-dir>: build fx.woz / fx.nib and fx.json
import json, sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A
base, ctl, out = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read(), sys.argv[4]
sec = lambda d, t, s: d[(t * 16 + s) * 256:(t * 16 + s + 1) * 256]
changed = [(t, s) for t in range(35) for s in range(16) if sec(base, t, s) != sec(ctl, t, s)]
T = min(t for t, _ in changed if t != 17)                      # the file's data track
W = sorted(s for t, s in changed if t == T)                    # logical sectors written there
B = max(s for s in range(16) if s not in W)                    # stays unwritten -> made unreadable
X = W[0]                                                       # gets a timing bit (WOZ)
PROT = [0xD4, 0xAA, 0xB7, 0xEE, 0xF7, 0xD4, 0xAA]              # not a prologue the readers use

def units_for(t, woz):
    u = A.track_units(base, t)
    if t != T:
        return u, {}
    info = {}
    # bad address checksum on physical sector of B
    pB = A.DOS_L2P[B]
    for i in range(len(u) - 13):
        if [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0x96] and A.dec44(u[i + 7][0], u[i + 8][0]) == pB:
            vol, trk = A.dec44(u[i + 3][0], u[i + 4][0]), A.dec44(u[i + 5][0], u[i + 6][0])
            o, e = A.enc44(vol ^ trk ^ pB ^ 1)
            u[i + 9], u[i + 10] = (o, 8), (e, 8)
            info['bad_addr_unit'] = i
            break
    if woz:
        # timing bit inside the data field of physical sector of X (after nibble 100)
        pX = A.DOS_L2P[X]
        for i in range(len(u) - 13):
            if [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0x96] and A.dec44(u[i + 7][0], u[i + 8][0]) == pX:
                j = next(k for k in range(i + 13, i + 40) if [v for v, _ in u[k:k + 3]] == [0xD5, 0xAA, 0xAD])
                u[j + 100] = (u[j + 100][0], 9)
                info['timing_unit'] = j + 100
                break
        # weak bits: 40 zeros after the first sync of gap 3 behind physical sector 3
        p3 = None
        for i in range(len(u) - 13):
            if [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0x96] and A.dec44(u[i + 7][0], u[i + 8][0]) == 3:
                j = next(k for k in range(i + 13, i + 40) if [v for v, _ in u[k:k + 3]] == [0xD5, 0xAA, 0xAD])
                p3 = j + 349                                    # first gap-3 sync
                break
        u[p3] = (0xFF, 10 + 40)
        info['weak_unit'] = p3
    # protection nibbles in gap 1 (replacing as many syncs)
    u[5:5 + len(PROT)] = [(v, 8) for v in PROT]
    info['prot_unit'] = 5
    return u, info

def bit_offsets(u):
    out, pos = [], 0
    for v, n in u:
        out.append(pos)
        pos += n
    return out

meta = dict(T=T, W=W, B=B, X=X)
woz_tracks, nib = [], bytearray()
for t in range(35):
    u, info = units_for(t, True)
    woz_tracks.append(A.units_to_bits(u))
    if t == T:
        off = bit_offsets(u)
        meta['woz_prot'] = [off[info['prot_unit']], off[info['prot_unit'] + len(PROT)]]
        meta['woz_weak'] = [off[info['weak_unit']] + 10, off[info['weak_unit']] + 50]
        meta['woz_bad_addr'] = [off[info['bad_addr_unit']], off[info['bad_addr_unit'] + 14]]
    un, info = units_for(t, False)
    nibs = [v for v, _ in un]
    nibs = [0xFF] * (6656 - len(nibs)) + nibs                   # longer gap 1 fills the NIB track
    nib += bytes(nibs)
    if t == T:
        k = 6656 - len(un)
        meta['nib_prot'] = [k + info['prot_unit'], k + info['prot_unit'] + len(PROT)]
        meta['nib_bad_addr'] = [k + info['bad_addr_unit'], k + info['bad_addr_unit'] + 14]
open(out + '/fx.woz', 'wb').write(A.woz2(woz_tracks, A.standard_tmap(35)))
open(out + '/fx.nib', 'wb').write(bytes(nib))
json.dump(meta, open(out + '/fx.json', 'w'))

# WOZ2 with a WRIT chunk (write hints for the old bitstreams; must be dropped)
open(out + '/fx_writ.woz', 'wb').write(A.woz2(woz_tracks, A.standard_tmap(35),
                                              extra=A.chunk(b'WRIT', bytes(range(32)))))
# WOZ1 whose track T splice point lies behind the field with the timing bit
u, info = units_for(T, True)
off = bit_offsets(u)
splice = off[info['timing_unit']] + 3000
w1 = bytearray(A.woz1(woz_tracks, A.standard_tmap(35)))
trks = w1.index(b'TRKS') + 8
import struct, zlib
struct.pack_into('<H', w1, trks + T * 6656 + 6650, splice)
struct.pack_into('<I', w1, 8, zlib.crc32(bytes(w1[12:])) & 0xFFFFFFFF)
open(out + '/fx1.woz', 'wb').write(bytes(w1))
json.dump(dict(meta, woz1_splice=splice), open(out + '/fx1.json', 'w'))
# NIB with track T turned so the data field of the last written sector wraps
un, info = units_for(T, False)
k = 6656 - len(un)
pW = A.DOS_L2P[W[-1]]
for i in range(len(un) - 13):
    if [v for v, _ in un[i:i + 3]] == [0xD5, 0xAA, 0x96] and A.dec44(un[i + 7][0], un[i + 8][0]) == pW:
        dfield = k + next(j for j in range(i + 13, i + 40) if [v for v, _ in un[j:j + 3]] == [0xD5, 0xAA, 0xAD])
        break
r = dfield + 150                                   # cut 150 nibbles into that data field
rot = bytearray(nib)
trk = bytes(nib[T * 6656:(T + 1) * 6656])
rot[T * 6656:(T + 1) * 6656] = trk[r:] + trk[:r]
open(out + '/fx_rot.nib', 'wb').write(bytes(rot))
json.dump(dict(T=T, W=W, B=B, X=X), open(out + '/fx_rot.json', 'w'))
EOF
python3 -I -B "$WORK/fixture.py" "$TOOLS" "$WORK/base.do" "$WORK/ctl.do" "$WORK"
cp "$WORK/fx.woz" "$WORK/w.woz"; cp "$WORK/fx.nib" "$WORK/n.nib"

cat >"$WORK/check.py" <<'EOF'
# check.py <tools> <kind woz|nib> <before> <after> <ctl.do> <fx.json>
import json, sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A
kind, before_p, after_p, ctl_p, meta = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], json.load(open(sys.argv[6]))
ctl = open(ctl_p, 'rb').read()
T, B, X = meta['T'], meta['B'], meta['X']
err = []

def tracks(path):
    if kind == 'woz':
        w = A.read_woz(path)
        return [A.woz_track(w, t) for t in range(35)]
    d = open(path, 'rb').read()
    return [list(d[t * 6656:(t + 1) * 6656]) for t in range(35)]

def nibble_positions(trk):
    """(nibble, first unit index, last unit index) over two turns; units = bits or NIB bytes."""
    if kind == 'woz':
        return [(v, k - 7, k) for v, k in A.lss(trk, 2)]
    n = len(trk)
    return [(trk[k % n], k, k) for k in range(2 * n)]

def readable_data_spans(trk, t):
    """Units (mod track length) inside the data field D5 AA AD .. DE AA of readable sectors."""
    nl = nibble_positions(trk)
    vals = [v for v, _, _ in nl]
    found = A.parse_stream(vals, t)
    n, spans = len(trk), set()
    for p, s in found.items():
        i = s['at']
        j = next(k for k in range(i + 13, i + 45) if vals[k:k + 3] == [0xD5, 0xAA, 0xAD])
        for u in range(nl[j][1], nl[j + 347][2] + 1):
            spans.add(u % n)
    return spans, found

bt, at = tracks(before_p), tracks(after_p)
if kind == 'woz' and len(at[T]) != len(bt[T]) - 1:
    err.append('timing track: %d -> %d bits, want one bit shorter' % (len(bt[T]), len(at[T])))
for t in range(35):
    b, a = bt[t], at[t]
    spans, _ = readable_data_spans(b, t)
    if len(a) == len(b):
        bad = [u for u in range(len(b)) if a[u] != b[u] and u not in spans]
    else:
        # the WOZ timing track: one bit shorter, after the rewritten field
        if not (kind == 'woz' and t == T and len(a) == len(b) - 1):
            err.append('track %d: length %d -> %d' % (t, len(b), len(a)))
            continue
        nl = nibble_positions(b)
        vals = [v for v, _, _ in nl]
        sx = A.parse_stream(vals, t)[A.DOS_L2P[X]]['at']
        j = next(k for k in range(sx + 13, sx + 45) if vals[k:k + 3] == [0xD5, 0xAA, 0xAD])
        s0, s1 = nl[j][1] % len(b), nl[j + 347][2] % len(b) + 1
        bad = [u for u in range(len(b)) if not (s0 <= u < s1) and u not in spans and
               b[u] != a[u if u < s0 else u - 1]]
    if bad:
        err.append('track %d: %d unit(s) changed outside data fields, first %d' % (t, len(bad), bad[0]))
    # sectors read back = the .do copy, the damaged sector still unreadable
    _, found = readable_data_spans(a, t)
    for p in range(16):
        want = ctl[(t * 16 + A.DOS_P2L[p]) * 256:(t * 16 + A.DOS_P2L[p] + 1) * 256]
        if t == T and A.DOS_P2L[p] == B:
            if p in found:
                err.append('track %d: damaged sector became readable' % t)
        elif p not in found:
            err.append('track %d physical %d unreadable' % (t, p))
        elif found[p]['data'] != want:
            err.append('track %d physical %d differs from the .do copy' % (t, p))
# the protection nibbles, weak bits and damaged address field are untouched
# (on the shortened WOZ track, units after the rewritten field moved back)
b, a = bt[T], at[T]
s0 = len(b)
if len(a) != len(b):
    nl = nibble_positions(b)
    vals = [v for v, _, _ in nl]
    sx = A.parse_stream(vals, T)[A.DOS_L2P[X]]['at']
    j = next(k for k in range(sx + 13, sx + 45) if vals[k:k + 3] == [0xD5, 0xAA, 0xAD])
    s0 = nl[j][1] % len(b)
for key in ([k for k in meta if k.startswith(kind + '_')]):
    lo, hi = meta[key]
    d = len(b) - len(a) if lo >= s0 else 0
    if a[lo - d:hi - d] != b[lo:hi]:
        err.append('%s changed' % key)
for e in err:
    print('check:', e)
sys.exit(1 if err else 0)
EOF
check() { python3 -I -B "$WORK/check.py" "$TOOLS" "$@"; }

# the fixtures read as intended: sector B of track T unreadable, the rest = base.do
python3 -I -B - "$TOOLS" "$WORK/fx.woz" "$WORK/fx.nib" "$WORK/base.do" "$WORK/fx.json" <<'EOF' || fail "fixture is not what the test needs"
import json, sys
sys.path.insert(0, sys.argv[1]); import a2_nibref as A
m = json.load(open(sys.argv[5])); base = open(sys.argv[4], 'rb').read()
w = A.read_woz(sys.argv[2]); nib = open(sys.argv[3], 'rb').read()
for t in range(35):
    for name, nibs in (('woz', [v for v, _ in A.lss(A.woz_track(w, t), 2)]),
                       ('nib', list(nib[t * 6656:(t + 1) * 6656]) * 2)):
        f = A.parse_stream(nibs, t)
        want = 15 if t == m['T'] else 16
        assert len(f) == want, (name, t, len(f))
        assert (A.DOS_L2P[m['B']] in f) == (t != m['T'])
        for p, s in f.items():
            assert s['data'] == base[(t * 16 + A.DOS_P2L[p]) * 256:(t * 16 + A.DOS_P2L[p] + 1) * 256]
EOF
pass

for case in "woz:fx.woz:fx.json" "nib:fx.nib:fx.json" "woz:fx1.woz:fx1.json" "nib:fx_rot.nib:fx_rot.json" "woz:fx_writ.woz:fx.json"; do
  IFS=: read -r kind src meta <<<"$case"
  img="$WORK/out_$src"; cp "$WORK/$src" "$img"
  rde add "$img" "$WORK/f.bin" FILE --type B --addr 0x2000 >"$WORK/out" 2>&1 || { cat "$WORK/out"; fail "$src: add refused"; }
  check "$kind" "$WORK/$src" "$img" "$WORK/ctl.do" "$WORK/$meta" || fail "$src: track contents"
  rde extract "$img" FILE "$WORK/got_$src" >/dev/null && cmp -s "$WORK/f.bin" "$WORK/got_$src" || fail "$src: extract"
  pass
done
# WOZ1: the splice point behind the rewritten field moved back with the bits
python3 -I -B - "$TOOLS" "$WORK/out_fx1.woz" "$WORK/fx1.json" <<'EOF' || fail "WOZ1 splice points"
import json, struct, sys
sys.path.insert(0, sys.argv[1]); import a2_nibref as A
m = json.load(open(sys.argv[3])); d = open(sys.argv[2], 'rb').read()
trks = d.index(b'TRKS') + 8
sp = [struct.unpack('<H', d[trks + i * 6656 + 6650:trks + i * 6656 + 6652])[0] for i in range(35)]
want = [0x1234] * 35
want[m['T']] = m['woz1_splice'] - 1
sys.exit(0 if sp == want else 1)
EOF
pass
# WRIT hints described the old bitstreams: dropped once a track changed
python3 -I -B - "$TOOLS" "$WORK/fx_writ.woz" "$WORK/out_fx_writ.woz" <<'EOF' || fail "WRIT chunk"
import sys
sys.path.insert(0, sys.argv[1]); import a2_nibref as A
ids = lambda p: [c[0] for c in A.read_woz(p)['chunks']]
sys.exit(0 if b'WRIT' in ids(sys.argv[2]) and b'WRIT' not in ids(sys.argv[3]) else 1)
EOF
pass

echo "PASS test_apple_nibwoz_partial_write ($CHECKS checks)"
