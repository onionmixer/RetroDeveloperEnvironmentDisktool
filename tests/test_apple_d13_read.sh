#!/usr/bin/env bash
# 13-sector (DOS 3.2) disks: read-only support.
#
# Expected values come from tests/tools/a2_nibref.py (5-and-3 model derived
# from real DOS 3.2 masters), never from rdedisktool:
#   - a synthetic DOS 3.2 disk (.d13) with two files and a known free count
#   - NIB / rotated NIB / NB2 / WOZ2 / WOZ1 images of it and of a random .d13
# Checks: detection (13 sectors, "DOS 3.2"), convert -> .d13 byte-identical,
# list / extract / free space, every write refused with the image unchanged,
# conversions between 13- and 16-sector formats refused, 16-sector images
# still read as 16-sector.
# Optional: A2_REAL_D13_DIR = directory with "Apple DOS 3.2.1 Standard.nib",
# its ".d13" and "DOS 3.2 System Master.woz" (asimov images/masters).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
REF="$SCRIPT_DIR/tools/a2_nibref.py"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rde_a2_d13.XXXXXX")"
trap '[[ -n "${KEEP_WORK:-}" ]] || rm -rf "$WORK"' EXIT

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }
fail() { echo "FAIL: $*" >&2; exit 1; }
rc_of() {
  set +e
  "$@" >"$WORK/out.log" 2>&1
  local rc=$?
  set -e
  [[ $rc -lt 128 ]] || { cat "$WORK/out.log" >&2; fail "crashed (rc=$rc): $*"; }
  echo "$rc"
}

# --- reference images
mkdir "$WORK/exp"
python3 -I "$REF" make-d13-fs "$WORK/fs.d13" 7 "$WORK/exp"
python3 -I -c 'import random, sys
r = random.Random(1313)
open(sys.argv[1], "wb").write(bytes(r.randrange(256) for _ in range(116480)))' "$WORK/rnd.d13"
declare -A EXT=([nib]=nib [nibrot]=nib [nb2]=nb2 [woz]=woz [woz1]=woz)
for k in nib nibrot nb2 woz woz1; do
  python3 -I "$REF" make-13 "$k" "$WORK/fs.d13" "$WORK/fs_$k.${EXT[$k]}"
  python3 -I "$REF" make-13 "$k" "$WORK/rnd.d13" "$WORK/rnd_$k.${EXT[$k]}"
done
FREE=$(cat "$WORK/exp/free")
[[ $FREE -gt 0 ]] || fail "reference free count"

# --- every 13-sector image (the .d13 itself and its nibble/bit images)
for img in "$WORK/fs.d13" "$WORK"/fs_*.nib "$WORK"/fs_*.nb2 "$WORK"/fs_*.woz; do
  n=$(basename "$img")
  "$RDEDISKTOOL" info "$img" >"$WORK/info.txt"
  grep -q "Sectors/Track: 13" "$WORK/info.txt" || fail "$n: not read as 13-sector"; pass
  grep -q "File System: DOS 3.2" "$WORK/info.txt" || fail "$n: not DOS 3.2"; pass
  grep -q "Free Space: $FREE bytes" "$WORK/info.txt" || { cat "$WORK/info.txt" >&2; fail "$n: free space != $FREE"; }
  pass

  "$RDEDISKTOOL" validate "$img" >"$WORK/val.txt" || { cat "$WORK/val.txt" >&2; fail "$n: validate"; }
  grep -q "File system: DOS 3.2" "$WORK/val.txt" || fail "$n: handler is not DOS 3.2"
  grep -q "0 error(s), 0 warning(s)" "$WORK/val.txt" || { cat "$WORK/val.txt" >&2; fail "$n: validate found problems"; }
  pass

  "$RDEDISKTOOL" list "$img" >"$WORK/list.txt"
  grep -q "^HELLO " "$WORK/list.txt" && grep -q "^NOTES " "$WORK/list.txt" || fail "$n: list"
  grep -q "^2 file(s)" "$WORK/list.txt" || fail "$n: file count"; pass

  for f in HELLO NOTES; do
    rm -f "$WORK/x_$f"
    "$RDEDISKTOOL" extract "$img" "$f" "$WORK/x_$f" >/dev/null
    cmp -s "$WORK/exp/$f" "$WORK/x_$f" || fail "$n: extract $f"; pass
  done

  if [[ $n != fs.d13 ]]; then
    rm -f "$WORK/back.d13"
    [[ $(rc_of "$RDEDISKTOOL" convert "$img" "$WORK/back.d13" -f d13) == 0 ]] || { cat "$WORK/out.log" >&2; fail "$n: convert -> d13"; }
    cmp -s "$WORK/fs.d13" "$WORK/back.d13" || fail "$n: convert -> d13 not byte-identical"; pass
  fi

  # writes are refused and leave the image unchanged
  cp "$img" "$WORK/try.${n##*.}"
  echo x >"$WORK/t.txt"
  for cmd in "add $WORK/try.${n##*.} $WORK/t.txt NEW" "delete $WORK/try.${n##*.} HELLO" \
             "rename $WORK/try.${n##*.} HELLO HI"; do
    # shellcheck disable=SC2086
    [[ $(rc_of "$RDEDISKTOOL" --bootdisk-mode warn $cmd) != 0 ]] || fail "$n: ${cmd%% *} accepted"
    grep -q "read-only" "$WORK/out.log" || { cat "$WORK/out.log" >&2; fail "$n: ${cmd%% *} refused for another reason"; }
    cmp -s "$img" "$WORK/try.${n##*.}" || fail "$n: ${cmd%% *} changed the image"; pass
  done

  # 13 -> 16-sector formats refused, nothing written
  for to in do po nib woz; do
    rm -f "$WORK/no.$to"
    [[ $(rc_of "$RDEDISKTOOL" convert "$img" "$WORK/no.$to" -f "$to") == 1 ]] || fail "$n: convert -> $to accepted"
    grep -q "convert only to .d13" "$WORK/out.log" || { cat "$WORK/out.log" >&2; fail "$n: -> $to refused for another reason"; }
    [[ ! -e "$WORK/no.$to" ]] || fail "$n: -> $to wrote a file"; pass
  done
