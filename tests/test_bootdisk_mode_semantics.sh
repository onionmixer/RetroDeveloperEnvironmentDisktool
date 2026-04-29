#!/usr/bin/env bash
# PR-B 2.1 — bootdisk-mode strict / warn / off semantic matrix.
#
# Pre-PR-B: warn behaved exactly like strict for delete/mkdir/rmdir/rename
# (all blocked without --force-bootdisk per call). Post-PR-B:
#
#   strict (default)  add: safe-add  | destructive: BLOCKED (force needed)
#   warn              add: safe-add  | destructive: ALLOWED + stderr warning
#   off               add: free      | destructive: free
#
# Critical-file [y/N] confirmation in delete still fires regardless of mode.
# safe-add still runs for both strict and warn (boot block protected).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

FX="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
[[ -f "$FX" ]] || { echo "missing $FX" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_bootdisk_semantics_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"
printf 'plain\n' > "$WORK/in.txt"

# === strict: destructive ops are BLOCKED ====================================
cp "$FX" "$WORK/strict.img"
set +e
"$RDEDISKTOOL" --bootdisk-mode strict delete "$WORK/strict.img" "TeachText" \
    >/tmp/rdedisktool_modes.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || {
  echo "strict + delete on bootdisk should be blocked, but exited 0" >&2
  cat /tmp/rdedisktool_modes.log >&2
  exit 1
}
rg -q "Boot disk protection.*strict.*blocked" /tmp/rdedisktool_modes.log || {
  echo "strict + delete: expected 'Boot disk protection (strict): ... blocked'" >&2
  cat /tmp/rdedisktool_modes.log >&2
  exit 1
}

# === warn: destructive ops are ALLOWED (with stderr warning) ================
cp "$FX" "$WORK/warn.img"
"$RDEDISKTOOL" --bootdisk-mode warn delete "$WORK/warn.img" "TeachText" \
    1>"$WORK/warn_out.log" 2>"$WORK/warn_err.log" || {
  echo "warn + delete on bootdisk should succeed, but exited non-zero" >&2
  cat "$WORK/warn_err.log" >&2
  exit 1
}
rg -q "warning.*bootdisk-mode warn.*destructive mutation allowed" "$WORK/warn_err.log" || {
  echo "warn + delete: expected stderr warning, got:" >&2
  cat "$WORK/warn_err.log" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/warn.img" | rg -q "TeachText" && {
  echo "warn + delete: TeachText still listed after delete" >&2
  exit 1
} || true

# === off: destructive ops are SILENT ========================================
cp "$FX" "$WORK/off.img"
"$RDEDISKTOOL" --bootdisk-mode off delete "$WORK/off.img" "TeachText" \
    1>"$WORK/off_out.log" 2>"$WORK/off_err.log" || {
  echo "off + delete on bootdisk should succeed" >&2
  cat "$WORK/off_err.log" >&2
  exit 1
}
if rg -q "warning.*bootdisk-mode" "$WORK/off_err.log" 2>/dev/null; then
  echo "off + delete: should NOT emit bootdisk-mode warning, got:" >&2
  cat "$WORK/off_err.log" >&2
  exit 1
fi

# === safe-add still runs in strict AND warn (boot block protected) ==========
for mode in strict warn; do
  cp "$FX" "$WORK/sa_$mode.img"
  ORIG_BOOT=$(head -c 1024 "$WORK/sa_$mode.img" | sha256sum | awk '{print $1}')
  "$RDEDISKTOOL" --bootdisk-mode "$mode" add "$WORK/sa_$mode.img" \
      "$WORK/in.txt" "Probe_$mode.txt" >/tmp/rdedisktool_modes.log 2>&1 || {
    echo "$mode + add: failed" >&2
    cat /tmp/rdedisktool_modes.log >&2
    exit 1
  }
  rg -q "Bootdisk safe-add verification enabled" /tmp/rdedisktool_modes.log || {
    echo "$mode + add: expected safe-add verification message, got:" >&2
    cat /tmp/rdedisktool_modes.log >&2
    exit 1
  }
  NEW_BOOT=$(head -c 1024 "$WORK/sa_$mode.img" | sha256sum | awk '{print $1}')
  [[ "$ORIG_BOOT" == "$NEW_BOOT" ]] || {
    echo "$mode + add: boot block mutated (sha256 mismatch — safe-add failed silently)" >&2
    exit 1
  }
done

# === warn + delete of critical file still asks [y/N] (and rejects on 'n') ====
cp "$FX" "$WORK/critical.img"
set +e
echo n | "$RDEDISKTOOL" --bootdisk-mode warn delete "$WORK/critical.img" \
    "System Folder/System" >/tmp/rdedisktool_modes.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || {
  echo "warn + delete System (critical) with 'n' input should reject" >&2
  cat /tmp/rdedisktool_modes.log >&2
  exit 1
}
rg -q "boot-critical file" /tmp/rdedisktool_modes.log || {
  echo "warn + delete System: expected 'boot-critical file' prompt, got:" >&2
  cat /tmp/rdedisktool_modes.log >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/critical.img" "System Folder" \
    | rg -q '^System +[0-9]+ +FILE' || {
  echo "warn + delete System with 'n': System file should still exist" >&2
  "$RDEDISKTOOL" list "$WORK/critical.img" "System Folder" >&2
  exit 1
}

# === --force-system-file bypass works in warn mode ==========================
cp "$FX" "$WORK/force.img"
"$RDEDISKTOOL" --bootdisk-mode warn --force-system-file delete \
    "$WORK/force.img" "System Folder/System" \
    >/tmp/rdedisktool_modes.log 2>&1 || {
  echo "warn + --force-system-file delete System should succeed" >&2
  cat /tmp/rdedisktool_modes.log >&2
  exit 1
}
if "$RDEDISKTOOL" list "$WORK/force.img" "System Folder" \
    | rg -q '^System +[0-9]+ +FILE'; then
  echo "warn + --force-system-file: System file should be gone" >&2
  "$RDEDISKTOOL" list "$WORK/force.img" "System Folder" >&2
  exit 1
fi

echo "[PASS] bootdisk-mode strict/warn/off semantic matrix"
