#!/usr/bin/env bash
# 13-sector DOS 3.2 NIB / NB2 / WOZ images: add / rename / delete.
#
# Like DOS 3.2 RWTS, a sector write replaces only the sector's data field
# (D5 AA AD, 411 5-and-3 nibbles, DE AA); before, 13-sector NIB/WOZ images
# refused every write. Expected values, all independent of rdedisktool's
# nibble code (tests/tools/a2_nibref.py; its encode53 gives back the raw
# nibbles of every sector real DOS 3.2 wrote on the Asimov masters):
#   - every sector that reads afterwards has raw nibbles = encode53(its data)
#   - with the readable data fields taken out, both tracks are the same unit
#     sequence (nothing else on a track moves or changes; a data field with
#     a timing bit is written back as plain nibbles, the track gets shorter)
#   - the readable sectors stay readable and hold exactly what the same
#     command gives on a .d13 copy (sector image path, test_apple_d13_write.sh)
#   - an independent DOS 3.2 reader judges add / rename / delete
# Disks: synthetic (a2_nibref make-13: nib, nb2, rotated nib, woz, woz1, woz
# with a timing bit) and, when present, copies of the Asimov images
# (A2_REAL_D13_DIR, default ../resource/AppleII/dos32): DOS 3.2.1 Standard.nib,
# DOS 3.2 Standard.nib and the Applesauce capture DOS 3.2 System Master.woz.
# Never-written sectors (DOS 3.2 INIT writes address fields only; real
# Utility.nib / Plus.nib and synthetic copies): the data field is written after
# the address field like DOS 3.2 (14 syncs, D5 AA AD .. DE AA EB; NIB down to 5
# syncs in a short gap); no room before the next address field = refused.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
TOOLS="$SCRIPT_DIR/tools"
REF="$TOOLS/a2_nibref.py"
A2_REAL_D13_DIR="${A2_REAL_D13_DIR:-$TOOL_ROOT/../resource/AppleII/dos32}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_d13_nibwoz.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }

cat >"$WORK/n13.py" <<'EOF'
# n13.py <tools> <cmd> ...: 13-sector NIB/NB2/WOZ tracks via a2_nibref
import sys
sys.path.insert(0, sys.argv[1])
import a2_nibref as A

def kind_of(p):
    return 'woz' if p.lower().endswith('.woz') else 'nib'

def tracks(p):
    if kind_of(p) == 'woz':
        w = A.read_woz(p)
        return [A.woz_track(w, t) for t in range(35)]
    d = open(p, 'rb').read()
    n = len(d) // 35
    return [list(d[t * n:(t + 1) * n]) for t in range(35)]

def units(kind, trk):
    """(nibble, first unit, last unit) over two turns; units = bits (WOZ) or bytes (NIB)."""
    if kind == 'woz':
        return [(v, k - 7, k) for v, k in A.lss(trk, 2)]
    return [(trk[k % len(trk)], k, k) for k in range(2 * len(trk))]

def addresses(vals, t):
    """{sector: [nibble index of each valid D5 AA B5 address field]} (same rules as the reader)."""
    out = {}
    for i in range(len(vals) - 13):
        if vals[i:i + 3] != [0xD5, 0xAA, 0xB5] or vals[i + 11:i + 13] != [0xDE, 0xAA]:
            continue
        a = vals[i + 3:i + 11]
        vol, trk, sec, cs = A.dec44(a[0], a[1]), A.dec44(a[2], a[3]), A.dec44(a[4], a[5]), A.dec44(a[6], a[7])
        if vol ^ trk ^ sec == cs and trk == t and sec <= 12:
            out.setdefault(sec, []).append(i)
    return out

