#!/usr/bin/env python3
"""Characterise the broadband noise added to the guest's audio buffer."""
import numpy as np

SR = 48000
g = np.fromfile("/tmp/opencode/gs.bin", dtype=">f4").reshape(-1, 6, 256)
n = len(g)

ch0 = g[:, 0, :].reshape(-1)
ch1 = g[:, 1, :].reshape(-1)

clean = ch0[:1500 * 256]
noise_region = ch0[1500 * 256:]

# crude high-pass: remove the music (<1 kHz) with a difference of long window
def hp(x, taps=257):
    # moving-average subtraction -> high pass
    k = np.ones(taps) / taps
    ma = np.convolve(x, k, mode="same")
    return x - ma

nz = hp(noise_region)
cl = hp(clean)

print("noise band rms = %.5f (%.0f int16)   clean band rms = %.5f (%.0f int16)" % (
    np.sqrt((nz ** 2).mean()), np.sqrt((nz ** 2).mean()) * 32767,
    np.sqrt((cl ** 2).mean()), np.sqrt((cl ** 2).mean()) * 32767))

# --- is the noise continuous across frames, or independent per frame? ---
frames = g[1500:, 0, :]
nframes = frames.shape[0]
hf = hp(frames.reshape(-1)).reshape(nframes, 256)
c0 = hf[0]
c1 = hf[1]
print("\nframe-to-frame corr of HF noise (frame0 vs frame1): %.3f" % np.corrcoef(c0, c1)[0, 1])
same = []
for i in range(0, nframes - 1, 37):
    same.append(np.corrcoef(hf[i], hf[i + 1])[0, 1])
print("mean frame-to-frame HF corr: %.3f" % np.mean(same))
for lag in (1, 2, 3, 4, 8):
    cs = [np.corrcoef(hf[i], hf[i + lag])[0, 1] for i in range(0, min(nframes - lag, 400), 13)]
    print("  lag %d: %.3f" % (lag, np.mean(cs)))

# --- L/R correlation of the HF noise ---
hp1 = hp(ch1[1500 * 256:])
print("\nL/R HF-noise corr: %.3f" % np.corrcoef(nz, hp1)[0, 1])

# --- fine spectrum of the noise (looking for comb / discrete lines) ---
seg = nz[:16384]
S = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
f = np.fft.rfftfreq(len(seg), 1.0 / SR)
db = 20 * np.log10(S / S.max())
print("\nnoise spectrum (dB re max), 0-24 kHz:")
for i in range(0, 48):
    a = int(i * 300)
    b = int((i + 1) * 300)
    m = (f >= a) & (f < b)
    if m.any():
        print("   %5d-%5d Hz : %6.1f dB" % (a, b, db[m].max()))

# --- autocorrelation of the noise (look for a repeat period) ---
x = nz - nz.mean()
ac = np.correlate(x[:20000], x[:20000], mode="full")[20000 - 1:]
ac = ac / ac[0]
top = np.argsort(ac[1:2000])[::-1][:12] + 1
print("\nnoise autocorrelation peaks (lag, value):",
      [(int(t), round(float(ac[t]), 3)) for t in sorted(top)])

# --- how does the noise level evolve over time? ---
rms_per = np.sqrt((g[:, 0, :] ** 2).mean(axis=1))
print("\nch0 rms per frame from 1400 to 1600:")
print(np.round(rms_per[1400:1600] * 32767).astype(int))
