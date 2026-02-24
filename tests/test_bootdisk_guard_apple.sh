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

DOS33_SRC="$PROJECT_ROOT/diskwork/bootdisk/AppleII/dos33.dsk"
PRODOS_SRC="$PROJECT_ROOT/diskwork/bootdisk/AppleII/prodos242.dsk"
FIXTURE="$TOOL_ROOT/tests/fixtures/README.TXT"

[[ -f "$DOS33_SRC" ]] || { echo "missing $DOS33_SRC" >&2; exit 1; }
[[ -f "$PRODOS_SRC" ]] || { echo "missing $PRODOS_SRC" >&2; exit 1; }
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_boot_guard_apple}"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$DOS33_SRC" "$WORK/dos33.dsk"
cp "$PRODOS_SRC" "$WORK/prodos242.dsk"

"$RDEDISKTOOL" extract "$WORK/dos33.dsk" INTBASIC "$WORK/INTBASIC_before"
"$RDEDISKTOOL" extract "$WORK/prodos242.dsk" PRODOS "$WORK/PRODOS_before"

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

"$RDEDISKTOOL" --bootdisk-mode strict info "$WORK/prodos242.dsk" -v >/tmp/rdedisktool_test.log 2>&1
rg -q "BootDisk:\s+yes" /tmp/rdedisktool_test.log || { echo "bootdisk detection missing for prodos" >&2; sed -n '1,120p' /tmp/rdedisktool_test.log; exit 1; }

assert_not_policy_blocked "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/prodos242.dsk" "$FIXTURE" README.TXT || true
assert_not_policy_blocked "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/dos33.dsk" "$FIXTURE" README || true

"$RDEDISKTOOL" extract "$WORK/dos33.dsk" INTBASIC "$WORK/INTBASIC_after"
cmp "$WORK/INTBASIC_before" "$WORK/INTBASIC_after"
"$RDEDISKTOOL" extract "$WORK/prodos242.dsk" PRODOS "$WORK/PRODOS_after"
cmp "$WORK/PRODOS_before" "$WORK/PRODOS_after"

# Force override should bypass policy; filesystem-level failure (e.g. no space)
# is acceptable in this guard test.
assert_not_policy_blocked "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/prodos242.dsk" "$FIXTURE" README.TXT || true

echo "[PASS] apple bootdisk guard"
