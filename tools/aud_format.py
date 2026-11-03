#!/usr/bin/env python3
"""What format are the noisy frames actually in?"""
import numpy as np

g = np.fromfile("/tmp/opencode/gs.bin", dtype=">f4").reshape(-1, 6, 256)
raw = g.tobytes()


def hf_ratio(x, sr=48000):
    x = np.asarray(x, dtype=float)
    if len(x) < 1024 or np.abs(x).max() == 0:
        return None
    S = np.abs(np.fft.rfft(x[:4096] * np.hanning(4096)))
    f = np.fft.rfftfreq(4096, 1.0 / sr)
    return float((S[f > 4000] ** 2).sum() / ((S ** 2).sum() + 1e-20))


for fi, lbl in ((100, "CLEAN"), (1490, "CLEAN"), (1560, "LOUD"), (1700, "LOUD"), (1900, "LOUD")):
    b = np.frombuffer(raw[fi * 6144:(fi + 1) * 6144], dtype=np.uint8)
    print("\n=== frame %d (%s) ===" % (fi, lbl))
    # a) as BE float32, 6x256
    f = b.view(">f4").reshape(6, 256)
    # b) as BE int16, 3072 samples
    i16 = b.view(">i2").astype(float)
    # c) as BE int32
    i32 = b.view(">i4").astype(float)
    print("  BE float32 ch0 rms=%9.1f  ch1 rms=%9.1f  ch2..5 rms=%s" % (
        np.sqrt((f[0] ** 2).mean()) * 32767, np.sqrt((f[1] ** 2).mean()) * 32767,
        np.round(np.sqrt((f[2:] ** 2).mean(axis=1)) * 32767)))
    print("  BE int16   rms=%9.1f   (as float rms=%9.1f)" % (
        np.sqrt((i16 ** 2).mean()), np.sqrt((i16.astype(np.float32) ** 2).mean())))
    print("  BE int32   rms=%12.1f" % np.sqrt((i32 ** 2).mean()))
    print("  first 24 BE int16:", i16[:24].astype(int))
    print("  first 24 BE int32:", i32[:6].astype(int))
    print("  first 12 BE float32:", np.round(f[0][:12] * 32767).astype(int))
    # raw hex of first 64 bytes
    print("  hex[0:48]:", b[:48].tobytes().hex())
    # HF ratio of each interpretation
    print("  HF/LF float32 ch0 = %s" % (("%.4f" % hf_ratio(f[0])) if hf_ratio(f[0]) is not None else "n/a"))
    print("  HF/LF int16[0:1024] = %s" % (("%.4f" % hf_ratio(i16[:1024])) if hf_ratio(i16[:1024]) is not None else "n/a"))
