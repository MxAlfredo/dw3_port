#!/usr/bin/env python3
"""Draw the pieces of an exported menu screen or stage-object sheet over their pages (menus-menu-v1, stages-objects-v1),
and the frames and animations of an exported sprite (sprites-sprite-v1 or sprite-v1).

The port writes one PNG per piece plus menu.json (data/menus/<screen>/) or objects.json
(data/stages/s<nnn>/objects/): where each piece sits in the screen's pages (u, v, w, h) and where the game draws it.
This tool reads the JSON and its PNGs, places every piece at its rectangle on a canvas the size of the pages and
outlines it with a number, so you can see what each region of the art is. Pieces in 4-bit and 8-bit mode have their
own canvas (the pages are 256 texels wide in 4 bits, 128 in 8 bits); the canvases are stacked in one preview.

    python3 tools/image_atlas_preview.py <data>/menus/stshop                 # the folder, its menu.json or any piece PNG
    python3 tools/image_atlas_preview.py <data>/stages/s202/objects --out /tmp/preview.png --scale 3

Sprites: pass the folder, its sprite.json or any frame PNG of data/sprites/<name>/ (or a partner's field/ folder):

    python3 tools/image_atlas_preview.py <data>/sprites/p001play              # every frame, numbered by id
    python3 tools/image_atlas_preview.py <data>/sprites/p001play --anim 146   # animation 146 as a strip
    python3 tools/image_atlas_preview.py <data>/sprites/p001play --set 4 --dir 6 --gif   # walk set, facing 6 (mirrored), as a GIF

--anim takes an animation id of the file; --set and --dir pick it the way the game does (the set's slot for that
facing, mirrored for the facings 5 to 7). A strip shows each step side by side on the actor's position (the cross);
--gif writes an animated GIF instead (a tick lasts --tick-ms, default 1000/60: that unit is an assumption).
--scale zooms; the legend of the steps is printed.

Without --out the preview goes to the system temp folder (never into data/); the path is printed. The numbers
are the pieces' order in the JSON (0-based); the legend is printed on stdout. Pieces flagged "outside" are
drawn in red, pieces that could not be cut (file null) in cyan, the others in yellow. Needs Pillow
(python3 -m pip install pillow). Nothing from the game is stored here.
"""
import argparse
import json
import os
import sys
import tempfile

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("image_atlas_preview: Pillow is required (python3 -m pip install pillow)")


def default_out(path, tag, ext="png"):
    parts = [x for x in os.path.normpath(path).split(os.sep) if x][-3:]
    name = "_".join(parts).replace(".png", "").replace(".json", "")
    return os.path.join(tempfile.gettempdir(), f"{tag}_{name}.{ext}")


SPRITE_FORMATS = ("sprites-sprite-v1", "sprite-v1")
PIECE_FORMATS = ("menus-menu-v1", "stages-objects-v1")
# the facings of a sprite that has no "directions" member (a mod's own sprite.json): the engine's fixed table
DEFAULT_DIRECTIONS = [{"dir": d, "slot": sl, "flip": fl} for d, (sl, fl) in enumerate([(0, False), (1, False), (2, False), (3, False), (4, False), (3, True), (2, True), (1, True)])]
DOC_NAMES = ("sprite.json", "menu.json", "objects.json", "objects/objects.json")


def find_doc(arg):
    """(path, document) of the sprite.json, menu.json or objects.json an argument points at (a folder, the file, or a PNG next to it), or None."""
    if os.path.isfile(arg) and arg.endswith(".json"):
        cand = [arg]
    else:
        base = arg if os.path.isdir(arg) else os.path.dirname(arg) or "."
        cand = [os.path.join(base, n) for n in DOC_NAMES]
    for c in cand:
        if os.path.isfile(c):
            try:
                doc = json.load(open(c, encoding="utf-8"))
            except (OSError, ValueError):
                continue
            if doc.get("format") in SPRITE_FORMATS + PIECE_FORMATS:
                return c, doc
    return None


def load_frames(folder, sprite):
    imgs = {}
    for f in sprite["frames"]:
        try:
            imgs[f["id"]] = Image.open(os.path.join(folder, f["image"])).convert("RGBA")
        except OSError as e:
            print(f"warning: frame {f['id']}: {e}", file=sys.stderr)
    return imgs


