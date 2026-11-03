#!/bin/bash
# wide-disasm.sh <label> <seconds>
# Same as gdbsample.sh but dumps a wide disassembly window around the fault
# to trace where the bad pointer register is loaded.
set -u
LABEL=${1:-d1}
SECS=${2:-16}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/gdb_${LABEL}.log
CMDS=/tmp/opencode/gdb_${LABEL}.cmds
mkdir -p /tmp/opencode

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
set args --game_data_root="${GAME_ROOT}"
run
printf "\\n=== si_addr ===\\n"
p/x \$_siginfo._sifields._sigfault.si_addr
printf "\\n=== wide disasm (fault-260 .. fault+40) ===\\n"
x/90i \$rip-260
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!
sleep "$SECS"
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 25); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT"
