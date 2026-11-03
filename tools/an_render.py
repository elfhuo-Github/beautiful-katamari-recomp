import numpy as np

def be_floats(b):
    return np.frombuffer(b, dtype='>f4').astype(np.float32)

rb = open('/tmp/opencode/render.bin','rb').read()
nfr = len(rb)//6144
print("render frames:", nfr)
for i in range(nfr):
    b = rb[i*6144:(i+1)*6144]
    c0 = be_floats(b[0:1024])
    c1 = be_floats(b[1024:2048])
    d0 = np.abs(np.diff(c0.astype(np.float64)))
    # per-16-sample block roughness
    blocks = [float(np.mean(np.abs(np.diff(c0[k:k+16].astype(np.float64))))) for k in range(0,256,16)]
    print(f"fr={1490+i} rms0={np.sqrt(np.mean(c0.astype(np.float64)**2)):.5f} "
          f"rms1={np.sqrt(np.mean(c1.astype(np.float64)**2)):.5f} "
          f"maxdiff0={d0.max():.5f}")
    print("    blocks16:", ' '.join('%.4f'%x for x in blocks))

# Show samples 185..255 of ch0 for frames 1494..1497
for i in range(4, 8):
    b = rb[i*6144:(i+1)*6144]
    c0 = be_floats(b[0:1024])
    print(f"\n--- fr={1490+i} ch0[185:256] ---")
    print(' '.join('%.5f'%x for x in c0[185:256]))