def fields(kind, trk, t):
    """{physical: (data, raw)}, the units (mod track length) of their data fields,
    and the sectors whose address field has no D5 AA within 32 nibbles (never written)."""
    nl = units(kind, trk)
    vals = [v for v, _, _ in nl]
    found = A.parse_stream13(vals, t)
    spans = set()
    for s in found.values():
        for u in range(nl[s['dp']][1], nl[s['dp'] + 415][2] + 1):
            spans.add(u % len(trk))
    addr = addresses(vals, t)
    unwritten = {s for s, idx in addr.items() if s not in found and
                 any(i + 13 + 34 <= len(vals) and
                     not any(vals[j:j + 2] == [0xD5, 0xAA] for j in range(i + 13, i + 13 + 32))
                     for i in idx)}
    return {p: (s['data'], s['raw']) for p, s in found.items()}, spans, unwritten, set(addr), nl, found

def assemble(p):
    out = bytearray(35 * 13 * 256)
    k = kind_of(p)
    for t, trk in enumerate(tracks(p)):
        if trk is None:
            continue
        f = fields(k, trk, t)[0]
        for s, (data, _) in f.items():
            out[(t * 13 + s) * 256:(t * 13 + s + 1) * 256] = data
    return bytes(out)

import os
FULL_SYNC = os.environ.get('FULL_SYNC') == '1'   # room for all 14 syncs (synthetic disks)
cmd, a = sys.argv[2], sys.argv[3:]
if cmd == 'assemble':                      # img out.d13
    open(a[1], 'wb').write(assemble(a[0]))
elif cmd == 'step':                        # before after want.d13 [shorter-track]
    k = kind_of(a[0])
    bt, at, want = tracks(a[0]), tracks(a[1]), open(a[2], 'rb').read()
    short = int(a[3]) if len(a) > 3 else -1
    err = []
    for t in range(35):
        b, c = bt[t], at[t]
        if b is None or c is None:
            if (b is None) != (c is None):
                err.append('track %d appeared / vanished' % t)
            continue
        fb, sb, ub, ab, _, _ = fields(k, b, t)
        fc, sc, uc, ac, nlc, foundc = fields(k, c, t)
        new = set(fc) - set(fb)
        if set(fb) - set(fc) or not new <= ub:
            err.append('track %d: readable %s -> %s (unwritten before %s)' % (t, sorted(fb), sorted(fc), sorted(ub)))
        if ab != ac or (ub - new) != uc:
            err.append('track %d: address fields %s -> %s, unwritten %s -> %s' % (t, sorted(ab), sorted(ac), sorted(ub), sorted(uc)))
        for s, (data, raw) in fc.items():
            if A.encode53(data) != raw:
                err.append('T%d S%d: raw nibbles != encode53(data)' % (t, s))
            if data != want[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]:
                err.append('T%d S%d: differs from the .d13 copy' % (t, s))
        if len(b) == len(c):
            # every changed unit lies in a data field (before or after) or, for
            # a newly written one, in its sync run after the address field or
            # its closing EB (DOS writes D5 AA AD .. DE AA EB)
            ok = sb | sc
            vals = [v for v, _, _ in nlc]
            for s in new:
                dp = foundc[s]['dp']
                ai = max(i for i in addresses(vals, t)[s] if i < dp)
                for u in list(range(nlc[ai + 13][2] + 1, nlc[dp][1])) + \
                         list(range(nlc[dp + 416][1], nlc[dp + 416][2] + 1)):
                    ok.add(u % len(c))
                # laid out as DOS 3.2 writes it: address EB, syncs (9 bits in
                # WOZ: FF + 0), D5 AA AD .. DE AA EB
                gap = [c[u % len(c)] for u in range(nlc[ai + 13][2] + 1, nlc[dp][1])]
                if k == 'woz':
                    syncs = len(gap) // 9
                    good = gap == [1, 1, 1, 1, 1, 1, 1, 1, 0] * syncs
                else:
                    syncs = len(gap)
                    good = all(x == 0xFF for x in gap)
                if vals[ai + 13] != 0xEB or vals[dp + 416] != 0xEB or not good or \
                        not (FULL_SYNC and syncs == 14 or not FULL_SYNC and 5 <= syncs <= 14):
                    err.append('T%d S%d: new data field not laid out like DOS 3.2 (%d syncs)' % (t, s, syncs))
            bad = [u for u in range(len(b)) if b[u] != c[u] and u not in ok]
            if bad:
                err.append('track %d: %d unit(s) changed outside the data fields, first %d' % (t, len(bad), bad[0]))
        else:
            if new:
                err.append('track %d: length changed while writing an unwritten sector' % t)
            rb = [b[u] for u in range(len(b)) if u not in sb]
            rc = [c[u] for u in range(len(c)) if u not in sc]
            if rb != rc:
                err.append('track %d: units outside the data fields changed' % t)
        if t == short:
            if len(c) != len(b) - 1:
                err.append('track %d: %d -> %d bits, want one bit shorter' % (t, len(b), len(c)))
        elif len(c) != len(b):
            err.append('track %d: length %d -> %d' % (t, len(b), len(c)))
    for e in err[:20]:
        print('step:', e)
    sys.exit(1 if err else 0)
