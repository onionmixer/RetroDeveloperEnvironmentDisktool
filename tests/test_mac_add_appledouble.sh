#!/usr/bin/env bash
# PR-C 1.1 — `add --apple-double` regression.
#
# Verifies that adding a Retro68-produced AppleDouble pair (data fork +
# %<name>.ad sidecar):
#   * succeeds with auto-discovered sidecar
#   * derives the target name from the sidecar's filename when no
#     explicit target is given (Retro68 convention: %<name>.ad → name)
#   * preserves type / creator / Finder info
#   * preserves rsrc fork bytes byte-for-byte (cross-tool via Python)
#   * cross-tool: Python ls sees the file with the right metadata

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

PY_TOOL="${PY_TOOL:-/mnt/USERS/onion/DATA_ORIGN/Workspace/MacDiskcopy/tools/macdiskimage.py}"
HAVE_PY=0
if [[ -f "$PY_TOOL" ]] && command -v python3 >/dev/null 2>&1; then
  HAVE_PY=1
fi

PROTO_DIR="${PROTO_DIR:-/mnt/USERS/onion/DATA_ORIGN/Workspace/05_RetroDeveloperEnvironmentProject/Examples/prototype_01_mac_finder/build}"
DATA_FORK="$PROTO_DIR/Hello.APPL"
SIDECAR="$PROTO_DIR/%Hello.ad"
[[ -f "$DATA_FORK" ]] || { echo "[SKIP] missing $DATA_FORK"; exit 0; }
[[ -f "$SIDECAR"  ]] || { echo "[SKIP] missing $SIDECAR";  exit 0; }

WORK="${WORK:-/tmp/rdedisktool_add_appledouble_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# 1. add --apple-double Hello.APPL — sidecar auto-discovered as
#    %Hello.ad (Retro68 convention).
"$RDEDISKTOOL" create "$WORK/v.img" -f mac_img --fs hfs -n V \
    >/dev/null 2>&1
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$DATA_FORK" \
    --apple-double >/tmp/rdedisktool_add_ad.log 2>&1 || {
  echo "C 1.1: add --apple-double failed" >&2
  cat /tmp/rdedisktool_add_ad.log >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/v.img" | rg -q '^Hello' || {
  echo "C 1.1: target name 'Hello' not in list" >&2
  "$RDEDISKTOOL" list "$WORK/v.img" >&2
  exit 1
}

# 2. Type/creator preserved (cross-tool).
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -qF "type='APPL' creator='????'" || {
    echo "C 1.1: type=APPL creator=???? not preserved" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
  python3 "$PY_TOOL" ls "$WORK/v.img" | rg -q "rsrc= +3891" || {
    echo "C 1.1: rsrc fork size != 3891 in catalog" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
fi

# 3. Cross-tool rsrc fork SHA byte-identical.
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/ad"
  python3 "$PY_TOOL" extract-appledouble "$WORK/v.img" "$WORK/ad" \
      >/dev/null 2>&1
  RT_SIDECAR=$(find "$WORK/ad" -name "._*" | head -1)
  [[ -n "$RT_SIDECAR" ]] || { echo "C 1.1: Python extract sidecar missing" >&2; exit 1; }

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
  [[ "$(rsrc_sha "$SIDECAR")" == "$(rsrc_sha "$RT_SIDECAR")" ]] || {
    echo "C 1.1: rsrc fork bytes corrupted by add --apple-double" >&2
    exit 1
  }
fi

# 4. Sidecar discovery: try %<name>.ad style. Stage the same sidecar
#    under the macOS convention to verify ._<basename> is also picked up.
mkdir -p "$WORK/macstyle"
cp "$DATA_FORK" "$WORK/macstyle/Hello.APPL"
cp "$SIDECAR"   "$WORK/macstyle/._Hello.APPL"
"$RDEDISKTOOL" create "$WORK/mac.img" -f mac_img --fs hfs -n V \
    >/dev/null 2>&1
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/mac.img" \
    "$WORK/macstyle/Hello.APPL" --apple-double >/dev/null 2>&1 || {
  echo "C 1.1: ._<basename> sidecar discovery failed" >&2; exit 1
}
"$RDEDISKTOOL" list "$WORK/mac.img" | rg -q "Hello" || {
  echo "C 1.1: ._<basename> add did not produce expected entry" >&2; exit 1
}

# 5. Missing sidecar → clear error.
mkdir -p "$WORK/orphan"
cp "$DATA_FORK" "$WORK/orphan/Solo.APPL"
"$RDEDISKTOOL" create "$WORK/o.img" -f mac_img --fs hfs -n V \
    >/dev/null 2>&1
set +e
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/o.img" \
    "$WORK/orphan/Solo.APPL" --apple-double >/tmp/rdedisktool_add_ad.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "C 1.1: missing sidecar should fail" >&2; exit 1; }
rg -q "cannot find sidecar" /tmp/rdedisktool_add_ad.log || {
  echo "C 1.1: expected 'cannot find sidecar' message; got:" >&2
  cat /tmp/rdedisktool_add_ad.log >&2
  exit 1
}

echo "[PASS] mac add --apple-double"