def sprite_sheet(folder, sprite, sc, out):
    """Every frame in a grid, numbered by id (frames no animation uses in red)."""
    imgs = load_frames(folder, sprite)
    cells = [(f, imgs[f["id"]]) for f in sprite["frames"] if f["id"] in imgs]
    if not cells:
        sys.exit("image_atlas_preview: no frames to draw")
    pad, maxw = 14, 1100
    x = y = rowh = 0
    place = []
    for f, im in cells:
        w, h = im.width * sc, im.height * sc + 10
        if x and x + w + pad > maxw:
            x, y, rowh = 0, y + rowh + pad, 0
        place.append((f, im, x, y))
        x += w + pad
        rowh = max(rowh, h)
    sheet = Image.new("RGBA", (maxw, y + rowh + pad), (96, 96, 96, 255))
    d = ImageDraw.Draw(sheet)
    for f, im, px, py in place:
        big = im.resize((im.width * sc, im.height * sc), Image.NEAREST)
        sheet.alpha_composite(big, (px, py + 10))
        col = (255, 60, 60, 255) if f.get("used") is False else (255, 230, 0, 255)
        d.rectangle([px, py + 10, px + big.width - 1, py + 10 + big.height - 1], outline=col)
        d.text((px, py), str(f["id"]), fill=col)
    sheet.convert("RGB").save(out)
    print(f"preview: {out} ({len(cells)} frames, {sum(1 for f in sprite['frames'] if f.get('used') is False)} unused in red)")


def pick_animation(sprite, a):
    anims = {x["id"]: x for x in sprite["animations"]}
    flip = False
    if a.anim is not None:
        aid = a.anim
        if a.flip:
            flip = True
    else:
        sets = {x["id"]: x for x in sprite["sets"]}
        if a.set not in sets:
            sys.exit(f"image_atlas_preview: no set {a.set}; sets: {sorted(sets)}")
        dirs = {x["dir"]: x for x in sprite.get("directions") or DEFAULT_DIRECTIONS}
        if a.dir not in dirs:
            sys.exit("image_atlas_preview: --dir is 0 to 7")
        aid = sets[a.set]["animations"][dirs[a.dir]["slot"]]
        flip = dirs[a.dir]["flip"]
    if aid not in anims:
        sys.exit(f"image_atlas_preview: no animation {aid}")
    return anims[aid], flip


def render_animation(folder, sprite, a, sc, out):
    anim, flip = pick_animation(sprite, a)
    imgs = load_frames(folder, sprite)
    sizes = {f["id"]: (f["w"], f["h"]) if "w" in f else imgs[f["id"]].size for f in sprite["frames"] if "w" in f or f["id"] in imgs}
    boxes = []
    for st in anim["steps"]:
        w, h = sizes.get(st["frame"], (0, 0))
        x = -(w + st["x"]) if flip else st["x"]
        boxes.append((x, st["y"], w, h))
    if not boxes:
        sys.exit("image_atlas_preview: the animation has no steps")
    x0 = min(0, min(b[0] for b in boxes)) - 2
    y0 = min(0, min(b[1] for b in boxes)) - 2
    x1 = max(1, max(b[0] + b[2] for b in boxes)) + 2
    y1 = max(1, max(b[1] + b[3] for b in boxes)) + 2
    cw, ch = (x1 - x0) * sc, (y1 - y0) * sc
    tiles = []
    for st, (bx, by, bw, bh) in zip(anim["steps"], boxes):
        t = Image.new("RGBA", (cw, ch), (96, 96, 96, 255))
        im = imgs.get(st["frame"])
        if im is not None:
            if flip:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            t.alpha_composite(im.resize((im.width * sc, im.height * sc), Image.NEAREST), ((bx - x0) * sc, (by - y0) * sc))
        d = ImageDraw.Draw(t)
        cx, cy = -x0 * sc, -y0 * sc
        d.line([cx - 4, cy, cx + 4, cy], fill=(255, 60, 60, 255))
        d.line([cx, cy - 4, cx, cy + 4], fill=(255, 60, 60, 255))
        tiles.append(t)
    what = f"animation {anim['id']}" + (" mirrored" if flip else "") + (" (loops)" if anim.get("loop", True) else " (plays once)")
    for n, st in enumerate(anim["steps"]):
        print(f"{n:3d}  frame {st['frame']:>3}  time {st['time']:>3}  x={st['x']} y={st['y']}")
    if a.gif:
        ms = [max(20, int(round(st["time"] * a.tick_ms))) for st in anim["steps"]]
        tiles[0].convert("RGB").save(out, save_all=True, append_images=[t.convert("RGB") for t in tiles[1:]], duration=ms, loop=0 if anim.get("loop", True) else 1)
        print(f"preview: {out} ({what}, {len(tiles)} steps, {sum(ms)} ms)")
    else:
        strip = Image.new("RGBA", (cw * len(tiles) + 2 * (len(tiles) - 1), ch + 12), (40, 40, 40, 255))
        d = ImageDraw.Draw(strip)
        for n, t in enumerate(tiles):
            strip.alpha_composite(t, (n * (cw + 2), 12))
            d.text((n * (cw + 2) + 2, 0), f"{n}: f{anim['steps'][n]['frame']} t{anim['steps'][n]['time']}", fill=(255, 230, 0, 255))
        strip.convert("RGB").save(out)
        print(f"preview: {out} ({what}, {len(tiles)} steps)")


