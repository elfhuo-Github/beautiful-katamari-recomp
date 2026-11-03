#!/bin/bash
# proc-diag.sh <label> <seconds>
# Runs the game standalone (no gdb) and, at <seconds>, snapshots the live
# process state from /proc: per-thread state/wchan/syscall + the caught-signal
# mask (SigCgt tells us whether the runtime installed a SIGSEGV handler).
set -u
LABEL=${1:-pd}
SECS=${2:-20}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/proc_${LABEL}.log
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cd "$BUILD"
./beautiful_katamari \
  --game_data_root="${GAME_ROOT}" \
  --log_level=debug --log_noisy \
  --window_width=640 --window_height=360 > "$OUT" 2>&1 &
GPID=$!
echo "game pid=$GPID"

sleep "$SECS"
echo "=== /proc/$GPID/status (signal masks) ==="
grep -E "State|SigCgt|SigIgn|SigBlk|Threads" /proc/$GPID/status 2>/dev/null
echo
echo "=== per-thread state/wchan/syscall ==="
for t in /proc/$GPID/task/*; do
  tid=$(basename "$t")
  name=$(cat "$t/comm" 2>/dev/null)
  st=$(awk '{print $3}' "$t/stat" 2>/dev/null)
  wc=$(cat "$t/wchan" 2>/dev/null)
  sc=$(awk '{print $1}' "$t/syscall" 2>/dev/null)
  echo "tid=$tid state=$st wchan=$wc syscall=$sc name=$name"
done
echo
echo "=== last 12 log lines ==="
tail -12 "$(ls -t logs/*.log | head -1)" 2>/dev/null
echo
echo "=== newest log file ==="
ls -t logs/*.log | head -1
kill -9 "$GPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
