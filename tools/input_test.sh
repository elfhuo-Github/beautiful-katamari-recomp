#!/usr/bin/env bash
# Launch beautiful_katamari with keyboard emulation, focus its window, send a
# key sequence and report whether the screen reacts.
#
#   tools/input_test.sh [seconds] [key ...]
set -u
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
export DISPLAY=:0.0

pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

cd "$BUILD" || exit 1
./beautiful_katamari --game_data_root="$GAME" --log_level=debug \
    --window_width=640 --window_height=360 --mnk_mode "${@:2}" \
    >/tmp/opencode/input_run.log 2>&1 &
GPID=$!
echo "pid $GPID"

# wait for the window
WIN=""
for _ in $(seq 1 60); do
  sleep 1
  WIN=$(wmctrl -l | grep beautiful_katamari | awk '{print $1}')
  [ -n "$WIN" ] && break
done
echo "window: ${WIN:-<none>}"

if [ -n "$WIN" ]; then
  wmctrl -i -a "$WIN"
  sleep 1
  echo "focus now: $(/mnt/sdb/bkrecomp/BKReXGlue/tools/xtest_input focus)"
fi

sleep "${1:-20}"

# capture before
import -window "$WIN" /tmp/opencode/in_before.png 2>/dev/null
echo "--- sending keys ---"
for k in "${@:2}"; do :; done
sleep 1
import -window "$WIN" /tmp/opencode/in_after.png 2>/dev/null

kill -9 "$GPID" 2>/dev/null
pkill -9 -x beautiful_katamari 2>/dev/null
sleep 1

LOG=$(ls -t "$BUILD"/logs/*.log | head -1)
echo "log: $LOG"
python3 - "$LOG" <<'EOF'
import sys,datetime,re
ts=[];bad=[]
for l in open(sys.argv[1],errors='replace'):
    if 'XELOG_GPU PRESENT' in l:
        m=re.match(r'\[([^\]]+)\]',l)
        if m: ts.append(datetime.datetime.strptime(m.group(1),"%Y-%m-%d %H:%M:%S.%f"))
    if '[FATAL]' in l or '[ERROR]' in l: bad.append(l.strip()[:200])
print("presents:",len(ts))
if ts:
    print("span: %.1f s"%(ts[-1]-ts[0]).total_seconds())
    gaps=[(ts[i+1]-ts[i]).total_seconds() for i in range(len(ts)-1)]
    if gaps: print("max gap: %.3f s"%max(gaps))
for l in bad[-10:]: print("  !",l)
EOF
python3 - <<'EOF'
from PIL import Image
import numpy as np
try:
    a=Image.open('/tmp/opencode/in_before.png').convert('RGB')
    b=Image.open('/tmp/opencode/in_after.png').convert('RGB')
    d=np.abs(np.asarray(a,int)-np.asarray(b,int)).sum(axis=2)
    print("screen changed: %.2f%% of pixels"%(100.0*(d>16).sum()/d.size))
except Exception as e:
    print("screenshot diff failed:",e)
EOF
