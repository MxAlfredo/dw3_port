#!/usr/bin/env python3
"""Draw the piece map (atlas-v1) of an extracted menu art PNG over the image (feature menu-art-png, T5), and
the frames and animations (sprite-v1) of an extracted field sprite (feature field-sprites-png, T2).

The port writes <NN>_<MM>.json next to every data/images/<dir>/<name>/<NN>_<MM>.png: the rectangles of that
image that the game's sprite sheet uses. This tool reads the PNG and its JSON and draws every piece as a
numbered rectangle into a preview image, so you can see what each region of the art is.

    python3 tools/image_atlas_preview.py <data>/images/screen/stshoptm/00_00.png
    python3 tools/image_atlas_preview.py <png> --out /tmp/preview.png --scale 3

Field sprites: pass the folder, its sprite.json or any frame PNG of data/images/field/sprt/<name>/:

    python3 tools/image_atlas_preview.py <data>/images/field/sprt/p001play            # every frame, numbered by id
    python3 tools/image_atlas_preview.py <...>/P001PLAY --anim 146                    # animation 146 as a strip
    python3 tools/image_atlas_preview.py <...>/P001PLAY --set 4 --dir 6 --gif         # walk set, facing 6 (mirrored), as a GIF

--anim takes an animation id of the file; --set and --dir pick it the way the game does (the set's slot for that
facing, mirrored for the facings 5 to 7). A strip shows each step side by side on the actor's position (the cross);
--gif writes an animated GIF instead (a tick lasts --tick-ms, default 1000/60: that unit is an assumption).
--scale zooms; the legend of the steps is printed.

Without --out the preview goes to the system temp folder (never into data/); the path is printed. The numbers
are the pieces' order in the JSON (0-based); the legend is printed on stdout. Pieces flagged "outside" are
drawn in red, the others in yellow, and pieces the game reads in the other texture
mode ("read_as") in cyan. Needs Pillow (python3 -m pip install pillow). Nothing from the game is stored here.
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


def sprite_json_path(arg):
    """The sprite.json an argument points at (a folder, the file, or a frame PNG next to it), or None."""
    cand = [arg] if os.path.isfile(arg) and arg.endswith(".json") else [os.path.join(arg if os.path.isdir(arg) else os.path.dirname(arg) or ".", "sprite.json")]
    for c in cand:
        if os.path.isfile(c):
            try:
                if json.load(open(c, encoding="utf-8")).get("format") == "sprite-v1":
                    return c
            except (OSError, ValueError):
                pass
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
        dirs = {x["dir"]: x for x in sprite["directions"]}
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
    sizes = {f["id"]: (f["w"], f["h"]) for f in sprite["frames"]}
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
    what = f"animation {anim['id']}" + (" mirrored" if flip else "") + (" (loops)" if anim["loop"] else " (plays once)")
    for n, st in enumerate(anim["steps"]):
        print(f"{n:3d}  frame {st['frame']:>3}  time {st['time']:>3}  x={st['x']} y={st['y']}")
    if a.gif:
        ms = [max(20, int(round(st["time"] * a.tick_ms))) for st in anim["steps"]]
        tiles[0].convert("RGB").save(out, save_all=True, append_images=[t.convert("RGB") for t in tiles[1:]], duration=ms, loop=0 if anim["loop"] else 1)
        print(f"preview: {out} ({what}, {len(tiles)} steps, {sum(ms)} ms)")
    else:
        strip = Image.new("RGBA", (cw * len(tiles) + 2 * (len(tiles) - 1), ch + 12), (40, 40, 40, 255))
        d = ImageDraw.Draw(strip)
        for n, t in enumerate(tiles):
            strip.alpha_composite(t, (n * (cw + 2), 12))
            d.text((n * (cw + 2) + 2, 0), f"{n}: f{anim['steps'][n]['frame']} t{anim['steps'][n]['time']}", fill=(255, 230, 0, 255))
        strip.convert("RGB").save(out)
        print(f"preview: {out} ({what}, {len(tiles)} steps)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("png", help="an extracted PNG (its .json is read from the same folder), or a field sprite folder / sprite.json")
    ap.add_argument("--out", help="preview PNG (or GIF) to write (default: the system temp folder)")
    ap.add_argument("--scale", type=int, default=2, help="integer zoom of the preview (default 2)")
    ap.add_argument("--anim", type=int, help="field sprite: draw this animation id as a strip of its steps")
    ap.add_argument("--set", type=int, help="field sprite: the animation set (with --dir)")
    ap.add_argument("--dir", type=int, default=0, help="field sprite: the facing 0 to 7 (default 0; 5 to 7 are mirrored)")
    ap.add_argument("--flip", action="store_true", help="field sprite: mirror --anim as the facings 5 to 7 do")
    ap.add_argument("--gif", action="store_true", help="field sprite: write an animated GIF instead of a strip")
    ap.add_argument("--tick-ms", type=float, default=1000 / 60, help="field sprite: milliseconds of one time tick (default 1000/60, an assumption)")
    a = ap.parse_args()

    sj = sprite_json_path(a.png)
    if sj:
        sprite = json.load(open(sj, encoding="utf-8"))
        folder = os.path.dirname(sj)
        sc = max(1, a.scale)
        if a.anim is not None or a.set is not None:
            render_animation(folder, sprite, a, sc, a.out or default_out(sj, "anim", "gif" if a.gif else "png"))
        else:
            sprite_sheet(folder, sprite, sc, a.out or default_out(sj, "sprite"))
        return
    jpath = os.path.splitext(a.png)[0] + ".json"
    try:
        atlas = json.load(open(jpath, encoding="utf-8"))
    except (OSError, ValueError) as e:
        sys.exit(f"image_atlas_preview: cannot read {jpath}: {e}")
    if atlas.get("format") != "atlas-v1":
        sys.exit(f"image_atlas_preview: {jpath} is not atlas-v1")
    img = Image.open(a.png).convert("RGBA")
    if (img.width, img.height) != (atlas["width"], atlas["height"]):
        print(f"warning: the PNG is {img.width}x{img.height} but the map says {atlas['width']}x{atlas['height']}", file=sys.stderr)
    sc = max(1, a.scale)
    bg = Image.new("RGBA", img.size, (96, 96, 96, 255))  # index 0 is transparent: show it as grey
    bg.alpha_composite(img)
    view = bg.resize((img.width * sc, img.height * sc), Image.NEAREST)
    d = ImageDraw.Draw(view)
    for n, p in enumerate(atlas["pieces"]):
        col = (255, 60, 60, 255) if p.get("outside") else (80, 255, 255, 255) if "read_as" in p else (255, 230, 0, 255)
        x0, y0 = p["x"] * sc, p["y"] * sc
        x1, y1 = (p["x"] + p["w"]) * sc - 1, (p["y"] + p["h"]) * sc - 1
        d.rectangle([x0, y0, x1, y1], outline=col)
        d.text((x0 + 2, y0 + 1), str(n), fill=col)
        print(f"{n:3d}  {p['id']:>8}  x={p['x']} y={p['y']} {p['w']}x{p['h']}  palette {p['palette']}  frames {p['frames']}"
              + ("  outside" if p.get("outside") else "") + (f"  read_as {p['read_as']}" if "read_as" in p else ""))
    out = a.out
    if not out:
        tag = "_".join(os.path.normpath(a.png).split(os.sep)[-3:]).replace(".png", "")
        out = os.path.join(tempfile.gettempdir(), f"atlas_{tag}.png")
    view.convert("RGB").save(out)
    print(f"preview: {out} ({len(atlas['pieces'])} pieces)")


if __name__ == "__main__":
    main()
