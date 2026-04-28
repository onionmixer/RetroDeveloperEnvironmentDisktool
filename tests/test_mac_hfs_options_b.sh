#!/usr/bin/env bash
# B1 / B2 / B3 regression for HFS write-side completeness.
#
#   B1 — writeFile / deleteFile keep MDB write-side bookkeeping coherent
#        (drLsMod, drWrCnt, drFilCnt, root folder valence).
#   B2 — createDirectory / deleteDirectory work on a single catalog leaf.
#   B3 — format() builds an empty HFS volume that Python detects as
#        raw_hfs and round-trips a written file.

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

WORK="${WORK:-/tmp/rdedisktool_hfs_optb_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# === B3 (format): create a blank 1440K HFS volume ============================
"$RDEDISKTOOL" create "$WORK/blank.img" -f mac_img --fs hfs -n "B3Vol" \
    >/tmp/rdedisktool_optb.log 2>&1 || {
  echo "B3 format failed" >&2; cat /tmp/rdedisktool_optb.log >&2; exit 1
}
# rdedisktool reads it back as HFS with empty root.
"$RDEDISKTOOL" list "$WORK/blank.img" | rg -q "Volume: B3Vol" || {
  echo "B3 list does not show volume name" >&2
  "$RDEDISKTOOL" list "$WORK/blank.img" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/blank.img" | rg -q "0 file" || {
  echo "B3 fresh volume should be empty" >&2
  "$RDEDISKTOOL" list "$WORK/blank.img" >&2
  exit 1
}
# Boot block scaffolding: LK signature, BRA.W to 0x08a, sample-default
# bbVersion + Pascal name fields, halt loader at 0x08a.
HEAD32=$(xxd -p -l 32 "$WORK/blank.img" | tr -d '\n')
EXPECT='4c4b6000008600170000065379737465 6d20202020202020202006'
EXPECT_NORM=$(printf '%s' "$EXPECT" | tr -d ' ')
[[ "${HEAD32:0:54}" == "${EXPECT_NORM:0:54}" ]] || {
  echo "B3 boot block prefix mismatch" >&2
  echo "  got:    ${HEAD32:0:54}" >&2
  echo "  expect: ${EXPECT_NORM:0:54}" >&2
  exit 1
}
HALT=$(xxd -p -s 0x08a -l 2 "$WORK/blank.img")
[[ "$HALT" == "60fe" ]] || {
  echo "B3 boot block halt loader missing at 0x08a (got $HALT, expected 60fe)" >&2
  exit 1
}
# Python independently confirms HFS classification + root listing.
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" detect "$WORK/blank.img" \
      | rg -q '"classification": "raw_hfs"' || {
    echo "B3: Python does not classify the new volume as raw_hfs" >&2
    python3 "$PY_TOOL" detect "$WORK/blank.img" >&2
    exit 1
  }
  python3 "$PY_TOOL" detect "$WORK/blank.img" \
      | rg -q '"volume_name": "B3Vol"' || {
    echo "B3: Python does not see the volume name" >&2; exit 1
  }
  python3 "$PY_TOOL" ls "$WORK/blank.img" | rg -q "DIR.*parent=.*1.*B3Vol" || {
    echo "B3: Python ls does not show the root folder record" >&2
    python3 "$PY_TOOL" ls "$WORK/blank.img" >&2
    exit 1
  }
fi

# === B1 + B3 cross-tool: write a file on the freshly-formatted volume ========
INPUT="$WORK/in.txt"
printf 'B1/B3 round-trip\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')

"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/blank.img" "$INPUT" "Probe.txt" \
    >/dev/null 2>&1 || { echo "B1/B3 add failed on fresh volume" >&2; exit 1; }
"$RDEDISKTOOL" extract "$WORK/blank.img" "Probe.txt" "$WORK/out_rde.bin" \
    >/dev/null 2>&1
[[ "$INPUT_SHA" == "$(sha256sum "$WORK/out_rde.bin" | awk '{print $1}')" ]] || {
  echo "B1/B3 round-trip SHA mismatch" >&2; exit 1
}
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/py_b3"
  python3 "$PY_TOOL" extract "$WORK/blank.img" "$WORK/py_b3" >/dev/null 2>&1
  PY_PROBE=$(find "$WORK/py_b3" -type f -name 'Probe.txt' | head -1)
  [[ -n "$PY_PROBE" ]] || { echo "Python did not see Probe.txt on fresh HFS volume" >&2; exit 1; }
  [[ "$INPUT_SHA" == "$(sha256sum "$PY_PROBE" | awk '{print $1}')" ]] || {
    echo "B1/B3 cross-tool SHA mismatch" >&2; exit 1
  }
fi

# === B1 MDB scalars after write/delete =======================================
# drFilCnt (0x54, u32) and drNmFls (0x0c, u16) must move with file count;
# drWrCnt (0x46, u32) must monotonically increase.
mdb_u32() {
  python3 -c "import sys,struct; d=open(sys.argv[1],'rb').read()[0x400+$2:0x400+$2+4]; print(struct.unpack('>I', d)[0])" "$1" "$2"
}
mdb_u16() {
  python3 -c "import sys,struct; d=open(sys.argv[1],'rb').read()[0x400+$2:0x400+$2+2]; print(struct.unpack('>H', d)[0])" "$1" "$2"
}

