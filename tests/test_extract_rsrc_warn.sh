#!/usr/bin/env bash
# PR-A 4.1 — bare extract emits a stderr warning when the source file has
# a non-empty resource fork. Silently dropping the rsrc fork was one of
# the easiest ways to corrupt Mac applications round-tripped through this
# tool; the warning makes the data loss visible.
#
# Pass conditions:
#   * extract of a file with rsrc fork → exit 0, stderr contains "warning"
#     and "resource fork", and the data fork is still written correctly.
#   * extract of a file with NO rsrc fork → exit 0, stderr empty.
#   * extract with --apple-double or --macbinary → no warning (rsrc fork
#     is being preserved).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

FX="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
[[ -f "$FX" ]] || { echo "missing $FX" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_rsrc_warn_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# 1. File WITH rsrc fork (608's "Read Me" has rsrc=344 bytes).
"$RDEDISKTOOL" extract "$FX" "Read Me" "$WORK/readme.bin" \
    1>/dev/null 2>"$WORK/err.log" || {
  echo "extract of 'Read Me' failed unexpectedly" >&2
  cat "$WORK/err.log" >&2
  exit 1
}
[[ -s "$WORK/readme.bin" ]] || {
  echo "extract did not produce a non-empty data fork file" >&2; exit 1
}
rg -q "warning.*'Read Me'.*resource fork.*344.*bytes" "$WORK/err.log" || {
  echo "expected stderr warning for rsrc-bearing file; got:" >&2
  cat "$WORK/err.log" >&2
  exit 1
}
rg -q "use --apple-double or --macbinary" "$WORK/err.log" || {
  echo "stderr warning missing actionable hint" >&2
  cat "$WORK/err.log" >&2
  exit 1
}

# 2. File with NO rsrc fork — write one ourselves into a fresh HFS volume.
"$RDEDISKTOOL" create "$WORK/clean.img" -f mac_img --fs hfs -n V \
    >/dev/null 2>&1
printf 'plain data\n' > "$WORK/in.txt"
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/clean.img" \
    "$WORK/in.txt" "Plain.txt" >/dev/null 2>&1
"$RDEDISKTOOL" extract "$WORK/clean.img" "Plain.txt" "$WORK/out.bin" \
    1>/dev/null 2>"$WORK/err2.log"
if [[ -s "$WORK/err2.log" ]]; then
  echo "stderr should be empty for rsrc-less file extract; got:" >&2
  cat "$WORK/err2.log" >&2
  exit 1
fi

# 3. --apple-double should suppress the warning entirely (we are
#    preserving the rsrc fork via the sidecar). Output path is a file
#    path; the ._<basename> sidecar is created next to it.
"$RDEDISKTOOL" extract "$FX" "Read Me" --apple-double "$WORK/ReadMe" \
    1>/dev/null 2>"$WORK/err3.log"
if rg -q "resource fork" "$WORK/err3.log" 2>/dev/null; then
  echo "--apple-double should NOT emit the rsrc-fork warning; got:" >&2
  cat "$WORK/err3.log" >&2
  exit 1
fi
[[ -f "$WORK/ReadMe" && -f "$WORK/._ReadMe" ]] || {
  echo "--apple-double should produce data fork + sidecar; got:" >&2
  ls -la "$WORK" >&2
  exit 1
}

# 4. --macbinary should also suppress the warning.
"$RDEDISKTOOL" extract "$FX" "Read Me" --macbinary "$WORK/ReadMe.bin" \
    1>/dev/null 2>"$WORK/err4.log"
if rg -q "resource fork" "$WORK/err4.log" 2>/dev/null; then
  echo "--macbinary should NOT emit the rsrc-fork warning; got:" >&2
  cat "$WORK/err4.log" >&2
  exit 1
fi
[[ -s "$WORK/ReadMe.bin" ]] || { echo "--macbinary did not write output" >&2; exit 1; }

echo "[PASS] extract rsrc-fork drop warning"
