#!/usr/bin/env python3
"""Dump the two virtual-method tables that contain the unregistered thunks and
check whether the candidate thunk addresses overlap any already-registered
function body."""
import re
import struct
import sys

BASE = 0x82000000
END = 0x82A10000
NEW_INIT = "/mnt/sdb/bkrecomp/BKReXGlue/generated/default/beautiful_katamari_init.cpp"
OLD_MAP = "/mnt/sdb/bkrecomp/BKRecomp/ppc/ppc_func_mapping.cpp"

CANDIDATES = [
    0x8253E9F8, 0x8253EA00, 0x8253EA28, 0x8253EA30, 0x8253EA40,
    0x8253FAA0, 0x82540C38, 0x82540C58, 0x82540C60, 0x82540C70,
]


def load_addr_set(path):
    out = set()
    for line in open(path, errors="replace"):
        m = re.search(r"\{\s*(0x[0-9A-Fa-f]{8})\s*,", line)
        if m:
            out.add(int(m.group(1), 16))
    return out


img = open(sys.argv[1], "rb")
img.seek(BASE)
blob = img.read(END - BASE)


def u32(va):
    o = va - BASE
    if o < 0 or o + 4 > len(blob):
        return None
    return struct.unpack_from(">I", blob, o)[0]


reg = load_addr_set(NEW_INIT)
old = load_addr_set(OLD_MAP)
regs = sorted(reg)

# --- registered function extents --------------------------------------------
# Recover each registered function's size from the *next* registered entry in
# the address order is not reliable; instead pull the declared size out of the
# generated code (number of instructions) -- too complex.  Use the old build's
# mapping order as a proxy for extent only for overlap sanity checking.
print("=== candidate thunks ===")
for c in CANDIDATES:
    w1, w2 = u32(c), u32(c + 4)
    li = w2 & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    tgt = (c + 4 + li) & 0xFFFFFFFF
    nr = next((r for r in regs if r > c), None)
    print("  0x%08X  w1=0x%08X w2=0x%08X -> b 0x%08X  next_reg=%s  tail_target_reg=%s"
          % (c, w1, w2, tgt, hex(nr) if nr else "-", tgt in reg))

# --- dump the two vtables --------------------------------------------------
for tbl, count in ((0x8210B7E8, 0x88), (0x8210B950, 0x50)):
    print()
    print("=== vtable at 0x%08X ===" % tbl)
    for i in range(count // 4):
        va = tbl + i * 4
        v = u32(va)
        if v is None:
            break
        if not (BASE <= v < END):
            print("  0x%08X: 0x%08X   (end of table)" % (va, v))
            break
        tags = []
        if v in reg:
            tags.append("REG")
        else:
            tags.append("**NOT REGISTERED**")
            if v in CANDIDATES:
                tags.append("thunk-candidate")
            if v in old:
                tags.append("in-old-build")
        print("  0x%08X: 0x%08X  %s" % (va, v, " ".join(tags)))
