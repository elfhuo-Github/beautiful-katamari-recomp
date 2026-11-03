#!/bin/bash
# sig-pass-diag.sh <label> <seconds>
# Runs the game under gdb but PASSES every guest-fault signal (SEGV/ILL/BUS/FPE)
# through to the runtime's own handlers instead of stopping. If the game keeps
# presenting, the standalone freeze is a signal-handling problem in the runtime;
# if it still stops, the guest state itself is broken.
# Takes screenshots at 5s intervals so frame-to-frame motion can be checked.
set -u
LABEL=${1:-sp}
SECS=${2:-40}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/sigpass_${LABEL}.log
CMDS=/tmp/opencode/sigpass_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
handle SIGSEGV nostop noprint pass
handle SIGILL nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGFPE nostop noprint pass
set args --game_data_root="${GAME_ROOT}" --log_level=debug --window_width=640 --window_height=360
run
printf "\\n=== STOPPED after ${SECS}s ===\\n"
printf "\\n=== ALL THREADS ===\\n"
thread apply all bt 8
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!

for t in $(seq 5 5 "$SECS"); do
  sleep 5
  import -window root "/tmp/opencode/sigpass_${LABEL}_${t}s.png" 2>/dev/null || echo "screenshot failed at ${t}s"
done

kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 30); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT ($(wc -l < "$OUT" 2>/dev/null) lines)"
ls -t logs/*.log | head -1
