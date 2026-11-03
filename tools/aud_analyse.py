#!/usr/bin/env python3
"""
Analyse the captured guest audio frames against the host recording.

  arena_audio_poll.py -> seq.bin (6144-byte frames) + seq.idx (wallclock, offset)
  host recording      -> seq.wav (48 kHz stereo s16)

Checks:
  1. per-channel content of the guest buffer (NaN / zero / magnitude)
  2. buffer-layout hypothesis: is ch1 a continuation of ch0 (same channel,
     next time slice) or the right channel (same time slice)?
  3. best alignment + residual between the runtime's expected downmix and
     what the host actually played.
"""
import sys
import wave

import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/seq.bin"
IDX = sys.argv[2] if len(sys.argv) > 2 else "/tmp/opencode/seq.idx"
WAV = sys.argv[3] if len(sys.argv) > 3 else "/tmp/opencode/seq.wav"

FRAME = 6144
CH = 6
NS = 256


def load_frames():
    raw = np.fromfile(BIN, dtype=np.uint8)
    n = len(raw) // FRAME
    raw = raw[: n * FRAME].reshape(n, FRAME)
    # guest writes big-endian float32
    f = raw.view(">f4").reshape(n, CH, NS)
    ts = []
    with open(IDX) as fh:
        for line in fh:
            p = line.split()
            if len(p) == 2:
                ts.append(float(p[0]))
    return f[: len(ts)], np.array(ts)


def main():
    frames, ts = load_frames()
    print("frames: %d  span %.2f s (%.1f fps)" % (
        len(frames), ts[-1] - ts[0], len(frames) / (ts[-1] - ts[0])))

    # ---- 1. per channel stats
    print("\n=== per-channel stats over all frames ===")
    for c in range(CH):
        s = frames[:, c, :]
        fin = s[np.isfinite(s)]
        print(" ch%d: nan=%d inf=%d  rms=%.5g peak=%.5g  frac_zero=%.3f" % (
            c, np.isnan(s).sum(), np.isinf(s).sum(),
            np.sqrt((fin ** 2).mean()) if fin.size else 0,
            np.abs(fin).max() if fin.size else 0,
            (s == 0).mean()))

    # ---- 2. layout test
    # Hypothesis A (correct): ch0..ch5 are 6 channels, 256 samples each.
    # Hypothesis B: ch1 continues ch0 (i.e. guest wrote 512-sample channels)
    #   -> stream = ch0,ch1,ch2,... is one continuous mono stream.
    # Test continuity across the ch0|ch1 boundary for both readings.
    print("\n=== continuity test (rms of 1st difference, normalised) ===")

    def cont(a, b):
        # a: last samples of one block, b: first samples of next block
        d = np.abs(b[0] - a[-1])
        return d.mean()

    for label, order in (
        ("A: ch0|ch1 boundary (interleaved channels)", [(0, 1)]),
        ("B: ch0(N)|ch0(N+1) (per-channel over time)", [(0, 0)]),
    ):
        vals = []
        for i in range(0, len(frames) - 1):
            for (c1, c2) in order:
                a = frames[i, c1, :]
                b = frames[i + 1, c2, :]
                vals.append(cont(a, b))
        print("  %-45s mean|jump|=%.6g" % (label, np.mean(vals)))

    # within-frame continuity ch0->ch1 (sample to sample)
    a = frames[:, 0, :].reshape(-1)
    b = frames[:, 1, :].reshape(-1)
    print("  %-45s mean|jump|=%.6g" % (
        "within frame: ch0[-1] -> ch1[0]",
        np.abs(b[0::NS] - a[NS - 1::NS]).mean()))
    print("  %-45s mean|jump|=%.6g" % (
        "within frame: ch1[-1] -> ch2[0]",
        np.abs(frames[:, 2, 0] - frames[:, 1, NS - 1]).mean()))

    # ---- 3. align guest stream with host recording
    w = wave.open(WAV)
    sr = w.getframerate()
    host = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    host = host.reshape(-1, 2)
    hostL = host[:, 0]
    print("\nhost recording: %d samples (%.1f s)" % (len(hostL), len(hostL) / sr))

    # expected stereo stream from the guest (runtime's downmix formula)
    fl = frames[:, 0, :].reshape(-1)
    fr = frames[:, 1, :].reshape(-1)
    fc = frames[:, 2, :].reshape(-1)
    bl = frames[:, 4, :].reshape(-1)
    br = frames[:, 5, :].reshape(-1)
    expL = (fl + bl + 0.5 * fc) * 32767.0
    expR = (fr + br + 0.5 * fc) * 32767.0

    # brute-force lag: correlate a 1-second chunk of expL with the host
    chunk = expL[: sr]
    best = (0, -1.0)
    for lag in range(0, len(hostL) - sr, 256):
        seg = hostL[lag:lag + sr]
        c = np.dot(seg, chunk) / (np.linalg.norm(seg) * np.linalg.norm(chunk) + 1e-9)
        if c > best[1]:
            best = (lag, c)
    lag, c = best
    print("best coarse alignment: host sample %d (t=%.2f s) corr=%.4f" % (
        lag, lag / sr, c))

    # refine
    best = (lag, -1.0)
    for l in range(lag - 300, lag + 300):
        seg = hostL[l:l + sr]
        if len(seg) < sr:
            continue
        cc = np.dot(seg, chunk) / (np.linalg.norm(seg) * np.linalg.norm(chunk) + 1e-9)
        if cc > best[1]:
            best = (l, cc)
    lag, c = best
    print("refined alignment: host sample %d (t=%.2f s) corr=%.4f" % (
        lag, lag / sr, c))

    # ---- 4. per-second comparison
    print("\n=== per-second: host vs expected-from-guest ===")
    print("  t(s)  hostRms  expRms  residRms  hostPeak  expPeak  corr")
    for sec in range(0, min(int(len(hostL) / sr) - 1, 60)):
        # expected stream starts at the guest frame that corresponds to this second.
        # Use frame timestamps: find the frame whose ts is closest to (t_rec).
        # We align the guest stream to the host by the sample lag found above.
        s0 = sec * sr - lag
        e0 = s0
        if e0 < 0 or e0 + sr > len(expL):
            continue
        hseg = hostL[sec * sr:(sec + 1) * sr]
        eseg = expL[e0:e0 + sr]
        if len(hseg) < sr:
            continue
        res = hseg - eseg
        cc = np.dot(hseg, eseg) / (np.linalg.norm(hseg) * np.linalg.norm(eseg) + 1e-9)
        print("  %4d  %8.0f %8.0f %8.0f  %8.0f %8.0f  %6.3f" % (
            sec, np.sqrt((hseg ** 2).mean()), np.sqrt((eseg ** 2).mean()),
            np.sqrt((res ** 2).mean()), np.abs(hseg).max(), np.abs(eseg).max(), cc))


if __name__ == "__main__":
    main()
