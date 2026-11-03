#!/bin/bash
# trap-diag.sh <label> <seconds>
# Breaks directly on the ud2 (trap) inside the compiled sub_8222E438 and dumps
# the guest switch state:
#   rsi = guest base pointer, rdi = &PPCContext
#   ctx.r10 @ +0x50 = switch subject (byte offset), ctx.r11 @ +0x58 = target
#   guest index word  = base + 0x82B095E0
#   guest jump table  = base + 0x82A196A8
set -u
LABEL=${1:-tr}
SECS=${2:-45}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/trap_${LABEL}.log
CMDS=/tmp/opencode/trap_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
handle SIGSEGV nostop noprint pass
set args --game_data_root="${GAME_ROOT}" --log_level=debug --window_width=640 --window_height=360
break *(&sub_8222E438 + 0x80)
commands
silent
printf "\\n=== TRAP in sub_8222E438 ===\\n"
printf "ctx.r10 (switch subject, byte offset) = 0x%08X\\n", *(unsigned long*)(\$rdi + 0x50)
printf "ctx.r11 (loaded target)            = 0x%08X\\n", *(unsigned long*)(\$rdi + 0x58)
printf "guest index word (base+0x82B095E0) = 0x%08X\\n", *(unsigned int*)(\$rsi + 0x82b095e0)
printf "guest table (base+0x82A196A8):\\n"
x/10xw (\$rsi + 0x82a196a8)
continue
end
run
printf "\\n=== dump complete ===\\n"
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
sed -n '/TRAP in sub_8222E438/,/^$/p' "$OUT" | head -40
