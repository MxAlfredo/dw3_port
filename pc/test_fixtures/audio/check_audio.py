#!/usr/bin/env python3
"""The independent half of `make audiotest` (content-export E3a/E3b).

audiotest (C) builds a tiny synthetic bank, exports it and leaves under <dir>/:
  data/audio/TEST01/...   what the exporter wrote
  expect/vag_NNN.adpcm    the ADPCM bytes of each sample, as put in the bank
  expect/seq_P_RR.txt     what the port's own SEP parser decoded from each record

This script shares no code with the exporter: it decodes the ADPCM again (double precision, the SPU recurrence), parses the WAV and MIDI
files with its own readers (no `wave`, no `mido`) and checks the JSON against the tone definitions the C test wrote into the bank.
Run with `python3 -I check_audio.py <dir>`. Exit 0: all held.
"""
import json
import math
import os
import struct
import sys

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("check_audio: FAIL: " + what)


def read(path):
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------- ADPCM

K0 = [0.0, 0.9375, 1.796875, 1.53125, 1.90625]
K1 = [0.0, 0.0, -0.8125, -0.859375, -0.9375]


def decode_vag(data):
    """Returns (samples, ends, loop_start, loop_end): the blocks up to the first end flag (the SPU repeats or mutes there)."""
    out = []
    p1 = p2 = 0.0
    start = None
    ends = 0
    loop_end = 0
    for b in range(len(data) // 16):
        blk = data[b * 16:(b + 1) * 16]
        shift, filt, flags = blk[0] & 15, min(blk[0] >> 4, 4), blk[1]
        if flags & 4 and start is None:
            start = len(out)
        for i in range(28):
            byte = blk[2 + i // 2]
            nib = (byte >> 4) if (i & 1) else (byte & 15)
            if nib > 7:
                nib -= 16
            y = nib * 2.0 ** (12 - shift) + p1 * K0[filt] + p2 * K1[filt]
            p2, p1 = p1, y
            out.append(max(-32768, min(32767, int(math.floor(abs(y) + 0.5)) * (1 if y >= 0 else -1))))
        if flags & 1:
            ends = 2 if flags & 2 else 1
            loop_end = len(out) - 1
            break
    return out, ends, (start or 0) if ends == 2 else 0, loop_end


# ---------------------------------------------------------------- WAV

def parse_wav(data):
    check(data[:4] == b"RIFF" and data[8:12] == b"WAVE", "RIFF/WAVE header")
    check(struct.unpack("<I", data[4:8])[0] == len(data) - 8, "RIFF size")
    chunks = {}
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        chunks[cid] = data[pos + 8:pos + 8 + size]
        check(pos + 8 + size <= len(data), "chunk %r inside the file" % cid)
        pos += 8 + size + (size & 1)
    check(pos == len(data), "the chunks end with the file")
    return chunks


# the tones of the synthetic bank, as the C test defines them: (program, tone, vag, vol, pan, center, shift, min, max, bend down, bend up,
# adsr1, adsr2, priority)
TONES = [
    (0, 0, 1, 100, 64, 60, 0, 0, 71, 2, 2, 0x957C, 0xC9AA, 16),
    (0, 1, 2, 90, 32, 48, 16, 72, 127, 12, 12, 0x000F, 0x0000, 0),
    (5, 0, 3, 80, 100, 72, -32, 0, 127, 0, 0, 0x1234, 0x5678, 3),
]


def root_of(center, shift):
    """The note a sample sounds at its recorded pitch: center - shift / 128 semitones -> (note, fraction in 1/2^32)."""
    total = center * 128 - shift
    note, rest = divmod(total, 128)
    return note, rest * (2 ** 32) // 128


def adsr_expect(a1, a2):
    dec = (a2 >> 14) & 1
    raw = (a2 >> 6) & 3
    return {
        "attack_mode": "exponential" if a1 >> 15 else "linear",
        "attack_shift": (a1 >> 10) & 31,
        "attack_step": 7 - ((a1 >> 8) & 3),
        "decay_shift": (a1 >> 4) & 15,
        "sustain_level": min(((a1 & 15) + 1) * 0x800, 0x7FFF),
        "sustain_mode": "exponential" if a2 >> 15 else "linear",
        "sustain_direction": "decrease" if dec else "increase",
        "sustain_shift": (a2 >> 8) & 31,
        "sustain_step": raw - 8 if dec else 7 - raw,
        "release_mode": "exponential" if (a2 >> 5) & 1 else "linear",
        "release_shift": a2 & 31,
    }


def check_samples(root, bank):
    adir = os.path.join(root, "data", "audio", "TEST01")
    for v in (1, 2, 3):
        adpcm = read(os.path.join(root, "expect", "vag_%03d.adpcm" % v))
        want, ends, ls, le = decode_vag(adpcm)
        ch = parse_wav(read(os.path.join(adir, "vag_%03d.wav" % v)))
        fmt = struct.unpack("<HHIIHH", ch[b"fmt "])
        check(fmt == (1, 1, 44100, 88200, 2, 16), "vag %d fmt chunk %r" % (v, fmt))
        got = list(struct.unpack("<%dh" % (len(ch[b"data"]) // 2), ch[b"data"]))
        check(len(got) == len(want), "vag %d: %d frames, want %d" % (v, len(got), len(want)))
        worst = max((abs(a - b) for a, b in zip(got, want)), default=0)
        check(worst <= 2, "vag %d: samples differ from the reference decode by up to %d" % (v, worst))
        sm = ch.get(b"smpl")
        check(sm is not None, "vag %d has a smpl chunk" % v)
        if sm is None:
            continue
        period, note, frac, _, _, nloops = struct.unpack("<IIIIII", sm[8:32])
        tone = [t for t in TONES if t[2] == v][0]
        wnote, wfrac = root_of(tone[5], tone[6])
        check((note, frac) == (wnote, wfrac), "vag %d root %r, want %r" % (v, (note, frac), (wnote, wfrac)))
        check(period == 1000000000 // 44100, "vag %d sample period" % v)
        check(nloops == (1 if ends == 2 else 0), "vag %d loop count %d" % (v, nloops))
        if ends == 2:
            _, typ, s, e, _, plays = struct.unpack("<IIIIII", sm[36:60])
            check((typ, s, e, plays) == (0, ls, le, 0), "vag %d loop %r, want %r" % (v, (s, e), (ls, le)))
            check(0 <= s <= e < len(got), "vag %d loop inside the sample" % v)
        check(len(sm) == 36 + 24 * nloops, "vag %d smpl size" % v)
        info = [s for s in bank["samples"] if s["vag"] == v][0]
        check(info["frames"] == len(want), "bank.json frames of vag %d" % v)
        check(info["end"] == {0: "none", 1: "stop", 2: "loop"}[ends], "bank.json end of vag %d" % v)
        if ends == 2:
            check((info["loop_start"], info["loop_end"]) == (ls, le), "bank.json loop of vag %d" % v)


def check_bank_json(root):
    bank = json.loads(read(os.path.join(root, "data", "audio", "TEST01", "bank.json")))
    check(bank["format"] == "bank-v1" and bank["bank"] == "TEST01" and bank["bank_id"] == 3, "bank header fields")
    check(bank["sample_rate"] == 44100 and bank["vag_count"] == 4, "bank counts")
    check(bank["master_volume"] == 127 and bank["master_pan"] == 64, "bank master volume and pan")
    check(isinstance(bank["notes"], list) and all(isinstance(n, str) and n for n in bank["notes"]), "notes block")
    check([s["vag"] for s in bank["samples"]] == [1, 2, 3, 4] and bank["samples"][3]["file"] is None, "samples list")
    progs = {p["program"]: p for p in bank["programs"]}
    check(sorted(progs) == [0, 5], "programs %r" % sorted(progs))
    check((progs[0]["volume"], progs[0]["pan"], progs[5]["volume"], progs[5]["pan"]) == (110, 70, 120, 60), "program volume and pan")
    n = 0
    for (prog, tone, vag, vol, pan, center, shift, mn, mx, bd, bu, a1, a2, prior) in TONES:
        got = [t for t in progs[prog]["tones"] if t["tone"] == tone]
        check(len(got) == 1, "tone %d/%d present" % (prog, tone))
        if not got:
            continue
        t = got[0]
        note, frac = root_of(center, shift)
        want = {"vag": vag, "min": mn, "max": mx, "center": center, "shift": shift, "root_note": note, "volume": vol, "pan": pan,
                "priority": prior, "pitch_bend_down": bd, "pitch_bend_up": bu, "adsr1": "0x%04X" % a1, "adsr2": "0x%04X" % a2}
        for k, v in want.items():
            check(t.get(k) == v, "tone %d/%d %s = %r, want %r" % (prog, tone, k, t.get(k), v))
        check(abs(t["root_cents"] - frac * 100.0 / 2 ** 32) < 1e-3, "tone %d/%d root_cents" % (prog, tone))
        check(t["adsr"] == adsr_expect(a1, a2), "tone %d/%d decoded adsr %r" % (prog, tone, t["adsr"]))
        n += 1
    check(n == 3 and sum(len(p["tones"]) for p in bank["programs"]) == 3, "three tones")
    return bank


# ---------------------------------------------------------------- MIDI

def vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse_midi(d):
    """-> (format, division, [track events]); an event is (tick, 'meta', type, bytes) or (tick, 'ch', status, d1, d2 or None)."""
    check(d[:4] == b"MThd" and struct.unpack(">I", d[4:8])[0] == 6, "MThd")
    fmt, ntr, div = struct.unpack(">HHH", d[8:14])
    pos = 14
    tracks = []
    for _ in range(ntr):
        check(d[pos:pos + 4] == b"MTrk", "MTrk")
        size = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + size]
        check(len(body) == size, "track length")
        pos += 8 + size
        ev = []
        i = 0
        tick = 0
        status = 0
        while i < len(body):
            delta, i = vlq(body, i)
            tick += delta
            b = body[i]
            if b == 0xFF:
                typ = body[i + 1]
                ln, j = vlq(body, i + 2)
                ev.append((tick, "meta", typ, bytes(body[j:j + ln])))
                i = j + ln
                continue
            if b >= 0x80:
                status = b
                i += 1
            n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
            args = list(body[i:i + n])
            i += n
            ev.append((tick, "ch", status, args[0], args[1] if n == 2 else None))
        check(ev and ev[-1][1] == "meta" and ev[-1][2] == 0x2F, "track ends with FF 2F")
        tracks.append(ev)
    check(pos == len(d), "no bytes after the last track")
    return fmt, div, tracks


def parse_expect(path):
    rec = {"events": [], "loop": None}
    for line in read(path).decode().splitlines():
        w = line.split()
        if w[0] == "res":
            rec["res"] = int(w[1])
        elif w[0] == "tempo":
            rec["tempo"] = int(w[1])
        elif w[0] == "sig":
            rec["num"], rec["den"] = int(w[1]), int(w[2])
        elif w[0] == "loop":
            if w[1] != "none":
                rec["loop"] = tuple(int(x) for x in w[1:5])  # start tick, end tick, count, index of the body's first event
        elif w[0] == "ev":
            rec["events"].append((int(w[1]), w[2], int(w[3]), int(w[4]), int(w[5]), int(w[6])))
    return rec


def expected_seconds(rec):
    tempo, last, sec = rec["tempo"], 0, 0.0
    for tick, kind, ch, note, data, datum in rec["events"]:
        if kind == "tempo" and datum:
            tempo = datum
        if tick > last:
            sec += (tick - last) * tempo / (rec["res"] * 1e6)
            last = tick
    return sec


def check_midi(root, pkg, r):
    name = "seq_%d_%02d" % (pkg, r)
    rec = parse_expect(os.path.join(root, "expect", name + ".txt"))
    fmt, div, tracks = parse_midi(read(os.path.join(root, "data", "audio", "TEST01", name + ".mid")))
    check(fmt == 1 and div == rec["res"], "%s: format %d division %d" % (name, fmt, div))
    evs = rec["events"]
    end = max(e[0] for e in evs)
    channels = sorted({e[2] for e in evs if e[1] not in ("tempo", "end")})
    check(len(tracks) == 1 + len(channels), "%s: %d tracks, want %d" % (name, len(tracks), 1 + len(channels)))
    if len(tracks) != 1 + len(channels):
        return
    cond = tracks[0]
    tempos = [(e[0], int.from_bytes(e[3], "big")) for e in cond if e[1] == "meta" and e[2] == 0x51]
    want_t = [(0, rec["tempo"])] + [(e[0], e[5]) for e in evs if e[1] == "tempo"]
    check(tempos == want_t, "%s: tempo events %r, want %r" % (name, tempos, want_t))
    sigs = [(e[0], tuple(e[3])) for e in cond if e[1] == "meta" and e[2] == 0x58]
    check(sigs == [(0, (rec["num"], rec["den"], 24, 8))], "%s: time signature %r" % (name, sigs))
    check(any(e[1] == "meta" and e[2] == 0x03 and b"package %d record %d" % (pkg, r) in e[3] for e in cond), "%s: track name" % name)
    marks = [(e[0], e[3].decode()) for e in cond if e[1] == "meta" and e[2] == 0x06]
    first_ch = channels[0]
    if rec["loop"]:
        ls, le, count, _body = rec["loop"]
        word = "forever" if count in (0, 0x7F) else str(count)
        check(marks == [(ls, "loopStart count=" + word), (le, "loopEnd")], "%s: loop markers %r" % (name, marks))
    else:
        check(marks == [], "%s: no loop markers" % name)
    for e in cond:
        check(e[0] <= end, "%s: conductor event past the end" % name)
    check(cond[-1][0] == end, "%s: conductor ends at %d (want %d)" % (name, cond[-1][0], end))
    for ti, ch in enumerate(channels, start=1):
        got = []
        cc111 = []
        for e in tracks[ti][:-1]:
            check(e[1] == "ch" and (e[2] & 15) == ch, "%s: only channel %d events in its track" % (name, ch))
            if (e[2] & 0xF0) == 0xB0 and e[3] == 111:
                cc111.append(e[0])
                continue
            got.append(e)
        want = []
        for (tick, kind, c, note, data, datum) in evs:
            if c != ch or kind in ("tempo", "end"):
                continue
            if kind == "keyon":
                want.append((tick, "ch", 0x90 | ch, note, data))
            elif kind == "keyoff":
                want.append((tick, "ch", 0x80 | ch, note, 0x40))
            elif kind == "prog":
                want.append((tick, "ch", 0xC0 | ch, data, None))
            elif kind == "ctrl":
                want.append((tick, "ch", 0xB0 | ch, note, data))
            elif kind == "bend":
                want.append((tick, "ch", 0xE0 | ch, 0, data))
        check(got == want, "%s: channel %d events differ\n  got  %r\n  want %r" % (name, ch, got, want))
        check(tracks[ti][-1][0] == end, "%s: channel %d ends at the record's end" % (name, ch))
        if rec["loop"] and ch == first_ch:
            check(cc111 == [rec["loop"][0]], "%s: CC111 at the loop start, got %r" % (name, cc111))
        else:
            check(cc111 == [], "%s: no CC111 on channel %d" % (name, ch))
    return rec, channels, end


def check_sequences(root, recs):
    seq = json.loads(read(os.path.join(root, "data", "audio", "TEST01", "sequences.json")))
    check(seq["format"] == "sequences-v1" and seq["bank"] == "TEST01" and seq["bank_id"] == 3, "sequences header")
    check(isinstance(seq["notes"], list) and seq["notes"], "sequences notes")
    got = {s["file"]: s for s in seq["sequences"]}
    check(sorted(got) == sorted("seq_%d_%02d.mid" % k for k in recs), "sequence files %r" % sorted(got))
    rec0 = len([k for k in recs if k[0] == 0])
    for (pkg, r), (rec, channels, end) in recs.items():
        s = got["seq_%d_%02d.mid" % (pkg, r)]
        flat = pkg * 16 + r
        check((s["package"], s["record"], s["flat"]) == (pkg, r, flat), "%r numbering" % ((pkg, r),))
        direct = 0x60000000 | (3 << 18) | (pkg << 8) | r
        check(s["id_direct"] == "0x%08X" % direct, "%r id_direct %r" % ((pkg, r), s["id_direct"]))
        reach = pkg == 0 or flat >= rec0
        want_id = "0x%08X" % (0x60000000 | (3 << 18) | (r if pkg == 0 else flat)) if reach else None
        check(s["id"] == want_id, "%r id %r want %r" % ((pkg, r), s["id"], want_id))
        check(s["resolution"] == rec["res"] and s["tempo_us"] == rec["tempo"], "%r resolution/tempo" % ((pkg, r),))
        check(s["time_signature"] == "%d/%d" % (rec["num"], 2 ** rec["den"]), "%r time signature %r" % ((pkg, r), s["time_signature"]))
        check(abs(s["bpm"] - 60000000.0 / rec["tempo"]) < 0.01, "%r bpm" % ((pkg, r),))
        check(s["events"] == len(rec["events"]) and s["length_ticks"] == end and s["channels"] == channels, "%r counts" % ((pkg, r),))
        check(abs(s["length_seconds"] - expected_seconds(rec)) < 0.001, "%r length_seconds %r want %r" % ((pkg, r), s["length_seconds"], expected_seconds(rec)))
        if rec["loop"]:
            ls, le, count, _ = rec["loop"]
            check(s["loop"] == {"start_tick": ls, "end_tick": le, "count": ("forever" if count in (0, 0x7F) else count)}, "%r loop %r" % ((pkg, r), s["loop"]))
        else:
            check(s["loop"] is None, "%r no loop" % ((pkg, r),))
    # the flat numbering the game uses: package 1, record 0 is track 16
    check(got["seq_1_00.mid"]["flat"] == 16, "package 1 starts at flat track 16")


def check_manifest(root):
    man = json.loads(read(os.path.join(root, "data", "audio", "manifest.json")))
    files = man["files"]
    check(man["stamp"] == "audio-v1", "manifest stamp")
    for rel in ("vag_001.wav", "vag_002.wav", "vag_003.wav", "bank.json", "seq_0_00.mid", "seq_0_01.mid", "seq_1_00.mid", "sequences.json"):
        check("TEST01/" + rel in files, "manifest has TEST01/" + rel)
    check(all(not k.endswith(".new") for k in files), "the manifest has no .new file")
    check(files["TEST01/exporter"] == "%016x" % 1, "bank exporter version")


def main():
    root = sys.argv[1]
    bank = check_bank_json(root)
    check_samples(root, bank)
    recs = {}
    for pkg, r in ((0, 0), (0, 1), (1, 0)):
        res = check_midi(root, pkg, r)
        if res:
            recs[(pkg, r)] = res
    check_sequences(root, recs)
    check_manifest(root)
    if fails:
        print("check_audio: %d check(s) FAILED" % fails)
        return 1
    print("check_audio: OK")
    return 0


sys.exit(main())
