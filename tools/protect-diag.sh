#!/bin/bash
# protect-diag.sh <label> <seconds>
# Same harness as freeze-diag.sh, but breakpoints mprotect() inside
# librexruntime.so and logs every call (args + caller backtrace). This shows
# WHO turns guest physical-window pages read-only.
set -u
LABEL=${1:-pr}
SECS=${2:-20}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/protect_${LABEL}.log
CMDS=/tmp/opencode/protect_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set args --game_data_root="${GAME_ROOT}"
break mprotect
commands
silent
printf "MPROTECT addr=%p len=%#lx prot=%d\\n", \$rdi, \$rsi, \$rdx
bt 4
continue
end
run
printf "\\n=== STOPPED after ${SECS}s ===\\n"
printf "\\n=== faulting thread registers ===\\n"
info registers
printf "\\n=== si_addr ===\\n"
p/x \$_siginfo._sifields._sigfault.si_addr
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!

sleep "$SECS"
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 30); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT ($(wc -l < "$OUT" 2>/dev/null) lines), mprotect hits: $(grep -c MPROTECT "$OUT" 2>/dev/null)"
