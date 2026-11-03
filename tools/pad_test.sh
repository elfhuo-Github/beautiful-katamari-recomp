#!/usr/bin/env bash
# Verify that the virtual Xbox 360 pad (uinput_pad) reaches the guest through
# the runtime's SDL gamepad backend.
#
#   tools/pad_test.sh [seconds]
#
# Starts the virtual pad, launches the game under gdb with a breakpoint on
# __imp__XamInputGetState, then presses A and START while the game runs.
set -u

BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
TOOLS=/mnt/sdb/bkrecomp/BKReXGlue/tools
export DISPLAY=:0.0

DUR=${1:-22}

pkill -9 -x beautiful_katamari 2>/dev/null
pkill -9 -x uinput_pad 2>/dev/null
sleep 1

FIFO=/tmp/opencode/pad.cmd
rm -f "$FIFO"
mkfifo "$FIFO"
"$TOOLS/uinput_pad" --keep < "$FIFO" > /tmp/opencode/pad.out 2>&1 &
PADPID=$!
exec 9>"$FIFO"
sleep 1
echo "pad started pid=$PADPID"

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
  if $n <= 4000
    printf "[S] n=%d ptr=0x%x pkt=%u buttons=0x%04x lt=%u rt=%u lx=%d ly=%d\n", $n, $rdx, *(unsigned int*)($rdx+0x100000000), *(unsigned short*)($rdx+0x100000004), *(unsigned char*)($rdx+0x100000006), *(unsigned char*)($rdx+0x100000007), *(short*)($rdx+0x100000008), *(short*)($rdx+0x10000000a)
  end
  continue
end
run
EOF

cd "$BUILD" || exit 1
gdb -q -batch -x /tmp/opencode/input.gdb --args ./beautiful_katamari \
    --game_data_root="$GAME" --log_level=info >/tmp/opencode/gdb_input.log 2>&1 &
GDBPID=$!

sleep "$DUR"
echo "=== sending A=1 ==="
echo "A=1" >&9
sleep 3
echo "=== sending A=0 START=1 ==="
echo "A=0 START=1" >&9
sleep 3
echo "=== sending A=0 START=0 ==="
echo "A=0 START=0" >&9
sleep 3

kill $GDBPID 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
kill $PADPID 2>/dev/null
exec 9>&-

echo "=== distinct states seen ==="
grep -o 'pkt=[0-9]* buttons=0x[0-9a-f]* lt=[0-9]* rt=[0-9]* lx=[-0-9]* ly=[-0-9]*' \
    /tmp/opencode/gdb_input.log | sort -u | head -40
echo "=== total polls ==="
grep -c '^\[S\]' /tmp/opencode/gdb_input.log
echo "=== last 25 polls ==="
grep '^\[S\]' /tmp/opencode/gdb_input.log | tail -25
