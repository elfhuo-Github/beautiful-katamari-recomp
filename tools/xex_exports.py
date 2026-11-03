#!/usr/bin/env python3
"""Parse the XEX export table and list ordinals -> name."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else \
    "/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)/default.xex"

data = open(path, "rb").read()
print("file size: %d" % len(data))
print("first 32 bytes:", data[:32].hex())

# locate the PE header
pe = data.find(b"PE\x00\x00")
if pe < 0:
    # XEX2: dword at 0x08 is the PE offset
    pe = struct.unpack_from("<I", data, 0x08)[0]
    print("PE offset from XEX header: 0x%x" % pe)
else:
    print("PE header at 0x%x" % pe)

coff = pe + 4
machine, nsec, ts, symptr, nsym, optsize, chars = struct.unpack_from("<HHIIIHH", data, coff)
print("machine=0x%x nsec=%d optsize=%d" % (machine, nsec, optsize))
opt = coff + 20
magic = struct.unpack_from("<H", data, opt)[0]
print("optional header magic 0x%x" % magic)
dd_off = opt + (96 if magic == 0x10b else 112)
exp_rva, exp_size = struct.unpack_from("<II", data, dd_off)
print("export dir rva=0x%x size=0x%x" % (exp_rva, exp_size))

# section table
secs = []
st = opt + optsize
for i in range(nsec):
    name = data[st + i * 40:st + i * 40 + 8].rstrip(b"\x00")
    vs, va, rs, raw = struct.unpack_from("<IIII", data, st + i * 40 + 8)
    secs.append((name, va, vs, raw, rs))
    print("  section %-8s va=0x%x vs=0x%x raw=0x%x rs=0x%x" % (name.decode(), va, vs, raw, rs))


def rva2off(rva):
    for name, va, vs, raw, rs in secs:
        if va <= rva < va + max(vs, rs):
            return raw + (rva - va)
    return None


eo = rva2off(exp_rva)
print("export dir file offset 0x%x" % eo)
(flags, ts2, maj, min_, name_rva, base, nfunc, nname,
 func_rva, nameptr_rva, ord_rva) = struct.unpack_from("<IIHHIIIIIII", data, eo)
print("export dll name rva=0x%x  base=%d  nfunc=%d nname=%d" % (name_rva, base, nfunc, nname))
no = rva2off(name_rva)
print("dll name:", data[no:no + 64].split(b"\x00")[0].decode(errors="replace"))

fo = rva2off(func_rva)
po = rva2off(nameptr_rva)
oo = rva2off(ord_rva)

names = {}
for i in range(nname):
    nrva = struct.unpack_from("<I", data, po + i * 4)[0]
    off = rva2off(nrva)
    nm = data[off:data.index(b"\x00", off)].decode(errors="replace")
    ordi = struct.unpack_from("<H", data, oo + i * 2)[0]
    names[base + ordi] = nm

print("\n=== exports with ordinal 140..175 ===")
for o in range(140, 176):
    if o in names:
        print("  %3d  %s" % (o, names[o]))
    else:
        print("  %3d  <none>" % o)

print("\n=== all exports containing Xma/XMA/audio ===")
for o in sorted(names):
    if any(k in names[o].lower() for k in ("xma", "audio", "sound")):
        print("  %3d  %s" % (o, names[o]))
