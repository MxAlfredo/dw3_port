#!/usr/bin/env python3
"""Recolour the palette of exported PNGs to make an art mod (features menu-art-png T4, field-sprites-png T4, readable-assets A8.6).

The port exports the game's art to <data> from your own disc, one topic folder each (ADR-016): menu pieces in
<data>/menus/<screen>/<piece>.png, sprite frames in <data>/sprites/<name>/<NNN>.png, card faces in <data>/cards/<name>/face.png,
partner art in <data>/partners/<name>/ and the few pages still under <data>/images/.
This script copies some of those PNGs into a mod folder at the SAME relative path, changing ONLY the palette (the PLTE chunk):
every colour whose hue is in the source range gets the target hue, keeping its saturation and
brightness. The pixel data is copied byte for byte, so the size, the indices and the colour count
stay exactly what the port requires. Nothing from the game is ever stored in this repository: the
mod's PNGs are made on your machine from your own copy.

    python3 tools/image_mod_palette.py                        # blue -> gold on the shop menu pieces (menus/stshop)
    python3 tools/image_mod_palette.py --to 120 menus/stshop/f000p00.png   # one piece, blue -> green
    python3 tools/image_mod_palette.py --list                 # the images you can recolour
    python3 tools/image_mod_palette.py --sprite p001play --to 0   # every frame of a sprite (sprites/p001play): blue -> red
    python3 tools/image_mod_palette.py cards/c001 partners/kotemon/portrait.png   # a folder: every PNG in it

Names are paths under <data> (any case; the port writes lower case): a topic folder (menus, sprites, cards, partners, images)
and what is inside it. A folder takes every PNG below it. Stage tiles and objects (stages/s<nnn>/tiles, .../objects) work the
same way. The result goes to <mod>/<the same path>, which is where the game looks for a mod's replacement.

Defaults: --data <pref>/data (DW3_DATA_DIR), --mod <pref>/mods/menu-frames-gold (DW3_MODS_DIR; with --sprite,
<pref>/mods/player-recolor), where
<pref> is the port's preference folder (Linux ~/.local/share/dw3port/dw3, Windows %APPDATA%\\dw3port\\dw3).
"""
import argparse
import colorsys
import os
import struct
import sys
import zlib

DEFAULT_IMAGES = ["menus/stshop"]
LIST_TOPICS = ("menus", "sprites", "cards", "partners", "images")  # --list skips stages/ (thousands of tiles) and digimon/ (models)
SIG = b"\x89PNG\r\n\x1a\n"


def pref_dir():
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "dw3port", "dw3")


def chunks(data):
    if data[:8] != SIG:
        raise ValueError("not a PNG file")
    at = 8
    while at + 12 <= len(data):
        n = struct.unpack(">I", data[at:at + 4])[0]
        typ = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + n]
        yield typ, body
        at += 12 + n


def chunk(typ, body):
    crc = zlib.crc32(typ + body) & 0xFFFFFFFF
    return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", crc)


def recolour(rgb, alpha, src_lo, src_hi, target, min_sat):
    """The entry with its hue moved to `target` when it is a saturated colour in [src_lo, src_hi]."""
    if alpha == 0:
        return rgb  # transparent entries stay as they are
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in rgb))
    hue = h * 360
    if s < min_sat or not (src_lo <= hue <= src_hi):
        return rgb
    return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(target / 360, s, v))


def recolour_png(data, src_lo, src_hi, target, min_sat):
    out = [SIG]
    ihdr = None
    trns = b""
    for typ, body in chunks(data):
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"tRNS":
            trns = body
    if ihdr is None or ihdr[3] != 3:
        raise ValueError("not an indexed (palette) PNG")
    changed = 0
    for typ, body in chunks(data):
        if typ == b"PLTE":
            ent = []
            for i in range(0, len(body), 3):
                rgb = tuple(body[i:i + 3])
                alpha = trns[i // 3] if i // 3 < len(trns) else 255
                new = recolour(rgb, alpha, src_lo, src_hi, target, min_sat)
                changed += new != rgb
                ent.append(bytes(new))
            body = b"".join(ent)
        out.append(chunk(typ, body))
    return b"".join(out), changed


def resolve_ci(root, name):
    """name under root matched case-insensitively, component by component (ADR-010); lower-cased if absent."""
    cur, out = root, []
    for part in name.replace("\\", "/").split("/"):
        hit = None
        try:
            hit = next((n for n in os.listdir(cur) if n.lower() == part.lower()), None)
        except OSError:
            pass
        part = hit or part.lower()
        out.append(part)
        cur = os.path.join(cur, part)
    return "/".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("images", nargs="*", help="paths under <data> (any case; the port writes lower case), e.g. menus/stshop/f000p00.png, or a folder of them")
    ap.add_argument("--sprite", action="append", default=[], metavar="NAME",
                    help="a sprite, e.g. p001play: every frame PNG of sprites/NAME (repeatable)")
    ap.add_argument("--data", default=os.environ.get("DW3_DATA_DIR") or os.path.join(pref_dir(), "data"))
    mods = os.environ.get("DW3_MODS_DIR") or os.path.join(pref_dir(), "mods")
    ap.add_argument("--mod", default=None, help="the mod folder to write into (default: menu-frames-gold, or player-recolor with --sprite)")
    ap.add_argument("--from-hue", type=float, nargs=2, default=(190, 270), metavar=("LO", "HI"),
                    help="source hue range in degrees (default 190 270, the blues)")
    ap.add_argument("--to", type=float, default=45, help="target hue in degrees (default 45, gold)")
    ap.add_argument("--min-saturation", type=float, default=0.2, help="greys below this stay (default 0.2)")
    ap.add_argument("--list", action="store_true", help="list the extracted images and exit")
    args = ap.parse_args(argv)

    root = args.data
    if args.list:
        for topic in LIST_TOPICS:
            for dirpath, _, names in sorted(os.walk(os.path.join(root, topic))):
                for n in sorted(names):
                    if n.endswith(".png"):
                        print(os.path.relpath(os.path.join(dirpath, n), root).replace(os.sep, "/"))
        return 0
    if args.mod is None:
        args.mod = os.path.join(mods, "player-recolor" if args.sprite else "menu-frames-gold")
    names = []
    wanted = list(args.images) + ["sprites/" + n for n in args.sprite]
    for name in wanted or DEFAULT_IMAGES:
        name = resolve_ci(root, name)
        top = os.path.join(root, name)
        if os.path.isdir(top):  # a folder: every PNG below it
            found = [os.path.relpath(os.path.join(d, f), root) for d, _, fs in sorted(os.walk(top)) for f in sorted(fs) if f.endswith(".png")]
            if not found:
                print(f"{name}: no PNG in it (run the game once so it extracts {root})", file=sys.stderr)
            names += found
        else:
            names.append(name)
    rc = 0 if names else 1
    done = total = 0
    for name in names:
        src = os.path.join(root, name)
        try:
            with open(src, "rb") as f:
                new, changed = recolour_png(f.read(), args.from_hue[0], args.from_hue[1], args.to, args.min_saturation)
        except (OSError, ValueError) as e:
            print(f"{name}: {e} (run the game once so it extracts {root})", file=sys.stderr)
            rc = 1
            continue
        dst = os.path.join(args.mod, name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(new)
        done += 1
        total += changed
        if len(names) <= 5:
            print(f"{name}: {changed} palette entries recoloured -> {dst}")
    if done > 5:
        print(f"{done} images, {total} palette entries recoloured -> {args.mod}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
