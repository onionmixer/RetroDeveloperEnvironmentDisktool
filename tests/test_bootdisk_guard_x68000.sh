#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

X68_SRC="$PROJECT_ROOT/diskwork/bootdisk/x68000/HUMAN302.XDF"
FIXTURE="$TOOL_ROOT/tests/fixtures/README.TXT"

[[ -f "$X68_SRC" ]] || { echo "missing $X68_SRC" >&2; exit 1; }
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_boot_guard_x68000}"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$X68_SRC" "$WORK/HUMAN302.XDF"

"$RDEDISKTOOL" extract "$WORK/HUMAN302.XDF" COMMAND.X "$WORK/COMMAND_before.X"

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

"$RDEDISKTOOL" --bootdisk-mode strict info "$WORK/HUMAN302.XDF" -v >/tmp/rdedisktool_test.log 2>&1
rg -q "BootDisk:\s+yes" /tmp/rdedisktool_test.log || { echo "bootdisk detection missing for x68000" >&2; sed -n '1,120p' /tmp/rdedisktool_test.log; exit 1; }

"$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/HUMAN302.XDF" "$FIXTURE" README.TXT
"$RDEDISKTOOL" list "$WORK/HUMAN302.XDF" | rg -q "README.TXT"
"$RDEDISKTOOL" extract "$WORK/HUMAN302.XDF" COMMAND.X "$WORK/COMMAND_after.X"
cmp "$WORK/COMMAND_before.X" "$WORK/COMMAND_after.X"

assert_not_policy_blocked "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/HUMAN302.XDF" "$FIXTURE" README2.TXT
"$RDEDISKTOOL" list "$WORK/HUMAN302.XDF" | rg -q "README2.TXT"

echo "[PASS] x68000 bootdisk guard"
