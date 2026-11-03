#!/usr/bin/env python3
"""Scan the guest image for function pointers that the recompiler does not
know as function entries.

Any such pointer is a candidate for an indirect call that will hit the
runtime fatal "Call to invalid or unregistered function".  We classify each
candidate by looking at the guest code at the target address.

Usage: vtable_scan.py <arena.bin> [-v]
"""
import re
import struct
import sys

BASE = 0x82000000
END = 0x82A10000

NEW_INIT = "/mnt/sdb/bkrecomp/BKReXGlue/generated/default/beautiful_katamari_init.cpp"
OLD_MAP = "/mnt/sdb/bkrecomp/BKRecomp/ppc/ppc_func_mapping.cpp"


def load_addr_set(path):
    out = set()
    for line in open(path, errors="replace"):
        m = re.search(r"\{\s*(0x[0-9A-Fa-f]{8})\s*,", line)
        if m:
            out.add(int(m.group(1), 16))
    return out


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(BASE)
        self.img = self.f.read(END - BASE)

    def u32(self, va):
        o = va - BASE
        if o < 0 or o + 4 > len(self.img):
            return None
        return struct.unpack_from(">I", self.img, o)[0]


def disasm_thunk(w1, w2=None):
    """Decode the classic 'this-adjusting' thunk: addi r3,r3,-N ; b <t>."""
    # addi rD,rA,SIMM  (opcode 14)
    if (w1 >> 26) != 14:
        return None
    rD = (w1 >> 21) & 0x1F
    rA = (w1 >> 16) & 0x1F
    simm = w1 & 0xFFFF
    if simm & 0x8000:
        simm -= 0x10000
    if rD != 3 or rA != 3 or simm >= 0:
        return None
    if w2 is None:
        return None
    # b/bl  (opcode 18)
    if (w2 >> 26) != 18:
        return None
    li = w2 & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    if w2 & 1:
        return None  # bl, not a tail branch
    return simm, li


def main():
    path = sys.argv[1]
    verbose = "-v" in sys.argv
    img = Image(path)
    reg = load_addr_set(NEW_INIT)
    old = load_addr_set(OLD_MAP)
    regs = sorted(reg)

    def next_reg(a):
        import bisect
        i = bisect.bisect_right(regs, a)
        return regs[i] if i < len(regs) else None

    def codeish(v):
        return v is not None and BASE <= v < END

    # ---- find runs of consecutive code-pointer words (vtables) -------------
    n = len(img.img) // 4
    vals = [struct.unpack_from(">I", img.img, o * 4)[0] for o in range(n)]
    runs = []
    i = 0
    while i < n:
        if codeish(vals[i]):
            j = i
            while j < n and codeish(vals[j]):
                j += 1
            if j - i >= 3:
                runs.append((BASE + i * 4, vals[i:j]))
            i = j
        else:
            i += 1

    targets = {}
    for base, arr in runs:
        for v in arr:
            if v not in reg:
                targets.setdefault(v, set()).add(base)

    thunks = []
    others = []
    for t in sorted(targets):
        w1 = img.u32(t)
        w2 = img.u32(t + 4)
        tk = disasm_thunk(w1, w2) if w2 is not None else None
        if tk:
            nr = next_reg(t)
            thunks.append((t, tk, nr, targets[t]))
        else:
            others.append((t, targets[t]))

    print("vtable-like runs (>=3 code pointers): %d" % len(runs))
    print("distinct unregistered pointer targets: %d" % len(targets))
    print("  of those, 'addi r3,r3,-N; b t' thunks: %d" % len(thunks))
    print("  other (real functions / labels):    %d" % len(others))
    print()
    print("=== THUNKS (candidate fix) ===")
    for t, (simm, li), nr, srcs in thunks:
        size = (nr - t) if nr else None
        print("  0x%08X  addi r3,r3,%d ; b +%d   next_reg=%s  in_old=%s  refs=%s"
              % (t, simm, li, hex(nr) if nr else "-", t in old,
                 ",".join(hex(s) for s in sorted(srcs)[:3])))
    print()
    print("=== OTHERS (first 60) ===")
    for t, srcs in others[:60]:
        nr = next_reg(t)
        print("  0x%08X  next_reg=%-12s in_old=%-3s refs=%s"
              % (t, hex(nr) if nr else "-", t in old,
                 ",".join(hex(s) for s in sorted(srcs)[:3])))
    print("  ... total %d" % len(others))


if __name__ == "__main__":
    main()
