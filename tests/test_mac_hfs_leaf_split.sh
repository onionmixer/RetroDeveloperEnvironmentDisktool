#!/usr/bin/env bash
# C4 — HFS catalog leaf split regression.
#
# Verifies that:
#   * fresh-format (treeDepth=1) volume promotes to depth=2 on first leaf
#     overflow (allocates a new leaf + a new index node)
#   * subsequent splits (treeDepth=2) allocate new leaves and add entries
#     to the existing root index
#   * leaf chain stays consistent (fLink/bLink reciprocal; reachable from
#     firstLeaf; terminates at lastLeaf)
#   * root index entries' keys match each pointed leaf's actual first key
#     (no stale entries)
#   * Python cross-tool sees every record after split
#   * exhaustion of the B-tree node map surfaces as a clean
#     NotImplementedException

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RDEDISKTOOL="${RDEDISKTOOL:-$TOOL_ROOT/build_local/rdedisktool}"
if [[ ! -x "$RDEDISKTOOL" ]]; then
  RDEDISKTOOL="$TOOL_ROOT/build/rdedisktool"
fi
[[ -x "$RDEDISKTOOL" ]] || { echo "missing rdedisktool binary" >&2; exit 1; }

PY_TOOL="${PY_TOOL:-/mnt/USERS/onion/DATA_ORIGN/Workspace/MacDiskcopy/tools/macdiskimage.py}"
HAVE_PY=0
if [[ -f "$PY_TOOL" ]] && command -v python3 >/dev/null 2>&1; then
  HAVE_PY=1
fi

WORK="${WORK:-/tmp/rdedisktool_leaf_split_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

"$RDEDISKTOOL" create "$WORK/v.img" -f mac_img --fs hfs -n "Split" \
    >/dev/null 2>&1
INPUT="$WORK/in.txt"
printf 'split test\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')

# 1. Add 5 files. Pre-C4, the 4th add would hit "leaf full". Post-C4 the
#    split fires and tree depth grows from 1 to 2.
for i in 1 2 3 4 5; do
  "$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$INPUT" "f${i}.txt" \
      >/dev/null 2>&1 || {
    echo "C4: leaf split (depth 1→2) failed at f${i}.txt" >&2; exit 1
  }
done

