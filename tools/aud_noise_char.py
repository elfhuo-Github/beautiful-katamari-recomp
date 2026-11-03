#!/usr/bin/env python3
"""Characterise the noise inside the guest's XAudio render buffer."""
import numpy as np

raw = np.fromfile("/tmp/opencode/seq.bin", dtype=">f4").reshape(-1, 6, 256)
print("frames:", len(raw))

# ---- locate the frames by rms (clean vs buzz) -------------------------
ch0 = raw[:, 0, :]
rms = np.sqrt((ch0 ** 2).mean(axis=1))
print("rms histogram (int16 units):")
for lo, hi in [(0, 500), (500, 2000), (2000, 4000), (4000, 6000), (6000, 20000)]:
    print("   %5d-%5d : %d frames" % (lo, hi, int(((rms * 32767 >= lo) & (rms * 32767 < hi)).sum())))

first_buzz = int(np.argmax(rms * 32767 > 2500))
print("first loud frame index:", first_buzz)

clean = raw[first_buzz - 40:first_buzz - 20]
buzz = raw[first_buzz + 100:first_buzz + 120]

print()
print("=== clean frames: ch0 vs ch1 ===")
c0 = clean[:, 0, :].reshape(-1)
c1 = clean[:, 1, :].reshape(-1)
print("  corr(ch0,ch1) = %.4f" % np.corrcoef(c0, c1)[0, 1])
print("  ch0[:32]:", np.round(c0[:32] * 32767).astype(int))

print()
print("=== buzz frames ===")
b0 = buzz[:, 0, :].reshape(-1)
b1 = buzz[:, 1, :].reshape(-1)
print("  corr(ch0,ch1) = %.4f" % np.corrcoef(b0, b1)[0, 1])
print("  ch0[:64]:", np.round(b0[:64] * 32767).astype(int))
print("  ch1[:64]:", np.round(b1[:64] * 32767).astype(int))
print("  ch0 mean = %.6f (DC)   ch1 mean = %.6f" % (b0.mean(), b1.mean()))
print("  ch0 max |x| = %.4f at sample %d" % (np.abs(b0).max(), int(np.argmax(np.abs(b0)))))
print("  ch1 max |x| = %.4f at sample %d" % (np.abs(b1).max(), int(np.argmax(np.abs(b1)))))

# where are the large samples?
thr = 0.30
idx = np.where(np.abs(b0) > thr)[0]
print("\n  |ch0| > 0.30 in %d/%d samples (%.2f%%)" % (len(idx), len(b0), 100.0 * len(idx) / len(b0)))
print("  their positions (mod 256):", sorted(set((idx % 256).tolist()))[:40])
print("  their positions (mod 512):", sorted(set((idx % 512).tolist()))[:40])

# per-frame statistics: is the peak in every frame?
pf = np.abs(raw[:, 0, :]).max(axis=1)
print("\n  per-frame peak (int16) first 40 buzz frames:",
      np.round(pf[first_buzz:first_buzz + 40] * 32767).astype(int))

# is the peak always at the same position within the frame?
pos = np.argmax(np.abs(raw[:, 0, :]), axis=1)
print("  peak position histogram (buzz region):",
      np.bincount(pos[first_buzz:first_buzz + 2000], minlength=8)[:16])

# difference between consecutive frames (continuity)
d = np.diff(raw[:, 0, :], axis=0).reshape(-1)
print("\n  consecutive-frame diff: rms=%.5f  max=%.5f" % (
    np.sqrt((d ** 2).mean()), np.abs(d).max()))

# distribution
q = np.percentile(np.abs(b0), [50, 90, 99, 99.9, 100])
print("  |ch0| percentiles 50/90/99/99.9/100:", np.round(q * 32767).astype(int))