NM_FLS=$(mdb_u16 "$WORK/blank.img" 0x0c)
FIL_CNT=$(mdb_u32 "$WORK/blank.img" 0x54)
[[ "$NM_FLS" == "1" ]] || {
  echo "B1: drNmFls expected 1, got $NM_FLS" >&2; exit 1
}
[[ "$FIL_CNT" == "1" ]] || {
  echo "B1: drFilCnt expected 1, got $FIL_CNT" >&2; exit 1
}

DRWR_BEFORE=$(mdb_u32 "$WORK/blank.img" 0x46)
"$RDEDISKTOOL" --bootdisk-mode off delete "$WORK/blank.img" "Probe.txt" \
    >/dev/null 2>&1 || { echo "B1 delete failed" >&2; exit 1; }
DRWR_AFTER=$(mdb_u32 "$WORK/blank.img" 0x46)
[[ "$DRWR_AFTER" -gt "$DRWR_BEFORE" ]] || {
  echo "B1: drWrCnt did not increase after delete" >&2; exit 1
}
[[ "$(mdb_u16 "$WORK/blank.img" 0x0c)" == "0" ]] || {
  echo "B1: drNmFls did not return to 0 after delete" >&2; exit 1
}
[[ "$(mdb_u32 "$WORK/blank.img" 0x54)" == "0" ]] || {
  echo "B1: drFilCnt did not return to 0 after delete" >&2; exit 1
}

# === B2 mkdir / rmdir on a fresh volume (separate, to avoid leaf-full) =======
"$RDEDISKTOOL" create "$WORK/dirvol.img" -f mac_img --fs hfs -n "DirVol" \
    >/dev/null 2>&1
DRWR_BEFORE=$(mdb_u32 "$WORK/dirvol.img" 0x46)
"$RDEDISKTOOL" --bootdisk-mode off mkdir "$WORK/dirvol.img" "MyDir" \
    >/dev/null 2>&1 || { echo "B2 mkdir failed" >&2; exit 1; }
DRWR_AFTER=$(mdb_u32 "$WORK/dirvol.img" 0x46)
[[ "$DRWR_AFTER" -gt "$DRWR_BEFORE" ]] || {
  echo "B2: drWrCnt did not increase after mkdir" >&2; exit 1
}
NM_RT_DIRS=$(mdb_u16 "$WORK/dirvol.img" 0x52)
DIR_CNT=$(mdb_u32 "$WORK/dirvol.img" 0x58)
[[ "$NM_RT_DIRS" == "1" && "$DIR_CNT" == "1" ]] || {
  echo "B2: drNmRtDirs / drDirCnt expected 1/1, got $NM_RT_DIRS/$DIR_CNT" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/dirvol.img" | rg -q "MyDir.*DIR" || {
  echo "B2: mkdir result not visible in list" >&2
  "$RDEDISKTOOL" list "$WORK/dirvol.img" >&2
  exit 1
}
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" ls "$WORK/dirvol.img" | rg -q "DIR.*parent=.*2.*MyDir" || {
    echo "B2: Python ls does not see new directory" >&2
    python3 "$PY_TOOL" ls "$WORK/dirvol.img" >&2
    exit 1
  }
fi

"$RDEDISKTOOL" --bootdisk-mode off rmdir "$WORK/dirvol.img" "MyDir" \
    >/dev/null 2>&1 || { echo "B2 rmdir failed" >&2; exit 1; }
NM_RT_DIRS=$(mdb_u16 "$WORK/dirvol.img" 0x52)
DIR_CNT=$(mdb_u32 "$WORK/dirvol.img" 0x58)
[[ "$NM_RT_DIRS" == "0" && "$DIR_CNT" == "0" ]] || {
  echo "B2: rmdir did not restore drNmRtDirs / drDirCnt (got $NM_RT_DIRS/$DIR_CNT)" >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/dirvol.img" | rg -q "MyDir" && {
  echo "B2: MyDir still listed after rmdir" >&2; exit 1
} || true

# === B3 negative: format refuses unsupported geometries ======================
set +e
"$RDEDISKTOOL" create "$WORK/big.img" -f mac_img --fs hfs -n "Big" \
    -g 80:2:36:512 >/tmp/rdedisktool_optb.log 2>&1
rc=$?
set -e
# 80*2*36*512 = 2949120 bytes (5760 sectors) — out of B3 scope.
# Either create succeeds with format throwing NotImplementedException after
# layout, or filesystem init fails outright. Both are acceptable: we just
# need the unsupported size NOT to silently produce a garbage volume.
if [[ $rc -eq 0 ]]; then
  rg -q "B3 scope" /tmp/rdedisktool_optb.log || \
  rg -q "not implemented" /tmp/rdedisktool_optb.log || {
    echo "B3 negative: unsupported geometry should refuse format" >&2
    cat /tmp/rdedisktool_optb.log >&2
    exit 1
  }
fi

echo "[PASS] mac hfs option-B (B1/B2/B3)"
