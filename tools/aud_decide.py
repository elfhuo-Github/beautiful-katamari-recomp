#!/usr/bin/env python3
"""Decisive test: does the guest's XAudio render buffer explain the host output?

For each guest frame dump (taken at XAudioSubmitRenderDriverFrame), search the
host recording over a WIDE lag window and report the best correlation plus the
residual.  A correlation near 1.0 means the guest data fully explains what the
host played; a large residual means something else is injected.

  g_NNNN.bin : guest memory at 0x14001FA00, 6 ch x 256 big-endian float32
  corr.wav   : host monitor recording, 48 kHz stereo s16
"""
import re
import sys
import wave

import numpy as np

rec_start = None
for line in open("/tmp/opencode/rec_start.txt"):
    if line.startswith("REC_START"):
        rec_start = float(line.split()[1])

ts = {}
for line in open("/tmp/opencode/corr.log"):
    m = re.match(r"TS hit(\d+) ([\d.]+)", line.strip())
    if m:
        ts[int(m.group(1))] = float(m.group(2))

w = wave.open("/tmp/opencode/corr.wav")
sr = w.getframerate()
host = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float).reshape(-1, 2)
hL = host[:, 0]
hR = host[:, 1]
print("rec_start=%.3f  host dur=%.1f s" % (rec_start, len(hL) / sr))

for hit in (2000, 4000, 6000, 8000):
    g = np.fromfile("/tmp/opencode/g_%d.bin" % hit, dtype=">f4").reshape(6, 256)
    expL = (g[0] + g[4] + 0.5 * g[2]) * 32767.0
    expR = (g[1] + g[5] + 0.5 * g[2]) * 32767.0
    t = ts[hit] - rec_start
    c0 = int(t * sr)

    best = (0, -2.0)
    for s in range(max(0, c0 - 40000), min(len(hL) - 256, c0 + 40000)):
        seg = hL[s:s + 256]
        cc = np.dot(seg, expL) / (np.linalg.norm(seg) * np.linalg.norm(expL) + 1e-9)
        if cc > best[1]:
            best = (s, cc)
    s, c = best
    gotL = hL[s:s + 256]
    gotR = hR[s:s + 256]
    resL = gotL - expL
    print("hit %-5d rec-t %6.2f  lag %+7d  corrL=%.4f corrR=%.4f | "
          "hostL rms=%7.1f expL rms=%7.1f resid rms=%7.1f | "
          "hostL peak=%7.0f expL peak=%7.0f" % (
              hit, t, s - c0, c,
              np.corrcoef(gotL, expR)[0, 1] if np.std(expR) else 0,
              np.sqrt((gotL ** 2).mean()), np.sqrt((expL ** 2).mean()),
              np.sqrt((resL ** 2).mean()),
              np.abs(gotL).max(), np.abs(expL).max()))
