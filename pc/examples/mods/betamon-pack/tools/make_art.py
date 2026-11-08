#!/usr/bin/env python3
"""Generate the art of the Betamon partner and of the river MegaSeadramon of the betamon-pack example mod.

Everything here is original (the procedural models in tools/specs/, written for this project: nothing traced, nothing from the game disc: ADR-001).

Install the tool once, in a venv (SpriteKiln 0.3.0, MIT; only an art tool, the port does not depend on it):

    python3 -m venv ~/.cache/spritekiln-venv
    ~/.cache/spritekiln-venv/bin/pip install spritekiln==0.3.0
    ~/.cache/spritekiln-venv/bin/python make_art.py            # writes ../partners/betamon/ and ../sprites/river-digimon/

An artist (a future contributor) can ignore the script: overwrite the PNG files of `partners/betamon/` and `sprites/river-digimon/` (same names and
sizes; the JSON files only place the frames) and the port shows them. Or edit the models (specs/betamon.yaml,
specs/megaseadramon.yaml: the models; the other specs only change the camera, the size and the poses) and run the
script again. Never run `spritekiln update`.

What it writes (docs/modding/README.md 6.2f and 6.9b):
  portrait.png   32 x 128, four cells (the menus play them back and forth: 0 1 2 3 2 1)
  icon.png       44 x 28, the memory card icon
  field/         sprite.json (sprite-v1) and one PNG per frame: sets 1 (stand), 4 (walk), 5 (run: the walk, faster), 6 (stop),
                 facings 0..4 (the walk of the model, 8 frames)
  training/<t>/  the 12 gym scenes: training.json (partners-training-v1) and one PNG per sprite
  ../../sprites/river-digimon/   the stage sprite of the river MegaSeadramon (sprite-v1, docs/stages.md section 11): the model's
                 idle loop, five facings; every set plays it

Every PNG is indexed (4 bits), index 0 transparent (tRNS) and at most 15 visible colours: one palette per asset set (field,
portrait, icon, gym, river sprite), merged down from the model's ramps. The output is deterministic: running it twice gives the same
bytes (tools/test_make_art.py checks it). The renders are cached by SpriteKiln; the work folder is temporary.

Seadramon and MegaSeadramon as forms need no 2D art: a form is drawn by its 3D model, and no menu shows a form as a picture
(docs/formats.md 4.12), so only the partner has art.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SPECS = os.path.join(HERE, "specs")
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "partners", "betamon"))
DEFAULT_RIVER = os.path.normpath(os.path.join(HERE, "..", "sprites", "river-digimon"))
MAX_COLOURS = 15  # visible colours; index 0 is the transparent one
FLOOR = 60  # pixels added to every colour count when choosing which two colours to merge

TRAININGS = ["punch", "defense", "yoga", "study", "jump-rope", "heat-tolerance", "water-tolerance", "ice-tolerance",
             "wind-tolerance", "thunder-defense", "missile-defense", "haunting"]


# ---------------------------------------------------------------------------------------------- rendering
def render(spec, workdir, anims):
    """Render the given animations of one spec with SpriteKiln; return ({frame name: RGBA array}, pivot (x, y))."""
    out = os.path.join(workdir, os.path.splitext(spec)[0])
    cmd = [sys.executable, "-m", "spritekiln", "render", os.path.join(SPECS, spec), "-o", out, "--jobs", "1",
           "--anims", ",".join(anims)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    meta_path = os.path.join(out, os.path.splitext(spec)[0] + ".json")
    if not os.path.exists(meta_path):
        sys.exit("spritekiln failed on %s:\n%s%s" % (spec, res.stdout[-2000:], res.stderr[-2000:]))
    with open(meta_path) as f:
        meta = json.load(f)
    atlas = np.array(Image.open(os.path.join(out, meta["meta"]["image"])).convert("RGBA"))
    frames = {}
    for name, v in meta["frames"].items():
        r = v["frame"]
        frames[name] = atlas[r["y"]:r["y"] + r["h"], r["x"]:r["x"] + r["w"]].copy()
    pv = meta["spritekiln"]["pivot"]
    return frames, (int(round(pv["x"])), int(round(pv["y"])))


def trim(arr):
    """(RGB array with a -1 alpha marker as a mask, x0, y0) of the opaque box of an RGBA frame."""
    mask = arr[:, :, 3] >= 128
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        sys.exit("an empty frame")
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return arr[y0:y1, x0:x1], int(x0), int(y0)


# ---------------------------------------------------------------------------------------------- palette
class Palette:
    """One palette for an asset set: the colours the renders use, merged down to MAX_COLOURS if there are more."""

    def __init__(self, arrays):
        counts = {}
        for a in arrays:
            op = a[:, :, 3] >= 128
            for c in map(tuple, a[:, :, :3][op].tolist()):
                counts[c] = counts.get(c, 0) + 1
        cols = {c: n for c, n in counts.items()}
        self.map = {c: c for c in cols}
        while len(cols) > MAX_COLOURS:  # merge the cheapest pair into the commoner colour
            keys = sorted(cols)
            best = None
            for i, a in enumerate(keys):
                for b in keys[i + 1:]:
                    # squared distance weighted like Ward's method, with a floor on the counts so that a small but distinct
                    # colour (a pupil, a blue stripe) is not eaten before a big flat one
                    na, nb = cols[a] + FLOOR, cols[b] + FLOOR
                    d = sum((x - y) ** 2 for x, y in zip(a, b)) * na * nb / (na + nb)
                    if best is None or d < best[0]:
                        best = (d, a, b)
            _, a, b = best
            keep, drop = (a, b) if cols[a] >= cols[b] else (b, a)
            cols[keep] += cols.pop(drop)
            for k, v in self.map.items():
                if v == drop:
                    self.map[k] = keep
        # the darkest first: index 1 is the outline
        self.colours = sorted(cols, key=lambda c: (c[0] * 299 + c[1] * 587 + c[2] * 114, c))
        self.index = {c: i + 1 for i, c in enumerate(self.colours)}

    def quantize(self, arr):
        """RGBA array -> 2D uint8 of palette indices (0 = transparent)."""
        out = np.zeros(arr.shape[:2], dtype=np.uint8)
        for y in range(arr.shape[0]):
            for x in range(arr.shape[1]):
                if arr[y, x, 3] >= 128:
                    out[y, x] = self.index[self.map[tuple(int(v) for v in arr[y, x, :3])]]
        return out


# ---------------------------------------------------------------------------------------------- PNG output
def write_png(path, idx, palette):
    """Indexed 4-bit PNG, index 0 transparent: no timestamps, fixed filter, so the bytes are the same every run."""
    h, w = idx.shape
    if w < 1 or h < 1:
        sys.exit("empty image " + path)
    rows = []
    for y in range(h):
        row = list(idx[y]) + ([0] if w % 2 else [])
        rows.append(b"\x00" + bytes((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2)))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    plte = b"\x00\x00\x00" + b"".join(bytes(c) for c in palette.colours)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 4, 3, 0, 0, 0))
    png += chunk(b"PLTE", plte) + chunk(b"tRNS", b"\x00") + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


# ---------------------------------------------------------------------------------------------- the field sprite
FIELD_DIRS = ["s", "sw", "w", "nw", "n"]  # facing slots 0..4: down, down-left, left, up-left, up
# set id, name, source animation, frame times in ticks, loops
FIELD_SETS = [(1, "STAND", "stand", [14] * 4, True), (4, "WALK", "walk", [6] * 8, True),
              (5, "RUN", "walk", [4] * 8, True), (6, "STOP", "stop", [4, 10, 10], False)]


def build_field(frames, pivot, palette, out):
    entries, pngs, animations, sets = [], [], [], []
    next_frame = 0
    frame_ids = {}  # (animation, slot, i) -> (id, x, y)
    for anim in ("stand", "walk", "stop"):
        for slot, d in enumerate(FIELD_DIRS):
            i = 0
            while "%s_%s_%d" % (anim, d, i) in frames:
                crop, x0, y0 = trim(frames["%s_%s_%d" % (anim, d, i)])
                frame_ids[(anim, slot, i)] = (next_frame, x0 - pivot[0], y0 - pivot[1])
                pngs.append(("%03d.png" % next_frame, palette.quantize(crop)))
                entries.append({"id": next_frame, "image": "%03d.png" % next_frame})
                next_frame += 1
                i += 1
    next_anim = next_frame
    for set_id, name, anim, times, loop in FIELD_SETS:
        ids = []
        for slot in range(5):
            steps = []
            for i, t in enumerate(times):
                fid, x, y = frame_ids[(anim, slot, i)]
                steps.append({"frame": fid, "time": t, "x": x, "y": y})
            a = {"id": next_anim, "loop": loop, "steps": steps}
            animations.append(a)
            ids.append(next_anim)
            next_anim += 1
        sets.append({"id": set_id, "name": name, "animations": ids})
    for name, idx in pngs:
        write_png(os.path.join(out, "field", name), idx, palette)
    write_json(os.path.join(out, "field", "sprite.json"),
               {"format": "sprite-v1", "frames": entries, "animations": animations, "sets": sets})


# ---------------------------------------------------------------------------------------------- the river sprite
ANIM_BASE = 100  # the animation ids start here, after the 40 frame ids
RIVER_TICKS = 7  # per frame of the idle loop (8 frames, 120 ms each)


def build_river(frames, pivot, palette, out):
    """The stage sprite of the river MegaSeadramon: the idle loop in five facings (docs/stages.md section 11)."""
    entries, animations, ids = [], [], []
    for slot, d in enumerate(FIELD_DIRS):
        steps = []
        i = 0
        while "idle_%s_%d" % (d, i) in frames:
            crop, x0, y0 = trim(frames["idle_%s_%d" % (d, i)])
            fid = len(entries)
            write_png(os.path.join(out, "%03d.png" % fid), palette.quantize(crop), palette)
            entries.append({"id": fid, "image": "%03d.png" % fid, "w": int(crop.shape[1]), "h": int(crop.shape[0])})
            steps.append({"frame": fid, "time": RIVER_TICKS, "x": x0 - pivot[0], "y": y0 - pivot[1]})
            i += 1
        animations.append({"id": ANIM_BASE + slot, "loop": True, "steps": steps})  # frame and animation ids share one list
        ids.append(ANIM_BASE + slot)
    write_json(os.path.join(out, "sprite.json"), {
        "format": "sprite-v1", "source": "original (tools/make_art.py, tools/specs/megaseadramon.yaml)", "set_table": -1,
        "frames": entries, "animations": animations,
        "sets": [{"id": n, "animations": list(ids)} for n in (1, 4, 5, 6, 7)],
        "directions": [{"dir": 0, "slot": 0, "flip": False}, {"dir": 1, "slot": 1, "flip": False},
                       {"dir": 2, "slot": 2, "flip": False}, {"dir": 3, "slot": 3, "flip": False},
                       {"dir": 4, "slot": 4, "flip": False}, {"dir": 5, "slot": 3, "flip": True},
                       {"dir": 6, "slot": 2, "flip": True}, {"dir": 7, "slot": 1, "flip": True}],
        "actor_width": 40,
        "notes": "MegaSeadramon on the river bank: the idle loop (tools/specs/megaseadramon.yaml); nothing comes from the game disc."})


# ---------------------------------------------------------------------------------------------- portrait and icon
def build_portrait(frames, palette, out):
    cells = [trim(frames["cell%d_s_0" % i]) for i in range(4)]
    left = min(x0 for _, x0, _ in cells)
    right = max(x0 + c.shape[1] for c, x0, _ in cells)
    top = min(y0 for _, _, y0 in cells)
    wx = (left + right) // 2 - 16  # the window: centred on the head, the crest tip on row 1
    wy = top - 1
    if right - left > 32:
        sys.exit("the portrait is %d px wide: raise camera.scale in betamon_portrait.yaml" % (right - left))
    strip = np.zeros((32, 32 * len(cells), 4), dtype=np.uint8)
    for n in range(len(cells)):
        full = frames["cell%d_s_0" % n]
        win = full[wy:wy + 32, wx:wx + 32]
        strip[:win.shape[0], n * 32:n * 32 + win.shape[1]] = win
    write_png(os.path.join(out, "portrait.png"), palette.quantize(strip), palette)


def build_icon(frames, palette, out):
    crop, _, _ = trim(frames["icon_e_0"])
    h, w = crop.shape[:2]
    if w > 44 or h > 28:
        sys.exit("the icon is %dx%d: raise camera.scale in betamon_icon.yaml" % (w, h))
    canvas = np.zeros((28, 44, 4), dtype=np.uint8)
    ox, oy = (44 - w) // 2, 28 - h - (28 - h) // 3
    canvas[oy:oy + h, ox:ox + w] = crop
    write_png(os.path.join(out, "icon.png"), palette.quantize(canvas), palette)


# ---------------------------------------------------------------------------------------------- the gym
# Each training: where the feet stand in the scene (x, y), then the four animations the game asks for by number:
# 0 the start, 1 the exercise, 2 it worked, 3 it did not. A step is (pose, ticks, dx, dy).
# The shared effects (the bag, the flames, the ice...) play on their own timing; the lengths are about the originals'.
START = [("stand", 16), ("stand2", 16), ("crouch", 12), ("stand", 20), ("stand2", 22)]
WIN = [("cheer", 12), ("jump", 10), ("cheer", 12), ("jump", 10), ("cheer", 60)]
LOSE = [("stand", 12), ("hunch", 20), ("slump", 20), ("slump", 60)]
HOP = [("crouch", 8), ("jump", 10), ("stand", 6)]
SHIVER = [("hunch", 6, -1, 0), ("hunch", 6, 1, 0)]

GYM = {
    "punch": ((118, 128), [("windup", 20), ("strike", 12), ("windup", 16), ("strike", 12), ("windup", 12), ("strike", 16)]),
    "defense": ((86, 128), [("brace", 20), ("crouch", 16), ("brace", 20), ("crouch", 18)]),
    "yoga": ((129, 131), [("crouch", 16), ("sit", 30), ("sit", 30), ("crouch", 16)]),
    "study": ((124, 130), [("lookdown", 32), ("stand2", 16), ("lookdown", 32), ("stand", 16), ("lookdown", 32)]),
    "jump-rope": ((128, 135), HOP * 5),
    "heat-tolerance": ((128, 130), [("pant", 16), ("hunch", 16)] * 3),
    "water-tolerance": ((124, 130), [("stand", 16), ("brace", 20), ("hunch", 20), ("brace", 20)]),
    "ice-tolerance": ((124, 130), SHIVER * 8),
    "wind-tolerance": ((92, 128), [("blown", 24), ("brace", 24)] * 3),
    "thunder-defense": ((126, 129), [("lookup", 24), ("scared", 20), ("brace", 20), ("scared", 20), ("brace", 20)]),
    "missile-defense": ((128, 130), [("lookup", 16), ("jump", 12), ("crouch", 12)] * 3),
    "haunting": ((96, 128), [("scared", 20), ("lookup", 16), ("scared", 20), ("crouch", 16), ("scared", 20), ("lookup", 16)]),
}


def poses_used():
    """The poses the 12 scenes ask for."""
    for _, (_, loop) in GYM.items():
        for step in START + loop + WIN + LOSE:
            yield step[0]


def build_gym(poses, pivot, palette, out):
    cache = {}
    for name in sorted(GYM):
        (fx, fy), loop = GYM[name]
        script = [START, loop, WIN, LOSE]
        frame_ids, frames, animations = {}, [], []
        for aid, steps in enumerate(script):
            out_steps = []
            for s in steps:
                pose, ticks = s[0], s[1]
                dx, dy = (s[2], s[3]) if len(s) > 2 else (0, 0)
                if pose not in frame_ids:
                    if pose not in cache:
                        cache[pose] = trim(poses["%s_e_0" % pose])
                    crop, x0, y0 = cache[pose]
                    fid = len(frame_ids)
                    frame_ids[pose] = fid
                    frames.append({"id": fid, "image": "%03d.png" % fid, "origin": [fx + x0 - pivot[0], fy + y0 - pivot[1]]})
                    write_png(os.path.join(out, "training", name, "%03d.png" % fid), palette.quantize(crop), palette)
                out_steps.append({"frame": frame_ids[pose], "time": ticks, "x": dx, "y": dy})
            animations.append({"id": aid, "steps": out_steps})
        write_json(os.path.join(out, "training", name, "training.json"),
                   {"format": "partners-training-v1", "name": name, "frames": frames, "animations": animations})


# ---------------------------------------------------------------------------------------------- main
def merged(*groups):
    return [a for g in groups for k in sorted(g) for a in [g[k]]]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=DEFAULT_OUT, help="the partner folder to write (default: ../partners/betamon)")
    ap.add_argument("--river-out", default=DEFAULT_RIVER, help="the river sprite folder to write (default: ../sprites/river-digimon)")
    args = ap.parse_args()
    ver = subprocess.run([sys.executable, "-m", "spritekiln", "--version"], capture_output=True, text=True)
    if ver.returncode != 0 or "0.3.0" not in ver.stdout:
        sys.exit("SpriteKiln 0.3.0 is needed (see the header of this file); run it with the venv's python")
    work = tempfile.mkdtemp(prefix="betamon-art-")
    try:
        field, pf = render("betamon_field.yaml", work, ["stand", "walk", "stop"])
        train, pt = render("betamon_train.yaml", work, sorted({p for p in poses_used()}))
        portrait, _ = render("betamon_portrait.yaml", work, ["cell0", "cell1", "cell2", "cell3"])
        icon, _ = render("betamon_icon.yaml", work, ["icon"])
        river, pr = render("megaseadramon_river.yaml", work, ["idle"])
        pals = {name: Palette([trim(a)[0] for a in merged(group)])
                for name, group in (("field", field), ("portrait", portrait), ("icon", icon), ("gym", train), ("river", river))}
        for stale in ("field", "training"):  # only what this script writes (partner.json and other files stay)
            shutil.rmtree(os.path.join(args.out, stale), ignore_errors=True)
        build_field(field, pf, pals["field"], args.out)
        build_portrait(portrait, pals["portrait"], args.out)
        build_icon(icon, pals["icon"], args.out)
        build_gym(train, pt, pals["gym"], args.out)
        for stale in os.listdir(args.river_out) if os.path.isdir(args.river_out) else []:
            os.remove(os.path.join(args.river_out, stale))
        build_river(river, pr, pals["river"], args.river_out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("wrote %s and %s (colours: %s)" % (args.out, args.river_out,
                                             ", ".join("%s %d" % (k, len(v.colours)) for k, v in sorted(pals.items()))))


if __name__ == "__main__":
    main()
