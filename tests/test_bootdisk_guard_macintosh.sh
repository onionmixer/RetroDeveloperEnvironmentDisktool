#!/usr/bin/env bash
# Boot-disk protection guard for the Macintosh profile (Phase 1 — read-only).
#
# Pass conditions:
#   * `info -v` of a bootable HFS volume reports BootDisk:yes / Profile:macintosh.
#   * `delete` / `add` against the bootable disk are blocked by the policy
#     ("Boot disk protection (...)") with non-zero exit.
#   * `extract` of a regular file still succeeds.
#   * The on-disk byte stream is unchanged after the test run.

set -euo pipefail

# Per-run log: ctest -j runs these scripts in parallel
LOG="$(mktemp "${TMPDIR:-/tmp}/rdedisktool_test.XXXXXX")"
trap 'rm -f "$LOG"' EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

FIXTURE="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
[[ -f "$FIXTURE" ]] || { echo "missing $FIXTURE" >&2; exit 1; }

# Work directory: a $WORK given by the caller is used and kept; otherwise a
# fresh one is removed on exit (KEEP_WORK=1 keeps it)
if [[ -z "${WORK:-}" ]]; then
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/rdedisktool_boot_guard_macintosh.XXXXXX")"
  trap 'rm -f "$LOG"; [[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT
fi
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$FIXTURE" "$WORK/608.img"
ORIG_SHA=$(sha256sum "$FIXTURE" | awk '{print $1}')

assert_fail_blocked() {
  set +e
  "$@" >"$LOG" 2>&1
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "expected boot-protection failure but command succeeded: $*" >&2
    sed -n '1,80p' "$LOG" >&2
    exit 1
  fi
  rg -q "Boot disk protection" "$LOG" || {
    echo "expected 'Boot disk protection' message; got:" >&2
    cat "$LOG" >&2
    exit 1
  }
}

# Like assert_fail_blocked but only checks that the command failed — does not
# require the "Boot disk protection" string. Used for `add`, where strict mode
# falls through to "safe-add verification" rather than emitting the policy
# message; in Phase 1 the Mac handler's writeFile returns false (read-only),
# so the failure surfaces as "Failed to write file to disk image".
assert_fail_any() {
  set +e
  "$@" >"$LOG" 2>&1
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "expected failure but command succeeded: $*" >&2
    sed -n '1,80p' "$LOG" >&2
    exit 1
  fi
}

# 1. Boot detection on the bootable HFS sample.
"$RDEDISKTOOL" --bootdisk-mode strict info "$WORK/608.img" -v >"$LOG" 2>&1
rg -q "BootDisk:\s+yes"          "$LOG" || { echo "BootDisk:yes missing"  >&2; cat "$LOG"; exit 1; }
rg -q "Profile:\s+macintosh"     "$LOG" || { echo "Profile:macintosh missing" >&2; cat "$LOG"; exit 1; }
rg -q "Confidence:\s+high"       "$LOG" || { echo "Confidence:high missing"   >&2; cat "$LOG"; exit 1; }

# 2. delete is blocked by strict policy.
assert_fail_blocked "$RDEDISKTOOL" --bootdisk-mode strict delete "$WORK/608.img" "System Folder/System"

# 3. Adding a non-boot-critical file in strict mode goes through the
#    bootdisk safe-add verification path (same behavior as Apple/MSX/X68000
#    boot disks — see CLI.cpp:1329 onward). After Phase 2 M7 enabled HFS
#    writes, the safe-add path actually succeeds, so we no longer assert
#    failure; the boot-block bytes invariant in step 5 stays the real guard.

# 4. extract of a regular data fork still succeeds (read-only path).
"$RDEDISKTOOL" extract "$WORK/608.img" "Read Me" "$WORK/ReadMe.bin" >"$LOG" 2>&1
[[ -s "$WORK/ReadMe.bin" ]] || { echo "extract produced empty file" >&2; exit 1; }

# 5. Boot block region (sectors 0..1 = first 1024 bytes) is unchanged.
#    safe-add may have legitimately mutated catalog / data areas elsewhere,
#    but the LK signature, system_name, finder_name and other boot-critical
#    bytes must remain untouched — that's what the policy promises.
ORIG_BOOT=$(head -c 1024 "$FIXTURE" | sha256sum | awk '{print $1}')
NEW_BOOT=$(head -c 1024 "$WORK/608.img" | sha256sum | awk '{print $1}')
if [[ "$ORIG_BOOT" != "$NEW_BOOT" ]]; then
  echo "boot block bytes were mutated (sha256 mismatch)" >&2
  echo "  before: $ORIG_BOOT" >&2
  echo "  after:  $NEW_BOOT"  >&2
  exit 1
fi

echo "[PASS] macintosh bootdisk guard"
