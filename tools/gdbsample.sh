#!/bin/bash
# gdbsample.sh <label> <seconds>
# Launches the game as a CHILD of gdb (ptrace_scope=1 forbids attach),
# lets it run <seconds>, then SIGINTs the GDB pid (not the game) to stop the
# inferior, dumps registers + all thread backtraces, then kills it.
# gdb launches via a shell, so the game_data_root path (has parens/spaces) is
# double-quoted inside `set args`.
set -u
LABEL=${1:-s1}
SECS=${2:-15}
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
printf "\\n=== STOPPED after ${SECS}s ===\\n"
printf "\\n=== faulting thread registers ===\\n"
info registers
printf "\\n=== faulting host address (si_addr) ===\\n"
p/x \$_siginfo._sifields._sigfault.si_addr
printf "\\n=== disasm around fault ===\\n"
x/20i \$rip-40
printf "\\n=== faulting thread bt ===\\n"
bt 14
printf "\\n=== all threads ===\\n"
info threads
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!
sleep "$SECS"
# SIGINT the gdb pid: stops the inferior and resumes gdb's queued commands
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 25); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT ($(wc -l < "$OUT" 2>/dev/null) lines)"
