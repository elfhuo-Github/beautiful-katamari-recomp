import numpy as np

def be_floats(b):
    return np.frombuffer(b, dtype='>f4').astype(np.float32)

mix = []
for line in open('/tmp/opencode/mixcap.log', errors='ignore'):
    if line.startswith('MIX '):
        p = line.split()
        mix.append(dict(call=int(p[1].split('=')[1]), fr=int(p[2].split('=')[1]),
                        p3=int(p[3].split('=')[1],16), p4=int(p[4].split('=')[1],16),
                        k5=int(p[5].split('=')[1],16)))
p3f = open('/tmp/opencode/m_p3.bin','rb').read()
p4f = open('/tmp/opencode/m_p4.bin','rb').read()

def rough(a):
    a = a.astype(np.float64)
    d = np.diff(a)
    return float(np.mean(np.abs(np.diff(d)))) if len(d) > 1 else 0.0

for target_fr in (1494, 1495):
    print(f"\n================ frame {target_fr} ================")
    for i, m in enumerate(mix):
        if m['fr'] != target_fr: continue
        a3 = be_floats(p3f[i*1024:(i+1)*1024])
        a4 = be_floats(p4f[i*1024:(i+1)*1024])
        r3 = float(np.sqrt(np.mean(a3.astype(np.float64)**2)))
        m3 = float(np.max(np.abs(a3)))
        r4 = float(np.sqrt(np.mean(a4.astype(np.float64)**2)))
        print(f"call={m['call']} p3=0x{m['p3']:08x} p4=0x{m['p4']:08x} k5=0x{m['k5']:08x} "
              f"p3rms={r3:.5f} p3max={m3:.4f} p3rough={rough(a3):.6f} p4rms={r4:.5f}")
