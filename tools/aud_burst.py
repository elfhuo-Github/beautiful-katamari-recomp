#!/usr/bin/env python3
"""Burst structure of the noise + layout hypothesis tests."""
import numpy as np

SR = 48000
g = np.fromfile("/tmp/opencode/gs.bin", dtype=">f4").reshape(-1, 6, 256)
n = len(g)
rms = np.sqrt((g[:, 0, :] ** 2).mean(axis=1)) * 32767

# ---- burst structure -------------------------------------------------
loud = rms > 2500
print("loud frames: %d / %d" % (loud.sum(), n))
runs = []
i = 0
while i < n:
    if loud[i]:
        j = i
        while j < n and loud[j]:
            j += 1
        runs.append((i, j - i))
        i = j
    else:
        i += 1
print("first 25 loud runs (start_frame, length):")
for r in runs[:25]:
    print("   %5d  len=%3d  (%.3f s)" % (r[0], r[1], r[1] / 187.5))
if len(runs) > 1:
    starts = [r[0] for r in runs]
    print("   start deltas:", [starts[i + 1] - starts[i] for i in range(min(15, len(starts) - 1))])

# ---- layout hypothesis tests on a noisy frame ------------------------
print("\n=== layout hypotheses (spectral flatness / HF energy of reconstruction) ===")


def quality(x):
    """lower = cleaner.  Uses HF-to-LF ratio of the spectrum."""
    x = np.asarray(x, dtype=float)
    if len(x) < 512 or np.abs(x).max() == 0:
        return None
    S = np.abs(np.fft.rfft(x[:8192] * np.hanning(8192)))
    f = np.fft.rfftfreq(8192, 1.0 / SR)
    tot = (S ** 2).sum() + 1e-20
    hf = (S[f > 3000] ** 2).sum() / tot
    return hf


for fi in (100, 1490, 1520, 1600, 1800):
    b = g[fi].reshape(-1)
    print("\nframe %d (ch0 rms=%.0f)" % (fi, rms[fi]))
    cands = {
        "A seq6 ch0 (runtime view)": b[0:256],
        "A seq6 ch1": b[256:512],
        "B interleaved stereo L (buf[0::2])": b[0:512:2],
        "B interleaved stereo R (buf[1::2])": b[1:512:2],
        "C interleaved6 L (buf[0::6])": b[0:1536:6],
        "D seq stereo L (buf[0:256])": b[0:256],
    }
    for k, v in cands.items():
        q = quality(v)
        if q is None:
            print("   %-36s silent" % k)
        else:
            print("   %-36s HF/LF=%.4f  rms=%.0f" % (k, q, np.sqrt((np.asarray(v) ** 2).mean()) * 32767))

# ---- is ch0 == ch1 in the clean region consistent with anything? ----
print("\n=== clean region: structure of the 1536-float buffer ===")
b = g[100].reshape(-1)
print("buf[0:256]   :", np.round(b[0:12] * 32767).astype(int))
print("buf[256:268] :", np.round(b[256:268] * 32767).astype(int))
print("buf[512:524] :", np.round(b[512:524] * 32767).astype(int))
print("even/odd equal over first 512? ",
      np.allclose(b[0:512:2], b[1:512:2], atol=1e-7))
print("ch1 == ch0 exactly?", np.allclose(b[0:256], b[256:512], atol=1e-7))
