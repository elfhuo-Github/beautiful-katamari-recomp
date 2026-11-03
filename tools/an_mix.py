import struct, sys, math
import numpy as np

def be_floats(b):
    a = np.frombuffer(b, dtype='>f4')
    return a.astype(np.float32)

# ---- mixer log ----
mix = []
for line in open('/tmp/opencode/mixcap.log', errors='ignore'):
    if line.startswith('MIX '):
        p = line.split()
        mix.append(dict(call=int(p[1].split('=')[1]), fr=int(p[2].split('=')[1]),
                        p3=int(p[3].split('=')[1],16), p4=int(p[4].split('=')[1],16),
                        k5=int(p[5].split('=')[1],16)))
print("mixer records:", len(mix))

# ---- render buffer frames ----
rb = open('/tmp/opencode/render.bin','rb').read()
nfr = len(rb)//6144
print("render frames:", nfr)
frames = []
for i in range(nfr):
    b = rb[i*6144:(i+1)*6144]
    ch = []
    for c in range(6):
        ch.append(be_floats(b[c*1024:(c+1)*1024]))
    frames.append(ch)

for i,ch in enumerate(frames):
    r = [float(np.sqrt(np.mean(c.astype(np.float64)**2))) for c in ch]
    pk = [float(np.max(np.abs(c))) for c in ch]
    print(f"render[{i}] fr={1490+i} rms={['%.5f'%x for x in r]} peak={['%.4f'%x for x in pk]}")

# ---- mixer buffers ----
p3f = open('/tmp/opencode/m_p3.bin','rb').read()
p4f = open('/tmp/opencode/m_p4.bin','rb').read()
k5f = open('/tmp/opencode/m_coef.bin','rb').read()
print("p3 recs:", len(p3f)//1024, "p4 recs:", len(p4f)//1024, "coef recs:", len(k5f)//32)

# which records touch the render buffer (p3 == 0x4001fa00)?
print("\n--- records with p3==0x4001fa00 (render ch0 region) ---")
for i,m in enumerate(mix):
    if m['p3'] == 0x4001fa00:
        a3 = be_floats(p3f[i*1024:(i+1)*1024])
        a4 = be_floats(p4f[i*1024:(i+1)*1024])
        ak = be_floats(k5f[i*32:(i+1)*32])
        print(f"call={m['call']} fr={m['fr']} p4=0x{m['p4']:x} k5=0x{m['k5']:x} "
              f"p3rms={np.sqrt(np.mean(a3.astype(np.float64)**2)):.5f} p3max={np.max(np.abs(a3)):.4f} "
              f"p4rms={np.sqrt(np.mean(a4.astype(np.float64)**2)):.5f} p4max={np.max(np.abs(a4)):.4f}")
        print(f"    coef={['%.6f'%x for x in ak]}")
        if m['fr'] >= 1495 and m['fr'] <= 1499:
            print("    p3[0:16]=", ['%.5f'%x for x in a3[:16]])
            print("    p3[240:256]=", ['%.5f'%x for x in a3[240:256]])
