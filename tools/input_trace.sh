#!/usr/bin/env bash
# Trace the guest's input API while keys are injected, to verify that the
# keyboard emulation actually reaches the game.
#
#   tools/input_trace.sh [key ...]
set -u
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
TOOLS=/mnt/sdb/bkrecomp/BKReXGlue/tools
export DISPLAY=:0.0

pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

cat >/tmp/opencode/input.gdb <<'EOF'
set pagination off
set confirm off
set print pretty on
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
break __imp__XamInputGetState
commands
  silent
  set $n = $n + 1
  if $n < 12
    printf "[TRACE] XamInputGetState n=%d rdi=%p rsi=%p rdx=%p rcx=%p\n", $n, $rdi, $rsi, $rdx, $rcx
    printf "        rdi[0..11]: "
    x/12wx $rdi
    printf "        rsi[0..11]: "
    x/12wx $rsi
  end
  continue
end
break __imp__XamInputGetCapabilities
commands
  silent
  printf "[TRACE] XamInputGetCapabilities rdi=%p rsi=%p\n", $rdi, $rsi
  continue
end
run
EOF

cd "$BUILD" || exit 1
gdb -q -batch -x /tmp/opencode/input.gdb --args ./beautiful_katamari \
    --game_data_root="$GAME" --log_level=info --mnk_mode \
    >/tmp/opencode/gdb_input.log 2>&1 &
GPID=$!
echo "gdb pid $GPID"

# wait for the window and focus it
WIN=""
for _ in $(seq 1 90); do
  sleep 1
  WIN=$(wmctrl -l | grep beautiful_katamari | awk '{print $1}')
  [ -n "$WIN" ] && break
done
echo "window: ${WIN:-<none>}"
[ -n "$WIN" ] && wmctrl -i -a "$WIN"
sleep 5

echo "--- injecting keys ---"
"$TOOLS/xtest_input" key Return 300
sleep 1
"$TOOLS/xtest_input" key space 300
sleep 1
"$TOOLS/xtest_input" hold Down 2500
sleep 1
"$TOOLS/xtest_input" hold Left 2000
sleep 1
"$TOOLS/xtest_input" key Escape 200
sleep 4

kill -9 "$GPID" 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1
echo "--- trace summary ---"
echo "GetState hits: $(grep -c 'TRACE\] XamInputGetState' /tmp/opencode/gdb_input.log)"
echo "GetCapabilities hits: $(grep -c 'TRACE\] XamInputGetCapabilities' /tmp/opencode/gdb_input.log)"
grep 'TRACE' /tmp/opencode/gdb_input.log | head -30
