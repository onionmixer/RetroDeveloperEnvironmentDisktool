#!/usr/bin/env bash
# a2run.sh <d1-image> <boot-seconds> [step ...]
#   step: "type:TEXT"  type TEXT + Return, then wait $WAIT seconds (default 6)
#         "wait:N"     wait N seconds
#         "mem:ADDR,LEN"  save memory (hex) to mem_ADDR.bin
#         "shot:NAME"  save the emulator window as NAME.png
#         "key:KEYS"   xdotool key names (e.g. alt+o, Tab, Return), then wait $WAIT
# env:  A2RUN_WORK  parent directory for the run directory (default $TMPDIR or /tmp)
#       D2SRC       image for drive 2 (optional)
#       A2RUN_MODEL iie (default: Apple //e Enhanced) or iicp (Apple //c Plus,
#                   ROM 05 at IIC_ROM, default <workspace>/resource/AppleII/rom/
#                   iicp_rom05.bin; 1 MHz)
#       A2RUN_DISK35  //c Plus only: 800K .po/.2mg for the built-in 3.5" drive
#                   (a copy, kept as d35.* in the run directory)
#       SA2         sa2 binary (default: <workspace>/Emulator/AppleWin/build/sa2)
#
# Boots an Apple //e Enhanced in sa2 (AppleWin) with copies of the images and
# a copy of aw_base.yaml, and prints the run directory. It holds screen.txt
# (40-column text page at the end), d1.*/d2.* (the images after the run) and
# any mem_*.bin. Exit 0 = ran, 1 = sa2 failed, 3 = environment missing/unsafe.
#
# Isolation: sa2's debug server uses fixed ports 64501-64505, so sa2 and every
# debug read run in a private network namespace (unshare). The inner part runs
# only after checking that its namespace differs from the outer one. Xvfb
# runs outside (it cannot start in the namespace) on a display it picks
# itself; sa2 reaches it through the path socket /tmp/.X11-unix/X<n>. The
# user's display is never used. Everything started here is stopped on exit,
# Ctrl-C or SIGTERM.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"

