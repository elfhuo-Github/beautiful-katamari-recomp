#!/usr/bin/env bash
# Verify that injected keys reach the application.
#   1) F3 should toggle the SDK debug overlay (proves SDL sees the keys).
#   2) The guest XINPUT_STATE should change when a bound key is held.
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

WIN=""
for _ in $(seq 1 90); do
  sleep 1
  WIN=$(wmctrl -l | grep beautiful_katamari | awk '{print $1}')
  [ -n "$WIN" ] && break
done
echo "window: ${WIN:-<none>}"
[ -n "$WIN" ] && wmctrl -i -a "$WIN"
sleep 8

echo "--- F3 overlay test ---"
import -window "$WIN" /tmp/opencode/ov_before.png 2>/dev/null
"$TOOLS/xtest_input" key F3 120
sleep 2
import -window "$WIN" /tmp/opencode/ov_after.png 2>/dev/null
python3 - <<'EOF'
from PIL import Image
import numpy as np
try:
    a=Image.open('/tmp/opencode/ov_before.png').convert('RGB')
    b=Image.open('/tmp/opencode/ov_after.png').convert('RGB')
    d=np.abs(np.asarray(a,int)-np.asarray(b,int)).sum(axis=2)
    print("F3 changed %.2f%% of pixels"%(100.0*(d>16).sum()/d.size))
except Exception as e: print("diff failed:",e)
EOF

echo "--- hold A (space) and sample the guest state ---"
"$TOOLS/xtest_input" hold space 4000 &
sleep 2
"$TOOLS/xtest_input" hold Shift+Down 4000 &
wait
sleep 2

kill -9 "$GPID" 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1
echo "--- distinct guest states ---"
grep '\[S\]' /tmp/opencode/gdb_state.log | sed 's/ n=[0-9]*//' | sort | uniq -c | sort -rn | head -20
