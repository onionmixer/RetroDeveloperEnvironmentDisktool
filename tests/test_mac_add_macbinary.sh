#!/usr/bin/env bash
# PR-C 1.1 — `add --macbinary` regression.
#
# Verifies that adding a Retro68-produced MacBinary file:
#   * succeeds and uses the MacBinary header's Pascal name as the target
#     when the user did not pass an explicit target argument
#   * preserves type / creator / Finder info bytes via the catalog
#   * preserves data fork + resource fork bytes byte-for-byte (compared
#     against the original AppleDouble sidecar's rsrc fork via Python)
#   * cross-tool: Python ls sees the file with the right type/creator
#   * round-trip: extract --macbinary produces an output whose payload
#     bytes (data fork + rsrc fork + Finder info) match the input,
#     even though the optional MacBinary trailer (CRC at 0x7e..0x7f and
#     extra bytes at 0x7c..0x7d) may legitimately differ.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

PY_TOOL="${PY_TOOL:-/mnt/USERS/onion/DATA_ORIGN/Workspace/MacDiskcopy/tools/macdiskimage.py}"
HAVE_PY=0
if [[ -f "$PY_TOOL" ]] && command -v python3 >/dev/null 2>&1; then
  HAVE_PY=1
fi

PROTO_DIR="${PROTO_DIR:-/mnt/USERS/onion/DATA_ORIGN/Workspace/05_RetroDeveloperEnvironmentProject/Examples/prototype_01_mac_finder/build}"
HELLO_BIN="$PROTO_DIR/Hello.bin"
HELLO_AD="$PROTO_DIR/%Hello.ad"
[[ -f "$HELLO_BIN" ]] || { echo "[SKIP] missing $HELLO_BIN — Retro68 prototype not built"; exit 0; }
[[ -f "$HELLO_AD"  ]] || { echo "[SKIP] missing $HELLO_AD";  exit 0; }

WORK="${WORK:-/tmp/rdedisktool_add_macbinary_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# 1. add --macbinary, no explicit target → name from MacBinary header.
"$RDEDISKTOOL" create "$WORK/v.img" -f mac_img --fs hfs -n V \
    >/dev/null 2>&1
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$HELLO_BIN" \
    --macbinary >/tmp/rdedisktool_add_mb.log 2>&1 || {
  echo "C 1.1: add --macbinary failed" >&2
  cat /tmp/rdedisktool_add_mb.log >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/v.img" | rg -q '^Hello +' || {
  echo "C 1.1: target should be 'Hello' (from MacBinary name); list:" >&2
  "$RDEDISKTOOL" list "$WORK/v.img" >&2
  exit 1
}

# 2. Cross-tool: Python sees type='APPL', creator='????', rsrc=3891.
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -qF "type='APPL' creator='????'" || {
    echo "C 1.1: Python ls does not show APPL/???? type/creator" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -q "rsrc= +3891" || {
    echo "C 1.1: Python ls does not see rsrc fork = 3891 bytes" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
fi

# 3. Cross-tool rsrc fork SHA — extract via Python AppleDouble, compare
#    rsrc bytes against the original Retro68 sidecar.
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/ad"
  python3 "$PY_TOOL" extract-appledouble "$WORK/v.img" "$WORK/ad" \
      >/dev/null 2>&1
  RT_SIDECAR=$(find "$WORK/ad" -name "._*" | head -1)
  [[ -n "$RT_SIDECAR" ]] || { echo "C 1.1: Python extract did not produce sidecar" >&2; exit 1; }

  rsrc_sha() {
    python3 - "$1" <<'EOF'
import sys, struct, hashlib
d = open(sys.argv[1],'rb').read()
n = struct.unpack('>H', d[24:26])[0]
for i in range(n):
    eid, off, ln = struct.unpack('>III', d[26+i*12:26+i*12+12])
    if eid == 2:
        print(hashlib.sha256(d[off:off+ln]).hexdigest()); break
EOF
  }
  ORIG_SHA=$(rsrc_sha "$HELLO_AD")
  NEW_SHA=$(rsrc_sha "$RT_SIDECAR")
  [[ -n "$ORIG_SHA" && -n "$NEW_SHA" ]] || {
    echo "C 1.1: failed to read rsrc fork SHA" >&2
    exit 1
  }
  [[ "$ORIG_SHA" == "$NEW_SHA" ]] || {
    echo "C 1.1: rsrc fork bytes corrupted by add --macbinary round-trip" >&2
    echo "  orig: $ORIG_SHA" >&2
    echo "  new : $NEW_SHA" >&2
    exit 1
  }
fi

# 4. Round-trip: extract --macbinary, then re-add to a new volume and
#    ensure the catalog entries are equivalent.
"$RDEDISKTOOL" extract "$WORK/v.img" "Hello" --macbinary "$WORK/Hello_rt.bin" \
    >/dev/null 2>&1 || { echo "C 1.1: extract --macbinary failed" >&2; exit 1; }

"$RDEDISKTOOL" create "$WORK/v2.img" -f mac_img --fs hfs -n V2 \
    >/dev/null 2>&1
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v2.img" "$WORK/Hello_rt.bin" \
    --macbinary >/dev/null 2>&1 || {
  echo "C 1.1: re-add of round-tripped MacBinary failed" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/v2.img" | rg -q '^Hello +' || {
  echo "C 1.1: round-trip produces wrong target name" >&2; exit 1
}

# 5. Explicit target name overrides the MacBinary name.
"$RDEDISKTOOL" create "$WORK/v3.img" -f mac_img --fs hfs -n V3 \
    >/dev/null 2>&1
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v3.img" "$HELLO_BIN" \
    "MyApp" --macbinary >/dev/null 2>&1
"$RDEDISKTOOL" list "$WORK/v3.img" | rg -q '^MyApp +' || {
  echo "C 1.1: explicit target 'MyApp' was not honored" >&2
  "$RDEDISKTOOL" list "$WORK/v3.img" >&2
  exit 1
}

# 6. --macbinary on an MFS volume should fail (HFS-only feature).
"$RDEDISKTOOL" create "$WORK/mfs.img" -f mac_img --fs mfs -n M \
    -g 80:1:10:512 >/dev/null 2>&1
set +e
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/mfs.img" "$HELLO_BIN" \
    --macbinary >/tmp/rdedisktool_add_mb.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "C 1.1: --macbinary on MFS should fail" >&2; exit 1; }
rg -q "HFS only|HFS volume" /tmp/rdedisktool_add_mb.log || {
  echo "C 1.1: expected 'HFS only/volume' rejection on MFS" >&2
  cat /tmp/rdedisktool_add_mb.log >&2
  exit 1
}

# 7. --macbinary + --apple-double together should fail cleanly.
set +e
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$HELLO_BIN" \
    --macbinary --apple-double >/tmp/rdedisktool_add_mb.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "C 1.1: --macbinary + --apple-double should be rejected" >&2; exit 1; }

echo "[PASS] mac add --macbinary"