# Verify treeDepth bumped to 2.
treedepth=$(python3 -c "
import struct
d=open('$WORK/v.img','rb').read()
mdb=d[0x400:0x500]
ctBlk=struct.unpack('>H',mdb[0x96:0x98])[0]
drAlBlSt=struct.unpack('>H',mdb[0x1c:0x1e])[0]
drAlBlkSiz=struct.unpack('>I',mdb[0x14:0x18])[0]
ctOff=drAlBlSt*512+ctBlk*drAlBlkSiz
print(struct.unpack('>H', d[ctOff+14:ctOff+16])[0])
")
[[ "$treedepth" == "2" ]] || {
  echo "C4: treeDepth expected 2 after first split; got $treedepth" >&2; exit 1
}

# Verify each file is independently extractable.
"$RDEDISKTOOL" extract "$WORK/v.img" "f3.txt" "$WORK/out3.bin" \
    >/dev/null 2>&1
[[ "$INPUT_SHA" == "$(sha256sum "$WORK/out3.bin" | awk '{print $1}')" ]] || {
  echo "C4: rdedisktool extract f3.txt SHA mismatch" >&2; exit 1
}

# 2. Stress test: add many more files until the B-tree map is exhausted.
#    Each subsequent split allocates one more leaf node + adds an entry
#    to the root index.
LAST_OK=5
for i in $(seq 6 60); do
  if "$RDEDISKTOOL" --bootdisk-mode off add "$WORK/v.img" "$INPUT" "f${i}.txt" \
        >/tmp/rdedisktool_split.log 2>&1; then
    LAST_OK=$i
  else
    rg -q "B-tree map has no free nodes" /tmp/rdedisktool_split.log || {
      echo "C4: unexpected error at f${i}.txt:" >&2
      cat /tmp/rdedisktool_split.log >&2
      exit 1
    }
    break
  fi
done

# Confirm we got at least 30 files in (depth-2 splits really happened).
[[ "$LAST_OK" -ge 30 ]] || {
  echo "C4: too few files inserted ($LAST_OK) — expected ≥30 before exhaustion" >&2
  exit 1
}

# 3. Cross-tool: Python sees all the inserted files.
if [[ "$HAVE_PY" == "1" ]]; then
  py_count=$(python3 "$PY_TOOL" ls "$WORK/v.img" | grep -c "FILE")
  [[ "$py_count" == "$LAST_OK" ]] || {
    echo "C4: Python file count $py_count != inserted $LAST_OK" >&2; exit 1
  }
  # Extract every file and verify SHA round-trip.
  mkdir -p "$WORK/py"
  python3 "$PY_TOOL" extract "$WORK/v.img" "$WORK/py" >/dev/null 2>&1
  for i in $(seq 1 "$LAST_OK"); do
    [[ -f "$WORK/py/f${i}.txt" ]] || {
      echo "C4: Python missed f${i}.txt" >&2; exit 1
    }
    [[ "$INPUT_SHA" == "$(sha256sum "$WORK/py/f${i}.txt" | awk '{print $1}')" ]] || {
      echo "C4: cross-tool SHA mismatch on f${i}.txt" >&2; exit 1
    }
  done
fi

# 4. Index integrity: every root-index entry's key must match the actual
#    first key of the leaf it points to (no stale entries).
mismatches=$(python3 - "$WORK/v.img" <<'EOF'
import sys, struct
data = open(sys.argv[1],'rb').read()
mdb = data[0x400:0x500]
ctBlk = struct.unpack('>H', mdb[0x96:0x98])[0]
drAlBlSt = struct.unpack('>H', mdb[0x1c:0x1e])[0]
drAlBlkSiz = struct.unpack('>I', mdb[0x14:0x18])[0]
ctOff = drAlBlSt*512 + ctBlk*drAlBlkSiz
hdr = data[ctOff+14:ctOff+14+0x66]
rootNode = struct.unpack('>I', hdr[0x02:0x06])[0]
nodeSize = struct.unpack('>H', hdr[0x12:0x14])[0]
idxOff = ctOff + rootNode*nodeSize
nrec = struct.unpack('>H', data[idxOff+0x0a:idxOff+0x0c])[0]
mm = 0
for i in range(nrec):
    pos = idxOff + nodeSize - 2*(i+1)
    o = struct.unpack('>H', data[pos:pos+2])[0]
    kl = data[idxOff+o]
    parent = struct.unpack('>I', data[idxOff+o+2:idxOff+o+6])[0]
    nl = data[idxOff+o+6]
    name = data[idxOff+o+7:idxOff+o+7+nl]
    body_off = idxOff + o + 1 + kl
    if body_off & 1: body_off += 1
    child = struct.unpack('>I', data[body_off:body_off+4])[0]
    leafBase = ctOff + child*nodeSize
    leafFirstOff = struct.unpack('>H', data[leafBase+nodeSize-2:leafBase+nodeSize])[0]
    leafKL = data[leafBase + leafFirstOff]
    leafParent = struct.unpack('>I', data[leafBase+leafFirstOff+2:leafBase+leafFirstOff+6])[0]
    leafNL = data[leafBase + leafFirstOff + 6]
    leafName = data[leafBase+leafFirstOff+7:leafBase+leafFirstOff+7+leafNL]
    if parent != leafParent or name != leafName:
        mm += 1
print(mm)
EOF
)
[[ "$mismatches" == "0" ]] || {
  echo "C4: $mismatches stale index entries (key doesn't match leaf's first record)" >&2
  exit 1
}

# 5. Leaf chain integrity: walk firstLeaf → fLink chain; confirm it
#    terminates at lastLeaf and bLink reciprocates.
chain_ok=$(python3 - "$WORK/v.img" <<'EOF'
import sys, struct
data = open(sys.argv[1],'rb').read()
mdb = data[0x400:0x500]
ctBlk = struct.unpack('>H', mdb[0x96:0x98])[0]
drAlBlSt = struct.unpack('>H', mdb[0x1c:0x1e])[0]
drAlBlkSiz = struct.unpack('>I', mdb[0x14:0x18])[0]
ctOff = drAlBlSt*512 + ctBlk*drAlBlkSiz
hdr = data[ctOff+14:ctOff+14+0x66]
nodeSize = struct.unpack('>H', hdr[0x12:0x14])[0]
firstLeaf = struct.unpack('>I', hdr[0x0a:0x0e])[0]
lastLeaf = struct.unpack('>I', hdr[0x0e:0x12])[0]
prev = 0
n = firstLeaf
visited = set()
last = 0
while n and n not in visited:
    visited.add(n)
    base = ctOff + n*nodeSize
    f = struct.unpack('>I', data[base:base+4])[0]
    b = struct.unpack('>I', data[base+4:base+8])[0]
    if b != prev:
        print(f"BAD bLink at leaf {n}: expected {prev}, got {b}")
        sys.exit(1)
    last = n
    prev = n
    n = f
if last != lastLeaf:
    print(f"BAD lastLeaf: header says {lastLeaf}, walk ended at {last}")
    sys.exit(1)
print("OK")
EOF
)
[[ "$chain_ok" == "OK" ]] || {
  echo "C4: leaf chain integrity check failed: $chain_ok" >&2; exit 1
}

echo "[PASS] mac hfs leaf split (C4)"