# ------------------------------------------------------------------ inner part
if [[ "${1:-}" == "--inner" ]]; then
  RUN=$2 OUTER_NS=$3 D=$4 BOOT=$5; shift 5
  inner_ns=$(readlink /proc/self/ns/net 2>/dev/null || true)
  if [[ -z "$inner_ns" || "$inner_ns" == "$OUTER_NS" ]]; then
    echo "a2run: not in a private network namespace - refusing to start sa2" >&2
    exit 3
  fi
  if ss -ltn 2>/dev/null | grep -qE ':6450[1-5]\b'; then
    echo "a2run: debug ports already in use inside the namespace" >&2
    exit 3
  fi
  D2ARGS=(); [[ -f "$RUN/d2.img" ]] && D2ARGS=(--d2 "$(cat "$RUN/d2.img")")
  MODEL=(); while IFS= read -r line; do MODEL+=(-r "$line"); done <"$RUN/model.args"
  DISPLAY=$D "$SA2" -c "$RUN/aw.yaml" --no-audio "${MODEL[@]}" \
    --d1 "$(cat "$RUN/d1.img")" "${D2ARGS[@]}" >"$RUN/sa2.log" 2>&1 &
  SP=$!
  sleep "$BOOT"
  # the window of the emulator started here (by PID), unique
  W=""
  for _ in $(seq 1 50); do
    W=$(DISPLAY=$D xdotool search --pid "$SP" 2>/dev/null | head -1)
    [[ -n "$W" ]] && break
    sleep 0.2
  done
  if [[ -z "$W" ]] || ! kill -0 "$SP" 2>/dev/null; then
    echo "a2run: sa2 window not found or sa2 exited" >&2
    tail -5 "$RUN/sa2.log" >&2
    kill "$SP" 2>/dev/null; wait "$SP" 2>/dev/null
    exit 1
  fi
  for st in "$@"; do
    case "$st" in
      type:*) DISPLAY=$D xdotool type --window "$W" --delay 100 -- "${st#type:}" &&
              DISPLAY=$D xdotool key --window "$W" Return || { echo "a2run: typing failed" >&2; break; }
              sleep "${WAIT:-6}" ;;
      wait:*) sleep "${st#wait:}" ;;
      mem:*)  a=${st#mem:}; python3 -I "$HERE/memdump.py" "${a%%,*}" "${a#*,}" "$RUN/mem_${a%%,*}.bin" ;;
      shot:*) DISPLAY=$D import -window "$W" "$RUN/${st#shot:}.png" 2>>"$RUN/shot.log" ;;
      key:*)  DISPLAY=$D xdotool key --window "$W" --delay 150 ${st#key:} || { echo "a2run: key failed" >&2; break; }
              sleep "${WAIT:-6}" ;;
    esac
  done
  rc=0
  python3 -I "$HERE/textpage.py" >"$RUN/screen.txt" || rc=1
  kill "$SP" 2>/dev/null; wait "$SP" 2>/dev/null
  exit $rc
fi

# ------------------------------------------------------------------ outer part
if [[ $# -lt 2 ]]; then
  sed -n '2,8p' "$0" >&2
  exit 3
fi
DISK=$1 BOOT=$2; shift 2
TOOL_ROOT="$(cd "$HERE/../.." && pwd)"
export SA2="${SA2:-$TOOL_ROOT/../Emulator/AppleWin/build/sa2}"
for need in Xvfb xdotool unshare ip ss setsid python3; do
  command -v "$need" >/dev/null || { echo "a2run: missing $need" >&2; exit 3; }
done
[[ -x "$SA2" ]] || { echo "a2run: sa2 not found: $SA2" >&2; exit 3; }
[[ -f "$DISK" ]] || { echo "a2run: no image $DISK" >&2; exit 3; }

RUN=$(mktemp -d "${A2RUN_WORK:-${TMPDIR:-/tmp}}/a2run.XXXXXX") || exit 3
XP="" IP=""
cleanup() {
  if [[ -n "$IP" ]]; then
    kill -TERM -- "-$IP" 2>/dev/null
    for _ in $(seq 1 30); do kill -0 -- "-$IP" 2>/dev/null || break; sleep 0.1; done
    kill -KILL -- "-$IP" 2>/dev/null
    wait "$IP" 2>/dev/null
  fi
  if [[ -n "$XP" ]]; then
    kill "$XP" 2>/dev/null
    wait "$XP" 2>/dev/null
  fi
}
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap cleanup EXIT

cp "$HERE/aw_base.yaml" "$RUN/aw.yaml"
cp "$DISK" "$RUN/d1.${DISK##*.}"; echo "$RUN/d1.${DISK##*.}" >"$RUN/d1.img"
if [[ -n "${D2SRC:-}" ]]; then
  cp "$D2SRC" "$RUN/d2.${D2SRC##*.}"; echo "$RUN/d2.${D2SRC##*.}" >"$RUN/d2.img"
fi
case "${A2RUN_MODEL:-iie}" in
  iie)
    [[ -z "${A2RUN_DISK35:-}" ]] || { echo "a2run: A2RUN_DISK35 needs A2RUN_MODEL=iicp" >&2; exit 3; }
    echo "Configuration.Apple2 Type=17" >"$RUN/model.args" ;;
  iicp)
    ROM="${IIC_ROM:-$TOOL_ROOT/../resource/AppleII/rom/iicp_rom05.bin}"
    [[ -f "$ROM" ]] || { echo "a2run: //c Plus ROM not found: $ROM" >&2; exit 3; }
    D35=""
    if [[ -n "${A2RUN_DISK35:-}" ]]; then
      [[ -f "$A2RUN_DISK35" ]] || { echo "a2run: no image $A2RUN_DISK35" >&2; exit 3; }
      D35="$RUN/d35.${A2RUN_DISK35##*.}"; cp "$A2RUN_DISK35" "$D35"; chmod u+w "$D35"
    fi
    printf '%s\n' "Configuration.Apple2 Type=32" "Configuration.IIc ROM=$ROM" \
      "Configuration\\Slot 1.Card type=0" "Configuration.IIc Plus Accelerator=0" \
      "Configuration.IIc Plus Disk35=$D35" >"$RUN/model.args" ;;
  *) echo "a2run: A2RUN_MODEL must be iie or iicp" >&2; exit 3 ;;
esac

# Xvfb picks a free display and reports it on fd 9
exec 9>"$RUN/xvfb.fd"
Xvfb -displayfd 9 -screen 0 1280x1024x24 -nolisten tcp >"$RUN/xvfb.log" 2>&1 &
XP=$!
exec 9>&-
num=""
for _ in $(seq 1 50); do
  if [[ -s "$RUN/xvfb.fd" ]] && [[ $(tail -c1 "$RUN/xvfb.fd" | od -An -c | tr -d ' ') == '\n' ]]; then
    num=$(head -1 "$RUN/xvfb.fd"); break
  fi
  sleep 0.1
done
if ! [[ "$num" =~ ^[0-9]+$ ]] || ! kill -0 "$XP" 2>/dev/null; then
  echo "a2run: Xvfb did not start" >&2; exit 3
fi
sock="/tmp/.X11-unix/X$num"
if ! [[ -S "$sock" ]] || [[ $(stat -c %u "$sock") != "$(id -u)" ]]; then
  echo "a2run: display socket $sock is not ours" >&2; exit 3
fi

outer_ns=$(readlink /proc/self/ns/net) || exit 3
me=$(id -u)
# own session/process group for everything inside, so cleanup can stop it all
setsid unshare -rn bash -c 'ip link set lo up && exec unshare --user --map-user='"$me"' "$0" "$@"' \
  "$0" --inner "$RUN" "$outer_ns" ":$num" "$BOOT" "$@" &
IP=$!
wait "$IP"; rc=$?
IP=""
echo "$RUN"
exit $rc
