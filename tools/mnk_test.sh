#!/usr/bin/env bash
# Verify whether keyboard input reaches the guest through the runtime's
# MnK (keyboard/mouse -> controller) driver.
#
#   tools/mnk_test.sh
set -u
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
TOOLS=/mnt/sdb/bkrecomp/BKReXGlue/tools
export DISPLAY=:0.0

pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

cd "$BUILD" || exit 1
gdb -q -batch -x "$TOOLS/mnk_probe.gdb" --args ./beautiful_katamari \
    --game_data_root="$GAME" --log_level=info --mnk_mode \
    --window_width=640 --window_height=360 \
    >/tmp/opencode/mnk_probe.log 2>&1 &
GPID=$!
echo "gdb pid $GPID"

WIN=""
for i in $(seq 1 60); do
  sleep 1
  WIN=$(wmctrl -l | grep -i katamari | awk '{print $1}')
  [ -n "$WIN" ] && break
done
echo "window after ${i}s: ${WIN:-<none>}"
if [ -n "$WIN" ]; then
  "$TOOLS/xtest_input" setfocus "$WIN"
  echo "focus: $("$TOOLS/xtest_input" focus)"
fi

sleep 8
echo "--- injecting keys (space, Down, Escape) ---"
"$TOOLS/xtest_input" key space 300
sleep 1
"$TOOLS/xtest_input" key Down 300
sleep 1
"$TOOLS/xtest_input" key Escape 300
sleep 2
echo "--- injecting mouse move ---"
"$TOOLS/xtest_input" mouse 400 300 1 200
sleep 2

kill -9 "$GPID" 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

echo "--- keydown events ---"
grep '\[KEYDOWN\]' /tmp/opencode/mnk_probe.log | head -20
echo "--- mnk device states ---"
grep '\[MNKSTATE\]' /tmp/opencode/mnk_probe.log | head -12
echo "--- guest states ---"
grep '\[GUEST\]' /tmp/opencode/mnk_probe.log | head -24
echo "--- summary ---"
grep '=== keydown' /tmp/opencode/mnk_probe.log
