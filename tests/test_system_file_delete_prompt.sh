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

WORK="${WORK:-/tmp/rdedisktool_system_file_prompt}"
rm -rf "$WORK"
mkdir -p "$WORK"

cp "$PROJECT_ROOT/diskwork/bootdisk/AppleII/ProDOS_2_4_3.po" "$WORK/ProDOS_2_4_3.po"
cp "$PROJECT_ROOT/diskwork/bootdisk/msx/msxdos23.dsk" "$WORK/msxdos23.dsk"
cp "$PROJECT_ROOT/diskwork/bootdisk/x68000/HUMAN302.XDF" "$WORK/HUMAN302.XDF"

cancel_case() {
  local img="$1"
  local target="$2"

  set +e
  printf '\n' | "$RDEDISKTOOL" --bootdisk-mode off delete "$img" "$target" >/tmp/rdedisktool_prompt.log 2>&1
  local rc=$?
  set -e

  if [[ $rc -eq 0 ]]; then
    echo "expected cancel failure, but delete succeeded: $img $target" >&2
    sed -n '1,120p' /tmp/rdedisktool_prompt.log >&2
    exit 1
  fi

  rg -q "boot-critical file" /tmp/rdedisktool_prompt.log || {
    echo "missing boot-critical prompt text" >&2
    sed -n '1,120p' /tmp/rdedisktool_prompt.log >&2
    exit 1
  }

  "$RDEDISKTOOL" list "$img" | rg -q "$target" || {
    echo "critical file unexpectedly missing after cancel: $target" >&2
    exit 1
  }
}

cancel_case "$WORK/ProDOS_2_4_3.po" "PRODOS"
cancel_case "$WORK/msxdos23.dsk" "COMMAND2.COM"
cancel_case "$WORK/HUMAN302.XDF" "COMMAND.X"

# Force-system-file bypasses prompt and allows intentional delete.
"$RDEDISKTOOL" --bootdisk-mode off --force-system-file delete "$WORK/msxdos23.dsk" "COMMAND2.COM"
"$RDEDISKTOOL" list "$WORK/msxdos23.dsk" | rg -q "COMMAND2.COM" && {
  echo "force-system-file delete did not remove COMMAND2.COM" >&2
  exit 1
}

echo "[PASS] system file delete prompt"
