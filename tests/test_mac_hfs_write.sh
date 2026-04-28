#!/usr/bin/env bash
# M7 round-trip regression for HFS write (Phase 2).
#
# HFS write in this phase is intentionally restricted to:
#   * single new file at the catalog root (no subdirectories yet)
#   * data fork that fits in a contiguous run of allocation blocks
#   * target leaf node has enough free space (no node-split implementation)
#
# Pass conditions:
#   * Boot-protected HFS images refuse mutation in strict mode (already
#     covered by tests/test_bootdisk_guard_macintosh.sh — re-checked here).
#   * On a non-bootable HFS image, rdedisktool add → list shows the file.
#   * rdedisktool extract round-trip = SHA identical.
#   * Python macdiskimage.py extract sees the file with byte-identical data.
#   * If the catalog leaf is full, the command refuses cleanly with a
#     "split not implemented" message and the image is unchanged.

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

FX_BOOT="$TOOL_ROOT/tests/fixtures/macintosh/608_SystemTools.img"
FX_FULL="$TOOL_ROOT/tests/fixtures/macintosh/stuffit_expander_5.5.img"
[[ -f "$FX_BOOT" ]] || { echo "missing $FX_BOOT" >&2; exit 1; }
[[ -f "$FX_FULL" ]] || { echo "missing $FX_FULL" >&2; exit 1; }

WORK="${WORK:-/tmp/rdedisktool_hfs_write_$$}"
rm -rf "$WORK"; mkdir -p "$WORK"

# 1. Synthesize a non-bootable HFS image by zeroing the LK signature on a
# copy of the bootable fixture. Needed because we cannot mutate bootable
# disks, and the existing non-bootable HFS fixture has a full leaf node.
cp "$FX_BOOT" "$WORK/nonboot.img"
printf '\x00\x00' | dd of="$WORK/nonboot.img" bs=1 seek=0 count=2 conv=notrunc \
    >/dev/null 2>&1

INPUT="$WORK/in.txt"
printf 'Hello from rdedisktool HFS write!\n' > "$INPUT"
INPUT_SHA=$(sha256sum "$INPUT" | awk '{print $1}')

# 2. add succeeds on the non-bootable image.
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/nonboot.img" "$INPUT" "Hello.txt" \
    >/tmp/rdedisktool_test.log 2>&1 || {
  echo "HFS add failed" >&2; cat /tmp/rdedisktool_test.log >&2; exit 1
}

# 3. rdedisktool extract round-trip.
"$RDEDISKTOOL" extract "$WORK/nonboot.img" "Hello.txt" "$WORK/out_rde.bin" \
    >/dev/null 2>&1
RDE_SHA=$(sha256sum "$WORK/out_rde.bin" | awk '{print $1}')
[[ "$INPUT_SHA" == "$RDE_SHA" ]] || {
  echo "rdedisktool round-trip SHA mismatch: $INPUT_SHA != $RDE_SHA" >&2; exit 1
}

# 4. Python cross-tool parity — strongest check, since the Python tool has
# no HFS write of its own and reads the volume independently.
if [[ "$HAVE_PY" == "1" ]]; then
  mkdir -p "$WORK/py_out"
  python3 "$PY_TOOL" extract "$WORK/nonboot.img" "$WORK/py_out" >/dev/null 2>&1 || {
    echo "Python extract failed on rdedisktool-written HFS" >&2; exit 1
  }
  [[ -f "$WORK/py_out/Hello.txt" ]] || {
    echo "Python did not see Hello.txt on rdedisktool-written HFS" >&2; exit 1
  }
  PY_SHA=$(sha256sum "$WORK/py_out/Hello.txt" | awk '{print $1}')
  [[ "$INPUT_SHA" == "$PY_SHA" ]] || {
    echo "cross-tool SHA mismatch (rde HFS write → py read): $INPUT_SHA != $PY_SHA" >&2
    exit 1
  }
fi

cp "$FX_BOOT" "$WORK/boot.img"
ORIG_BOOT_BLOCK=$(head -c 1024 "$WORK/boot.img" | sha256sum | awk '{print $1}')

