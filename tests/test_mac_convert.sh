#!/usr/bin/env bash
# M9 round-trip regression for the Macintosh DC42 ↔ IMG converter (Phase 3).
#
# Pass conditions:
#   * IMG → DC42 produces a valid Disk Copy 4.2 container that the Python
#     reference tool can also detect + load + extract.
#   * DC42 → IMG strips the 0x54 header losslessly.
#   * IMG → DC42 → IMG round-trip preserves the raw sector stream byte-for-byte.

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

FX_DIR="$TOOL_ROOT/tests/fixtures/macintosh"
SRC_IMG="$FX_DIR/608_SystemTools.img"
SRC_DC42="$FX_DIR/systemtools.image"
[[ -f "$SRC_IMG" ]] || { echo "missing $SRC_IMG" >&2; exit 1; }
[[ -f "$SRC_DC42" ]] || { echo "missing $SRC_DC42" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_mac_convert_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"
SRC_SHA=$(sha256sum "$SRC_IMG" | awk '{print $1}')

# 1. IMG → DC42 → IMG round-trip preserves raw bytes.
"$RDEDISKTOOL" convert "$SRC_IMG" "$WORK/wrap.dc42" >/dev/null 2>&1 \
    || { echo "IMG→DC42 failed" >&2; exit 1; }
"$RDEDISKTOOL" convert "$WORK/wrap.dc42" "$WORK/round.img" --format mac_img \
    >/dev/null 2>&1 || { echo "DC42→IMG failed" >&2; exit 1; }
ROUND_SHA=$(sha256sum "$WORK/round.img" | awk '{print $1}')
[[ "$SRC_SHA" == "$ROUND_SHA" ]] || {
  echo "round-trip SHA mismatch: $SRC_SHA != $ROUND_SHA" >&2; exit 1
}

# 2. DC42 → IMG produces an HFS-readable raw image.
"$RDEDISKTOOL" convert "$SRC_DC42" "$WORK/back.img" --format mac_img \
    >/dev/null 2>&1 || { echo "DC42→IMG (real DC42) failed" >&2; exit 1; }
"$RDEDISKTOOL" info "$WORK/back.img" 2>/dev/null | rg -q "^File System: HFS" || {
  echo "converted IMG does not parse as HFS" >&2; exit 1
}

# 3. The DC42 we produced must be valid + readable by the Python tool.
"$RDEDISKTOOL" validate "$WORK/wrap.dc42" >/dev/null 2>&1 || {
  echo "rdedisktool-written DC42 fails its own validate" >&2; exit 1
}
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" detect "$WORK/wrap.dc42" >/dev/null 2>&1 || {
    echo "Python detect rejects rdedisktool-written DC42" >&2; exit 1
  }
  mkdir -p "$WORK/py_out"
  python3 "$PY_TOOL" extract "$WORK/wrap.dc42" "$WORK/py_out" >/dev/null 2>&1 || {
    echo "Python extract rejects rdedisktool-written DC42" >&2; exit 1
  }
  # 'Read Me' data fork should match the same fixture's Python extract from
  # the original raw IMG.
  mkdir -p "$WORK/py_orig"
  python3 "$PY_TOOL" extract "$SRC_IMG" "$WORK/py_orig" >/dev/null 2>&1
  RDE_SHA=$(sha256sum "$WORK/py_out/Read Me" 2>/dev/null | awk '{print $1}')
  ORIG_SHA=$(sha256sum "$WORK/py_orig/Read Me" 2>/dev/null | awk '{print $1}')
  [[ "$RDE_SHA" == "$ORIG_SHA" && -n "$RDE_SHA" ]] || {
    echo "Python extract of rdedisktool DC42 differs from original" >&2
    echo "  rde DC42→Python: $RDE_SHA" >&2
    echo "  orig IMG→Python: $ORIG_SHA" >&2
    exit 1
  }
fi

echo "[PASS] mac convert"
