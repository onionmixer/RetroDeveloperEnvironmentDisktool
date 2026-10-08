#!/usr/bin/env bash
# 13-sector DOS 3.2 disks (.d13): add / rename / delete.
#
# DOS 3.2 keeps the DOS 3.3 catalog and track/sector lists, with 13 sectors per
# track and its own free-sector bitmap layout: bytes 0-1 of a track entry form
# a big-endian word, sector s is bit s+3 (measured on the Apple DOS 3.1 / 3.2 /
# 3.2.1 masters, see test_apple_d13_read.sh). Before, these disks were
# read-only; the bitmap writer also used the DOS 3.3 layout.
# An independent python reader (catalog -> track/sector lists -> sectors, the
# layout above) is the expected value:
#   add:    exactly the new file's T/S list + data sectors become used, no other
#           bitmap bit changes, the file reads back (B header address/length)
#   rename: only the name bytes of that catalog entry change
#   delete: the bitmap is byte-identical to the one before the add
#   tracks 0-2 (the DOS image of a master) are never written
# Disks: a synthetic DOS 3.2 disk (tests/tools/a2_nibref.py make-d13-fs, VTOC
# allocation fields set like a real master) and, when present, copies of the
# real Apple DOS 3.2 Utility (direction +1, 13 partly used tracks) and Plus
# (direction -1) masters (A2_REAL_D13_DIR, default ../resource/AppleII/dos32).
# 13-sector NIB/NB2/WOZ images: test_apple_d13_nibwoz_write.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_nibref.py"
A2_REAL_D13_DIR="${A2_REAL_D13_DIR:-$TOOL_ROOT/../resource/AppleII/dos32}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_d13_write.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rde() { "$RDEDISKTOOL" "$@"; }

cat >"$WORK/dos32.py" <<'EOF'
# dos32.py <cmd> ...: independent DOS 3.2 (.d13) reader
import json, sys
def load(p):
    d = open(p, 'rb').read(); assert len(d) == 35 * 13 * 256; return d
def sec(d, t, s): return d[(t * 13 + s) * 256:(t * 13 + s + 1) * 256]
def vtoc(d): return sec(d, 17, 0)
def free(d, t, s):
    v = vtoc(d); return (((v[0x38 + 4 * t] << 8) | v[0x39 + 4 * t]) >> (s + 3)) & 1
def bitmap(d): return vtoc(d)[0x38:0x38 + 4 * 35]
def catalog(d):
    """[(cat (t,s), slot, entry bytes)] for live entries."""
    v, out, seen, cur = vtoc(d), [], set(), None
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
            assert free(d, t, s) == want, 'bitmap T%d S%d: %d, want %d' % (t, s, free(d, t, s), want)
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
elif cmd == 'bad-pair':          # img name: append the pair T17 S14 (a sector DOS 3.2 has not) to its T/S list
    d = bytearray(load(a[0]))
    e = [e for _, _, e in catalog(bytes(d)) if name(e) == a[1]][0]
    lists, data = file_sectors(bytes(d), e)
    o = (lists[0][0] * 13 + lists[0][1]) * 256 + 0x0C + 2 * len(data)
    d[o], d[o + 1] = 17, 14
    open(a[0], 'wb').write(d)
elif cmd == 'set-alloc':        # img track dir  (VTOC allocation fields like a real master)
    d = bytearray(load(a[0])); o = 17 * 13 * 256
    d[o + 0x30], d[o + 0x31] = int(a[1]), int(a[2]) & 0xFF
    open(a[0], 'wb').write(d)
EOF
py() { python3 -I -B "$WORK/dos32.py" "$@"; }

python3 -I -c 'import random, sys
open(sys.argv[1], "wb").write(bytes(random.Random(32).randrange(256) for _ in range(3000)))' "$WORK/f.bin"

run_case() {   # run_case <label> <source .d13>
  local n=$1 src=$2
  cp "$src" "$WORK/$n.0.d13"; cp "$src" "$WORK/$n.d13"
  rde add "$WORK/$n.d13" "$WORK/f.bin" NEWFILE --type B --addr 0x2000 >/dev/null || fail "$n: add"
  local used; used=$(py diff-add "$WORK/$n.0.d13" "$WORK/$n.d13" NEWFILE "$WORK/f.bin" 0x2000) || fail "$n: add result"
  [[ $used == 13 ]] || fail "$n: $used sectors used, want 13 (12 data + 1 T/S list)"
  rde extract "$WORK/$n.d13" NEWFILE "$WORK/$n.out" >/dev/null && cmp -s "$WORK/f.bin" "$WORK/$n.out" || fail "$n: extract"
  pass
  cp "$WORK/$n.d13" "$WORK/$n.1.d13"
  rde rename "$WORK/$n.d13" NEWFILE RENAMED >/dev/null || fail "$n: rename"
  py renamed "$WORK/$n.1.d13" "$WORK/$n.d13" NEWFILE RENAMED || fail "$n: rename result"; pass
  rde delete "$WORK/$n.d13" RENAMED >/dev/null || fail "$n: delete"
  py same-bitmap "$WORK/$n.0.d13" "$WORK/$n.d13" || fail "$n: bitmap after delete"
  if rde list "$WORK/$n.d13" | grep -q "^RENAMED "; then fail "$n: still listed"; fi
  pass
}

# synthetic disk (allocation fields like a real master: last track 17, +1)
mkdir -p "$WORK/exp"
python3 -I -B "$REF" make-d13-fs "$WORK/synfx.d13" 7 "$WORK/exp"
py set-alloc "$WORK/synfx.d13" 17 1
run_case syn "$WORK/synfx.d13"

# a T/S list naming sector 14 (not on a 13-sector disk): delete must ignore it
# (with a 16-sector range its bit would land on sector 6 and free that sector)
cp "$WORK/synfx.d13" "$WORK/bad.d13"
rde add "$WORK/bad.d13" "$WORK/f.bin" NEWFILE --type B --addr 0x2000 >/dev/null || fail "bad: add"
py bad-pair "$WORK/bad.d13" NEWFILE
rde delete "$WORK/bad.d13" NEWFILE >/dev/null 2>&1 || fail "bad: delete"
py same-bitmap "$WORK/synfx.d13" "$WORK/bad.d13" || fail "bad: a nonexistent sector changed the bitmap"; pass

# real masters
for m in "Apple DOS 3.2 Utility:util" "Apple DOS 3.2 Plus:plus"; do
  f="$A2_REAL_D13_DIR/${m%%:*}.d13"
  if [[ -f "$f" ]]; then run_case "${m##*:}" "$f"; else echo "  (skip: $f missing; not judged)"; fi
done

echo "PASS test_apple_d13_write ($CHECKS checks)"