# 5a. Adding a non-system file in strict mode goes through safe-add and may
#     succeed on Mac (consistent with Apple/MSX/X68000 boot guards). The
#     real invariant is that the boot block bytes (sectors 0..1) stay
#     untouched, so the LK signature + System/Finder names are preserved.
"$RDEDISKTOOL" --bootdisk-mode strict add "$WORK/boot.img" "$INPUT" "RegularFile.txt" \
    >/tmp/rdedisktool_test.log 2>&1 || true
NEW_BOOT_BLOCK=$(head -c 1024 "$WORK/boot.img" | sha256sum | awk '{print $1}')
[[ "$ORIG_BOOT_BLOCK" == "$NEW_BOOT_BLOCK" ]] || {
  echo "bootable image's boot block was mutated by safe-add" >&2
  echo "  before: $ORIG_BOOT_BLOCK" >&2
  echo "  after:  $NEW_BOOT_BLOCK"  >&2
  exit 1
}

# 5b. Deleting a boot-critical file (System) is hard-blocked by the policy.
#     Note: M7 does not implement HFS delete, but the policy fires first.
set +e
"$RDEDISKTOOL" --bootdisk-mode strict delete "$WORK/boot.img" "System Folder/System" \
    >/tmp/rdedisktool_test.log 2>&1
rc=$?
set -e
[[ $rc -ne 0 ]] || { echo "expected delete of System Folder/System to fail" >&2; exit 1; }
rg -q "Boot disk protection" /tmp/rdedisktool_test.log || {
  echo "expected 'Boot disk protection' message; got:" >&2
  cat /tmp/rdedisktool_test.log >&2; exit 1
}

# 6. C4: catalog leaf split. The stuffit_expander fixture has its single
#    leaf nearly full (~80 bytes free); pre-C4 this branch rejected adds
#    with a "split not implemented" message. Post-C4, the leaf is split
#    and the add succeeds, surviving cross-tool read.
cp "$FX_FULL" "$WORK/full.img"
"$RDEDISKTOOL" --bootdisk-mode off add "$WORK/full.img" "$INPUT" "Hello.txt" \
    >/tmp/rdedisktool_test.log 2>&1 || {
  echo "C4: full-leaf add should now succeed via split" >&2
  cat /tmp/rdedisktool_test.log >&2
  exit 1
}
"$RDEDISKTOOL" list "$WORK/full.img" | rg -q "Hello.txt" || {
  echo "C4: Hello.txt missing from list after leaf-split add" >&2
  "$RDEDISKTOOL" list "$WORK/full.img" >&2
  exit 1
}
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" ls "$WORK/full.img" | rg -q "Hello.txt" || {
    echo "C4: Python ls does not see Hello.txt after split" >&2
    python3 "$PY_TOOL" ls "$WORK/full.img" >&2
    exit 1
  }
fi

# 7. M10: rename and delete cycle on the non-bootable image.
"$RDEDISKTOOL" --bootdisk-mode off rename "$WORK/nonboot.img" "Hello.txt" "World.txt" \
    >/dev/null 2>&1 || { echo "rename failed" >&2; exit 1; }
"$RDEDISKTOOL" list "$WORK/nonboot.img" | rg -q "World.txt" || {
  echo "World.txt missing after rename" >&2; exit 1
}
"$RDEDISKTOOL" list "$WORK/nonboot.img" | rg -q "Hello.txt" && {
  echo "Hello.txt still present after rename" >&2; exit 1
} || true
"$RDEDISKTOOL" --bootdisk-mode off delete "$WORK/nonboot.img" "World.txt" \
    >/dev/null 2>&1 || { echo "delete failed" >&2; exit 1; }
"$RDEDISKTOOL" list "$WORK/nonboot.img" | rg -q "World.txt" && {
  echo "World.txt still present after delete" >&2; exit 1
} || true
# Cross-tool: Python must see the cleaned-up state too.
if [[ "$HAVE_PY" == "1" ]]; then
  python3 "$PY_TOOL" detect "$WORK/nonboot.img" >/dev/null 2>&1 || {
    echo "Python detect rejects post-delete HFS image" >&2; exit 1
  }
fi

echo "[PASS] mac hfs write"
