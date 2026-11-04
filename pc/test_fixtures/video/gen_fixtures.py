#!/usr/bin/env python3
"""Fixtures for the movie replacement tests of pc/videotest.c (content-export E2b): JPEGs written by PIL's libjpeg in the variants
a modder's encoder produces, and AVI files laid out the way ffmpeg lays them out (hand built here, with PIL's JPEG frames).
Usage: gen_fixtures.py <scratch dir>. Writes <scratch dir>/fixtures/ with index.txt, which pc/videotest.c reads. Everything is
synthetic: no game data (ADR-001). Exit 0 on success."""
import io
import math
import os
import struct
import sys

from PIL import Image, ImageDraw


def picture(w, h, k=0):
    """A smooth gradient, a box and a circle: soft enough that chroma repetition and libjpeg's fancy upsampling stay close."""
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128 + (60 if (x + y + k) % 64 < 32 else -60) // 2)
    d = ImageDraw.Draw(im)
    d.rectangle([w // 8, h // 8, w // 3, h // 3], fill=(220, 30, 40))
    d.ellipse([w // 2 - h // 6, h // 2 - h // 6, w // 2 + h // 6, h // 2 + h // 6], fill=(20, 200, 60))
    return im


def jpeg_bytes(im, **kw):
    b = io.BytesIO()
    im.save(b, "JPEG", **kw)
    return b.getvalue()


def strip_dht(data):
    """The same JPEG without its DHT segments: an AVI1 frame has none, the decoder uses the Annex K tables (PIL writes those)."""
    out = bytearray(data[:2])
    pos = 2
    while pos < len(data):
        assert data[pos] == 0xFF
        marker = data[pos + 1]
        if marker == 0xDA:  # the scan: copy the rest
            out += data[pos:]
            break
        seg = struct.unpack(">H", data[pos + 2:pos + 4])[0]
        if marker != 0xC4:
            out += data[pos:pos + 2 + seg]
        pos += 2 + seg
    return bytes(out)


def chunk(fourcc, data):
    return fourcc + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def lst(kind, body):
    return b"LIST" + struct.pack("<I", len(body) + 4) + kind + body


def build_avi(frames, fps, size, audio, with_idx1=True, bad_sizes=False):
    """frames: JPEG bytes (b"" is a repeated frame); audio: (channels, rate, pcm bytes) or None. One audio chunk per frame."""
    w, h = size
    nframes = len(frames)
    avih = struct.pack("<IIIIIIIIII4I", 1000000 // fps, 0, 0, 0x10 | (0x100 if True else 0), nframes, 0, 2 if audio else 1, 1 << 20, w, h, 0, 0, 0, 0)
    strh_v = b"vids" + b"MJPG" + struct.pack("<IHHIIIIIIIIhhhh", 0, 0, 0, 0, 1, fps, 0, nframes, 1 << 20, 0xFFFFFFFF, 0, 0, 0, w, h)
    strf_v = struct.pack("<IiiHH4sIiiII", 40, w, h, 1, 24, b"MJPG", w * h * 3, 0, 0, 0, 0)
    strl_v = lst(b"strl", chunk(b"strh", strh_v) + chunk(b"strf", strf_v))
    hdrl_body = chunk(b"avih", avih) + strl_v
    idx = b""
    if audio:
        ch, rate, pcm = audio
        strh_a = b"auds" + b"\0\0\0\0" + struct.pack("<IHHIIIIIIIIhhhh", 0, 0, 0, 0, 1, rate, 0, len(pcm) // (2 * ch), 1 << 16, 0xFFFFFFFF, 2 * ch, 0, 0, 0, 0)
        strf_a = struct.pack("<HHIIHHH", 1, ch, rate, rate * ch * 2, ch * 2, 16, 0)
        hdrl_body += lst(b"strl", chunk(b"strh", strh_a) + chunk(b"strf", strf_a))
        per = len(pcm) // (2 * ch) // nframes
    body = b""
    for i, f in enumerate(frames):
        if audio:
            a = pcm[i * per * 2 * ch:(i + 1) * per * 2 * ch]
            c = chunk(b"01wb", a)
            idx += b"01wb" + struct.pack("<III", 0, 4 + len(body), len(a))
            body += c
        c = chunk(b"00dc", f)
        idx += b"00dc" + struct.pack("<III", 0x10, 4 + len(body), len(f))
        body += c
    movi = lst(b"movi", body)
    junk = chunk(b"JUNK", b"\0" * 37)
    riff_body = b"AVI " + lst(b"hdrl", hdrl_body) + junk + movi
    if with_idx1:
        riff_body += chunk(b"idx1", idx)
    size_field = 0 if bad_sizes else len(riff_body)
    return b"RIFF" + struct.pack("<I", size_field) + riff_body


def psnr(a, b):
    err = sum((x - y) * (x - y) for x, y in zip(a, b))
    return 99.0 if err == 0 else 10.0 * math.log10(255.0 * 255.0 * len(a) / err)


def main():
    out = os.path.join(sys.argv[1], "fixtures")
    os.makedirs(out, exist_ok=True)
    index = []

    def put(name, data):
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)

    def case(name, im, minpsnr, strip=False, **kw):
        data = jpeg_bytes(im, **kw)
        if strip:
            data = strip_dht(data)
        put(name + ".jpg", data)
        ref = Image.open(io.BytesIO(jpeg_bytes(im, **kw))).convert("RGB")
        put(name + ".rgb", ref.tobytes())
        index.append("ok %s %d %d %.1f" % (name, ref.size[0], ref.size[1], minpsnr))

    im = picture(100, 67)
    # PIL's own decode of its own 4:4:4 file is the reference: the C decoder must match to rounding
    case("p444", im, 40.0, subsampling=0, quality=90)
    case("p422", im, 30.0, subsampling=1, quality=90)
    case("p420", im, 29.0, subsampling=2, quality=90)
    case("p420_big", picture(320, 416), 29.0, subsampling=2, quality=85)
    case("pgray", im.convert("L"), 40.0, quality=90)
    case("prst", im, 29.0, subsampling=2, quality=90, restart_marker_rows=1)
    case("prst444", im, 40.0, subsampling=0, quality=90, restart_marker_blocks=5)
    case("pnodht", im, 29.0, strip=True, subsampling=2, quality=90)
    case("pnodht444", im, 40.0, strip=True, subsampling=0, quality=75)
    case("ptiny", picture(8, 8), 29.0, subsampling=0, quality=95)
    case("pq100", picture(64, 48), 40.0, subsampling=0, quality=100)
    # frames the decoder must refuse, with a word of the reason
    put("pprogressive.jpg", jpeg_bytes(im, progressive=True, quality=90))
    index.append("reject pprogressive progressive")

    # the AVI laid out like ffmpeg: JUNK, an idx1, a zero-byte chunk for a repeated frame, one audio chunk per frame
    colors = []
    frames = []
    for i in range(12):
        c = ((i * 20) % 256, 255 - (i * 20) % 256, 90 + i * 5)
        colors.append(c)
        frames.append(jpeg_bytes(Image.new("RGB", (160, 120), c), quality=95, subsampling=2))
    frames[5] = b""  # a repeated frame (the picture of frame 5 stays)
    colors[5] = colors[4]
    rate1 = 22050
    n1 = rate1 // 25 * 12
    pcm1 = b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * i / rate1))) for i in range(n1))
    put("hand.avi", build_avi(frames, 25, (160, 120), (1, rate1, pcm1)))
    put("hand.pcm", pcm1)
    put("hand.colors", ("\n".join("%d %d %d" % c for c in colors) + "\n").encode())
    index.append("avi hand.avi 12 160 120 25 1 %d %d" % (rate1, n1))

    # stereo at 44100 Hz, 15 fps, no idx1, a wrong RIFF size, a small picture (it is scaled up), a lowercase codec tag untouched
    frames2 = [jpeg_bytes(picture(64, 48, i), quality=90, subsampling=0) for i in range(8)]
    n2 = 44100 // 15 * 8
    pcm2 = b"".join(struct.pack("<hh", int(9000 * math.sin(2 * math.pi * 300 * i / 44100)), int(-9000 * math.sin(2 * math.pi * 500 * i / 44100))) for i in range(n2))
    put("hand44.avi", build_avi(frames2, 15, (64, 48), (2, 44100, pcm2), with_idx1=False, bad_sizes=True))
    put("hand44.pcm", pcm2)
    index.append("avi hand44.avi 8 64 48 15 2 44100 %d" % n2)

    # no audio at all, and a frame without DHT (AVI1 style)
    frames3 = [strip_dht(jpeg_bytes(picture(96, 64, i), quality=85)) for i in range(5)]
    put("hand_silent.avi", build_avi(frames3, 15, (96, 64), None))
    index.append("avi hand_silent.avi 5 96 64 15 0 0 0")

    with open(os.path.join(out, "index.txt"), "w") as f:
        f.write("\n".join(index) + "\n")
    print("gen_fixtures: %d fixtures in %s" % (len(index), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
