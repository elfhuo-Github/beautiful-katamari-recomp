#!/usr/bin/env bash
# Trace the guest's XINPUT_STATE while keys are held down, to verify that the
# SDK's keyboard/mouse controller emulation (--mnk_mode) reaches the game.
#
#   tools/input_state.sh
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
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
break __imp__XamInputGetState
commands
  silent
  set $n = $n + 1
  if $n <= 400
    printf "[S] n=%d pkt=%u buttons=0x%04x lt=%u rt=%u lx=%d ly=%d\n", $n, *(unsigned int*)($rdx+0x100000000), *(unsigned short*)($rdx+0x100000004), *(unsigned char*)($rdx+0x100000006), *(unsigned char*)($rdx+0x100000007), *(short*)($rdx+0x100000008), *(short*)($rdx+0x10000000a)
  end
  continue
end
run
EOF

cd "$BUILD" || exit 1
gdb -q -batch -x /tmp/opencode/input.gdb --args ./beautiful_katamari \
    --game_data_root="$GAME" --log_level=info --mnk_mode \
    >/tmp/opencode/gdb_state.log 2>&1 &
GPID=$!
echo "gdb pid $GPID"

WIN=""
for _ in $(seq 1 90); do
  sleep 1
  WIN=$(wmctrl -l | grep beautiful_katamari | awk '{print $1}')
  [ -n "$WIN" ] && break
done
echo "window: ${WIN:-<none>}"
[ -n "$WIN" ] && wmctrl -i -a "$WIN"
sleep 6

echo "--- idle sample done, now holding keys ---"
"$TOOLS/xtest_input" hold Down 3000 &
sleep 3
"$TOOLS/xtest_input" hold space 2000 &
sleep 3
wait
sleep 3

kill -9 "$GPID" 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

echo "--- distinct states seen ---"
grep '\[S\]' /tmp/opencode/gdb_state.log | sed 's/ n=[0-9]*//' | sort | uniq -c | sort -rn | head -20
echo "--- timeline (first 40) ---"
grep '\[S\]' /tmp/opencode/gdb_state.log | head -40
