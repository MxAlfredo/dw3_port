#!/usr/bin/env python3
"""Independent half of pc/videotest.c (content-export E2a): decodes the JPEGs with PIL, parses the AVI files byte by byte and
compares both with what the C side kept. Usage: check_video.py <scratch dir written by videotest>. Exit 0 when all holds."""
import io
import math
import os
import struct
import sys

from PIL import Image

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print("check_video: FAIL %s" % what)
        failures += 1
    return cond


def psnr(a, b):
    if len(a) != len(b):
        return 0.0
    err = sum((x - y) * (x - y) for x, y in zip(a, b))
    if err == 0:
        return 99.0
    return 10.0 * math.log10(255.0 * 255.0 * len(a) / err)


def decode(jpeg, w, h, what):
    try:
        im = Image.open(io.BytesIO(jpeg))
        im.load()
    except Exception as e:  # noqa: BLE001
        check(False, "%s: PIL cannot decode it (%s)" % (what, e))
        return None
    check(im.format == "JPEG" and im.size == (w, h), "%s: format %s size %s" % (what, im.format, im.size))
    return im.convert("RGB").tobytes()


def check_profile(jpeg, what):
    """Baseline, 4:2:0, 3 components, JFIF, standard Huffman tables (4 DHT of 0x1F + payload), no restart interval."""
    check(jpeg[:2] == b"\xff\xd8" and jpeg[-2:] == b"\xff\xd9", what + ": SOI/EOI")
    pos, sof = 2, None
    dht = 0
    while pos + 4 <= len(jpeg):
        if jpeg[pos] != 0xFF:
            break
        marker = jpeg[pos + 1]
        seg = struct.unpack(">H", jpeg[pos + 2:pos + 4])[0]
        body = jpeg[pos + 4:pos + 2 + seg]
        if marker == 0xC0:
            sof = body
        elif marker in (0xC1, 0xC2, 0xC3):
            check(False, what + ": not baseline")
        elif marker == 0xC4:
            dht += 1
        elif marker == 0xDD:
            check(False, what + ": restart interval")
        elif marker == 0xDA:
            break
        pos += 2 + seg
    check(sof is not None and sof[0] == 8 and sof[5] == 3, what + ": SOF0 8-bit 3 components")
    if sof is not None:
        comps = [(sof[6 + 3 * i], sof[7 + 3 * i], sof[8 + 3 * i]) for i in range(3)]
        check(comps[0][1] == 0x22 and comps[1][1] == 0x11 and comps[2][1] == 0x11, what + ": 4:2:0 sampling %s" % comps)
    check(dht == 4, what + ": four Huffman tables")


def reference_psnr(raw, w, h, q):
    """PSNR of PIL's own encoder (libjpeg, 4:2:0) on the same picture at the same quality."""
    buf = io.BytesIO()
    Image.frombytes("RGB", (w, h), raw).save(buf, "JPEG", quality=q, subsampling=2)
    return psnr(raw, Image.open(io.BytesIO(buf.getvalue())).convert("RGB").tobytes())


def check_jpeg(d, rgb, jpg, w, h, minp, q):
    raw = open(os.path.join(d, rgb), "rb").read()
    jpeg = open(os.path.join(d, jpg), "rb").read()
    px = decode(jpeg, w, h, jpg)
    check_profile(jpeg, jpg)
    if px is not None:
        p = psnr(raw, px)
        ref = reference_psnr(raw, w, h, q)
        check(p >= minp, "%s: PSNR %.2f dB < %.1f" % (jpg, p, minp))
        check(p >= ref - 1.0, "%s: PSNR %.2f dB is more than 1 dB under PIL's encoder (%.2f dB)" % (jpg, p, ref))
        print("jpeg %s: %d bytes, PSNR %.2f dB (PIL's encoder %.2f dB)" % (jpg, len(jpeg), p, ref))


def u32(b, o):
    return struct.unpack("<I", b[o:o + 4])[0]


def u16(b, o):
    return struct.unpack("<H", b[o:o + 2])[0]


