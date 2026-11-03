#!/usr/bin/env python3
"""Compare the guest's XAudio render buffer with the runtime's SDL output.

  gst_NNNNN.bin : guest memory at 0x14001FA00 (6 ch x 256 BE float32, sequential)
  rt_NNNNN.bin  : runtime's final SDL data       (6 ch x 256 LE float32, interleaved)
"""
import sys

import numpy as np

for hit in (2000, 5000, 9000):
    g = np.fromfile("/tmp/opencode/gst_%05d.bin" % hit, dtype=">f4").reshape(6, 256)
    r = np.fromfile("/tmp/opencode/rt_%05d.bin" % hit, dtype="<f4").reshape(256, 6)
    print("=== hit %d ===" % hit)
    for c in range(6):
        gc = g[c]
        rc = r[:, c]
        d = np.abs(gc - rc)
        print("  ch%d guest rms=%.6g peak=%.6g | runtime rms=%.6g peak=%.6g | maxdiff=%.3g" % (
            c, np.sqrt((gc ** 2).mean()), np.abs(gc).max(),
            np.sqrt((rc ** 2).mean()), np.abs(rc).max(), d.max()))
    print("  guest ch0[0:16]:", np.round(g[0][:16], 5))
    print("  rt    ch0[0:16]:", np.round(r[:16, 0], 5))
    # byte-level: are the guest bytes something other than float32?
    raw = open("/tmp/opencode/gst_%05d.bin" % hit, "rb").read()
    uniq = len(set(raw))
    zeros = raw.count(0)
    print("  guest bytes: %d unique, %d zero bytes (%.1f%%)" % (uniq, zeros, 100.0 * zeros / len(raw)))
    print()