def render_pieces(folder, doc, sc, out):
    """Each piece at its rectangle on a canvas the size of the pages (one canvas per pixel mode, stacked)."""
    pieces = doc["pieces"]
    pages = [p for p in doc.get("pages", []) if p]
    texels = {"4bit": 4, "8bit": 2}  # texels per halfword
    canvases = []
    for mode in ("4bit", "8bit"):
        used = [p for p in pieces if p["mode"] == mode]
        if not used:
            continue
        wpx = sum(pg[0] for pg in pages) * texels[mode]
        hpx = max([pg[1] for pg in pages] + [0])
        wpx = max(wpx, max(p["u"] + p["w"] for p in used))
        hpx = max(hpx, max(p["v"] + p["h"] for p in used))
        canvases.append((mode, used, max(1, wpx), max(1, hpx)))
    if not canvases:
        sys.exit("image_atlas_preview: the file lists no pieces")
    label, gap = 12, 6
    width = max(c[2] for c in canvases) * sc
    height = sum(c[3] * sc + label + gap for c in canvases)
    sheet = Image.new("RGBA", (width, height), (40, 40, 40, 255))
    d = ImageDraw.Draw(sheet)
    y = 0
    cut = 0
    for mode, used, cw, ch in canvases:
        d.text((2, y), f"{mode}: {len(used)} pieces, {cw}x{ch} texels", fill=(255, 255, 255, 255))
        y += label
        d.rectangle([0, y, cw * sc - 1, y + ch * sc - 1], fill=(96, 96, 96, 255))
        for p in used:
            n = pieces.index(p)
            x0, y0 = p["u"] * sc, y + p["v"] * sc
            if p.get("file"):
                try:
                    im = Image.open(os.path.join(folder, p["file"])).convert("RGBA")
                    sheet.alpha_composite(im.resize((im.width * sc, im.height * sc), Image.NEAREST), (x0, y0))
                    cut += 1
                except OSError as e:
                    print(f"warning: piece {n}: {e}", file=sys.stderr)
            col = (255, 60, 60, 255) if p.get("outside") else (80, 255, 255, 255) if not p.get("file") else (255, 230, 0, 255)
            d.rectangle([x0, y0, x0 + p["w"] * sc - 1, y0 + p["h"] * sc - 1], outline=col)
            d.text((x0 + 2, y0 + 1), str(n), fill=col)
            where = ", ".join(f"f{f['frame']}p{f['slot']}" for f in p["frames"][:4]) + (" ..." if len(p["frames"]) > 4 else "")
            print(f"{n:3d}  {p.get('file') or 'no image':>12}  {mode} u={p['u']} v={p['v']} {p['w']}x{p['h']}  clut x={p['clut']['x']} row={p['clut']['row']}  frames {where}"
                  + ("  outside" if p.get("outside") else "") + (f"  ({p['unexported']})" if p.get("unexported") else ""))
        y += ch * sc + gap
    sheet.convert("RGB").save(out)
    print(f"preview: {out} ({len(pieces)} pieces, {cut} drawn)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("path", help="a menu or stage-object folder (menu.json / objects.json), or a sprite folder (sprite.json), the JSON itself or a PNG next to it")
    ap.add_argument("--out", help="preview PNG (or GIF) to write (default: the system temp folder)")
    ap.add_argument("--scale", type=int, default=2, help="integer zoom of the preview (default 2)")
    ap.add_argument("--anim", type=int, help="sprite: draw this animation id as a strip of its steps")
    ap.add_argument("--set", type=int, help="sprite: the animation set (with --dir)")
    ap.add_argument("--dir", type=int, default=0, help="sprite: the facing 0 to 7 (default 0; 5 to 7 are mirrored)")
    ap.add_argument("--flip", action="store_true", help="sprite: mirror --anim as the facings 5 to 7 do")
    ap.add_argument("--gif", action="store_true", help="sprite: write an animated GIF instead of a strip")
    ap.add_argument("--tick-ms", type=float, default=1000 / 60, help="sprite: milliseconds of one time tick (default 1000/60, an assumption)")
    a = ap.parse_args()

    found = find_doc(a.path)
    if not found:
        sys.exit(f"image_atlas_preview: no sprite.json, menu.json or objects.json (sprites-sprite-v1, menus-menu-v1, stages-objects-v1) at {a.path}")
    path, doc = found
    folder = os.path.dirname(path)
    sc = max(1, a.scale)
    if doc["format"] in PIECE_FORMATS:
        render_pieces(folder, doc, sc, a.out or default_out(path, "pieces"))
    elif a.anim is not None or a.set is not None:
        render_animation(folder, doc, a, sc, a.out or default_out(path, "anim", "gif" if a.gif else "png"))
    else:
        sprite_sheet(folder, doc, sc, a.out or default_out(path, "sprite"))


if __name__ == "__main__":
    main()
