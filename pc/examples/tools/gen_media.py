#!/usr/bin/env python3
"""Generate the audio and video examples from synthetic content (no game data at all).

    gen_media.py <user dir> [sfx-beep] [song-title-chime] [video-bars]    (default: all three)

Writes into <user dir>/mods/:
  sfx-beep/audio/COMMON/vag_050.wav        a short two-tone beep (16-bit mono, 44100 Hz)
  song-title-chime/audio/TTLBGM/seq_0_00.wav   a 12 s chime loop (16-bit mono, 22050 Hz, smpl loop)
  video-bars/video/MOVIEOPN.avi (and MOVIEOP2.avi)   a 3 s colour-bars clip (MJPEG 320x416, 15 fps,
                                           16-bit stereo PCM 37800 Hz)
The video needs Pillow (the JPEG writer); without it only the video is skipped (exit code 3).
"""
import math
import os
import struct
import sys
import wave


def tone(freq, seconds, rate, amp=0.5, fade=0.01):
    n = int(seconds * rate)
    out = []
    for i in range(n):
        t = i / rate
        env = min(1.0, i / (fade * rate), (n - i) / (fade * rate))
        out.append(amp * env * math.sin(2 * math.pi * freq * t))
    return out


def write_wav(path, samples, rate, channels=1, loop=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    if loop:
        # append a smpl chunk (one forward loop; end = last frame) and fix the RIFF size
        smpl = struct.pack("<9I", 0, 0, int(1e9 / rate), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, loop[0], loop[1], 0, 0)
        with open(path, "ab") as f:
            f.write(b"smpl" + struct.pack("<I", len(smpl)) + smpl)
        size = os.path.getsize(path) - 8
        with open(path, "r+b") as f:
            f.seek(4)
            f.write(struct.pack("<I", size))


def make_sfx(mods):
    rate = 44100
    s = tone(880, 0.09, rate) + tone(1320, 0.14, rate)
    write_wav(os.path.join(mods, "sfx-beep", "audio", "COMMON", "vag_050.wav"), s, rate)


def make_song(mods):
    rate = 22050
    notes = [262, 330, 392, 523, 392, 330, 294, 370, 440, 587, 440, 370]  # a C and a D arpeggio
    s = []
    for f in notes:
        s += tone(f, 1.0, rate, amp=0.35, fade=0.02)
    path = os.path.join(mods, "song-title-chime", "audio", "TTLBGM", "seq_0_00.wav")
    write_wav(path, s, rate, loop=(0, len(s) - 1))


BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def riff(tag, body, form=None):
    data = (form or b"") + body
    return tag + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def make_video(mods):
    from PIL import Image, ImageDraw
    import io
    w, h, fps, frames, arate = 320, 416, 15, 45, 37800
    per = arate // fps  # 2520 samples per frame
    jpgs, auds = [], []
    for k in range(frames):
        im = Image.new("RGB", (w, h))
        d = ImageDraw.Draw(im)
        bw = w // len(BARS)
        for i, c in enumerate(BARS):
            d.rectangle([i * bw, 0, (i + 1) * bw - 1, h - 1], fill=c)
        y = int((h - 40) * k / (frames - 1))
        d.rectangle([0, y, w - 1, y + 39], outline=(255, 255, 255), width=4)  # a box that falls over the bars
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=85, subsampling=2)
        jpgs.append(buf.getvalue())
        freq = 440 * (1 + k / frames)
        a = b"".join(struct.pack("<hh", v, v) for v in (int(12000 * math.sin(2 * math.pi * freq * (k * per + i) / arate)) for i in range(per)))
        auds.append(a)
    movi, idx, off = b"", b"", 4
    for k in range(frames):
        for tag, data, flags in ((b"01wb", auds[k], 0), (b"00dc", jpgs[k], 0x10)):
            chunk = riff(tag, data)
            idx += tag + struct.pack("<III", flags, off, len(data))
            movi += chunk
            off += len(chunk)
    maxjpg = max(len(j) for j in jpgs)
    avih = struct.pack("<10I4I", 1000000 // fps, 0, 0, 0x10, frames, 0, 2, maxjpg + per * 4, w, h, 0, 0, 0, 0)
    strh_v = b"vids" + b"MJPG" + struct.pack("<IHHIIIIIIIIhhhh", 0, 0, 0, 0, 1, fps, 0, frames, maxjpg, 0xFFFFFFFF, 0, 0, 0, w, h)
    strf_v = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0x47504A4D, w * h * 3, 0, 0, 0, 0)
    strh_a = b"auds" + b"\0\0\0\0" + struct.pack("<IHHIIIIIIIIhhhh", 0, 0, 0, 0, 1, arate, 0, frames * per, per * 4, 0xFFFFFFFF, 4, 0, 0, 0, 0)
    strf_a = struct.pack("<HHIIHH", 1, 2, arate, arate * 4, 4, 16)
    hdrl = riff(b"LIST", riff(b"avih", avih)
                + riff(b"LIST", riff(b"strh", strh_v) + riff(b"strf", strf_v), b"strl")
                + riff(b"LIST", riff(b"strh", strh_a) + riff(b"strf", strf_a), b"strl"), b"hdrl")
    avi = riff(b"RIFF", hdrl + riff(b"LIST", movi, b"movi") + riff(b"idx1", idx), b"AVI ")
    for name in ("MOVIEOPN", "MOVIEOP2"):
        path = os.path.join(mods, "video-bars", "video", name + ".avi")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(avi)
    return len(avi)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    mods = os.path.join(sys.argv[1], "mods")
    want = sys.argv[2:]
    rc = 0
    if not want or "sfx-beep" in want:
        make_sfx(mods)
        print("gen_media: sfx-beep ok")
    if not want or "song-title-chime" in want:
        make_song(mods)
        print("gen_media: song-title-chime ok")
    if not want or "video-bars" in want:
        try:
            n = make_video(mods)
            print(f"gen_media: video-bars ok ({n // 1024} KB)")
        except ImportError:
            print("gen_media: video-bars skipped: needs Pillow (pip install pillow)")
            rc = 3
    return rc


if __name__ == "__main__":
    sys.exit(main())
