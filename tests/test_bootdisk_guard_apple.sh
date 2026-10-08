#!/usr/bin/env bash
set -euo pipefail

# Per-run log: ctest -j runs these scripts in parallel
LOG="$(mktemp "${TMPDIR:-/tmp}/rdedisktool_test.XXXXXX")"
trap 'rm -f "$LOG"' EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$TOOL_ROOT/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

DOS33_SRC="$PROJECT_ROOT/diskwork/bootdisk/AppleII/dos33.dsk"
PRODOS_SRC="$PROJECT_ROOT/diskwork/bootdisk/AppleII/prodos242.dsk"
PRODOS243_SRC="$PROJECT_ROOT/diskwork/bootdisk/AppleII/ProDOS_2_4_3.po"
FIXTURE="$TOOL_ROOT/tests/fixtures/README.TXT"

[[ -f "$DOS33_SRC" ]] || { echo "missing $DOS33_SRC" >&2; exit 1; }
[[ -f "$PRODOS_SRC" ]] || { echo "missing $PRODOS_SRC" >&2; exit 1; }
[[ -f "$PRODOS243_SRC" ]] || { echo "missing $PRODOS243_SRC" >&2; exit 1; }
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

# Work directory: a $WORK given by the caller is used and kept; otherwise a
# fresh one is removed on exit (KEEP_WORK=1 keeps it)
if [[ -z "${WORK:-}" ]]; then
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/rdedisktool_boot_guard_apple.XXXXXX")"
  trap 'rm -f "$LOG"; [[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT
fi
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$DOS33_SRC" "$WORK/dos33.dsk"
cp "$PRODOS_SRC" "$WORK/prodos242.dsk"
cp "$PRODOS243_SRC" "$WORK/prodos243.po"
# the same boot disk in DOS sector order (boot blocks are DOS sectors 0,14,13,12)
"$RDEDISKTOOL" convert "$WORK/prodos243.po" "$WORK/prodos243.dsk" -f do >/dev/null

"$RDEDISKTOOL" extract "$WORK/dos33.dsk" INTBASIC "$WORK/INTBASIC_before"
"$RDEDISKTOOL" extract "$WORK/prodos242.dsk" PRODOS "$WORK/PRODOS_before"

assert_fail() {
  set +e
  "$@" >"$LOG" 2>&1
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "expected failure but succeeded: $*" >&2
    sed -n '1,120p' "$LOG" >&2
    exit 1
  fi
}

assert_not_policy_blocked() {
  set +e
  "$@" >"$LOG" 2>&1
  local rc=$?
  set -e
  if rg -q "Boot disk protection" "$LOG"; then
    echo "force override was still blocked by policy: $*" >&2
    sed -n '1,120p' "$LOG" >&2
    exit 1
  fi
  return $rc
}

"$RDEDISKTOOL" --bootdisk-mode strict info "$WORK/prodos242.dsk" -v >"$LOG" 2>&1
rg -q "BootDisk:\s+yes" "$LOG" || { echo "bootdisk detection missing for prodos" >&2; sed -n '1,120p' "$LOG"; exit 1; }

# expect_add <rc: ok|fail> <log pattern> <cmd...>: the outcome of an add is judged,
# not just "not blocked by policy"
expect_add() {
  local want=$1 pattern=$2; shift 2
  local rc=0
  assert_not_policy_blocked "$@" || rc=$?
  if [[ $want == ok && $rc -ne 0 ]] || [[ $want == fail && $rc -eq 0 ]]; then
    echo "expected add to $want (rc=$rc): $*" >&2
    sed -n '1,120p' "$LOG" >&2
    exit 1
  fi
  rg -q "$pattern" "$LOG" || {
    echo "expected '$pattern' from: $*" >&2
    sed -n '1,120p' "$LOG" >&2
    exit 1
  }
}

# prodos242.dsk is full: the add stops before the safe-add check can run
expect_add fail "Not enough space" "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/prodos242.dsk" "$FIXTURE" README.TXT
expect_add ok "Bootdisk safe-add verification enabled" "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/dos33.dsk" "$FIXTURE" README
"$RDEDISKTOOL" extract "$WORK/dos33.dsk" README "$WORK/README_dos33"
cmp "$FIXTURE" "$WORK/README_dos33"

# ProDOS 2.4.3 has free space: safe-add must pass in both sector orders
for img in prodos243.po prodos243.dsk; do
  "$RDEDISKTOOL" extract "$WORK/$img" PRODOS "$WORK/PRODOS243_before_$img"
  expect_add ok "Bootdisk safe-add verification enabled" "$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/$img" "$FIXTURE" README.TXT
  "$RDEDISKTOOL" extract "$WORK/$img" README.TXT "$WORK/README_$img"
  cmp "$FIXTURE" "$WORK/README_$img"
  "$RDEDISKTOOL" extract "$WORK/$img" PRODOS "$WORK/PRODOS243_after_$img"
  cmp "$WORK/PRODOS243_before_$img" "$WORK/PRODOS243_after_$img"
done

"$RDEDISKTOOL" extract "$WORK/dos33.dsk" INTBASIC "$WORK/INTBASIC_after"
cmp "$WORK/INTBASIC_before" "$WORK/INTBASIC_after"
"$RDEDISKTOOL" extract "$WORK/prodos242.dsk" PRODOS "$WORK/PRODOS_after"
cmp "$WORK/PRODOS_before" "$WORK/PRODOS_after"

# Force override should bypass policy; filesystem-level failure (e.g. no space)
# is acceptable in this guard test.
expect_add fail "Not enough space" "$RDEDISKTOOL" --bootdisk-mode strict --force-bootdisk add "$WORK/prodos242.dsk" "$FIXTURE" README.TXT

echo "[PASS] apple bootdisk guard"
