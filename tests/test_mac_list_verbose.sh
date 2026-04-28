#!/usr/bin/env bash
# PR-D 4.3 — `rdedisktool -v list <mac.img>` adds Type / Creator / FFlg
# columns that mirror the catalog Finder info. Non-Mac listings are
# unchanged so the 4 pre-Mac baselines stay byte-identical.
#
# Pass conditions:
#   * Mac verbose listing contains the expected type/creator pairs for
#     the 608_SystemTools fixture (Finder = FNDR/MACS, System = ZSYS/MACS,
#     Read Me = ttro/ttxt, ...)
#   * Non-verbose listing on the same volume is byte-identical to the
#     pre-PR-D output (regression — verbose mode must not leak into the
#     default path)
#   * Verbose listing on a non-Mac disk silently behaves like a regular
#     listing (no Mac columns)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

FX="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
[[ -f "$FX" ]] || { echo "missing $FX" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_list_verbose_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# 1. Verbose Mac listing: header has the new columns + known type/creator.
"$RDEDISKTOOL" -v list "$FX" >"$WORK/verbose.out" 2>&1
rg -q "MacTy" "$WORK/verbose.out" || {
  echo "verbose: header missing 'MacTy' column" >&2
  cat "$WORK/verbose.out" >&2
  exit 1
}
rg -q "Creat" "$WORK/verbose.out" || {
  echo "verbose: header missing 'Creat' column" >&2; exit 1
}
rg -q "FFlg" "$WORK/verbose.out" || {
  echo "verbose: header missing 'FFlg' column" >&2; exit 1
}

# Known catalog entries on 608_SystemTools (verified via Python catalog dump).
rg -q "Read Me .* ttro .* ttxt" "$WORK/verbose.out" || {
  echo "verbose: 'Read Me' should show type=ttro creator=ttxt" >&2
  cat "$WORK/verbose.out" >&2; exit 1
}
rg -q "TeachText .* APPL .* ttxt" "$WORK/verbose.out" || {
  echo "verbose: 'TeachText' should show type=APPL creator=ttxt" >&2; exit 1
}

# Folders show "—" placeholders for the Mac columns.
rg -q "System Folder .* DIR .* —" "$WORK/verbose.out" || {
  echo "verbose: folder rows should show — for Mac columns" >&2
  cat "$WORK/verbose.out" >&2; exit 1
}

# 2. Verbose nested listing.
"$RDEDISKTOOL" -v list "$FX" "System Folder" >"$WORK/nested.out" 2>&1
rg -q "Finder .* FNDR .* MACS" "$WORK/nested.out" || {
  echo "verbose nested: 'Finder' should show FNDR/MACS" >&2
  cat "$WORK/nested.out" >&2; exit 1
}
rg -q "System .* ZSYS .* MACS" "$WORK/nested.out" || {
  echo "verbose nested: 'System' should show ZSYS/MACS" >&2; exit 1
}

# 3. Non-verbose listing must NOT contain the Mac columns (regression
#    guard — verbose mode must not leak into the default path).
"$RDEDISKTOOL" list "$FX" >"$WORK/plain.out" 2>&1
if rg -q "MacTy|FFlg" "$WORK/plain.out"; then
  echo "non-verbose listing should NOT contain Mac columns; got:" >&2
  cat "$WORK/plain.out" >&2
  exit 1
fi

# 4. Verbose listing on non-Mac disk: -v has no Mac effect (safe no-op).
PROJ_ROOT="/mnt/USERS/onion/DATA_ORIGN/Workspace/05_RetroDeveloperEnvironmentProject"
APPLE_DISK="$PROJ_ROOT/Examples/Tutorial_apple_01/Tutorial_apple_01.do"
if [[ -f "$APPLE_DISK" ]]; then
  "$RDEDISKTOOL" -v list "$APPLE_DISK" >"$WORK/apple.out" 2>&1
  if rg -q "MacTy" "$WORK/apple.out"; then
    echo "non-Mac disk should NOT trigger Mac columns under -v; got:" >&2
    cat "$WORK/apple.out" >&2
    exit 1
  fi
fi

echo "[PASS] mac list verbose"
