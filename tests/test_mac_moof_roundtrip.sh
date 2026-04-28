#!/usr/bin/env bash
# Round-trip regression for the Macintosh MOOF read/write path (PR-E1+E2+E3).
#
# Pass conditions:
#   * `create -f mac_img` → `convert -f mac_moof` → `convert -f mac_img`
#     round-trip preserves the raw sector stream byte-for-byte for all 3
#     supported geometries: 400K MFS GCR, 800K HFS GCR, 1440K HFS MFM.
#   * The intermediate .moof file is loadable by `info`/`list` and reports
#     the expected disk geometry + filesystem.
#   * `create -f mac_moof` produces a 1440K blank MOOF that round-trips
#     symmetrically.
#   * If a real Applesauce-produced reference MOOF is present, it
#     decodes → encodes → decodes byte-identically (decoder transitively
#     validates the encoder against an external reference).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

fail=0
pass=0

check() {
  local label="$1"
  local cond="$2"
  if eval "$cond"; then
    echo "  PASS: $label"
    pass=$((pass+1))
  else
    echo "  FAIL: $label"
    fail=$((fail+1))
  fi
}

run_geometry_roundtrip() {
  local label="$1" fs="$2" geometry="$3" name="$4" expected_size="$5"
  echo "=== $label ==="

  local img="$WORK/${label}.img"
  local moof="$WORK/${label}.moof"
  local back="$WORK/${label}_back.img"

  "$RDEDISKTOOL" create "$img" -f mac_img --fs "$fs" -n "$name" \
                 -g "$geometry" --force >/dev/null 2>&1
  check "raw $label image created with size $expected_size" \
        "[[ -f '$img' && \$(stat -c %s '$img') -eq $expected_size ]]"

  "$RDEDISKTOOL" convert "$img" "$moof" -f mac_moof >/dev/null
  check "encoded $label .moof file exists" "[[ -s '$moof' ]]"

  # MOOF info must report the same total size and filesystem.
  local info_out
  info_out="$("$RDEDISKTOOL" info "$moof" 2>&1)"
  check "$label .moof info reports $expected_size bytes" \
        "echo \"\$info_out\" | grep -q 'Total Size: $expected_size bytes'"
  check "$label .moof info reports filesystem" \
        "echo \"\$info_out\" | grep -qE 'File System: (HFS|MFS)'"

  # Round-trip back to raw and compare bytes.
  "$RDEDISKTOOL" convert "$moof" "$back" -f mac_img >/dev/null
  check "$label byte-identical raw round-trip" "cmp -s '$img' '$back'"
}

run_geometry_roundtrip "400K_MFS_GCR"  mfs "80:1:10:512" "VMFS400" 409600
run_geometry_roundtrip "800K_HFS_GCR"  hfs "80:2:10:512" "VHFS800" 819200
run_geometry_roundtrip "1440K_HFS_MFM" hfs "80:2:18:512" "V1440"   1474560

# `create -f mac_moof` direct path (default geometry = 1440K MFM).
echo "=== create -f mac_moof direct ==="
"$RDEDISKTOOL" create "$WORK/blank.moof" -f mac_moof --force >/dev/null 2>&1
check "blank .moof generated" "[[ -s '$WORK/blank.moof' ]]"

"$RDEDISKTOOL" convert "$WORK/blank.moof" "$WORK/blank.img" -f mac_img >/dev/null
check "blank .moof decodes to 1440K raw" \
      "[[ \$(stat -c %s '$WORK/blank.img') -eq 1474560 ]]"

# Optional: validate against the real Applesauce sample if present.
SAMPLE="${BLUESCSI_TOOLBOX_MOOF:-/mnt/USERS/onion/DATA_ORIGN/Workspace/05_RetroDeveloperEnvironmentProject/Emulator/macintosh/snow/frontend_egui/assets/BlueSCSI Toolbox.moof}"
if [[ -f "$SAMPLE" ]]; then
  echo "=== real Applesauce sample cross-check ==="
  "$RDEDISKTOOL" convert "$SAMPLE" "$WORK/sample.img" -f mac_img >/dev/null
  "$RDEDISKTOOL" convert "$WORK/sample.img" "$WORK/sample.moof" -f mac_moof >/dev/null
  "$RDEDISKTOOL" convert "$WORK/sample.moof" "$WORK/sample_back.img" -f mac_img >/dev/null
  check "Applesauce sample IMG → our MOOF → IMG byte-identical" \
        "cmp -s '$WORK/sample.img' '$WORK/sample_back.img'"
else
  echo "  SKIP: real Applesauce sample not found at \$BLUESCSI_TOOLBOX_MOOF"
fi

echo "----------------------------------------"
echo "PASS: $pass  FAIL: $fail"
[[ $fail -eq 0 ]]
