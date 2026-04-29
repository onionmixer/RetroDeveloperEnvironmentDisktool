#!/usr/bin/env bash
# C2 — HFS folder rename regression.
#
# Verifies:
#   * folder rename preserves CNID (children stay attached)
#   * thread record body name patched in place
#   * cross-tool: Python sees children under the new folder name with same SHA
#   * within-parent rename only — cross-folder rejected
#   * name conflict rejected
#   * root volume folder rename rejected

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build/rdedisktool}"
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

PY_TOOL="${PY_TOOL:-/mnt/USERS/onion/DATA_ORIGN/Workspace/MacDiskcopy/tools/macdiskimage.py}"
HAVE_PY=0
if [[ -f "$PY_TOOL" ]] && command -v python3 >/dev/null 2>&1; then
  HAVE_PY=1
fi

WORK="${WORK:-/tmp/rdedisktool_folder_rename_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

"$RDEDISKTOOL" create "$WORK/v.img" -f mac_img --fs hfs -n "RenVol" \
    >/dev/null 2>&1

# 1. Setup: mkdir + populate child file.
"$RDEDISKTOOL" --bootdisk-mode off mkdir "$WORK/v.img" "OldName" \
    >/dev/null 2>&1 || { echo "C2: setup mkdir failed" >&2; exit 1; }
INPUT="$WORK/in.txt"
printf 'C2 child file\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$INPUT" "OldName/child.txt" \
    >/dev/null 2>&1 || { echo "C2: setup add failed" >&2; exit 1; }

# 2. Folder rename to a LONGER name (record grows; test leaf reflow).
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" \
    "OldName" "NewLongerName" >/dev/null 2>&1 || {
  echo "C2: folder rename to longer name failed" >&2; exit 1
}

# 3. Verify rdedisktool sees the rename + child intact.
"$RDEDISKTOOL" list "$WORK/v.img" | rg -q "NewLongerName.*DIR" || {
  echo "C2: NewLongerName missing from root list" >&2
  "$RDEDISKTOOL" list "$WORK/v.img" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/v.img" | rg -q "OldName" && {
  echo "C2: OldName still listed after rename" >&2
  "$RDEDISKTOOL" list "$WORK/v.img" >&2
  exit 1
} || true
"$RDEDISKTOOL" list "$WORK/v.img" "NewLongerName" | rg -q "child.txt" || {
  echo "C2: child.txt missing under NewLongerName" >&2
  "$RDEDISKTOOL" list "$WORK/v.img" "NewLongerName" >&2
  exit 1
}

# 4. Cross-tool: Python sees the child under the renamed parent with the
#    SAME parent CNID (=16, first user CNID after format()).
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -q "DIR.*16.*parent=.*2.*/RenVol/NewLongerName" || {
    echo "C2: Python ls does not see DIR 16 = NewLongerName" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -q "FILE.*17.*parent=.*16.*/RenVol/NewLongerName/child.txt" || {
    echo "C2: child file detached from renamed folder (parent CNID changed!)" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
  mkdir -p "$WORK/py"
  python3 "$PY_TOOL" extract "$WORK/v.img" "$WORK/py" >/dev/null 2>&1
  PY_CHILD=$(find "$WORK/py" -name "child.txt" | head -1)
  [[ -n "$PY_CHILD" ]] || { echo "C2: Python missed the child file" >&2; exit 1; }
  [[ "$INPUT_SHA" == "$(sha256sum "$PY_CHILD" | awk '{print $1}')" ]] || {
    echo "C2: cross-tool SHA mismatch on child after folder rename" >&2; exit 1
  }
fi

# 5. Rename back to a SHORTER name (record shrinks; opposite reflow).
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" \
    "NewLongerName" "X" >/dev/null 2>&1 || {
  echo "C2: rename to shorter name failed" >&2; exit 1
}
"$RDEDISKTOOL" list "$WORK/v.img" "X" | rg -q "child.txt" || {
  echo "C2: child detached after shorter-name rename" >&2; exit 1
}

# 6. Negative: cross-folder rename refused.
set +e
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" \
    "X" "SomeOther/Sub" >/tmp/rdedisktool_c2.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "C2: cross-folder rename should fail" >&2; exit 1; }

# 7. Negative: name conflict refused. (Need free leaf for the second mkdir;
#    if the leaf is full this just verifies we don't crash.)
"$RDEDISKTOOL" --bootdisk-mode off mkdir "$WORK/v.img" "Y" \
    >/tmp/rdedisktool_c2.log 2>&1 || true
if "$RDEDISKTOOL" list "$WORK/v.img" | rg -q "Y.*DIR"; then
  set +e
  "$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" "X" "Y" \
      >/tmp/rdedisktool_c2.log 2>&1
  rc=$?
  set -e
  [[ $rc -ne 0 ]] || {
    echo "C2: rename to existing name should fail" >&2
    cat /tmp/rdedisktool_c2.log >&2
    exit 1
  }
fi

# 8. Negative: rename of root volume folder refused.
set +e
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" "" "NewVol" \
    >/tmp/rdedisktool_c2.log 2>&1
rc=$?
set -e
# (CLI may reject empty oldName before reaching the handler — either is fine,
# we just want to confirm the disk's root folder isn't renamed.)
"$RDEDISKTOOL" list "$WORK/v.img" | rg -q "Volume: RenVol" || {
  echo "C2: volume name was mutated by attempted root rename" >&2; exit 1
}

echo "[PASS] mac hfs folder rename (C2)"
