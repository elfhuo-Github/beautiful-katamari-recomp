#!/usr/bin/env python3
"""Detector v3: class A (bctr runs in registered gap segments) + class B
(uncovered code after a PDATA function end, left when GapFill skipped a segment
whose start fell inside the PDATA function)."""
import json, re, glob, os, bisect
from collections import Counter

OLD = "/mnt/sdb/bkrecomp/BKRecomp/ppc"
PART = "/mnt/sdb/bkrecomp/BKReXGlue/generated/default/codegen.partition.json"
LOG = os.environ.get("CGLOG", "/tmp/opencode/codegen_v3.log")

impl_re = re.compile(r"PPC_FUNC_IMPL\(__imp__sub_([0-9A-Fa-f]+)\)")
com_re = re.compile(r"^\s*//\s*(.*?)\s*$")
loc_re = re.compile(r"\bloc_([0-9A-Fa-f]+)\b")
gap_re = re.compile(r"GapFill: registered sub_[0-9A-Fa-f]+ \(0x([0-9A-Fa-f]+)-0x([0-9A-Fa-f]+),")
pdata_re = re.compile(r"discoverBlocks: entry=0x([0-9A-Fa-f]+) pdataSize=(\d+) funcEnd=0x([0-9A-Fa-f]+)")

starts = set(); mn = {}; locrefs = set()
for path in sorted(glob.glob(os.path.join(OLD, "ppc_recomp.*.cpp"))):
    if not re.search(r"ppc_recomp\.\d+\.cpp$", path):
        continue
    cur = addr = None
    for line in open(path, errors="replace"):
        for lm in loc_re.finditer(line):
            locrefs.add(int(lm.group(1), 16))
        m = impl_re.search(line)
        if m:
            cur = int(m.group(1), 16); addr = cur; starts.add(cur); continue
        if cur is None: continue
        mm = com_re.match(line)
        if mm:
            b = mm.group(1)
            if not b or b.startswith("PPC_FUNC_"): continue
            if addr is not None:
                mn.setdefault(addr, b); addr += 4

R = set(int(k, 16) for k in json.load(open(PART))["assignments"].keys())
A = starts

gaps = []; pdatas = []
for line in open(LOG, errors="replace"):
    m = gap_re.search(line)
    if m: gaps.append((int(m.group(1), 16), int(m.group(2), 16)))
    m = pdata_re.search(line)
    if m: pdatas.append((int(m.group(1), 16), int(m.group(3), 16)))
gaps.sort()
gap_starts = [g[0] for g in gaps]
print(f"gaps={len(gaps)} pdatas={len(pdatas)} R={len(R)} A={len(A)}")


def in_registered_gap(b):
    i = bisect.bisect_right(gap_starts, b) - 1
    while i >= 0:
        gs, ge = gaps[i]
        if gs < b < ge: return True
        if ge > b: return False
        i -= 1
    return False


def is_pad(op): return op.startswith(".long") or op.startswith("ERROR")
def prev_real_op(b):
    a = b - 4
    for _ in range(8):
        p = mn.get(a)
        if p is None: return "<none>"
        op = p.split()[0]
        if not is_pad(op): return op
        a -= 4
    return "<pad>"

cand = {}

# Class A: bctr-preceded old impl starts inside a registered gap segment
for b in sorted(A - R):
    if b in locrefs: continue
    fi = mn.get(b, "")
    if not fi or is_pad(fi.split()[0]): continue
    if not in_registered_gap(b): continue
    if prev_real_op(b) == "bctr":
        cand[b] = "A:bctr"

# Class B: PDATA ends not in R and not inside a registered gap segment
nB = 0
for (entry, fend) in pdatas:
    if fend in R or fend in cand: continue
    fi = mn.get(fend, "")
    if not fi or is_pad(fi.split()[0]): continue
    if in_registered_gap(fend): continue
    cand[fend] = "B:pdata-end"
    nB += 1

print("counts:", Counter(cand.values()))
for b, r in sorted(cand.items())[:80]:
    print(f"  {r} 0x{b:08X} size=0x? first='{mn.get(b,'?')}'")

# sizes: next candidate/R/A boundary
bounds = sorted(set(cand) | R | A)
out = []
for b in sorted(cand):
    i = bisect.bisect_right(bounds, b)
    nxt = bounds[i] if i < len(bounds) else b + 4
    out.append({"addr": b, "size": nxt - b, "reason": cand[b], "first": mn.get(b, "?")})
print("size hist:", Counter(o["size"] for o in out).most_common(10))
json.dump(out, open("/tmp/opencode/thunk_candidates.json", "w"), indent=0)
print(f"wrote ({len(out)})")
