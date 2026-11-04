#!/usr/bin/env python3
"""Builds the button-prompt atlas of the options menu (feature controls-config, C4).

usage: python3 -I pc/gen_prompt_atlas.py <kenney_pack_dir>

<kenney_pack_dir> is the extracted "Input Prompts Pixel 16x" pack by Kenney
(CC0 1.0, https://kenney.nl/assets/input-prompts-pixel, version 1.0): the folder that holds
Tilemap/tilemap_packed.png. Writes next to this script, in third_party/kenney-prompts/:

  prompts.png            the atlas, for a human to look at (RGBA)
  ../../shim/prompt_atlas_data.c   the same pixels as a C array of PS1 15-bit words

Layout: COLS x ROWS cells of CELL x CELL pixels, in the order of CELLS below; the
same order is the PI_* enum of shim/prompt_icons.h (a test in pc/inputtest.c
checks the count). Each cell is the 16x16 Kenney tile cropped by one pixel on every
side (the tile's outer shadow ring), which is why it fits the game's 12-14 px text
lines at 1:1 with no scaling.

The Switch prompts are Kenney's "Input Prompts" 1.5A (CC0 1.0, Nintendo Switch / Default set), neutral
art with no Nintendo logos; the eight PNGs are copied into third_party/kenney-prompts/switch/ so the
atlas rebuilds without the big pack. The A/B and X/Y labels are the ones printed on the controller
(SDL reports Nintendo pads by label, see prompt_icons.h).

The PlayStation face buttons are not in this version of the pack, so they are drawn
here: Kenney's blank round white button with a small cross, circle, square and
triangle on top (plain geometric shapes, CC0-compatible).

Pixel format: ABGR1555 as VRAM holds it (red in the low bits). 0x0000 is transparent;
an opaque pixel that would be 0x0000 is stored as 0x0421 (nearly black).
"""
import os
import sys

from PIL import Image, ImageDraw

CELL = 14
COLS = 4
ROWS = 9

# (name, source) source = (tile column, tile row) of tilemap_packed.png, "gen:<shape>", or
# "sw:<button>" (Kenney's Input Prompts 1.5A, Nintendo Switch set, kept in third_party/kenney-prompts/switch/)
CELLS = [
    ("xb_a", (4, 0)), ("xb_b", (5, 0)), ("xb_x", (6, 0)), ("xb_y", (7, 0)),
    ("sw_a", "sw:a"), ("sw_b", "sw:b"), ("sw_x", "sw:x"), ("sw_y", "sw:y"),
    ("ps_cross", "gen:cross"), ("ps_circle", "gen:circle"), ("ps_square", "gen:square"),
    ("ps_triangle", "gen:triangle"),
    ("dpad_up", (1, 1)), ("dpad_down", (3, 1)), ("dpad_left", (2, 1)), ("dpad_right", (4, 1)),
    ("xb_lb", (9, 16)), ("xb_rb", (10, 16)), ("ps_l1", (19, 18)), ("ps_r1", (20, 18)),
    ("sw_l", "sw:l"), ("sw_r", "sw:r"),
    ("xb_menu", (20, 22)), ("ps_options", (19, 20)), ("sw_plus", "sw:plus"), ("sw_minus", "sw:minus"),
    ("key_esc", (17, 0)), ("key_f1", (18, 0)), ("key_q", (17, 2)), ("key_e", (19, 2)),
    ("key_up", (30, 4)), ("key_down", (32, 4)), ("key_left", (33, 4)), ("key_right", (31, 4)),
    ("xb_view", "gen:view"), ("ps_select", (20, 20)),
]

LABEL_DARK = (0x2C, 0x30, 0x40)  # the Switch labels, drawn as holes in Kenney's art, are filled with this
BLANK_BUTTON = (12, 0)  # the plain white round button
SHAPE_COLOURS = {
    "cross": (0x3C, 0x82, 0xE0), "circle": (0xE0, 0x3C, 0x3C),
    "square": (0xE0, 0x6C, 0xB4), "triangle": (0x2C, 0xA8, 0x5C),
}
# 7x7 masks, drawn centred on the 14x14 cell
SHAPES = {
    "cross": ["X.....X", ".X...X.", "..X.X..", "...X...", "..X.X..", ".X...X.", "X.....X"],
    "circle": ["..XXX..", ".X...X.", "X.....X", "X.....X", "X.....X", ".X...X.", "..XXX.."],
    "square": ["XXXXXXX", "X.....X", "X.....X", "X.....X", "X.....X", "X.....X", "XXXXXXX"],
    "triangle": ["...X...", "..X.X..", "..X.X..", ".X...X.", ".X...X.", "X.....X", "XXXXXXX"],
}


