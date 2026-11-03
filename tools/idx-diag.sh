#!/bin/bash
# idx-diag.sh <label>
# Breaks in the guest function that contains the mis-detected jump table
# (sub_8222E438) and dumps, at the moment the switch would trap:
#   - the switch index word  (guest VA 0x82B09600 -> host 0x182B09600)
#   - the guest jump table    (guest VA 0x82A19698 -> host 0x182A19698)
#   - the ctx registers r10/r11 (ctx is the 1st argument of the recompiled fn)
set -u
LABEL=${1:-ix}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/idx_${LABEL}.log
CMDS=/tmp/opencode/idx_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
set args --game_data_root="${GAME_ROOT}" --log_level=debug --window_width=640 --window_height=360
break sub_8222E438
commands
silent
printf "\\n=== sub_8222E438 entered ===\\n"
printf "index word [0x182B09600] = 0x%08X\\n", *(unsigned int*)0x182B09600
printf "guest jump table @0x182A19698:\\n"
x/8xw 0x182A19698
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

sleep 45
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 20); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT"
grep -c "sub_8222E438 entered" "$OUT"
grep -A9 "sub_8222E438 entered" "$OUT" | tail -40
