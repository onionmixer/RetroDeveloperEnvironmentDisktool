#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

MSX_SRC="$PROJECT_ROOT/diskwork/bootdisk/msx/msxdos23.dsk"
FIXTURE="$TOOL_ROOT/tests/fixtures/README.TXT"

[[ -f "$MSX_SRC" ]] || { echo "missing $MSX_SRC" >&2; exit 1; }
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_boot_guard_msx}"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$MSX_SRC" "$WORK/msxdos23.dsk"

"$RDEDISKTOOL" extract "$WORK/msxdos23.dsk" COMMAND2.COM "$WORK/COMMAND2_before.COM"

assert_fail() {
  set +e
  "$@" >/tmp/rdedisktool_test.log 2>&1
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "expected failure but succeeded: $*" >&2
    sed -n '1,120p' /tmp/rdedisktool_test.log >&2
    exit 1
  fi
}

assert_not_policy_blocked() {
  set +e
  "$@" >/tmp/rdedisktool_test.log 2>&1
  local rc=$?
  set -e
  if rg -q "Boot disk protection" /tmp/rdedisktool_test.log; then
    echo "force override was still blocked by policy: $*" >&2
    sed -n '1,120p' /tmp/rdedisktool_test.log >&2
    exit 1
  fi
  return $rc
}

"$RDEDISKTOOL" --bootdisk-mode strict info "$WORK/msxdos23.dsk" -v >/tmp/rdedisktool_test.log 2>&1
rg -q "BootDisk:\s+yes" /tmp/rdedisktool_test.log || { echo "bootdisk detection missing for msx" >&2; sed -n '1,120p' /tmp/rdedisktool_test.log; exit 1; }

# strict mode should allow safe add when protected regions and existing files remain intact
"$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/msxdos23.dsk" "$FIXTURE" README.TXT
"$RDEDISKTOOL" list "$WORK/msxdos23.dsk" | rg -q "README.TXT"
"$RDEDISKTOOL" extract "$WORK/msxdos23.dsk" COMMAND2.COM "$WORK/COMMAND2_after.COM"
cmp "$WORK/COMMAND2_before.COM" "$WORK/COMMAND2_after.COM"

assert_not_policy_blocked "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/msxdos23.dsk" "$FIXTURE" README2.TXT
"$RDEDISKTOOL" list "$WORK/msxdos23.dsk" | rg -q "README2.TXT"

echo "[PASS] msx bootdisk guard"
