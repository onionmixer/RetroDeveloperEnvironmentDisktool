#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/test_bootdisk_guard_apple.sh"
"$SCRIPT_DIR/test_bootdisk_guard_msx.sh"
"$SCRIPT_DIR/test_bootdisk_guard_x68000.sh"
"$SCRIPT_DIR/test_invalid_bpb_guard.sh"
"$SCRIPT_DIR/test_system_file_delete_prompt.sh"
"$SCRIPT_DIR/test_format_registrar.sh"
"$SCRIPT_DIR/test_macdc42_checksum.sh"
"$SCRIPT_DIR/test_bootdisk_guard_macintosh.sh"

echo "[PASS] all bootdisk guard tests"
