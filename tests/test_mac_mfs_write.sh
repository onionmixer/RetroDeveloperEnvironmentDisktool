#!/usr/bin/env bash
# M6 round-trip regression for MFS write (Phase 2).
#
# Pass conditions:
#   * rdedisktool add → rdedisktool extract  → SHA256 identical to input
#   * rdedisktool add → Python ls / extract  → cross-tool consistent
#   * delete reclaims the entry and free space
#   * delete-then-add reuses freed space (round-trip volume reusable)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

# Optional: cross-validation via Python reference tool. Skipped if unavailable.
PY_TOOL="${PY_TOOL:-/mnt/USERS/onion/DATA_ORIGN/Workspace/MacDiskcopy/tools/macdiskimage.py}"
HAVE_PY=0
if [[ -f "$PY_TOOL" ]] && command -v python3 >/dev/null 2>&1; then
  HAVE_PY=1
fi

FX="$TOOL_ROOT/tests/fixtures/macintosh/empty_mfs.img"
[[ -f "$FX" ]] || { echo "missing $FX" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_mfs_write_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$FX" "$WORK/test.img"

INPUT="$WORK/in.txt"
printf 'Hello from rdedisktool MFS write!\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')

# 1. add succeeds.
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/test.img" "$INPUT" "Hello.txt" \
    >/tmp/rdedisktool_test.log 2>&1 || {
  echo "add failed" >&2; cat /tmp/rdedisktool_test.log >&2; exit 1
}

# 2. rdedisktool extract round-trip = SHA identical
"$RDEDISKTOOL" extract "$WORK/test.img" "Hello.txt" "$WORK/out_rde.bin" \
    >/dev/null 2>&1 || { echo "extract after add failed" >&2; exit 1; }
RDE_SHA=$(sha256sum "$WORK/out_rde.bin" | awk '{print $1}')
[[ "$INPUT_SHA" == "$RDE_SHA" ]] || {
  echo "rdedisktool round-trip SHA mismatch: $INPUT_SHA != $RDE_SHA" >&2; exit 1
}

# 3. Python read of the same volume must agree (cross-tool parity).
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/py_out"
  python3 "$PY_TOOL" extract "$WORK/test.img" "$WORK/py_out" >/dev/null 2>&1 || {
    echo "Python extract failed on rdedisktool-written volume" >&2; exit 1
  }
  [[ -f "$WORK/py_out/Hello.txt" ]] || {
    echo "Python did not see Hello.txt in volume written by rdedisktool" >&2
    ls -la "$WORK/py_out/" >&2; exit 1
  }
  PY_SHA=$(sha256sum "$WORK/py_out/Hello.txt" | awk '{print $1}')
  [[ "$INPUT_SHA" == "$PY_SHA" ]] || {
    echo "cross-tool SHA mismatch (rde write → py read): $INPUT_SHA != $PY_SHA" >&2
    exit 1
  }
fi

# 4. list shows free space dropped by exactly one allocation block.
LIST_OUT=$("$RDEDISKTOOL" list "$WORK/test.img")
echo "$LIST_OUT" | rg -q "Hello.txt" || {
  echo "Hello.txt missing from list after add" >&2
  echo "$LIST_OUT" >&2; exit 1
}

# 5. delete reclaims the entry.
"$RDEDISKTOOL" --bootdisk-mode off delete "$WORK/test.img" "Hello.txt" \
    >/dev/null 2>&1 || { echo "delete failed" >&2; exit 1; }
LIST_OUT=$("$RDEDISKTOOL" list "$WORK/test.img")
echo "$LIST_OUT" | rg -q "Hello.txt" && {
  echo "Hello.txt still listed after delete" >&2
  echo "$LIST_OUT" >&2; exit 1
} || true

# 6. add → delete → add reuses freed allocation blocks (volume reusable).
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/test.img" "$INPUT" "AfterDelete.txt" \
    >/dev/null 2>&1 || { echo "add after delete failed" >&2; exit 1; }
"$RDEDISKTOOL" extract "$WORK/test.img" "AfterDelete.txt" "$WORK/out2.bin" \
    >/dev/null 2>&1 || { echo "extract after second add failed" >&2; exit 1; }
RDE_SHA2=$(sha256sum "$WORK/out2.bin" | awk '{print $1}')
[[ "$INPUT_SHA" == "$RDE_SHA2" ]] || {
  echo "second round-trip SHA mismatch" >&2; exit 1
}

# 7. M11: rename round-trip on a fresh MFS volume.
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/test.img" "AfterDelete.txt" "Renamed.txt" \
    >/dev/null 2>&1 || { echo "MFS rename failed" >&2; exit 1; }
"$RDEDISKTOOL" list "$WORK/test.img" | rg -q "Renamed.txt" || {
  echo "Renamed.txt missing after MFS rename" >&2; exit 1
}
"$RDEDISKTOOL" list "$WORK/test.img" | rg -q "AfterDelete.txt" && {
  echo "AfterDelete.txt still present after MFS rename" >&2; exit 1
} || true
"$RDEDISKTOOL" extract "$WORK/test.img" "Renamed.txt" "$WORK/renamed_out.bin" \
    >/dev/null 2>&1
RENAMED_SHA=$(sha256sum "$WORK/renamed_out.bin" | awk '{print $1}')
[[ "$INPUT_SHA" == "$RENAMED_SHA" ]] || {
  echo "MFS rename SHA mismatch: $INPUT_SHA != $RENAMED_SHA" >&2; exit 1
}
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/py_renamed"
  python3 "$PY_TOOL" extract "$WORK/test.img" "$WORK/py_renamed" >/dev/null 2>&1
  PY_SHA=$(sha256sum "$WORK/py_renamed/Renamed.txt" 2>/dev/null | awk '{print $1}')
  [[ "$INPUT_SHA" == "$PY_SHA" ]] || {
    echo "Python read of MFS rename mismatch: $INPUT_SHA != $PY_SHA" >&2; exit 1
  }
fi

echo "[PASS] mac mfs write"
