#!/usr/bin/env python3
"""Compare the runtime's SDL output with the host monitor at one instant.

  aud_put.gdb  -> /tmp/opencode/put_NNNNN.bin  (runtime's final SDL data)
  ffmpeg       -> /tmp/opencode/put.wav         (host monitor, 48 kHz stereo s16)
"""
import re
import sys
import wave

import numpy as np

hit = int(sys.argv[1]) if len(sys.argv) > 1 else 5000

rec_start = None
for line in open("/tmp/opencode/rec_start2.txt"):
    if line.startswith("REC_START"):
        rec_start = float(line.split()[1])

ts = {}
for line in open("/tmp/opencode/put.log"):
    m = re.match(r"PUT hit(\d+) ([\d.]+)", line.strip())
    if m:
        ts[int(m.group(1))] = float(m.group(2))

w = wave.open("/tmp/opencode/put.wav")
sr = w.getframerate()
host = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float).reshape(-1, 2)
hL = host[:, 0]
hR = host[:, 1]

d = np.fromfile("/tmp/opencode/put_%05d.bin" % hit, dtype="<f4").reshape(256, 6)
expL = d[:, 0] * 32767
expR = d[:, 1] * 32767
t = ts[hit] - rec_start
c0 = int(t * sr)

best = (0, -2.0)
for s in range(max(0, c0 - 20000), min(len(hL) - 256, c0 + 20000)):
    seg = hL[s:s + 256]
    cc = np.dot(seg, expL) / (np.linalg.norm(seg) * np.linalg.norm(expL) + 1e-9)
    if cc > best[1]:
        best = (s, cc)
s, c = best
print("hit %d rec-t %.2f  aligned pos %d (off %d) corr=%.4f" % (hit, t, s, s - c0, c))
print()
print("hostL[0:48]:", np.round(hL[s:s + 48], 0).astype(int))
print("hostR[0:48]:", np.round(hR[s:s + 48], 0).astype(int))
print("expL [0:48]:", np.round(expL[:48], 0).astype(int))
print("expR [0:48]:", np.round(expR[:48], 0).astype(int))
print()


def band(x, label):
    w2 = x * np.hanning(len(x))
    S = np.abs(np.fft.rfft(w2))
    f = np.fft.rfftfreq(len(x), 1 / sr)
    db = 20 * np.log10(S / max(S.max(), 1e-9))
    edges = [0, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 24001]
    out = []
    for i in range(len(edges) - 1):
        m = (f >= edges[i]) & (f < edges[i + 1])
        out.append("%5d:%5.1f" % (edges[i], db[m].max() if m.any() else -99))
    print("  %-12s %s" % (label, " ".join(out)))


n = 4096
band(hL[s - 1024:s + 3072], "hostL")
band(expL, "expL")
band(hR[s - 1024:s + 3072], "hostR")
band(expR, "expR")
