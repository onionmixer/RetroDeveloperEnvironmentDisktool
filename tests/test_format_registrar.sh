#!/usr/bin/env bash
# Verifies that all built-in disk format classes are registered with the
# DiskImageFactory at link time (the static registrar pattern + --whole-archive
# linkage in CMakeLists.txt).
#
# This is the single-command sanity check that detects "registrar dropped from
# link" issues. If a new format class is added but its .cpp file is missing
# from CMakeLists.txt, this test fails because the registrar never runs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

OUT="$("$RDEDISKTOOL" list-formats)"

# Header line
echo "$OUT" | rg -q "^Format\sExtensions\sDisplayName$" || {
  echo "list-formats header missing or changed" >&2
  echo "$OUT" >&2
  exit 1
}

# Required identifiers: every currently-registered platform format must appear.
# AppleNIB2 is intentionally NOT in this set — it has a DiskFormat enum but no
# registered class as of this writing. If AppleNIB2 ever gets a registrar,
# add it here.
required=(
  "AppleDO"
  "ApplePO"
  "AppleNIB"
  "AppleWOZ1"
  "AppleWOZ2"
  "MSXDSK"
  "MSXDMK"
  "MSXXSA"
  "X68000XDF"
  "X68000DIM"
)

for ident in "${required[@]}"; do
  echo "$OUT" | rg -q "^${ident}\b" || {
    echo "format identifier '$ident' missing from list-formats output" >&2
    echo "$OUT" >&2
    exit 1
  }
done

echo "[PASS] format registrar"