elif cmd == 'timing-woz':                  # base.d13 out.woz track sector: an extra 0 bit in that data field
    d13 = open(a[0], 'rb').read()
    T, S = int(a[2]), int(a[3])
    trs = []
    for t in range(35):
        u = A.track_units13(d13, t)
        if t == T:
            seen = -1
            for i in range(len(u) - 3):
                if [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0xB5]:
                    seen = A.dec44(u[i + 7][0], u[i + 8][0])
                if seen == S and [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0xAD]:
                    u[i + 200] = (u[i + 200][0], 9)
                    break
            else:
                sys.exit('sector not found')
        trs.append(A.units_to_bits(u))
    info = bytearray(A.info_chunk(largest=13))
    info[38] = 2
    open(a[1], 'wb').write(A.woz2(trs, A.standard_tmap(35), info=bytes(info)))
elif cmd == 'clear-wp':                   # in.woz out.woz: INFO write-protected = 0, CRC redone
    import struct, zlib
    d = bytearray(open(a[0], 'rb').read())
    o = d.index(b'INFO') + 8
    d[o + 2] = 0
    struct.pack_into('<I', d, 8, zlib.crc32(bytes(d[12:])) & 0xFFFFFFFF)
    open(a[1], 'wb').write(bytes(d))
elif cmd == 'unwritten':                  # kind d13 out [short]: free sectors with address fields only
    # (the bitmap's free sectors lose their data field, replaced by syncs, as
    # DOS 3.2 INIT leaves them; "short": the next address field follows 60
    # syncs later - past the 32-nibble data search, but no room for a data field)
    kind, d13, out = a[0], open(a[1], 'rb').read(), a[2]
    short = len(a) > 3
    vt = d13[17 * 13 * 256:17 * 13 * 256 + 256]
    free = lambda t, s: (((vt[0x38 + 4 * t] << 8) | vt[0x39 + 4 * t]) >> (s + 3)) & 1
    trs = []
    for t in range(35):
        u = A.track_units13(d13, t)
        if t > 2 and t != 17:
            v, i, sec = [x for x, _ in u], 0, None
            res = []
            while i < len(u):
                if v[i:i + 3] == [0xD5, 0xAA, 0xB5]:
                    sec = A.dec44(v[i + 7], v[i + 8])
                if v[i:i + 3] == [0xD5, 0xAA, 0xAD] and free(t, sec):
                    res += [(0xFF, 10)] * (54 if short else 419)
                    i += 419
                    if short:
                        while i < len(u) and v[i] == 0xFF:
                            i += 1
                    continue
                res.append(u[i]); i += 1
            u = res
        trs.append(u)
    if kind == 'nib':
        nib = bytearray()
        for u in trs:
            tr = bytes(x for x, _ in u)
            nib += tr + b'\xff' * (6656 - len(tr))
        open(out, 'wb').write(bytes(nib))
    else:
        info = bytearray(A.info_chunk(largest=13))
        info[38] = 2
        open(out, 'wb').write(A.woz2([A.units_to_bits(u) for u in trs], A.standard_tmap(35), info=bytes(info)))
