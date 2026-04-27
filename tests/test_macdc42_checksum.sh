#!/usr/bin/env bash
# Validates DC42 container detection + payload checksum verification on the
# Macintosh fixture set. Pass conditions:
#   * 3 known-good DC42 fixtures pass `validate` (exit 0)
#   * 3 known-good raw IMG/DSK fixtures pass `validate` (exit 0)
#   * a 1-byte-corrupted DC42 copy fails with "DC42 data checksum mismatch"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

FX="$TOOL_ROOT/tests/fixtures/macintosh"

# 1. Verify integrity of the fixtures themselves before anything else runs.
( cd "$FX" && sha256sum -c SHA256SUMS >/tmp/macdc42_sha.log 2>&1 ) || {
  echo "fixture sha256 mismatch — aborting" >&2
  cat /tmp/macdc42_sha.log >&2
  exit 1
}

# 2. All 6 well-formed fixtures (3 DC42 + 3 raw) must validate cleanly.
for f in \
  "$FX/lido.image" \
  "$FX/systemtools.image" \
  "$FX/stuffit_expander_5_5.image" \
  "$FX/LIDO.dsk" \
  "$FX/608_SystemTools.img" \
  "$FX/stuffit_expander_5.5.img"; do
  if ! "$RDEDISKTOOL" validate "$f" >/dev/null 2>&1; then
    echo "validate failed for known-good fixture: $f" >&2
    "$RDEDISKTOOL" validate "$f" >&2 || true
    exit 1
  fi
done

# 3. 1-byte-flipped DC42 copy must surface as a checksum mismatch.
WORK="${WORK:-/tmp/rdedisktool_macdc42_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$FX/systemtools.image" "$WORK/corrupt.image"
# Flip a byte well inside the data payload (header + 0x100 byte offset).
printf '\xff' | dd of="$WORK/corrupt.image" bs=1 seek=$((0x100)) count=1 conv=notrunc >/dev/null 2>&1

set +e
"$RDEDISKTOOL" validate "$WORK/corrupt.image" >/tmp/macdc42_corrupt.log 2>&1
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  echo "expected validate to fail on corrupt DC42 but it succeeded" >&2
  cat /tmp/macdc42_corrupt.log >&2
  exit 1
fi
if ! rg -q "DC42 data checksum mismatch" /tmp/macdc42_corrupt.log; then
  echo "expected 'DC42 data checksum mismatch' in stderr; got:" >&2
  cat /tmp/macdc42_corrupt.log >&2
  exit 1
fi

echo "[PASS] macdc42 checksum"