# Xbox "View" (back) button: two overlapping squares, drawn on Kenney's blank wide pill in the
# colour of the pill's own "Menu" bars, since the 1.0 pack has no View icon.
VIEW_MASK = ["..........",
             "..XXXXX...",
             "..X...X...",
             "..X.XXXXX.",
             "..XXX...X.",
             "....X...X.",
             "....XXXXX.",
             ".........."]


def tile(sheet, col, row):
    return sheet.crop((col * 16 + 1, row * 16 + 1, col * 16 + 15, row * 16 + 15))


def generated(sheet, shape):
    if shape == "view":
        cell = tile(sheet, 19, 22)
        px, ink = cell.load(), tile(sheet, 20, 22).load()[5, 3]
        for y, line in enumerate(VIEW_MASK):
            for x, ch in enumerate(line):
                if ch == "X":
                    px[2 + x, 3 + y] = ink
        return cell
    cell = tile(sheet, *BLANK_BUTTON)
    px = cell.load()
    mask = SHAPES[shape]
    for y, line in enumerate(mask):
        for x, ch in enumerate(line):
            if ch == "X":
                px[3 + x, 3 + y] = SHAPE_COLOURS[shape] + (255,)
    return cell


def switch_cell(name):
    """Kenney's 64x64 Switch prompt (a light round or rounded-square key with a dark label),
    cropped to its art and shrunk to the 14x14 cell. VRAM has 1 bit of alpha, so edge pixels are
    kept when at least half covered, with the colour of the box-filtered (alpha-weighted) average."""
    here = os.path.dirname(os.path.abspath(__file__))
    src = Image.open(os.path.join(here, "third_party", "kenney-prompts", "switch", "switch_button_%s.png" % name))
    src = src.convert("RGBA")
    src = src.crop(src.getbbox())
    # the label is a hole in the white key: fill the holes (transparent pixels the outside cannot reach) with dark
    probe = src.getchannel("A").point(lambda a: 0 if a == 0 else 255)
    for corner in ((0, 0), (src.width - 1, 0), (0, src.height - 1), (src.width - 1, src.height - 1)):
        if probe.getpixel(corner) == 0:
            ImageDraw.floodfill(probe, corner, 128)
    sp, pp = src.load(), probe.load()
    for y in range(src.height):
        for x in range(src.width):
            if pp[x, y] == 0:
                sp[x, y] = LABEL_DARK + (255,)
    w, h = src.size
    out = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    scale = CELL / max(w, h)
    small = src.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.BOX)
    out.paste(small, ((CELL - small.width) // 2, (CELL - small.height) // 2))
    return out


def word(r, g, b, a):
    if a < 128:
        return 0
    w = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    return w if w != 0 else 0x0421


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sheet = Image.open(os.path.join(sys.argv[1], "Tilemap", "tilemap_packed.png")).convert("RGBA")
    assert len(CELLS) <= COLS * ROWS
    atlas = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for i, (name, src) in enumerate(CELLS):
        if isinstance(src, str) and src.startswith("sw:"):
            cell = switch_cell(src[3:])
        elif isinstance(src, str):
            cell = generated(sheet, src[4:])
        else:
            cell = tile(sheet, *src)
        atlas.paste(cell, ((i % COLS) * CELL, (i // COLS) * CELL))
    here = os.path.dirname(os.path.abspath(__file__))
    atlas.save(os.path.join(here, "third_party", "kenney-prompts", "prompts.png"))
    px = atlas.load()
    words = [word(*px[x, y]) for y in range(atlas.height) for x in range(atlas.width)]
    out = ["/* Generated by pc/gen_prompt_atlas.py from Kenney's Input Prompts Pixel 16x and Input Prompts 1.5A (both CC0 1.0); do not edit.",
           "   %d x %d halfwords in VRAM's 15-bit format; see shim/prompt_icons.h and third_party/kenney-prompts/LICENSE.txt */" %
           (atlas.width, atlas.height),
           '#include "prompt_icons.h"', "",
           "const unsigned short PROMPT_ATLAS[PI_ATLAS_W * PI_ATLAS_H] = {"]
    for i in range(0, len(words), 14):
        out.append("    " + ",".join("0x%04X" % w for w in words[i:i + 14]) + ",")
    out.append("};")
    out.append("")
    out.append("const int PROMPT_ATLAS_CELLS = %d;" % len(CELLS))
    with open(os.path.join(here, "shim", "prompt_atlas_data.c"), "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %d cells, atlas %dx%d" % (len(CELLS), atlas.width, atlas.height))


if __name__ == "__main__":
    main()