def parse_list(data, start, end):
    """Chunks and lists inside [start, end): (fourcc, listtype or None, body start, size)."""
    out = []
    pos = start
    while pos < end:
        if not check(pos + 8 <= end, "chunk header past the end at %d" % pos):
            break
        cid = data[pos:pos + 4]
        size = u32(data, pos + 4)
        body = pos + 8
        if cid == b"LIST":
            out.append((cid, data[body:body + 4], body + 4, size - 4))
        else:
            out.append((cid, None, body, size))
        pos = body + size + (size & 1)
    check(pos == end, "chunks do not end exactly at %d (at %d)" % (end, pos))
    return out


def check_avi(d, name, frames, channels, w, h, pcm, ref, spf):
    data = open(os.path.join(d, name), "rb").read()
    tag = name
    check(data[:4] == b"RIFF" and u32(data, 4) == len(data) - 8 and data[8:12] == b"AVI ", tag + ": RIFF header and size")
    top = parse_list(data, 12, len(data))
    check([t[0] + (t[1] or b"") for t in top] == [b"LISThdrl", b"LISTmovi", b"idx1"], tag + ": top level %s" % [(t[0], t[1]) for t in top])
    if len(top) != 3:
        return
    hdrl, movi, idx1 = top
    hl = parse_list(data, hdrl[2], hdrl[2] + hdrl[3])
    check(hl[0][0] == b"avih" and hl[0][3] == 56, tag + ": avih")
    a = hl[0][2]
    micro, maxbps, pad, flags, total, initial, streams, bufsize, aw, ah = struct.unpack("<10I", data[a:a + 40])
    check(micro == 66667 and (flags & 0x10) and total == frames, tag + ": avih micro=%d flags=%x total=%d" % (micro, flags, total))
    check(streams == (2 if channels else 1) and aw == w and ah == h and bufsize > 0, tag + ": avih streams/size")
    check(maxbps > 0, tag + ": avih max bytes per second")
    strls = [s for s in hl[1:] if s[0] == b"LIST" and s[1] == b"strl"]
    check(len(strls) == (2 if channels else 1) and len(hl) == 1 + len(strls), tag + ": stream lists")
    audio_frames = 0
    for i, s in enumerate(strls):
        sub = parse_list(data, s[2], s[2] + s[3])
        names = [x[0] for x in sub]
        # the video strl may end with an OpenDML vprp that declares the display aspect (the movie exports do)
        check(names == [b"strh", b"strf"] or (i == 0 and names == [b"strh", b"strf", b"vprp"]), tag + ": strl %d content" % i)
        if names[-1] == b"vprp":
            vp = struct.unpack("<9I", data[sub[2][2]:sub[2][2] + 36])
            check(sub[2][3] == 68 and vp[5] == (20 << 16 | 13) and vp[6] == w and vp[7] == h and vp[8] == 1, tag + ": vprp %s" % (vp,))
        elif i == 0 and tag.startswith("MOVIE"):
            check(False, tag + ": the movie export lacks the vprp aspect")
        sh = sub[0][2]
        typ, handler = data[sh:sh + 4], data[sh + 4:sh + 8]
        flags2, prio, lang, init, scale, rate, start, length, sbuf, quality, ssize = struct.unpack("<IHHIIIIIIII", data[sh + 8:sh + 48])
        sf = sub[1][2]
        if i == 0:
            check(typ == b"vids" and handler == b"MJPG", tag + ": video strh type %s handler %s" % (typ, handler))
            check(scale == 1 and rate == 15 and start == 0 and length == frames, tag + ": video rate %d/%d length %d" % (rate, scale, length))
            size, bw, bh, planes, bits, comp, simg = struct.unpack("<IiiHHII", data[sf:sf + 24])
            check(size == 40 and sub[1][3] == 40 and bw == w and bh == h and planes == 1 and bits == 24 and comp == 0x47504A4D,
                  tag + ": BITMAPINFOHEADER %s" % ((size, bw, bh, planes, bits, comp),))
        else:
            check(typ == b"auds" and handler == b"\0\0\0\0", tag + ": audio strh type")
            tagf, ch, sr, avg, align, bps = struct.unpack("<HHIIHH", data[sf:sf + 16])
            check(tagf == 1 and ch == channels and sr == 37800 and bps == 16 and align == ch * 2 and avg == sr * align,
                  tag + ": WAVEFORMATEX %s" % ((tagf, ch, sr, avg, align, bps),))
            check(scale == align and rate == 37800 * align and ssize == align, tag + ": audio strh scale/rate/sample size")
            audio_frames = length
    # the movie list: chunks in order, with their offsets from the 'movi' fourcc
    mchunks = parse_list(data, movi[2], movi[2] + movi[3])
    entries = []
    vids, auds = [], []
    audio_bytes = bytearray()
    audio_before = []
    for cid, _, body, size in mchunks:
        off = body - 8 - (movi[2] - 4)
        entries.append((cid, off, size))
        if cid == b"00dc":
            vids.append(data[body:body + size])
            audio_before.append(len(audio_bytes) // (2 * channels) if channels else 0)
        elif cid == b"01wb":
            audio_bytes += data[body:body + size]
            auds.append(size)
        else:
            check(False, tag + ": unknown chunk %s" % cid)
    check(len(vids) == frames, tag + ": %d video chunks, expected %d" % (len(vids), frames))
    # idx1: one entry per chunk, same order, offset and size, key frame flag
    ix = parse_list_idx(data, idx1[2], idx1[3])
    check(ix == [(c, 0x10, o, s) for c, o, s in entries], tag + ": idx1 does not match the chunks")
    if channels:
        check(len(audio_bytes) == audio_frames * channels * 2, tag + ": audio length %d vs strh %d" % (len(audio_bytes) // (2 * channels), audio_frames))
        if pcm != "-":
            want = open(os.path.join(d, pcm), "rb").read()
            check(bytes(audio_bytes) == want, tag + ": audio bytes differ from the decoded XA (%d vs %d)" % (len(audio_bytes), len(want)))
        for k, before in enumerate(audio_before):
            if before < min(audio_frames, k * spf):
                check(False, tag + ": frame %d has only %d audio frames before it" % (k, before))
                break
        dur_a = audio_frames / 37800.0
        dur_v = frames / 15.0
        check(abs(dur_a - dur_v) <= 4032 / 37800.0 + 1 / 15.0, tag + ": audio %.3f s vs video %.3f s" % (dur_a, dur_v))
        print("%s: %d frames, audio %.3f s, video %.3f s" % (tag, frames, dur_a, dur_v))
    else:
        check(not auds, tag + ": audio chunks without an audio stream")
        print("%s: %d frames, no audio" % (tag, frames))
    # pictures
    if ref != "-":
        rawref = open(os.path.join(d, ref), "rb").read()
        fs = w * h * 3
        check(len(rawref) == fs * frames, tag + ": reference size")
        worst = 99.0
        for k in range(frames):
            if k == 0:
                check_profile(vids[0], tag + " frame 0")
            px = decode(vids[k], w, h, "%s frame %d" % (tag, k))
            if px is None:
                continue
            worst = min(worst, psnr(rawref[k * fs:(k + 1) * fs], px))
        check(worst >= 30.0, tag + ": worst frame PSNR %.2f dB" % worst)
        print("%s: worst frame PSNR %.2f dB over %d frames" % (tag, worst, frames))


def parse_list_idx(data, start, size):
    out = []
    for p in range(start, start + size, 16):
        out.append((data[p:p + 4], u32(data, p + 4), u32(data, p + 8), u32(data, p + 12)))
    return out


def main():
    d = sys.argv[1]
    for line in open(os.path.join(d, "manifest.txt")):
        f = line.split()
        if not f:
            continue
        if f[0] == "jpeg":
            check_jpeg(d, f[1], f[2], int(f[3]), int(f[4]), float(f[5]), int(f[6]))
        elif f[0] == "avi":
            check_avi(d, f[1], int(f[2]), int(f[3]), int(f[4]), int(f[5]), f[6], f[7], int(f[8]) if len(f) > 8 else 2520)
    if failures:
        print("check_video: %d failures" % failures)
        return 1
    print("check_video: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
