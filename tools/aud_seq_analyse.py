#!/usr/bin/env python3
"""Analyse the clean guest/runtime audio frame sequences captured with gdb."""
import numpy as np

GS = "/tmp/opencode/gs.bin"   # guest buffer at XAudioSubmitRenderDriverFrame
PS = "/tmp/opencode/ps.bin"   # runtime's SDL_PutAudioStreamData payload

g = np.fromfile(GS, dtype=">f4").reshape(-1, 6, 256)
p = np.fromfile(PS, dtype="<f4").reshape(-1, 256, 6)
print("guest frames: %d   runtime puts: %d" % (len(g), len(p)))

# ---------- how much of the guest buffer is non-zero? ----------
nz = (g != 0).reshape(len(g), 6, 256).sum(axis=2)
print("\nper-channel nonzero-sample counts (first 12 frames):")
for i in range(12):
    print("  frame %3d: %s" % (i, nz[i]))

print("\nframes where ch2..ch5 contain anything:",
      int((nz[:, 2:].sum(axis=1) > 0).sum()), "/", len(g))

# ---------- is the guest data continuous across the 256-sample boundaries? --
print("\ncontinuity check (rms of |x[n]-x[n-1]| inside vs across boundaries):")


def continuity(seq, nch):
    """seq: (frames, nch, 256) -> rms of diff within channel, and across ch boundary"""
    within = []
    across = []
    for f in range(seq.shape[0]):
        s = seq[f]
        for c in range(nch):
            within.append(np.sqrt(((s[c, 1:] - s[c, :-1]) ** 2).mean()))
        for c in range(nch - 1):
            across.append(abs(s[c, -1] - s[c + 1, 0]))
    return np.mean(within), np.mean(across)


w, a = continuity(g, 6)
print("  guest: rms diff within channel = %.5f ; mean |jump| at 256-boundaries = %.5f"
      % (w, a))

# ---------- interpret the guest buffer under different layouts ----------
print("\n=== layout hypotheses (guest frame 0 and 100) ===")
for fi in (0, 100):
    b = g[fi].reshape(-1)      # 1536 floats, guest order
    print("\nframe %d" % fi)
    print("  A: 6 sequential x 256 -> ch rms:",
          np.round(np.sqrt((g[fi] ** 2).mean(axis=1)), 5))
    print("  B: first 512 floats = interleaved stereo -> L rms=%.5f R rms=%.5f" % (
        np.sqrt((b[0:512:2] ** 2).mean()), np.sqrt((b[1:512:2] ** 2).mean())))
    print("  B2: first 512 floats = sequential stereo -> L rms=%.5f R rms=%.5f" % (
        np.sqrt((b[0:256] ** 2).mean()), np.sqrt((b[256:512] ** 2).mean())))
    print("  A ch0[:16]:", np.round(g[fi][0][:16] * 32767).astype(int))
    print("  B interleaved[:16]:", np.round(b[0:512:2][:16] * 32767).astype(int))

# ---------- match runtime puts to guest frames ----------
print("\n=== matching runtime puts to guest frames ===")
# runtime output is interleaved 6ch LE: out[i,c] == guest[c,i] (byte-swapped)
def to_rt_order(gf):
    return np.stack([gf[c] for c in range(6)], axis=1).astype("<f4")


hits = 0
for i in range(min(200, len(p))):
    best = (-1, -2.0)
    for j in range(len(g)):
        c = np.dot(p[i].reshape(-1), to_rt_order(g[j]).reshape(-1))
        n = np.linalg.norm(p[i]) * np.linalg.norm(to_rt_order(g[j])) + 1e-12
        cc = c / n
        if cc > best[1]:
            best = (j, cc)
    if best[1] > 0.99:
        hits += 1
    if i < 12 or best[1] > 0.99:
        print("  put %3d -> guest frame %3d  corr=%.4f  (put rms=%.5f)" % (
            i, best[0], best[1], np.sqrt((p[i] ** 2).mean())))
print("exact matches in first 200 puts: %d" % hits)