elif cmd == 'changed':                     # x.d13 y.d13: first changed sector off the VTOC track
    x, y = open(a[0], 'rb').read(), open(a[1], 'rb').read()
    for i in range(455):
        if i // 13 != 17 and x[i * 256:(i + 1) * 256] != y[i * 256:(i + 1) * 256]:
            print(i // 13, i % 13)
            break
EOF
n13() { python3 -I -B "$WORK/n13.py" "$TOOLS" "$@"; }

cat >"$WORK/dos32.py" <<'EOF'
# dos32.py <cmd> ...: independent DOS 3.2 reader (same as test_apple_d13_write.sh)
import sys
def load(p):
    d = open(p, 'rb').read(); assert len(d) == 35 * 13 * 256; return d
def sec(d, t, s): return d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
def vtoc(d): return sec(d, 17, 0)
def free(d, t, s):
    v = vtoc(d); return (((v[0x38 + 4 * t] << 8) | v[0x39 + 4 * t]) >> (s + 3)) & 1
def bitmap(d): return vtoc(d)[0x38:0x38 + 4 * 35]
def catalog(d):
    v, out, seen = vtoc(d), [], set()
    cur = (v[1], v[2])
    while cur != (0, 0) and cur not in seen:
        seen.add(cur); c = sec(d, *cur)
        for i in range(7):
            e = c[0x0B + i * 0x23:0x0B + (i + 1) * 0x23]
            if e[0] not in (0, 0xFF):
                out.append((cur, i, e))
        cur = (c[1], c[2])
    return out
def name(e): return bytes(x & 0x7F for x in e[3:33]).decode().rstrip()
def file_sectors(d, e):
    lists, data, ts = [], [], (e[0], e[1])
    while ts != (0, 0) and ts not in lists:
        lists.append(ts); l = sec(d, *ts)
        data += [(l[0x0C + 2 * k], l[0x0D + 2 * k]) for k in range(122)]
        ts = (l[1], l[2])
    while data and data[-1] == (0, 0):
        data.pop()
    return lists, data
cmd, a = sys.argv[1], sys.argv[2:]
if cmd == 'diff-add':          # before after name expected-body addr
    b, d = load(a[0]), load(a[1])
    es = [e for _, _, e in catalog(d) if name(e) == a[2]]
    assert len(es) == 1, 'entry %s: %d' % (a[2], len(es))
    lists, data = file_sectors(d, es[0])
    new = set(lists) | set(data)
    raw = b''.join(sec(d, *ts) for ts in data)
    body = open(a[3], 'rb').read()
    assert es[0][2] & 0x7F == 0x04, 'type %02x' % es[0][2]
    assert raw[0] | raw[1] << 8 == int(a[4], 0) and raw[2] | raw[3] << 8 == len(body), 'B header'
    assert raw[4:4 + len(body)] == body, 'file data'
    assert es[0][33] | es[0][34] << 8 == len(lists) + len(data), 'sector count'
    for t in range(35):
        for s in range(13):
            want = 0 if (t, s) in new else free(b, t, s)
            assert free(d, t, s) == want, 'bitmap T%d S%d' % (t, s)
            if (t, s) in new:
                assert free(b, t, s) == 1, 'T%d S%d was not free' % (t, s)
    assert d[:3 * 13 * 256] == b[:3 * 13 * 256], 'tracks 0-2 changed'
    print(len(new))
elif cmd == 'same-bitmap':     # x y
    assert bitmap(load(a[0])) == bitmap(load(a[1])), 'bitmap differs'
elif cmd == 'renamed':          # before after old new
    b, d = load(a[0]), load(a[1])
    eb = {(c, i): e for c, i, e in catalog(b)}
    ed = {(c, i): e for c, i, e in catalog(d)}
    assert eb.keys() == ed.keys()
    for k in eb:
        if name(eb[k]) == a[2]:
            assert name(ed[k]) == a[3] and eb[k][:3] == ed[k][:3] and eb[k][33:] == ed[k][33:]
        else:
            assert eb[k] == ed[k]
    diff = [i for i in range(len(b)) if b[i] != d[i]]
    cat_secs = {ts[0] * 13 + ts[1] for ts, _ in eb}
    assert all(i // 256 in cat_secs for i in diff), 'bytes outside the catalog changed'
elif cmd == 'set-alloc':        # img track dir
    d = bytearray(load(a[0])); o = 17 * 13 * 256
    d[o + 0x30], d[o + 0x31] = int(a[1]), int(a[2]) & 0xFF
    open(a[0], 'wb').write(d)
EOF
py() { python3 -I -B "$WORK/dos32.py" "$@"; }

# f.bin: 12 data sectors + 1 T/S list; s.bin (400 B): 2 + 1, the 3 sectors the
# real masters have free
python3 -I -c 'import random, sys
r = random.Random(33)
open(sys.argv[1], "wb").write(bytes(r.randrange(256) for _ in range(3000)))
open(sys.argv[2], "wb").write(bytes(r.randrange(256) for _ in range(400)))' "$WORK/f.bin" "$WORK/s.bin"

# run_case <label> <image> [track that gets one bit shorter]   (BODY, USED: file and sectors it takes)
BODY="$WORK/f.bin" USED=13
run_case() {
  local n=$1 src=$2 short=${3:--1} ext=${2##*.}
  local img="$WORK/$n.$ext"
  cp "$src" "$WORK/$n.0.$ext"; cp "$src" "$img"
  n13 assemble "$img" "$WORK/$n.0.d13"; cp "$WORK/$n.0.d13" "$WORK/$n.ctl.d13"

  rde add "$img" "$BODY" NEWFILE --type B --addr 0x2000 >"$WORK/out" 2>&1 || { cat "$WORK/out"; fail "$n: add"; }
  rde add "$WORK/$n.ctl.d13" "$BODY" NEWFILE --type B --addr 0x2000 >/dev/null || fail "$n: add (.d13 copy)"
  n13 step "$WORK/$n.0.$ext" "$img" "$WORK/$n.ctl.d13" "$short" || fail "$n: add tracks"
  n13 assemble "$img" "$WORK/$n.1.d13"
  local used; used=$(py diff-add "$WORK/$n.0.d13" "$WORK/$n.1.d13" NEWFILE "$BODY" 0x2000) || fail "$n: add result"
  [[ $used == "$USED" ]] || fail "$n: $used sectors used, want $USED"
  rde extract "$img" NEWFILE "$WORK/$n.out" >/dev/null && cmp -s "$BODY" "$WORK/$n.out" || fail "$n: extract"
  pass

  cp "$img" "$WORK/$n.1.$ext"
  rde rename "$img" NEWFILE RENAMED >/dev/null || fail "$n: rename"
  rde rename "$WORK/$n.ctl.d13" NEWFILE RENAMED >/dev/null || fail "$n: rename (.d13 copy)"
  n13 step "$WORK/$n.1.$ext" "$img" "$WORK/$n.ctl.d13" || fail "$n: rename tracks"
  n13 assemble "$img" "$WORK/$n.2.d13"
  py renamed "$WORK/$n.1.d13" "$WORK/$n.2.d13" NEWFILE RENAMED || fail "$n: rename result"; pass

  cp "$img" "$WORK/$n.2.$ext"
  rde delete "$img" RENAMED >/dev/null || fail "$n: delete"
  rde delete "$WORK/$n.ctl.d13" RENAMED >/dev/null || fail "$n: delete (.d13 copy)"
  n13 step "$WORK/$n.2.$ext" "$img" "$WORK/$n.ctl.d13" || fail "$n: delete tracks"
  n13 assemble "$img" "$WORK/$n.3.d13"
  py same-bitmap "$WORK/$n.0.d13" "$WORK/$n.3.d13" || fail "$n: bitmap after delete"
  if rde list "$img" | grep -q "^RENAMED "; then fail "$n: still listed"; fi
  pass
}

# --- synthetic disks
mkdir -p "$WORK/exp"
python3 -I -B "$REF" make-d13-fs "$WORK/syn.d13" 7 "$WORK/exp"
py set-alloc "$WORK/syn.d13" 17 1
for k in nib nb2 nibrot woz woz1; do
  ext=$k; [[ $k == nibrot ]] && ext=nib; [[ $k == woz1 ]] && ext=woz
  python3 -I -B "$REF" make-13 "$k" "$WORK/syn.d13" "$WORK/src_$k.$ext"
  run_case "syn_$k" "$WORK/src_$k.$ext"
done
# WOZ: a timing bit inside the data field of the first sector the file takes
cp "$WORK/syn.d13" "$WORK/probe.d13"
rde add "$WORK/probe.d13" "$BODY" NEWFILE --type B --addr 0x2000 >/dev/null
read -r TT TS < <(n13 changed "$WORK/syn.d13" "$WORK/probe.d13")
n13 timing-woz "$WORK/syn.d13" "$WORK/src_timing.woz" "$TT" "$TS"
run_case syn_timing "$WORK/src_timing.woz" "$TT"

# never-written sectors (address field only, as DOS 3.2 INIT leaves them):
# the data field is written after the address field like DOS 3.2 does
for k in nib woz; do
  n13 unwritten "$k" "$WORK/syn.d13" "$WORK/src_unw.$k"
  FULL_SYNC=1 run_case "unwritten_$k" "$WORK/src_unw.$k"
done
# no room before the next address field: refused, image unchanged
n13 unwritten nib "$WORK/syn.d13" "$WORK/short.nib" short
cp "$WORK/short.nib" "$WORK/short0.nib"
rde add "$WORK/short.nib" "$BODY" NEWFILE --type B --addr 0x2000 >"$WORK/out" 2>&1 && fail "short gap: add accepted"
grep -q "no room for a data field" "$WORK/out" || { cat "$WORK/out"; fail "short gap: reason"; }
cmp -s "$WORK/short0.nib" "$WORK/short.nib" || fail "short gap: image changed"; pass

# --- real images (copies); the Standard masters have 3 free sectors
BODY="$WORK/s.bin" USED=3
for m in "Apple DOS 3.2.1 Standard.nib:std321" "Apple DOS 3.2 Standard.nib:std"; do
  f="$A2_REAL_D13_DIR/${m%:*}"
  if [[ -f "$f" ]]; then run_case "${m##*:}" "$f"; else echo "  (skip: $f missing; not judged)"; fi
done
# the System Master capture is marked write-protected in INFO: refused as is,
# then written on a copy with the flag cleared
f="$A2_REAL_D13_DIR/DOS 3.2 System Master.woz"
if [[ -f "$f" ]]; then
  cp "$f" "$WORK/wp.woz"
  rde add "$WORK/wp.woz" "$BODY" NEWFILE --type B --addr 0x2000 >"$WORK/out" 2>&1 && fail "master: write-protected WOZ accepted"
  grep -q "write protected" "$WORK/out" || { cat "$WORK/out"; fail "master: reason"; }
  cmp -s "$f" "$WORK/wp.woz" || fail "master: write-protected image changed"; pass
  n13 clear-wp "$f" "$WORK/src_master.woz"
  run_case master "$WORK/src_master.woz"
else
  echo "  (skip: $f missing; not judged)"
fi
# Utility / Plus: free sectors on tracks DOS 3.2 INIT formatted but nothing
# wrote (address fields only, noise where a data field would be)
BODY="$WORK/f.bin" USED=13
for m in "Apple DOS 3.2 Utility.nib:util" "Apple DOS 3.2 Plus.nib:plus"; do
  f="$A2_REAL_D13_DIR/${m%:*}"
  if [[ -f "$f" ]]; then run_case "${m##*:}" "$f"; else echo "  (skip: $f missing; not judged)"; fi
done

echo "PASS test_apple_d13_nibwoz_write ($CHECKS checks)"
