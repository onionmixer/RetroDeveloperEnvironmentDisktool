#!/usr/bin/env bash
# C3 — HFS rsrc-fork-preserving rename regression.
#
# Verifies on the 608_SystemTools fixture (LK stripped to disable boot
# policy):
#   * rename a file with non-empty rsrc fork
#   * rsrc fork bytes preserved (byte-for-byte SHA equality)
#   * fileType / creator / FInfo / FXInfo preserved
#   * dataLogical / rsrcLogical preserved
#   * cross-tool: Python AppleDouble extract sees both forks intact

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

FX="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
[[ -f "$FX" ]] || { echo "missing $FX" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_rsrc_rename_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$FX" "$WORK/v.img"
# Strip LK so the boot disk policy doesn't gate root-level mutations.
printf '\x00\x00' | dd of="$WORK/v.img" bs=1 seek=0 count=2 conv=notrunc \
    >/dev/null 2>&1

# Snapshot original rsrc fork bytes via Python AppleDouble extract.
if [[ "$HAVE_PY" != "1" ]]; then
  echo "[SKIP] Python tool unavailable — C3 cross-tool check needs it"
  exit 0
fi
mkdir -p "$WORK/orig_adf"
python3 "$PY_TOOL" extract-appledouble "$WORK/v.img" "$WORK/orig_adf" \
    >/dev/null 2>&1
ORIG_SIDECAR=$(find "$WORK/orig_adf" -name "._TeachText" | head -1)
[[ -n "$ORIG_SIDECAR" ]] || {
  echo "C3: TeachText AppleDouble sidecar missing in 608 fixture" >&2; exit 1
}

extract_rsrc_sha() {
  python3 - "$1" <<'EOF'
import sys, struct, hashlib
d = open(sys.argv[1],'rb').read()
nentries = struct.unpack('>H', d[24:26])[0]
for i in range(nentries):
    eo = 26 + i * 12
    eid, off, length = struct.unpack('>III', d[eo:eo+12])
    if eid == 2:
        print(hashlib.sha256(d[off:off+length]).hexdigest())
        break
EOF
}
ORIG_RSRC_SHA=$(extract_rsrc_sha "$ORIG_SIDECAR")
[[ -n "$ORIG_RSRC_SHA" ]] || { echo "C3: failed to read rsrc SHA from sidecar" >&2; exit 1; }

# Snapshot fileType / creator / FInfo / FXInfo from the catalog before rename.
extract_meta_before() {
  python3 - "$1" <<'EOF'
import sys, struct
data = open(sys.argv[1],'rb').read()
needle = bytes.fromhex('00000002') + bytes([9]) + b'TeachText'
i = 0
while True:
    j = data.find(needle, i)
    if j < 0: break
    if data[j-2] == 15:
        body = j - 2 + 1 + 15
        if body & 1: body += 1
        if data[body] == 0x02:
            print(data[body+0x04:body+0x14].hex(), data[body+0x38:body+0x48].hex(),
                  struct.unpack('>I', data[body+0x1a:body+0x1e])[0],
                  struct.unpack('>I', data[body+0x24:body+0x28])[0])
            break
    i = j + 1
EOF
}
META_BEFORE=$(extract_meta_before "$WORK/v.img")
[[ -n "$META_BEFORE" ]] || { echo "C3: TeachText not found in catalog" >&2; exit 1; }

# Rename TeachText (has rsrc) to MyEditor.
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/v.img" \
    "TeachText" "MyEditor" >/dev/null 2>&1 || {
  echo "C3: rename of file with rsrc fork failed" >&2; exit 1
}

# Old name gone, new name present.
python3 "$PY_TOOL" ls "$WORK/v.img" | rg -q "TeachText" && {
  echo "C3: TeachText still listed after rename" >&2; exit 1
} || true
python3 "$PY_TOOL" ls "$WORK/v.img" | rg -q "MyEditor" || {
  echo "C3: MyEditor missing from Python ls" >&2; exit 1
}

# Catalog metadata preserved.
extract_meta_after() {
  python3 - "$1" <<'EOF'
import sys, struct
data = open(sys.argv[1],'rb').read()
needle = bytes.fromhex('00000002') + bytes([8]) + b'MyEditor'
i = 0
while True:
    j = data.find(needle, i)
    if j < 0: break
    if data[j-2] == 14:
        body = j - 2 + 1 + 14
        if body & 1: body += 1
        if data[body] == 0x02:
            print(data[body+0x04:body+0x14].hex(), data[body+0x38:body+0x48].hex(),
                  struct.unpack('>I', data[body+0x1a:body+0x1e])[0],
                  struct.unpack('>I', data[body+0x24:body+0x28])[0])
            break
    i = j + 1
EOF
}
META_AFTER=$(extract_meta_after "$WORK/v.img")
[[ -n "$META_AFTER" ]] || { echo "C3: MyEditor not found in catalog after rename" >&2; exit 1; }
[[ "$META_BEFORE" == "$META_AFTER" ]] || {
  echo "C3: catalog metadata not preserved across rename" >&2
  echo "  before: $META_BEFORE" >&2
  echo "  after:  $META_AFTER"  >&2
  exit 1
}

# Rsrc fork bytes preserved (the load-bearing C3 invariant).
mkdir -p "$WORK/new_adf"
python3 "$PY_TOOL" extract-appledouble "$WORK/v.img" "$WORK/new_adf" \
    >/dev/null 2>&1
NEW_SIDECAR=$(find "$WORK/new_adf" -name "._MyEditor" | head -1)
[[ -n "$NEW_SIDECAR" ]] || { echo "C3: post-rename sidecar missing" >&2; exit 1; }
NEW_RSRC_SHA=$(extract_rsrc_sha "$NEW_SIDECAR")
[[ "$ORIG_RSRC_SHA" == "$NEW_RSRC_SHA" ]] || {
  echo "C3: rsrc fork bytes corrupted across rename" >&2
  echo "  orig SHA: $ORIG_RSRC_SHA" >&2
  echo "  new  SHA: $NEW_RSRC_SHA" >&2
  exit 1
}

echo "[PASS] mac hfs rsrc-fork rename (C3)"
