#!/usr/bin/env python3
"""Is the buzz already present in the guest's own audio buffer?

  g_NNNN.bin : guest memory at 0x14001FA00, 6 ch x 256 big-endian float32
  seq.bin    : arena poll capture of the same buffer (many frames)
  seq.idx    : timestamps for seq.bin frames
"""
import os

import numpy as np

SR = 48000


def spec(x, label):
    w = x * np.hanning(len(x))
    S = np.abs(np.fft.rfft(w))
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    db = 20 * np.log10(S / max(S.max(), 1e-9))
    edges = [0, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 24001]
    out = []
    for i in range(len(edges) - 1):
        m = (f >= edges[i]) & (f < edges[i + 1])
        out.append("%5d:%6.1f" % (edges[i], db[m].max() if m.any() else -99))
    print("  %-10s %s" % (label, " ".join(out)))


print("=== single-frame guest dumps (256 samples, ch0) ===")
for hit in (2000, 4000, 6000, 8000):
    p = "/tmp/opencode/g_%d.bin" % hit
    if not os.path.exists(p):
        continue
    g = np.fromfile(p, dtype=">f4").reshape(6, 256)
    ch0 = g[0]
    print("hit %-5d ch0 rms=%.5f peak=%.5f  ch1 rms=%.5f  ch2..5 rms=%s" % (
        hit, np.sqrt((ch0 ** 2).mean()), np.abs(ch0).max(),
        np.sqrt((g[1] ** 2).mean()),
        np.round(np.sqrt((g[2:] ** 2).mean(axis=1)), 6)))
    spec(ch0, "ch0")

print()
print("=== arena capture seq.bin (guest frames over time) ===")
seq = "/tmp/opencode/seq.bin"
if os.path.exists(seq):
    raw = np.fromfile(seq, dtype=">f4").reshape(-1, 6, 256)
    idx = []
    if os.path.exists("/tmp/opencode/seq.idx"):
        for line in open("/tmp/opencode/seq.idx"):
            line = line.strip()
            if line:
                try:
                    idx.append(float(line))
                except ValueError:
                    pass
    print("frames: %d  (idx: %d)" % (len(raw), len(idx)))
    t0 = idx[0] if idx else 0.0
    # group into ~1 s buckets by timestamp
    buckets = {}
    for i, fr in enumerate(raw):
        t = idx[i] - t0 if i < len(idx) else i / 187.5
        b = int(t)
        buckets.setdefault(b, []).append(fr)
    for b in sorted(buckets):
        if b % 3:
            continue
        frames = np.array(buckets[b])
        ch0 = frames[:, 0, :].reshape(-1)
        rms = np.sqrt((ch0 ** 2).mean())
        peak = np.abs(ch0).max()
        spec(ch0, "t=%d s" % b)
        print("  t=%3ds  n=%3d  ch0 rms=%.5f (%.0f)  peak=%.5f (%.0f)" % (
            b, len(frames), rms, rms * 32767, peak, peak * 32767))
