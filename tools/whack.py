#!/usr/bin/env python3
"""Automated whack-a-mole for rexglue 'invalid or unregistered function' fatals.

Loop: build -> run -> read newest log -> if a fatal address appears that is not
already declared, append a size-less [functions] entry (rexglue discovers the
extent itself) -> repeat.

Each entry is appended to bk_gapfill_thunks.toml (which bk_config.toml includes
and the codegen depfile tracks, so editing it re-runs codegen on the next build).
"""
import re
import subprocess
import sys
import time
import glob
import os

ROOT = "/mnt/sdb/bkrecomp/BKReXGlue"
BUILD = os.path.join(ROOT, "out/build/linux-amd64-release")
THUNKS = os.path.join(ROOT, "bk_gapfill_thunks.toml")
GAMEDATA = "/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
MAX_ITERS = int(sys.argv[1]) if len(sys.argv) > 1 else 40
RUN_TIMEOUT = 150

fatal_re = re.compile(r"invalid or unregistered function at guest address 0x([0-9A-Fa-f]+)")
unres_branch_re = re.compile(r"Unresolved (branch|call)")
crit_re = re.compile(r"\[(critical|error)\]")


def read_declared():
    txt = open(THUNKS).read()
    return set(int(m, 16) for m in re.findall(r'"0x([0-9A-Fa-f]+)"', txt))


def newest_log():
    logs = glob.glob(os.path.join(BUILD, "logs", "*.log"))
    return max(logs, key=os.path.getmtime) if logs else None


def build():
    r = subprocess.run(["cmake", "--build", BUILD, "-j3"], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("BUILD FAILED")
        print(r.stdout[-4000:])
        print(r.stderr[-2000:])
        return False
    return True


def run():
    env = dict(os.environ)
    try:
        subprocess.run(["timeout", str(RUN_TIMEOUT), "./beautiful_katamari",
                        f"--game_data_root={GAMEDATA}"],
                       cwd=BUILD, env=env, capture_output=True, text=True)
    except Exception as e:
        print("run error", e)


def main():
    declared = read_declared()
    print(f"starting with {len(declared)} declared thunk entries")
    for it in range(1, MAX_ITERS + 1):
        print(f"\n===== iteration {it} =====", flush=True)
        t0 = time.time()
        if not build():
            print("stopping: build failed")
            return 1
        print(f"  build {time.time()-t0:.0f}s", flush=True)
        run()
        log = newest_log()
        tail = open(log, errors="replace").read()
        m = fatal_re.search(tail)
        ub = unres_branch_re.findall(tail)
        if ub:
            print("  WARNING: Unresolved branch/call in log:", ub[:5])
        if not m:
            print("  no invalid-function fatal found in", os.path.basename(log))
            # show last critical line for context
            crit = [l for l in tail.splitlines() if crit_re.search(l)]
            if crit:
                print("  last critical:", crit[-1][-160:])
            print("  DONE (no more missing functions detected)")
            return 0
        addr = int(m.group(1), 16)
        print(f"  fatal at 0x{addr:08X}")
        if addr in declared:
            print("  already declared but still fatal -> needs size, stopping")
            return 2
        declared.add(addr)
        with open(THUNKS, "a") as f:
            f.write(f'"0x{addr:08X}" = {{}}  # auto-discovered {time.strftime("%H:%M:%S")}\n')
        print(f"  appended 0x{addr:08X}")
    print("reached max iterations")
    return 3


if __name__ == "__main__":
    sys.exit(main())
