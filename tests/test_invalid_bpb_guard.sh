#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

MSX_SRC="$PROJECT_ROOT/diskwork/bootdisk/msx/msxdos23.dsk"
X68_SRC="$PROJECT_ROOT/diskwork/bootdisk/x68000/HUMAN302.XDF"
FIXTURE="$TOOL_ROOT/tests/fixtures/README.TXT"

[[ -f "$MSX_SRC" ]] || { echo "missing $MSX_SRC" >&2; exit 1; }
[[ -f "$X68_SRC" ]] || { echo "missing $X68_SRC" >&2; exit 1; }
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_invalid_bpb_guard}"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$MSX_SRC" "$WORK/msx_invalid_bpb.dsk"
cp "$X68_SRC" "$WORK/x68_invalid_bpb.xdf"

# Corrupt BPB bytesPerSector field (offset 0x0B..0x0C) to 0x0000
printf '\x00\x00' | dd of="$WORK/msx_invalid_bpb.dsk" bs=1 seek=$((0x0B)) count=2 conv=notrunc >/dev/null 2>&1
printf '\x00\x00' | dd of="$WORK/x68_invalid_bpb.xdf" bs=1 seek=$((0x0B)) count=2 conv=notrunc >/dev/null 2>&1

assert_fail_with() {
  local pattern="$1"
  shift
  set +e
  "$@" >/tmp/rdedisktool_test.log 2>&1
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "expected failure but succeeded: $*" >&2
    sed -n '1,160p' /tmp/rdedisktool_test.log >&2
    exit 1
  fi
  rg -q "$pattern" /tmp/rdedisktool_test.log || {
    echo "expected pattern not found: $pattern" >&2
    sed -n '1,160p' /tmp/rdedisktool_test.log >&2
    exit 1
  }
}

"$RDEDISKTOOL" info "$WORK/msx_invalid_bpb.dsk" -v >/tmp/rdedisktool_test.log 2>&1
rg -q "Reason:\s+invalid_bpb_or_filesystem_init_failed" /tmp/rdedisktool_test.log || {
  echo "missing invalid_bpb reason for MSX" >&2
  sed -n '1,160p' /tmp/rdedisktool_test.log >&2
  exit 1
}

"$RDEDISKTOOL" info "$WORK/x68_invalid_bpb.xdf" -v >/tmp/rdedisktool_test.log 2>&1
rg -q "Reason:\s+invalid_bpb_or_filesystem_init_failed" /tmp/rdedisktool_test.log || {
  echo "missing invalid_bpb reason for X68000" >&2
  sed -n '1,160p' /tmp/rdedisktool_test.log >&2
  exit 1
}

assert_fail_with "invalid BPB/metadata" \
  "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/msx_invalid_bpb.dsk" "$FIXTURE" README.TXT

assert_fail_with "invalid BPB/metadata" \
  "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/x68_invalid_bpb.xdf" "$FIXTURE" README.TXT

echo "[PASS] invalid BPB guard"
