#!/bin/bash
# freeze-diag.sh <label> <seconds>
# Runs the game as a child of gdb (ptrace_scope=1 forbids attach). At <seconds>
# (past the ~16-18s fault/freeze) it: (1) screenshots the game window to PNG,
# then (2) SIGINTs the GDB pid to dump ALL thread backtraces (the KB's
# "~*k" equivalent). This answers "what is on screen" + "which thread is stuck
# and where" in one run.
set -u
LABEL=${1:-fz}
SECS=${2:-20}
BUILD=/mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release
GAME_ROOT="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
OUT=/tmp/opencode/freeze_${LABEL}.log
SHOT=/tmp/opencode/freeze_${LABEL}.png
CMDS=/tmp/opencode/freeze_${LABEL}.cmds
mkdir -p /tmp/opencode
export DISPLAY=:0.0

cat > "$CMDS" <<EOF
set pagination off
set confirm off
set print thread-events off
set args --game_data_root="${GAME_ROOT}"
run
printf "\\n=== STOPPED after ${SECS}s ===\\n"
printf "\\n=== ALL THREADS (name + top frames) ===\\n"
thread apply all bt 8
printf "\\n=== faulting thread registers ===\\n"
info registers
printf "\\n=== si_addr ===\\n"
p/x \$_siginfo._sifields._sigfault.si_addr
printf "\\n=== arena mappings ===\\n"
info proc mappings
printf "\\n=== guarded ctx/memory dumps ===\\n"
python
import gdb
def trycmd(c):
    try:
        gdb.execute(c)
    except Exception as e:
        print("SKIP [%s]: %s" % (c, e))
def ureg(n):
    return int(gdb.parse_and_eval("$" + n)) & 0xffffffffffffffff
trycmd("frame 0")
try:
    rbx = ureg("rbx"); rbp = ureg("rbp"); r14 = ureg("r14")
    print("host: rbx=%#x rbp=%#x r14=%#x" % (rbx, rbp, r14))
    # rbx/r11/r12/r13 = &PPCContext, rbp = arena base (guest 0)
    base = rbp
    ctx = r14  # prologue: mov %rdi,%r14 -> r14 = &PPCContext
    off = {"r3":0,"r0":8,"r1":16,"r2":24,"r4":32,"r5":40,"r6":48,"r7":56,"r8":64,
           "r9":72,"r10":80,"r11":88,"r12":96,"r13":104,"r14":112,"r15":120,
           "r28":224,"r29":232,"r30":240,"r31":248}
    gprs = {}
    for name,o in off.items():
        v = int(gdb.parse_and_eval("*(unsigned long long*)(%d)" % (ctx+o)))
        gprs[name] = v & 0xffffffff
        print("guest r%-3s = %08x" % (name, gprs[name]))
    r31 = gprs["r31"]; r1 = gprs["r1"]
    print("--- ring object at guest r31=%08x (fields +20..+84) ---" % r31)
    trycmd("x/20wx %d" % (base + r31 + 20))
    print("--- guest stack r1+232..+284 ---")
    trycmd("x/14wx %d" % (base + r1 + 232))
    print("--- cursor page content (guest 0xafcea9c0..) ---")
    trycmd("x/20wx %d" % (base + 0xafcea9c0))
    print("--- RO page tail table (guest 0xafcea800..0xafcea9e0) ---")
    trycmd("x/32wx %d" % (base + 0xafcea800))
    print("--- preceding RW page head (guest 0xafcce000) ---")
    trycmd("x/16wx %d" % (base + 0xafcce000))
    print("--- the four RO pages content ---")
    for a in (0xafca0000, 0xafcbc000, 0xafccd000, 0xafcea000):
        print("guest %08x:" % a)
        trycmd("x/8wx %d" % (base + a))
except Exception as e:
    print("DUMP FAILED:", e)
end
printf "\\n=== dump complete ===\\n"
kill
quit
EOF

cd "$BUILD"
gdb -q --nx -x "$CMDS" "$BUILD/beautiful_katamari" > "$OUT" 2>&1 &
GDBPID=$!

# Wait for the freeze, then screenshot the window before stopping gdb.
sleep "$SECS"
echo "--- screenshotting at ${SECS}s ---"
import -window root "$SHOT" 2>/dev/null || gnome-screenshot -f "$SHOT" 2>/dev/null || echo "screenshot failed"
echo "shot: $SHOT ($(stat -c%s "$SHOT" 2>/dev/null) bytes)"

# Now stop the inferior and dump stacks.
kill -INT "$GDBPID" 2>/dev/null
for i in $(seq 1 30); do
  grep -q "dump complete" "$OUT" 2>/dev/null && break
  sleep 1
done
kill -9 "$GDBPID" 2>/dev/null
pkill -9 -f 'beautiful_katamari --game_data_root' 2>/dev/null
wait "$GDBPID" 2>/dev/null
echo "wrote $OUT ($(wc -l < "$OUT" 2>/dev/null) lines)"
