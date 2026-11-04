#!/usr/bin/env python3
"""Generate the art examples (stage-tiles, stage-objects, stage-npc's sprite) from YOUR exported data.

Nothing from the game is stored in the repository (ADR-001): this script reads the PNGs the port
exported from your own disc and writes recoloured copies into the mod folders. Only the palette
(PLTE chunk) changes; the pixels stay byte for byte, which is what the port requires.

    gen_art.py <user dir> [stage-tiles] [stage-objects] [stage-npc]    (default: all three)

Needs, in <user dir>/data/: z_stage/s330pack/ (export_stages = S330, or Options > System > Prepare
modding files) for stage-tiles, images/field/stage/s233mask/ for stage-objects and images/field/sprt/p002play/ for
stage-npc (a green copy of the player's sprite, with its sprite.json, as the mod sprite "stage-npc:rival").
Exit code: 0 when both were made, 3 when one was skipped (the reason is printed), 1 on an error.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
import image_mod_palette as imp  # noqa: E402

HUE = 300.0  # purple
NPC_HUE = 120.0  # green: the rival NPC's clothes


def recolour_folder(src, dst, hue=HUE):
    n = 0
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if not name.lower().endswith(".png"):
            continue
        with open(os.path.join(src, name), "rb") as f:
            data = f.read()
        new, _ = imp.recolour_png(data, 0, 360, hue, 0.1)
        with open(os.path.join(dst, name), "wb") as f:
            f.write(new)
        n += 1
    return n


def make_rival_json(src, dst):
    """The exported sprite-v1 file is the mod's sprite.json (same format); this one also makes the rival march on the spot:
    its STAND set (the one an idle NPC plays) uses the walking animations, so the new sprite visibly animates."""
    with open(src, encoding="utf-8") as f:
        doc = json.load(f)
    walk = next(x for x in doc["sets"] if x["id"] == 4)
    for st in doc["sets"]:
        if st["id"] == 1:
            st["animations"] = list(walk["animations"])
    doc["notes"] = "The rival: the player's sprite, recoloured, with the WALK animations as its STAND set. Made by pc/examples/tools/gen_art.py from your own exported data."
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    user = sys.argv[1]
    want = sys.argv[2:]
    data = os.path.join(user, "data")
    mods = os.path.join(user, "mods")
    rc = 0
    jobs = [
        ("stage-tiles", os.path.join(data, "z_stage", "s330pack"),
         os.path.join(mods, "stage-tiles", "z_stage", "s330pack"),
         "export the stage first: add 'export_stages = S330' under [data] in settings.ini and start the game once, or use Options > System > Prepare modding files"),
        ("stage-objects", os.path.join(data, "images", "field", "stage", "s233mask"),
         os.path.join(mods, "stage-objects", "images", "field", "stage", "s233mask"),
         "start the game once with your disc so it exports data/images"),
        ("stage-npc", os.path.join(data, "images", "field", "sprt", "p002play"),
         os.path.join(mods, "stage-npc", "sprites", "rival"),
         "start the game once with your disc so it exports data/images"),
    ]
    for name, src, dst, hint in jobs:
        if want and name not in want:
            continue
        if not os.path.isdir(src):
            print(f"gen_art: {name} skipped: {src} not found ({hint})")
            rc = 3
            continue
        try:
            n = recolour_folder(src, dst, NPC_HUE if name == "stage-npc" else HUE)
            if name == "stage-npc":
                make_rival_json(os.path.join(src, "sprite.json"), os.path.join(dst, "sprite.json"))
        except (OSError, ValueError) as e:
            print(f"gen_art: {name}: {e}", file=sys.stderr)
            return 1
        print(f"gen_art: {name}: {n} PNG recoloured -> {dst}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
