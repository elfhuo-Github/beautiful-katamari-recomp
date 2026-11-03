#!/usr/bin/env python3
"""
Poll the guest memory arena for the XAudio render buffer and record every
distinct frame with a wall-clock timestamp.

Usage: arena_audio_poll.py <pid> <guest_va> <duration_s> <out.bin> <out.idx>

Output format:
  out.bin : concatenated 6144-byte frames (raw guest memory bytes)
  out.idx : one line per frame: <wallclock_seconds> <byte_offset_into_bin>
"""
import os
import sys
import time


def main():
    pid = sys.argv[1]
    guest_va = int(sys.argv[2], 0)
    duration = float(sys.argv[3])
    out_bin = sys.argv[4]
    out_idx = sys.argv[5]

    path = pid if pid.startswith("/") else "/dev/shm/xenia_memory_%s" % pid
    size = 6144
    fd = os.open(path, os.O_RDONLY)
    prev = None
    frames = []
    t0 = time.time()
    try:
        while time.time() - t0 < duration:
            data = os.pread(fd, size, guest_va)
            if len(data) != size:
                break
            if data != prev:
                prev = data
                frames.append((time.time(), data))
            time.sleep(0.0008)
    finally:
        os.close(fd)

    with open(out_bin, "wb") as f:
        off = 0
        with open(out_idx, "w") as ix:
            for ts, data in frames:
                f.write(data)
                ix.write("%.6f %d\n" % (ts, off))
                off += size
    print("captured %d frames -> %s" % (len(frames), out_bin))


if __name__ == "__main__":
    main()
