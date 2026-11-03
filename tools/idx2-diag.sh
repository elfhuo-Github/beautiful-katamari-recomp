#!/bin/bash
# idx2-diag.sh <label> <seconds>
# Like idx-diag.sh but ALSO passes SIGSEGV through to the runtime's handler
# (so the game survives the ring-buffer write-protection fault and reaches the
# jump-table dispatch), printing the guest switch index on every entry of
# sub_8222E438. This shows which index value hits the "Switch case out of range".
set -u
LABEL=${1:-ix2}
SECS=${2:-45}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/idx2_${LABEL}.log
CMDS=/tmp/opencode/idx2_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
handle SIGSEGV nostop noprint pass
handle SIGILL nostop noprint pass
set args --game_data_root="${GAME_ROOT}" --log_level=debug --window_width=640 --window_height=360
break sub_8222E438
commands
silent
printf "\\n=== sub_8222E438 entered: index word [0x182B09600] = 0x%08X ===\\n", *(unsigned int*)0x182B09600
continue
end
run
printf "\\n=== dump complete ===\\n"
thread apply all bt 6
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!

sleep "$SECS"
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 20); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT"
grep -c "sub_8222E438 entered" "$OUT"
grep "sub_8222E438 entered" "$OUT" | tail -20
