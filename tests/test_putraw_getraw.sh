#!/usr/bin/env bash
set -euo pipefail

# putraw / getraw — Apple II direct-boot raw sector write/read (UPDATE_PUTRAW_DIRECTBOOT.md).
# Covers: round-trip byte-exact, track-crossing, zero-pad, all reject/guard paths,
# marker guard (reject foreign FS / accept Unknown / accept DKFS-marked / --force-bootdisk),
# getraw -o output safety.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_putraw_getraw}"
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

assert_ok() {   # description, command...
  local desc="$1"; shift
  set +e; "$@" >/tmp/rde_pg.log 2>&1; local rc=$?; set -e
  if [[ $rc -ne 0 ]]; then
    echo "[FAIL] expected success: $desc" >&2; sed -n '1,80p' /tmp/rde_pg.log >&2; exit 1
  fi
}
assert_fail() { # description, command...
  local desc="$1"; shift
  set +e; "$@" >/tmp/rde_pg.log 2>&1; local rc=$?; set -e
  if [[ $rc -eq 0 ]]; then
    echo "[FAIL] expected rejection but succeeded: $desc" >&2; sed -n '1,80p' /tmp/rde_pg.log >&2; exit 1
  fi
}

# ── Fixtures ──
assert_ok "create blank .do"            "$RDEDISKTOOL" create blank.do  -f do --force
assert_ok "create real DOS33 .do"       "$RDEDISKTOOL" create dos33.do  -f do --fs dos33 -n DISK --force
head -c 600 /dev/urandom > payload.bin            # 3 sectors, crosses a track boundary
: > empty.bin                                     # 0 bytes

# DKFS build marker sector (T0S15): "DKFS20RAW" + version(1) + inverse-magic(9), padded to 256.
python3 - <<'PY'
m=b'DKFS20RAW'; s=bytearray(256); s[0:9]=m; s[9]=1; s[10:19]=bytes(b^0xFF for b in m)
open('marker.bin','wb').write(bytes(s))
PY

# ── Round-trip + track-crossing + zero-pad ──
# put 600 bytes at T0S14 -> occupies T0S14, T0S15, T1S0
assert_ok "putraw round-trip"           "$RDEDISKTOOL" putraw blank.do payload.bin -t 0 -s 14
assert_ok "getraw round-trip"           "$RDEDISKTOOL" getraw blank.do -o out.bin -t 0 -s 14 --count 3
[[ $(stat -c%s out.bin) -eq 768 ]] || { echo "[FAIL] getraw output size != 768" >&2; exit 1; }
head -c 600 out.bin > out600.bin
cmp -s payload.bin out600.bin || { echo "[FAIL] round-trip bytes differ" >&2; exit 1; }
# zero-pad tail (bytes 600..767 must be 0)
tail -c 168 out.bin | tr -d '\0' | wc -c | grep -qx 0 || { echo "[FAIL] tail not zero-padded" >&2; exit 1; }

# ── putraw reject paths ──
assert_fail "0-byte hostfile"           "$RDEDISKTOOL" putraw blank.do empty.bin   -t 0 -s 0
assert_fail "exceeds --max-sectors"     "$RDEDISKTOOL" putraw blank.do payload.bin  -t 0 -s 0 --max-sectors 1
assert_fail "track out of range"        "$RDEDISKTOOL" putraw blank.do payload.bin  -t 35 -s 0
assert_fail "sector out of range"       "$RDEDISKTOOL" putraw blank.do payload.bin  -t 0 -s 16
assert_fail "does not fit from start"   "$RDEDISKTOOL" putraw blank.do payload.bin  -t 34 -s 15
assert_fail "non-numeric track (hex)"   "$RDEDISKTOOL" putraw blank.do payload.bin  -t 0x5 -s 0
assert_fail "negative track"            "$RDEDISKTOOL" putraw blank.do payload.bin  -t -1 -s 0
assert_fail "bad --format"              "$RDEDISKTOOL" putraw blank.do payload.bin  -t 0 -s 0 -f po
cp blank.do blank.dsk
assert_fail "wrong extension (.dsk)"    "$RDEDISKTOOL" putraw blank.dsk payload.bin -t 0 -s 0

# ── Marker guard ──
# Unknown FS (blank) is accepted; real DOS33 (no marker) is rejected.
assert_ok   "Unknown FS accepted"       "$RDEDISKTOOL" putraw blank.do payload.bin  -t 2 -s 0
assert_fail "foreign DOS33 rejected"    "$RDEDISKTOOL" putraw dos33.do payload.bin  -t 0 -s 0
# --force-bootdisk overrides the guard.
assert_ok   "--force-bootdisk override" "$RDEDISKTOOL" --force-bootdisk putraw dos33.do marker.bin -t 0 -s 15
# Now DOS33 disk carries the DKFS marker -> accepted without --force.
assert_ok   "DKFS-marked accepted"      "$RDEDISKTOOL" putraw dos33.do payload.bin  -t 1 -s 0
# Marker survived the second write.
assert_ok   "read back marker"          "$RDEDISKTOOL" getraw dos33.do -o gm.bin -t 0 -s 15 --count 1 --force
head -c 19 gm.bin > gm19.bin; head -c 19 marker.bin > mk19.bin
cmp -s gm19.bin mk19.bin || { echo "[FAIL] DKFS marker corrupted" >&2; exit 1; }

# ── getraw reject / output-safety paths ──
assert_fail "count == 0"                "$RDEDISKTOOL" getraw blank.do -o g.bin   -t 0 -s 0 --count 0
assert_fail "count exceeds disk"        "$RDEDISKTOOL" getraw blank.do -o g.bin   -t 34 -s 15 --count 2
assert_fail "-o equals input image"     "$RDEDISKTOOL" getraw blank.do -o blank.do -t 0 -s 0 --count 1
echo keepme > exists.bin
assert_fail "existing -o without --force" "$RDEDISKTOOL" getraw blank.do -o exists.bin -t 0 -s 0 --count 1
cmp -s <(echo keepme) exists.bin || { echo "[FAIL] rejected getraw clobbered output" >&2; exit 1; }
assert_ok   "existing -o with --force"  "$RDEDISKTOOL" getraw blank.do -o exists.bin -t 0 -s 0 --count 1 --force
[[ $(stat -c%s exists.bin) -eq 256 ]] || { echo "[FAIL] forced getraw wrong size" >&2; exit 1; }
# no leftover temp file
[[ ! -e exists.bin.tmp.rdedisktool ]] || { echo "[FAIL] temp file left behind" >&2; exit 1; }
assert_fail "missing -o"                "$RDEDISKTOOL" getraw blank.do -t 0 -s 0 --count 1

echo "[PASS] putraw / getraw raw sector I/O"
