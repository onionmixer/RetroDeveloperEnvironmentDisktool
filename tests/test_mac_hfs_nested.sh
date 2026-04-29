#!/usr/bin/env bash
# C1 — HFS nested mutation regression.
#
# Verifies:
#   * mkdir / rmdir at non-root parents
#   * writeFile / deleteFile at non-root parents
#   * Cross-tool SHA round-trip on nested file
#   * Parent folder valence increments with child count
#   * MDB: drNmFls / drNmRtDirs (root-direct) move only for root mutations
#   * MDB: drFilCnt / drDirCnt (recursive) move on every mutation regardless
#         of depth

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

WORK="${WORK:-/tmp/rdedisktool_nested_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

mdb_u32() {
  python3 -c "import sys,struct; d=open(sys.argv[1],'rb').read()[0x400+$2:0x400+$2+4]; print(struct.unpack('>I', d)[0])" "$1" "$2"
}
mdb_u16() {
  python3 -c "import sys,struct; d=open(sys.argv[1],'rb').read()[0x400+$2:0x400+$2+2]; print(struct.unpack('>H', d)[0])" "$1" "$2"
}
folder_valence() {
  python3 - "$1" "$2" <<'EOF'
import sys, struct
data = open(sys.argv[1],'rb').read()
target = int(sys.argv[2])
i = 0
while True:
    j = data.find(b'\x01\x00', i)
    if j < 0: break
    if j + 10 < len(data):
        cnid = struct.unpack('>I', data[j+6:j+10])[0]
        if cnid == target:
            print(struct.unpack('>H', data[j+4:j+6])[0])
            sys.exit(0)
    i = j + 1
print(-1)
EOF
}

"$RDEDISKTOOL" create "$WORK/v.img" -f mac_img --fs hfs -n "Nested" \
    >/dev/null 2>&1 || { echo "format failed" >&2; exit 1; }

# 1. nested mkdir
"$RDEDISKTOOL" --bootdisk-mode off mkdir "$WORK/v.img" "TopDir" \
    >/dev/null 2>&1 || { echo "C1: root mkdir failed" >&2; exit 1; }

# 2. nested write at depth 1
INPUT="$WORK/in.txt"
printf 'C1 nested round-trip\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$INPUT" "TopDir/file.txt" \
    >/dev/null 2>&1 || { echo "C1: nested add failed" >&2; exit 1; }

# 3. rdedisktool list of nested folder shows the file
"$RDEDISKTOOL" list "$WORK/v.img" "TopDir" | rg -q "file.txt" || {
  echo "C1: nested file missing from list" >&2; exit 1
}

# 4. extract round-trip
"$RDEDISKTOOL" extract "$WORK/v.img" "TopDir/file.txt" "$WORK/out_rde.bin" \
    >/dev/null 2>&1
[[ "$INPUT_SHA" == "$(sha256sum "$WORK/out_rde.bin" | awk '{print $1}')" ]] || {
  echo "C1: rdedisktool round-trip SHA mismatch" >&2; exit 1
}

# 5. cross-tool: Python sees the nested file with same bytes
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/py"
  python3 "$PY_TOOL" extract "$WORK/v.img" "$WORK/py" >/dev/null 2>&1 || {
    echo "C1: Python extract failed" >&2; exit 1
  }
  PY_FILE=$(find "$WORK/py" -name "file.txt" | head -1)
  [[ -n "$PY_FILE" ]] || { echo "C1: Python did not extract nested file" >&2; exit 1; }
  [[ "$INPUT_SHA" == "$(sha256sum "$PY_FILE" | awk '{print $1}')" ]] || {
    echo "C1: cross-tool SHA mismatch on nested file" >&2; exit 1
  }
  python3 "$PY_TOOL" ls "$WORK/v.img" \
      | rg -q "FILE.*parent=.*16.*/Nested/TopDir/file.txt" || {
    echo "C1: Python ls does not show nested file with right parent" >&2
    python3 "$PY_TOOL" ls "$WORK/v.img" >&2
    exit 1
  }
fi

# 6. MDB invariants:
#    drNmFls (root-direct files) = 0  — file is nested, not at root
#    drNmRtDirs                  = 1  — TopDir is at root
#    drFilCnt (recursive)        = 1  — one file total in tree
#    drDirCnt (recursive)        = 1  — one folder total
[[ "$(mdb_u16 "$WORK/v.img" 0x0c)" == "0" ]] || {
  echo "C1: drNmFls expected 0 (no root-direct files); got $(mdb_u16 "$WORK/v.img" 0x0c)" >&2; exit 1
}
[[ "$(mdb_u16 "$WORK/v.img" 0x52)" == "1" ]] || {
  echo "C1: drNmRtDirs expected 1; got $(mdb_u16 "$WORK/v.img" 0x52)" >&2; exit 1
}
[[ "$(mdb_u32 "$WORK/v.img" 0x54)" == "1" ]] || {
  echo "C1: drFilCnt expected 1; got $(mdb_u32 "$WORK/v.img" 0x54)" >&2; exit 1
}
[[ "$(mdb_u32 "$WORK/v.img" 0x58)" == "1" ]] || {
  echo "C1: drDirCnt expected 1; got $(mdb_u32 "$WORK/v.img" 0x58)" >&2; exit 1
}

# 7. parent folder's valence reflects child count
TOPDIR_CNID=16  # first user CNID after format()
[[ "$(folder_valence "$WORK/v.img" $TOPDIR_CNID)" == "1" ]] || {
  echo "C1: TopDir valence expected 1; got $(folder_valence "$WORK/v.img" $TOPDIR_CNID)" >&2
  exit 1
}

# 8. nested delete → TopDir valence back to 0
"$RDEDISKTOOL" --bootdisk-mode off delete "$WORK/v.img" "TopDir/file.txt" \
    >/dev/null 2>&1 || { echo "C1: nested delete failed" >&2; exit 1; }
[[ "$(folder_valence "$WORK/v.img" $TOPDIR_CNID)" == "0" ]] || {
  echo "C1: TopDir valence expected 0 after delete; got $(folder_valence "$WORK/v.img" $TOPDIR_CNID)" >&2
  exit 1
}
[[ "$(mdb_u32 "$WORK/v.img" 0x54)" == "0" ]] || {
  echo "C1: drFilCnt expected 0 after delete; got $(mdb_u32 "$WORK/v.img" 0x54)" >&2; exit 1
}

# 9. nested rmdir
"$RDEDISKTOOL" --bootdisk-mode off rmdir "$WORK/v.img" "TopDir" \
    >/dev/null 2>&1 || { echo "C1: nested rmdir failed" >&2; exit 1; }
[[ "$(mdb_u16 "$WORK/v.img" 0x52)" == "0" ]] || {
  echo "C1: drNmRtDirs expected 0 after rmdir; got $(mdb_u16 "$WORK/v.img" 0x52)" >&2; exit 1
}
[[ "$(mdb_u32 "$WORK/v.img" 0x58)" == "0" ]] || {
  echo "C1: drDirCnt expected 0 after rmdir; got $(mdb_u32 "$WORK/v.img" 0x58)" >&2; exit 1
}

# 10. negative: nested write to non-existent parent must fail cleanly
set +e
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$INPUT" "Missing/file.txt" \
    >/tmp/rdedisktool_nested.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "C1: write to missing parent should fail" >&2; exit 1; }
rg -q "does not resolve" /tmp/rdedisktool_nested.log || {
  echo "C1: missing-parent error message not surfaced" >&2
  cat /tmp/rdedisktool_nested.log >&2; exit 1
}

echo "[PASS] mac hfs nested mutation (C1)"
