#!/bin/bash
# handler-diag.sh <label> <seconds>
# Like freeze-diag.sh, but gdb PASSES SIGSEGV through to the runtime's own
# handler (instead of stopping on it). This shows what the runtime does with
# the write-protection fault: recover (game continues) or deadlock (freeze).
set -u
LABEL=${1:-hd}
SECS=${2:-20}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/handler_${LABEL}.log
SHOT=/tmp/opencode/handler_${LABEL}.png
CMDS=/tmp/opencode/handler_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
handle SIGSEGV nostop noprint pass
set args --game_data_root="${GAME_ROOT}"
run
printf "\\n=== STOPPED after ${SECS}s ===\\n"
printf "\\n=== ALL THREADS (name + top frames) ===\\n"
thread apply all bt 12
printf "\\n=== thread states ===\\n"
info threads
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!

sleep "$SECS"
import -window root "$SHOT" 2>/dev/null || echo "screenshot failed"
echo "shot: $SHOT ($(stat -c%s "$SHOT" 2>/dev/null) bytes)"

kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 30); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT ($(wc -l < "$OUT" 2>/dev/null) lines)"
