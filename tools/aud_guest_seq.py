#!/usr/bin/env python3
"""Analyse the clean guest audio frame sequence (2001 frames captured at submit time)."""
import numpy as np

g = np.fromfile("/tmp/opencode/gs.bin", dtype=">f4").reshape(-1, 6, 256)
print("frames:", len(g))

nz = (g != 0).reshape(len(g), 6, 256).sum(axis=2)
print("frames with any non-zero in ch2..ch5:", int((nz[:, 2:].sum(axis=1) > 0).sum()))
print("frames where ch0 and ch1 are identical:",
      int((np.abs(g[:, 0] - g[:, 1]).max(axis=1) == 0).sum()), "/", len(g))

rms = np.sqrt((g[:, 0] ** 2).mean(axis=1))
print("\nch0 rms per frame (int16 units), every 25th frame:")
for i in range(0, len(g), 25):
    print("   f%-5d rms=%7.0f peak=%7.0f  ch1 rms=%7.0f" % (
        i, rms[i] * 32767, np.abs(g[i, 0]).max() * 32767,
        np.sqrt((g[i, 1] ** 2).mean()) * 32767))

# locate transition
loud = np.where(rms * 32767 > 2500)[0]
print("\nfirst frame with ch0 rms>2500:", loud[0] if len(loud) else None)
print("frames with ch0 rms<100 after that:",
      int((rms[loud[0]:] * 32767 < 100).sum()) if len(loud) else 0)

# --- continuity within channels and across the 256 boundary ---
def cont(seq):
    within = np.sqrt(((seq[:, :, 1:] - seq[:, :, :-1]) ** 2).mean(axis=2).mean())
    across = np.abs(seq[:, :-1, -1] - seq[:, 1:, 0]).mean()
    return within, across

w, a = cont(g)
print("\n6x256-sequential layout: rms diff within channel=%.5f  mean |jump| across ch boundary=%.5f" % (w, a))

b = g.reshape(len(g), -1)
w2 = np.sqrt(((b[:, 1:] - b[:, :-1]) ** 2).mean())
print("flat 1536-float layout: rms diff between consecutive floats=%.5f" % w2)
# interleaved hypothesis: even/odd split of first 512
ev = b[:, 0:512:2]
od = b[:, 1:512:2]
w3 = np.sqrt(((ev[:, 1:] - ev[:, :-1]) ** 2).mean())
w4 = np.sqrt(((od[:, 1:] - od[:, :-1]) ** 2).mean())
print("interleaved hypothesis (first 512): even rms diff=%.5f  odd rms diff=%.5f" % (w3, w4))

# --- spectra of the loud part ---
SR = 48000
for lo, hi, lbl in [(0, 100, "first 100 frames"), (1000, 1100, "frame 1000"),
                    (1900, 2000, "last 100 frames")]:
    ch0 = g[lo:hi, 0, :].reshape(-1)
    if np.abs(ch0).max() == 0:
        print("\n%s: silent" % lbl)
        continue
    print("\n%s: rms=%.5f peak=%.5f" % (lbl, np.sqrt((ch0 ** 2).mean()), np.abs(ch0).max()))
    print("  ch0[:48]:", np.round(ch0[:48] * 32767).astype(int))
    print("  ch1[:48]:", np.round(g[lo, 1, :][:48] * 32767).astype(int))
    wnd = ch0 * np.hanning(len(ch0))
    S = np.abs(np.fft.rfft(wnd))
    f = np.fft.rfftfreq(len(wnd), 1.0 / SR)
    db = 20 * np.log10(S / max(S.max(), 1e-9))
    edges = [0, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 24001]
    out = []
    for i in range(len(edges) - 1):
        m = (f >= edges[i]) & (f < edges[i + 1])
        out.append("%5d:%6.1f" % (edges[i], db[m].max() if m.any() else -99))
    print("  spec:", " ".join(out))
    # top peaks
    idx = np.argsort(S)[-12:][::-1]
    print("  top peaks:", [(round(float(f[i]), 1), round(float(db[i]), 1)) for i in sorted(idx, key=lambda i: -S[i])])