done

# --- random sector data (no file system): every bit of the 5-and-3 decoder
for img in "$WORK"/rnd_*.nib "$WORK"/rnd_*.nb2 "$WORK"/rnd_*.woz; do
  rm -f "$WORK/back.d13"
  [[ $(rc_of "$RDEDISKTOOL" convert "$img" "$WORK/back.d13" -f d13) == 0 ]] || { cat "$WORK/out.log" >&2; fail "$(basename "$img"): convert"; }
  cmp -s "$WORK/rnd.d13" "$WORK/back.d13" || fail "$(basename "$img"): random data not byte-identical"; pass
done

# --- a broken data epilogue loses that sector only (convert exit 2)
python3 -I "$REF" make-13 nibbad "$WORK/rnd.d13" "$WORK/bad.nib"
rm -f "$WORK/back.d13"
[[ $(rc_of "$RDEDISKTOOL" convert "$WORK/bad.nib" "$WORK/back.d13" -f d13) == 2 ]] || { cat "$WORK/out.log" >&2; fail "bad epilogue: exit 2 expected"; }
grep -q "T5/S0/H3" "$WORK/out.log" || { cat "$WORK/out.log" >&2; fail "bad epilogue: lost sector not reported"; }
python3 -I - "$WORK/rnd.d13" "$WORK/back.d13" <<'EOF2' || fail "bad epilogue: only T5/S3 may differ (and be blank)"
import sys
a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
bad = (5 * 13 + 3) * 256
diff = {i // 256 for i in range(len(a)) if a[i] != b[i]}
sys.exit(0 if diff == {bad // 256} and b[bad:bad + 256] == bytes(256) else 1)
EOF2
pass

# --- a 16-sector track inside a 13-sector disk yields no 13-sector sectors
python3 -I "$REF" make-13 nibmix "$WORK/rnd.d13" "$WORK/mix.nib"
rm -f "$WORK/back.d13"
[[ $(rc_of "$RDEDISKTOOL" convert "$WORK/mix.nib" "$WORK/back.d13" -f d13) == 2 ]] || { cat "$WORK/out.log" >&2; fail "mixed track: exit 2 expected"; }
[[ $(grep -c "Failed to copy sector T7/" "$WORK/out.log") == 13 ]] || { cat "$WORK/out.log" >&2; fail "mixed track: 13 lost sectors on track 7 expected"; }
python3 -I - "$WORK/rnd.d13" "$WORK/back.d13" <<'EOF2' || fail "mixed track: only track 7 may differ (and be blank)"
import sys
a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
t7 = slice(7 * 13 * 256, 8 * 13 * 256)
same = a[:t7.start] == b[:t7.start] and a[t7.stop:] == b[t7.stop:]
sys.exit(0 if same and b[t7] == bytes(13 * 256) else 1)
EOF2
pass

# --- 16-sector side: still 16, and .d13 output refused
"$RDEDISKTOOL" create "$WORK/d16.do" -f do --fs dos33 >/dev/null
for f in nib woz; do
  "$RDEDISKTOOL" convert "$WORK/d16.do" "$WORK/d16.$f" -f "$f" >/dev/null
  "$RDEDISKTOOL" info "$WORK/d16.$f" >"$WORK/info.txt"
  grep -q "Sectors/Track: 16" "$WORK/info.txt" && grep -q "File System: DOS 3.3" "$WORK/info.txt" \
    || fail "16-sector $f read wrongly"; pass
done
for f in do nib woz; do
  rm -f "$WORK/no16.d13"
  [[ $(rc_of "$RDEDISKTOOL" convert "$WORK/d16.$f" "$WORK/no16.d13" -f d13) == 1 ]] || fail "16-sector $f -> d13 accepted"
  grep -q "13-sector (DOS 3.2) disks only" "$WORK/out.log" || fail "16 -> d13 refused for another reason"
  [[ ! -e "$WORK/no16.d13" ]] || fail "16 -> d13 wrote a file"; pass
done

# --- optional: real DOS 3.2 masters
if [[ -n "${A2_REAL_D13_DIR:-}" ]]; then
  R="$A2_REAL_D13_DIR"
  for need in "Apple DOS 3.2.1 Standard.nib" "Apple DOS 3.2.1 Standard.d13" "DOS 3.2 System Master.woz"; do
    [[ -f "$R/$need" ]] || fail "A2_REAL_D13_DIR: missing $need"
  done
  "$RDEDISKTOOL" convert "$R/Apple DOS 3.2.1 Standard.nib" "$WORK/real.d13" -f d13 >/dev/null
  cmp -s "$R/Apple DOS 3.2.1 Standard.d13" "$WORK/real.d13" || fail "real nib -> d13 != shipped d13"; pass
  "$RDEDISKTOOL" list "$R/DOS 3.2 System Master.woz" >"$WORK/list.txt"
  grep -q "^14 file(s)" "$WORK/list.txt" && grep -q "^APPLE-TREK " "$WORK/list.txt" || fail "real woz list"; pass
  echo "  (real DOS 3.2 masters checked)"
fi

echo "PASS test_apple_d13_read ($CHECKS checks)"
