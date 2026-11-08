#!/usr/bin/env python3
"""Generate the art, the map layers and stage.json of the training-field example mod.

Everything here is original (nothing comes from the game disc, ADR-001) and Python standard library only:

    python3 make_art.py            # writes ../stages/lower/ and ../stages/upper/

Each stage gets background.png (the picture), floor.png (collision), areas.png (battle zones) and stage.json
(`stage-new-v1`). The pictures and the layers are drawn from ONE layout (rectangles and ellipses in pixels), so a
fence you see is a fence you cannot walk through and a tall-grass patch you see is the battle area 2. The output is
deterministic: running it again gives the same bytes. The script also checks that the start, the exits and every
battle area can be reached on foot (a flood fill on a grid of 16 px cells that are completely walkable).

Layer values (docs/stages.md section 13): floor 0 closed, 1 flat, 2 to 7 a slope (19 = 16 + 3: the same slope mirrored in x);
areas 0 no battles, n = the n-th entry of stage.json "battles" ("add"). The exits are rectangles of the map ("slots" with
an "area"), so they fire wherever the player stands in them and presses cross.

Heights (T10). The game has no height for a walker: the 2D map is an oblique view, and a slope is a floor cell that changes the
step of the walk (FIELDSTG_getWalkStep, src/field/field_mode/map.c:149-160): row n - 1 of FIELDSTG_dirVectors
(src/field/field_mode/data/fieldstg.c:3601) for the cell value n, with the x of the step negated and the direction mirrored
when bit 0x10 is set. Row 2 (value 3) walking east is (3939, -819) / 4096 per step: the player goes 0.2079 px up for each px
east, so a ramp drawn along that line is walked as a ramp. The lower floor's plateau is 32 px high (the cliff is 32 px of
picture), its two ramps are 154 px long (32 / 0.2079): west one value 3, east one value 19 (mirrored: walking east goes down).
The picture draws exactly the band that has the value, so the slant you see is the slant you walk.
"""
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.normpath(os.path.join(HERE, ".."))

CELL = 16  # px; the layout is drawn in cells of 16 px, the layers are one value per pixel


# ---------------------------------------------------------------------------------------------- PNG output
def write_png(path, w, h, rows, palette):
    """Indexed 8-bit PNG (colour type 3, filter 0). rows: list of bytes-like, one per pixel row."""

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    plte = b"".join(bytes(c) for c in palette)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", plte)
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------------------------------------- noise
def noise(x, y, s=0):
    """A small integer hash: the same (x, y, s) always gives the same value 0..65535."""
    v = (x * 73856093) ^ (y * 19349663) ^ (s * 83492791)
    v = (v ^ (v >> 13)) * 1274126177 & 0xFFFFFFFF
    return (v ^ (v >> 16)) & 0xFFFF


# ---------------------------------------------------------------------------------------------- themes
LOWER = {
    "grass": [(84, 168, 70), (92, 178, 76)], "grass_light": (120, 202, 98), "grass_dark": (62, 134, 58),
    "tall": (44, 112, 52), "tall_blade": (70, 150, 64), "tall_tip": (112, 192, 92),
    "path": (206, 176, 120), "path_light": (224, 198, 146), "path_dark": (172, 142, 94), "path_edge": (132, 104, 66),
    "tree_ground": (40, 96, 48), "tree_dark": (26, 80, 40), "tree_mid": (46, 130, 56), "tree_light": (96, 172, 84),
    "wood": (150, 104, 56), "wood_dark": (98, 62, 34), "wood_light": (188, 140, 84),
    "rock": (130, 130, 138), "rock_light": (178, 178, 188), "rock_dark": (82, 82, 96),
    "water": (64, 128, 208), "water_light": (126, 180, 242), "water_dark": (40, 88, 160), "shore": (222, 210, 152),
    "stone": (192, 192, 198), "stone_light": (226, 226, 232), "stone_dark": (118, 118, 130),
    "pad": (232, 196, 80), "pad_dark": (166, 122, 34), "arrow": (255, 250, 222),
    "top": [(138, 200, 94), (146, 208, 100)], "top_edge": (176, 230, 126), "lily": (74, 156, 84), "lily_light": (150, 214, 120),
}
UPPER = {
    "grass": [(150, 158, 72), (158, 166, 78)], "grass_light": (194, 200, 108), "grass_dark": (112, 124, 54),
    "tall": (122, 108, 46), "tall_blade": (176, 148, 60), "tall_tip": (224, 192, 92),
    "path": (176, 164, 182), "path_light": (200, 190, 206), "path_dark": (140, 128, 148), "path_edge": (96, 84, 108),
    "tree_ground": (84, 62, 40), "tree_dark": (104, 52, 30), "tree_mid": (176, 90, 40), "tree_light": (228, 152, 66),
    "wood": (120, 84, 110), "wood_dark": (74, 50, 72), "wood_light": (158, 120, 148),
    "rock": (118, 126, 148), "rock_light": (166, 176, 200), "rock_dark": (72, 78, 104),
    "water": (84, 168, 196), "water_light": (152, 214, 232), "water_dark": (48, 116, 148), "shore": (206, 200, 160),
    "stone": (180, 188, 206), "stone_light": (218, 224, 238), "stone_dark": (108, 114, 142),
    "pad": (236, 156, 92), "pad_dark": (162, 92, 40), "arrow": (255, 246, 226),
    "top": [(168, 172, 84), (176, 180, 90)], "top_edge": (212, 214, 130), "lily": (96, 170, 96), "lily_light": (170, 220, 140),
}


class Palette:
    """Colours get indexes in the order they are first used (the PNG palette is that list)."""

    def __init__(self):
        self.colors = []
        self.index = {}

    def get(self, rgb):
        rgb = tuple(rgb)
        if rgb not in self.index:
            if len(self.colors) >= 256:
                raise SystemExit("more than 256 colours")
            self.index[rgb] = len(self.colors)
            self.colors.append(rgb)
        return self.index[rgb]


# ---------------------------------------------------------------------------------------------- layout
class Stage:
    """One map: a canvas (indexes into the palette), the floor layer, the areas layer and masks of what was drawn."""

    def __init__(self, name, w, h, theme, seed):
        self.name, self.w, self.h, self.t, self.seed = name, w, h, theme, seed
        self.pal = Palette()
        self.canvas = [bytearray(w) for _ in range(h)]
        self.floor = [bytearray([1]) * w for _ in range(h)]
        self.areas = [bytearray(w) for _ in range(h)]
        self.pathmask = [bytearray(w) for _ in range(h)]
        self.rects = {}  # name -> (x, y, w, h) in px, for stage.json

    def put(self, x, y, color):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.canvas[y][x] = self.pal.get(color)

    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    # --- ground
    def grass(self):
        t = self.t
        for y in range(self.h):
            for x in range(self.w):
                base = t["grass"][((x // 8) + (y // 8)) & 1]  # a light checker of 8 px
                n = noise(x, y, self.seed)
                if n % 31 == 0:
                    base = t["grass_light"]
                elif n % 37 == 1:
                    base = t["grass_dark"]
                self.put(x, y, base)
        # a small tuft (a V of three pixels) in about every other cell
        for cy in range(self.h // CELL):
            for cx in range(self.w // CELL):
                n = noise(cx, cy, self.seed + 7)
                if n % 2:
                    continue
                x, y = cx * CELL + 2 + n % 11, cy * CELL + 3 + (n >> 4) % 9
                self.put(x, y, t["grass_dark"])
                self.put(x - 1, y - 1, t["grass_light"])
                self.put(x + 1, y - 1, t["grass_light"])

    def tall_grass(self, cx, cy, rx, ry):
        """An ellipse of tall grass (battle area 2). The areas layer is set later from this mask."""
        t = self.t
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if self.inside(x, y) and ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.areas[y][x] = 2
                    self.put(x, y, t["tall"])
        for y in range(cy - ry, cy + ry + 1, 3):
            for x in range(cx - rx, cx + rx + 1, 3):
                jx = x + noise(x, y, self.seed + 3) % 3
                jy = y + noise(x, y, self.seed + 5) % 3
                if self.inside(jx, jy) and self.areas[jy][jx] == 2:
                    for k in range(5):
                        self.put(jx, jy - k, t["tall_blade"] if k < 4 else t["tall_tip"])

    def path_rect(self, x, y, w, h):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy):
                    self.pathmask[yy][xx] = 1

    def path_ellipse(self, cx, cy, rx, ry, ring=None):
        """A filled ellipse of path; with ring = (rx2, ry2) only the band between the two ellipses."""
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if not self.inside(x, y):
                    continue
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 > 1.0:
                    continue
                if ring and ((x - cx) / ring[0]) ** 2 + ((y - cy) / ring[1]) ** 2 < 1.0:
                    continue
                self.pathmask[y][x] = 1

    def paint_paths(self):
        t = self.t
        for y in range(self.h):
            for x in range(self.w):
                if not self.pathmask[y][x]:
                    continue
                edge = False
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if self.inside(nx, ny) and not self.pathmask[ny][nx]:
                        edge = True
                n = noise(x, y, self.seed + 11)
                if edge:
                    color = t["path_edge"]
                elif n % 19 == 0:
                    color = t["path_light"]
                elif n % 23 == 1:
                    color = t["path_dark"]
                else:
                    color = t["path"]
                self.areas[y][x] = 0 if self.areas[y][x] == 2 else self.areas[y][x]
                self.put(x, y, color)

    # --- obstacles (closed floor)
    def close(self, x, y):
        if self.inside(x, y):
            self.floor[y][x] = 0

    def pond(self, cx, cy, rx, ry):
        t = self.t
        for y in range(cy - ry - 2, cy + ry + 3):
            for x in range(cx - rx - 2, cx + rx + 3):
                if not self.inside(x, y):
                    continue
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                d2 = ((x - cx) / (rx + 2)) ** 2 + ((y - cy) / (ry + 2)) ** 2
                if d <= 1.0:
                    n = noise(x // 2, y, self.seed + 13)
                    color = t["water_dark"] if d > 0.82 else t["water"]
                    if d <= 0.82 and n % 11 == 0:
                        color = t["water_light"]
                    self.put(x, y, color)
                    self.close(x, y)
                    self.areas[y][x] = 0
                elif d2 <= 1.0:
                    self.put(x, y, t["shore"])
                    self.areas[y][x] = 0

    def rock(self, x, y, w=32, h=28):
        """A boulder of w x h px with its top-left corner at (x, y)."""
        t = self.t
        cx, cy = x + w / 2.0, y + h / 2.0 + 1
        for yy in range(y, y + h + 2):
            for xx in range(x, x + w):
                d = ((xx - cx) / (w / 2.0)) ** 2 + ((yy - cy) / (h / 2.0)) ** 2
                if d > 1.0:
                    continue
                if d > 0.78:
                    color = t["rock_dark"]
                elif (xx - cx) + (yy - cy) < -w * 0.28:
                    color = t["rock_light"]
                else:
                    color = t["rock"]
                self.put(xx, yy, color)
                self.close(xx, yy)
                self.areas[yy][xx] = 0

    def fence(self, x, y, length):
        """A horizontal wooden fence, 16 px tall, `length` px long."""
        t = self.t
        for xx in range(x, x + length):
            for yy in range(y, y + CELL):
                color = None
                lx = (xx - x) % 32
                if lx in (1, 2, 3, 4) and 1 <= yy - y <= 14:  # a post every 32 px
                    color = t["wood_dark"] if lx in (1, 4) else t["wood_light"]
                elif yy - y in (4, 5, 6) or yy - y in (10, 11, 12):  # two rails
                    color = t["wood_dark"] if yy - y in (6, 12) else t["wood"]
                    if yy - y in (4, 10):
                        color = t["wood_light"]
                if color is not None:
                    self.put(xx, yy, color)
                self.close(xx, yy)
                self.areas[yy][xx] = 0

    def tree(self, cx, cy):
        """A tree filling the 16 px cell (cx, cy): closed."""
        t = self.t
        x0, y0 = cx * CELL, cy * CELL
        for ly in range(CELL):
            for lx in range(CELL):
                d = (lx - 7.5) ** 2 + (ly - 7.5) ** 2
                n = noise(x0 + lx, y0 + ly, self.seed + 17)
                if d > 7.4 ** 2:
                    color = t["tree_ground"] if n % 7 else t["tree_dark"]
                elif d > 5.9 ** 2:
                    color = t["tree_dark"]
                elif (lx - 5.5) ** 2 + (ly - 5.5) ** 2 < 3.4 ** 2:
                    color = t["tree_light"]
                else:
                    color = t["tree_mid"]
                    if n % 9 == 0:
                        color = t["tree_light"]
                self.put(x0 + lx, y0 + ly, color)
                self.close(x0 + lx, y0 + ly)
                self.areas[y0 + ly][x0 + lx] = 0

    def border(self, gaps):
        """Trees all around, two cells thick; `gaps` = list of (x, y, w, h) px rectangles left open."""
        for cy in range(self.h // CELL):
            for cx in range(self.w // CELL):
                if not (cx < 2 or cy < 2 or cx >= self.w // CELL - 2 or cy >= self.h // CELL - 2):
                    continue
                x, y = cx * CELL, cy * CELL
                if any(gx <= x and x + CELL <= gx + gw and gy <= y and y + CELL <= gy + gh for gx, gy, gw, gh in gaps):
                    continue
                self.tree(cx, cy)


    # --- the lake, the pier (T11)
    def lilies(self, spots):
        """Lily pads on the water: (cx, cy) centres in px, a 7 x 5 px ellipse with a notch and a light dot."""
        t = self.t
        for cx, cy in spots:
            for y in range(cy - 3, cy + 4):
                for x in range(cx - 4, cx + 5):
                    if ((x - cx) / 4.0) ** 2 + ((y - cy) / 2.6) ** 2 <= 1.0 and not (x > cx and abs(y - cy) <= 0 and x < cx + 3):
                        self.put(x, y, t["lily"])
            self.put(cx - 1, cy - 1, t["lily_light"])
            self.put(cx + 1, cy, t["lily_light"])

    def pier(self, x, y, w, h):
        """A wooden pier (x, y, w, h px): planks running north to south, cross beams every 24 px, a post at each end corner.
        The floor under it is open even where it crosses the water (the pond closes its pixels first)."""
        t = self.t
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                lx, ly = xx - x, yy - y
                plank = lx // 8
                if lx % 8 == 0:
                    color = t["wood_dark"]
                elif ly % 24 in (0, 1):
                    color = t["wood_dark"]
                else:
                    color = t["wood_light"] if (plank + noise(plank, ly // 24, self.seed + 23)) % 3 == 0 else t["wood"]
                self.put(xx, yy, color)
                self.floor[yy][xx] = 1
                self.areas[yy][xx] = 0
        for px in (x - 2, x + w - 2):
            for yy in range(y, y + 5):
                for xx in range(px, px + 4):
                    self.put(xx, yy, t["wood_dark"] if xx in (px, px + 3) or yy == y + 4 else t["wood_light"])

    # --- heights (T10)
    SLOPE = 819.0 / 3939.0   # FIELDSTG_dirVectors[2][6]: east on a value 3 cell is (3939, -819) / 4096

    def rock_wall(self, x0, y0, x1, y1, inside):
        """A rock face: every pixel of the box (x0, y0, x1, y1) that `inside(x, y)` accepts is painted, lighter at the top
        lip and darker toward the foot, with vertical cracks, and closed (floor 0, no battles). Returns nothing."""
        t = self.t
        for y in range(y0, y1):
            for x in range(x0, x1):
                if not inside(x, y):
                    continue
                f = (y - y0) / float(max(1, y1 - y0 - 1))
                n = noise(x // 2, 0, self.seed + 29)
                if f < 0.14:
                    color = t["rock_light"]
                elif f > 0.82:
                    color = t["rock_dark"]
                else:
                    color = t["rock"]
                    if n % 7 == 0 or (n % 5 == 0 and f > 0.5):
                        color = t["rock_dark"]
                self.put(x, y, color)
                self.floor[y][x] = 0
                self.areas[y][x] = 0

    def plateau(self, top, ramp_w, ramp_e, cliff_h=32, band=32, rim=8):
        """A raised plateau. top = (x, y, w, h) px of the walkable top (its south edge is the cliff's lip); the cliff face is
        the cliff_h px under it. ramp_w / ramp_e = True builds a ramp on that side: a band `band` px tall whose centre line
        climbs from the ground (cliff foot) to the top's level, 154 px long for a 32 px rise (cliff_h / SLOPE). West ramp:
        floor value 3 (walking east climbs). East ramp: value 19 = 16 + 3 (mirrored: walking east goes down).
        Returns the ramp bands as dicts for the checks."""
        t = self.t
        x, y, w, h = top
        foot = y + h + cliff_h               # y of the cliff foot (the ground)
        length = int(round(cliff_h / self.SLOPE))
        yc_end = y + h - band // 2           # the centre line at the top's level
        # the top surface
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                edge = xx < x + 2 or xx >= x + w - 2 or yy < y + 2
                base = t["top"][((xx // 8) + (yy // 8)) & 1]
                n = noise(xx, yy, self.seed + 31)
                color = t["top_edge"] if edge else (t["grass_light"] if n % 29 == 0 else (t["grass_dark"] if n % 41 == 1 else base))
                self.put(xx, yy, color)
                self.areas[yy][xx] = 0 if edge else self.areas[yy][xx]
        # the rim on the north, west and east edges: closed
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if yy < y + rim or xx < x + rim or xx >= x + w - rim:
                    if (xx < x + rim and yc_end - band // 2 <= yy < y + h and ramp_w) or (xx >= x + w - rim and yc_end - band // 2 <= yy < y + h and ramp_e):
                        continue          # the ramp comes in here
                    f = (xx - x) if xx < x + rim else ((x + w - 1 - xx) if xx >= x + w - rim else (yy - y))
                    self.put(xx, yy, t["rock_light"] if f < 2 else (t["rock"] if f < rim - 2 else t["rock_dark"]))
                    self.floor[yy][xx] = 0
                    self.areas[yy][xx] = 0
        # the cliff face under the top
        self.rock_wall(x, y + h, x + w, foot, lambda xx, yy: True)
        for xx in range(x, x + w):
            for k in range(5):
                yy = foot + k
                if self.inside(xx, yy) and self.floor[yy][xx] and not self.pathmask[yy][xx] and (xx + yy) % 2 == 0 and k < 3:
                    self.put(xx, yy, t["grass_dark"])
        bands = []
        for side, on in (("w", ramp_w), ("e", ramp_e)):
            if not on:
                continue
            x_far = x - length if side == "w" else x + w + length   # where the ramp meets the ground
            x_top = x if side == "w" else x + w                      # where it meets the top

            def centre(xx, side=side):
                # distance from the top edge along x, times SLOPE, below the level of the top
                d = (x - xx) if side == "w" else (xx - (x + w))
                return yc_end + d * self.SLOPE

            lo, hi = (x_far, x_top) if side == "w" else (x_top, x_far)
            # the side wall under the ramp: from the ramp's lower edge to the foot, a triangle that ends in a point on the ground
            self.rock_wall(lo, y + h - band // 2, hi, foot + 1, lambda xx, yy, c=centre: c(xx) + band // 2 <= yy < foot)
            value = 3 if side == "w" else 19
            for xx in range(lo, hi):
                yc = centre(xx)
                for yy in range(int(round(yc - band / 2.0)), int(round(yc + band / 2.0))):
                    if not self.inside(xx, yy):
                        continue
                    k = (yy - (yc - band / 2.0))
                    # steps: lines across the ramp every 10 px along x (a dark and a light one), the edges are curb stones
                    along = (xx - lo) if side == "w" else (hi - 1 - xx)
                    if k < 2:
                        color = t["stone_dark"]
                    elif k >= band - 3:
                        color = t["rock_dark"]
                    elif along % 10 == 0:
                        color = t["path_dark"]
                    elif along % 10 == 1:
                        color = t["path_light"]
                    else:
                        color = t["path"] if noise(xx, yy, self.seed + 37) % 17 else t["path_light"]
                    self.put(xx, yy, color)
                    self.floor[yy][xx] = value
                    self.areas[yy][xx] = 0
            # a soft shadow on the ground along the foot of the wall (dithered, walkable ground only)
            for xx in range(lo, hi):
                for k in range(5):
                    yy = foot + k
                    if self.inside(xx, yy) and self.floor[yy][xx] and not self.pathmask[yy][xx] and (xx + yy) % 2 == 0 and k < 3 - (1 if side == "w" and xx < lo + 12 else 0):
                        self.put(xx, yy, t["grass_dark"])
            bands.append({"side": side, "value": value, "x": lo, "w": hi - lo, "y": int(yc_end - band // 2), "far": x_far})
        return bands

    def set_floor(self, x, y, w, h, v):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy) and self.floor[yy][xx]:
                    self.floor[yy][xx] = v

    def clear_area_rect(self, x, y, w, h):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy):
                    self.areas[yy][xx] = 0

    def open_at(self, x, y, r=10):
        """True when every pixel within r px of (x, y) is walkable: a place for a character, a tree or a spot."""
        return all(self.floor[yy][xx] for yy in range(y - r, y + r + 1) for xx in range(x - r, x + r + 1) if self.inside(xx, yy))

    # --- walkable markers
    def stairs(self, x, y, w, h, up):
        """A flight of stone steps (x, y, w, h px) with rails at the sides and an arrow: up (north) or down (south)."""
        t = self.t
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                lx, ly = xx - x, yy - y
                if lx < 4 or lx >= w - 4:
                    color = t["stone_dark"] if lx in (0, 3, w - 1, w - 4) else t["stone"]
                else:
                    step = (ly // 8) if up else ((h - 1 - ly) // 8)
                    band = ly % 8 if up else (h - 1 - ly) % 8
                    color = t["stone_light"] if band < 6 else t["stone_dark"]
                    if step % 2:
                        color = t["stone"] if band < 6 else t["stone_dark"]
                self.put(xx, yy, color)
                self.areas[yy][xx] = 0
        self.arrow(x + w // 2, y + h // 2, up)

    def arrow(self, cx, cy, up):
        """A white arrow about 13 px tall centred on (cx, cy): pointing north when up, south otherwise."""
        t = self.t
        for k in range(7):
            half = k if up else 6 - k
            y = cy - 8 + k if up else cy + 2 + k
            for dx in range(-half, half + 1):
                self.put(cx + dx, y, t["arrow"])
        for k in range(6):
            y = cy - 1 + k if up else cy - 4 + k
            for dx in (-1, 0, 1):
                self.put(cx + dx, y, t["arrow"])

    def pad(self, x, y, w, h, up):
        """The mat of an exit that leads out of the map: a warm tile with a border and an arrow."""
        t = self.t
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                lx, ly = xx - x, yy - y
                border = lx < 2 or ly < 2 or lx >= w - 2 or ly >= h - 2
                color = t["pad_dark"] if border else t["pad"]
                if not border and noise(xx, yy, self.seed + 19) % 13 == 0:
                    color = t["arrow"]
                self.put(xx, yy, color)
                self.areas[yy][xx] = 0
        self.arrow(x + w // 2, y + h // 2, up)

    # --- areas
    def make_areas(self, zone, safe):
        """Area 1 on every walkable pixel inside `zone` = (x, y, w, h) that is not in a `safe` rectangle; area 2 stays
        where tall grass was drawn (and was not paved over); safe rectangles and closed pixels have none."""
        zx, zy, zw, zh = zone
        for y in range(self.h):
            for x in range(self.w):
                if not self.floor[y][x] or not (zx <= x < zx + zw and zy <= y < zy + zh):
                    self.areas[y][x] = 0
                elif self.areas[y][x] != 2:
                    self.areas[y][x] = 1
                if any(sx <= x < sx + sw and sy <= y < sy + sh for sx, sy, sw, sh in safe):
                    self.areas[y][x] = 0

    # --- checks and output
    def reach(self, start, targets):
        """Flood fill over the 16 px cells whose 256 pixels are all walkable; every target rectangle must be touched."""
        B = 8  # the blocks of the walk check are 8 px: a ramp is a slanted band, 16 px blocks would not fit in its steps
        cw, ch = self.w // B, self.h // B
        open_ = [[all(self.floor[cy * B + j][cx * B + i] for j in range(B) for i in range(B)) for cx in range(cw)] for cy in range(ch)]
        sx, sy = start[0] // B, start[1] // B
        if not open_[sy][sx]:
            raise SystemExit("%s: the start (%d, %d) is not on a walkable cell" % (self.name, start[0], start[1]))
        seen = {(sx, sy)}
        todo = [(sx, sy)]
        while todo:
            cx, cy = todo.pop()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = cx + dx, cy + dy
                if 0 <= nx < cw and 0 <= ny < ch and open_[ny][nx] and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    todo.append((nx, ny))
        for label, (x, y, w, h) in targets.items():
            hit = any(((x + i) // B, (y + j) // B) in seen for j in range(0, h, 4) for i in range(0, w, 4))
            if not hit:
                raise SystemExit("%s: %s cannot be reached on foot from the start" % (self.name, label))
        for aid in (1, 2):
            hit = any(self.areas[cy * B + 4][cx * B + 4] == aid for cx, cy in seen)
            if not hit:
                raise SystemExit("%s: battle area %d cannot be reached on foot" % (self.name, aid))
        return len(seen)

    def save(self, outdir):
        os.makedirs(outdir, exist_ok=True)
        write_png(os.path.join(outdir, "background.png"), self.w, self.h, self.canvas, self.pal.colors)
        # floor.png: the index of a pixel is the cell's value (0 closed, 1 flat, 2-7 slopes, 16 + n mirrored); the colours are for people
        floor_colors = [(0, 0, 0), (255, 255, 255)] + [(255, 200 - 20 * k, 40 + 25 * k) for k in range(6)] + [(90, 90, 90)] * 8 + [(40, 120, 255)] + [(40, 120 - 10 * k, 255 - 25 * k) for k in range(8)]
        write_png(os.path.join(outdir, "floor.png"), self.w, self.h, self.floor, floor_colors[:24])
        write_png(os.path.join(outdir, "areas.png"), self.w, self.h, self.areas, [(0, 0, 0), (90, 200, 90), (30, 110, 30)])


# ---------------------------------------------------------------------------------------------- the sign sprite (docs/stages.md section 11, S9)
SIGN_W, SIGN_H = 32, 40
SIGN_PALETTE = [(0, 0, 0), (48, 30, 18), (94, 58, 30), (150, 98, 52), (196, 142, 80), (226, 188, 120), (62, 44, 32), (40, 74, 36), (74, 124, 52)]


def sign_pixels():
    """A wooden signpost: a post, a board of three planks with nails and a few dark strokes that look like writing, and a tuft of grass at its foot.
    Palette index 0 is transparent; 1 outline, 2 dark wood, 3 wood, 4 light wood, 5 highlight, 6 writing, 7 and 8 grass."""
    px = [[0] * SIGN_W for _ in range(SIGN_H)]

    def put(x, y, c):
        if 0 <= x < SIGN_W and 0 <= y < SIGN_H:
            px[y][x] = c

    def fill(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, c)

    # the post (with an outline), standing from the board down to the ground
    fill(13, 19, 18, 36, 1)
    fill(14, 19, 17, 36, 3)
    fill(14, 19, 14, 36, 4)
    fill(17, 19, 17, 36, 2)
    # the board: 26 x 16, corners cut, three planks
    fill(3, 3, 28, 18, 1)
    for x, y in ((3, 3), (28, 3), (3, 18), (28, 18)):
        put(x, y, 0)
    for plank, (y0, y1) in enumerate(((4, 7), (8, 12), (13, 17))):
        fill(4, y0, 27, y1, 3)
        fill(4, y0, 27, y0, 5 if plank == 0 else 4)
        fill(4, y1, 27, y1, 2)
    for x in (5, 26):  # the nails
        for y in (5, 10, 15):
            put(x, y, 1)
    # the writing: short dark strokes on the two lower planks, and a small arrow-like mark on the top one
    for y, spans in ((9, ((7, 14), (17, 24))), (11, ((7, 11), (14, 24))), (14, ((7, 16), (19, 24))), (16, ((7, 12), (15, 20)))):
        for a, b in spans:
            fill(a, y, b, y, 6)
    fill(9, 5, 22, 5, 6)
    put(22, 4, 6)
    put(22, 6, 6)
    # a tuft of grass and a little shadow at the foot
    fill(9, 37, 22, 37, 1)
    for x in range(10, 22):
        put(x, 37, 2)
    for x, h, c in ((10, 3, 7), (12, 4, 8), (14, 2, 7), (17, 2, 7), (19, 4, 8), (21, 3, 7)):
        for y in range(36 - h + 1, 37):
            put(x, y, c)
    fill(12, 38, 19, 38, 1)
    return px


def write_sign(base):
    """mods/training-field/sprites/sign/: one frame and a sprite.json (sprite-v1). Every set plays the same still animation, in all five direction slots."""
    folder = os.path.join(base, "sprites", "sign")
    os.makedirs(folder, exist_ok=True)
    px = sign_pixels()
    rows = [bytes(r) for r in px]

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIGN_W, SIGN_H, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", b"".join(bytes(c) for c in SIGN_PALETTE)) + chunk(b"tRNS", b"\x00")
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(os.path.join(folder, "000.png"), "wb") as f:
        f.write(png)
    doc = {
        "format": "sprite-v1",
        "source": "original (tools/make_art.py)",
        "set_table": -1,
        "frames": [{"id": 0, "image": "000.png", "w": SIGN_W, "h": SIGN_H}],
        "animations": [{"id": 1, "loop": True, "ticks": 60, "steps": [{"frame": 0, "time": 60, "x": -SIGN_W // 2, "y": -(SIGN_H - 4)}]}],
        "sets": [{"id": i, "animations": [1, 1, 1, 1, 1]} for i in (1, 4, 5, 6, 7)],
        "directions": [{"dir": d, "slot": min(d, 4) if d <= 4 else 8 - d, "flip": d > 4} for d in range(8)],
        # 32 px, the width the game gives its own sign (an actor: FIELDSTG_actorWidths[63] = 0x20, wstag331.c "actor13"): the box a
        # signpost is read in is half this wide and a quarter this tall, so a narrower one is only found from beside it
        "actor_width": 32,
        "notes": "A wooden signpost, original art generated by tools/make_art.py (nothing comes from the game disc).",
    }
    with open(os.path.join(folder, "sprite.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def rect(cx, cy, cw, ch):
    """Cells to pixels."""
    return (cx * CELL, cy * CELL, cw * CELL, ch * CELL)


# ---------------------------------------------------------------------------------------------- the two floors
MUSIC = {"bank": 9, "track": 0}  # Central Park's field music (data/stages/s330/stage.json: soundBank 9, music MUSIC(9, 0))
NAME = "training-field"


def build_lower():
    s = Stage("lower", 1024, 768, LOWER, 1)
    gate = rect(29, 44, 6, 4)  # the opening of the south wall
    s.grass()
    s.tall_grass(20 * CELL, 33 * CELL, 7 * CELL, 4 * CELL)
    s.tall_grass(47 * CELL, 33 * CELL, 6 * CELL, 3 * CELL)
    s.path_rect(*rect(30, 30, 4, 18))   # from the gate to the main street
    s.path_rect(*rect(6, 26, 52, 4))    # the main street
    s.path_rect(*rect(53, 6, 4, 24))    # up to the stairs
    s.path_rect(*rect(17, 17, 3, 9))    # from the main street north to the foot of the west ramp
    s.paint_paths()
    s.pond(160, 192, 96, 72)            # the lake (north-west)
    s.lilies([(120, 170), (196, 150), (216, 214), (96, 214), (172, 140)])
    s.pier(144, 196, 32, 84)            # from the south shore into the lake
    s.fence(*rect(6, 37, 18, 1)[:2], 18 * CELL)
    s.fence(*rect(40, 37, 18, 1)[:2], 18 * CELL)
    for cx, cy in ((26, 19), (42, 20), (13, 41), (8, 21), (46, 41), (56, 32)):
        s.rock(cx * CELL, cy * CELL, 32, 28)
    bands = s.plateau((448, 96, 224, 144), True, True)  # the plateau: top 448..672 x 96..240, cliff foot at y 272
    s.border([gate])
    s.stairs(*rect(52, 3, 6, 3), True)
    s.pad(*rect(29, 45, 6, 2), False)                       # the way out to Central Park, south
    exit_park = rect(29, 45, 6, 2)
    stairs_rect = rect(52, 4, 6, 2)
    fish = {"pier": (144, 196, 32, 28), "east": (262, 160, 28, 64), "south": (88, 262, 48, 28)}
    safe = [rect(24, 41, 16, 7), rect(48, 2, 12, 8), (136, 190, 56, 100), fish["east"], fish["south"]]
    s.make_areas((2 * CELL, 2 * CELL, 60 * CELL, 44 * CELL), safe)
    start = (512, 656)
    npcs = {"ranger": (592, 520), "navi-lake": (212, 276), "navi-hill": (560, 128)}
    trees = [(520, 150), (610, 190), (150, 520), (780, 360), (400, 640)]
    for label, (x, y) in list(npcs.items()) + [("tree%d" % k, t) for k, t in enumerate(trees)]:
        if not s.open_at(x, y, 10):
            raise SystemExit("lower: %s at (%d, %d) is not on open floor" % (label, x, y))
    targets = {"exit to Central Park": exit_park, "stairs up": stairs_rect, "plateau top (over the ramps)": (500, 120, 16, 16),
               "fishing pier": fish["pier"], "fishing east shore": fish["east"], "fishing south shore": fish["south"],
               "ranger": (570, 510, 8, 8)}
    n = s.reach(start, targets)
    s.rects = {"park": exit_park, "up": stairs_rect, "fish": fish}
    s.bands, s.npcs, s.trees = bands, npcs, trees
    return s, start, n


def build_upper():
    s = Stage("upper", 768, 512, UPPER, 2)
    s.grass()
    s.tall_grass(38 * CELL, 9 * CELL, 6 * CELL, 4 * CELL)
    s.tall_grass(36 * CELL, 22 * CELL, 5 * CELL, 3 * CELL)
    s.path_rect(*rect(4, 22, 4, 8))     # from the stairs
    s.path_rect(*rect(4, 22, 20, 3))    # east to the ring
    s.path_ellipse(24 * CELL, 14 * CELL, 8 * CELL, 5 * CELL, ring=(6 * CELL, 4 * CELL))  # the training ring: a band of path
    s.path_rect(*rect(23, 18, 3, 5))    # connects the street with the ring
    s.paint_paths()
    s.pond(10 * CELL, 8 * CELL, 4 * CELL, 2 * CELL)
    s.fence(*rect(12, 26, 14, 1)[:2], 14 * CELL)
    for cx, cy in ((17, 13), (29, 12), (23, 8), (40, 15), (14, 19)):
        s.rock(cx * CELL, cy * CELL, 32, 28)
    s.border([])
    s.stairs(*rect(5, 26, 6, 3), False)   # stairs down, over the south trees' edge
    stairs_rect = rect(5, 27, 6, 2)
    s.make_areas((2 * CELL, 2 * CELL, 44 * CELL, 28 * CELL), [rect(2, 20, 12, 10)])
    start = (7 * CELL + 8, 24 * CELL)
    npcs = {"card": (176, 334), "duel": (384, 224), "navi-ring": (440, 336)}
    trees = [(96, 200), (330, 100), (470, 340), (700, 300)]
    for label, (x, y) in list(npcs.items()) + [("tree%d" % k, t) for k, t in enumerate(trees)]:
        if not s.open_at(x, y, 10):
            raise SystemExit("upper: %s at (%d, %d) is not on open floor" % (label, x, y))
    n = s.reach(start, {"stairs down": stairs_rect, "card tamer": (160, 340, 8, 8), "duel tamer": (370, 250, 8, 8)})
    s.rects = {"down": stairs_rect}
    s.npcs, s.trees = npcs, trees
    return s, start, n


# ---------------------------------------------------------------------------------------------- encounters
FIGHT_STAGE = 1          # the battle arena of Central Park's own random battles (data/stages/s330/stage.json battles)
FIGHT_MUSIC = 1611137024  # its battle music
CARDMON = {"paralysis": 1, "confusion": 1, "sleep": 1, "steal": 1}  # the flags of the disc's Cardmon rows (encounters 327, 328)
DUEL = {"confusion": 1, "sleep": 1, "knockOut": 1, "runAway": 1}   # the flags of the Seiryu Leader's trio (encounter 3): no running away


def fighter(enemy, level, hp=None, mp=9999):
    """An enemy of a row: PS = 24 x level + 120 unless said (the disc's random rows), strength 16 (README: Encuentros nuevos)."""
    return {"enemy": enemy, "level": level, "hp": 24 * level + 120 if hp is None else hp, "mp": mp, "strength": 16}


def row(enemies, ambush=8, kind=1, flags=None):
    """A row: the rewards are the disc's formulas, summed over the enemies (EXP 5 x level, BIT 10 x level, Digimon EXP = level)."""
    lv = [e["level"] for e in enemies]
    out = {"enemies": enemies, "ambushChance": ambush, "kind": kind}
    for k, v in (flags or {}).items():
        out[k] = v
    out["rewards"] = {"money": 10 * sum(lv), "exp": 5 * sum(lv), "digimonExp": sum(lv)}
    return out


def encounter_rows():
    r = {}
    # random battles on the grass (T6): lower floor levels 8 to 11, upper 13 to 16
    r["lower-coelamon"] = row([fighter(49, 8)])
    r["lower-woodmon"] = row([fighter(222, 9)])
    r["lower-apemon"] = row([fighter(207, 10)])
    r["lower-triceramon"] = row([fighter(203, 10, hp=540)], ambush=4)
    r["lower-pair"] = row([fighter(132, 10), fighter(197, 11)])
    r["lower-minotarumon"] = row([fighter(172, 11)])
    r["upper-tyrannomon"] = row([fighter(8, 13)])
    r["upper-apemon"] = row([fighter(207, 14)])
    r["upper-redvegiemon"] = row([fighter(77, 15)])
    r["upper-shogungekomon"] = row([fighter(119, 16, mp=200)])
    r["upper-trio"] = row([fighter(172, 13), fighter(8, 14), fighter(207, 15)], ambush=4)
    # kick trees (T11): Cardmon plus other Digimon of each floor's band
    r["tree-cardmon"] = row([fighter(457, 9)], kind=5, flags=CARDMON)
    r["tree-mojyamon"] = row([fighter(52, 9)])
    r["tree-vegiemon"] = row([fighter(25, 10)])
    r["tree-numemon"] = row([fighter(11, 8)])
    r["tree-giromon"] = row([fighter(41, 11)])
    r["tree-mamemon"] = row([fighter(13, 11)])
    r["tree-upper-cardmon"] = row([fighter(458, 14)], kind=5, flags=CARDMON)
    r["tree-kokatorimon"] = row([fighter(50, 14)])
    r["tree-gekomon"] = row([fighter(110, 13)])
    r["tree-sukamon"] = row([fighter(39, 13)])
    r["tree-flarerizamon"] = row([fighter(69, 16)])
    r["tree-bluemeramon"] = row([fighter(104, 16)])
    # fishing in the lake: aquatic Digimon (family 10 in content/digimon.json) and the Cardmon of that family
    r["lake-betamon"] = row([fighter(4, 8)])
    r["lake-gizamon"] = row([fighter(132, 9)])
    r["lake-cardmon"] = row([fighter(460, 10)], kind=5, flags=CARDMON)
    r["lake-shellmon"] = row([fighter(35, 10)])
    r["lake-tortomon"] = row([fighter(208, 11)])
    r["lake-dolphmon"] = row([fighter(210, 11)])
    r["lake-octomon"] = row([fighter(221, 11)])
    # the duel with the champion of the ring: the upper floor's strongest trio, no running away
    r["duel-trio"] = row([fighter(8, 15), fighter(207, 16), fighter(77, 15)], ambush=0, kind=2, flags=DUEL)
    return r


def result(name, weight=None):
    out = {"encounter": "%s:%s" % (NAME, name), "stage": FIGHT_STAGE, "music": FIGHT_MUSIC}
    if weight and weight > 1:
        out["weight"] = weight
    return out


# ---------------------------------------------------------------------------------------------- dialogs
def mv(name, op, n):
    return ["mod-var", "%s:%s" % (NAME, name), op, n]


def mf(name, value=1):
    return ["mod-flag", "%s:%s" % (NAME, name), value]


def talk(text, conditions=None, actions=None):
    t = {"text": "%s:%s" % (NAME, text)}
    if conditions:
        t["conditions"] = conditions
    if actions:
        t["actions"] = actions
    return t


# the card battle against opponent 0x13: the port records the result the moment the battle ends in the variable dw3:card-won/13 (shim/ext_state.h), which
# outlives the game's temporary flags 00:10/00:11 (they are cleared when the player leaves the stage), so the quest does not depend on talking to the Tamer
# again before leaving
CARD_WON = ["mod-flag", "dw3:card-won/13", 1]
# the original's "item received" signal: the last action of a reward talk, after its item (docs/modding/README.md)
ITEM_RECEIVED = ["special", "0x13", 1]
SURVEY_ALL = [mf("seen-lake"), mf("seen-hill"), mf("seen-ring"), CARD_WON]
ITEM_TREE_BOOTS, ITEM_POLE, ITEM_REWARD = "dw3:item/4", "dw3:item/5", "dw3:item/47"
# game flags a card battle leaves (src/engine/game/events.c:739-748): 00:11 = a card battle just ended, 00:10 = the player won it
CARD_WON_FLAGS = [["flag", "00:10", 1], ["flag", "00:11", 1]]
CARD_LOST_FLAGS = [["flag", "00:10", 0], ["flag", "00:11", 1]]


def ranger_talks():
    return [
        talk("ranger-done", [mv("quest", ">=", 2)]),
        talk("ranger-complete", [mv("quest", ">=", 1)] + SURVEY_ALL,
             [["give-money", 300], ["item", ITEM_REWARD, 1], ["set-var", "%s:quest" % NAME, 2], ITEM_RECEIVED]),
        talk("ranger-progress", [mv("quest", ">=", 1)]),
        talk("ranger-offer-tools", [["item", ITEM_TREE_BOOTS, 0]],
             [["item", ITEM_TREE_BOOTS, 1], ["item", ITEM_POLE, 1], ["set-var", "%s:quest" % NAME, 1], ["sound", "select"]]),
        talk("ranger-offer", None, [["set-var", "%s:quest" % NAME, 1], ["sound", "select"]]),
    ]


def navi_talks(text, seen):
    return [talk(text, [mv("quest", ">=", 1)], [mf(seen)]), talk(text)]


def setv(name, n):
    return ["set-var", "%s:%s" % (NAME, name), n]


# Rematches (docs/stages.md section 17). The `ask` action shows the game's own Yes/No panel after the talk's line and writes the answer into a variable
# (1 yes, 2 no); the character's talks are then chosen again, so the next talk reads the answer like any other condition.
ASK = ["ask", "%s:rematch" % NAME]


def card_talks():
    return [
        talk("card-lost", CARD_WON_FLAGS, [["flag", "00:11", 0], ["flag", "00:10", 0]]),
        talk("card-won", CARD_LOST_FLAGS, [["flag", "00:11", 0]]),
        talk("card-rematch-go", [mv("rematch", "==", 1)], [setv("rematch", 0), ["card-battle", "13:0", 1]]),
        talk("card-rematch-no", [mv("rematch", "==", 2)], [setv("rematch", 0)]),
        talk("card-again", [CARD_WON], [ASK]),
        talk("card-offer", None, [["card-battle", "13:0", 1]]),
    ]


# the duel: dw3:battle-won/training-field/duel-trio is set by the port when the battle that `start-battle` began is won (shim/ext_state.h; a lost battle goes to
# the title screen, so there is nothing to read for it). A rematch clears it first, so it tells whether THAT fight was won.
DUEL_WON = "dw3:battle-won/%s/duel-trio" % NAME
DUEL_PLAYED = "dw3:battle-played/%s/duel-trio" % NAME


def duel_talks():
    start = ["start-battle", "%s:duel-trio" % NAME, FIGHT_STAGE, FIGHT_MUSIC]
    return [
        talk("duel-rematch-go", [mv("rematch", "==", 1)], [["mod-flag", DUEL_WON, 0], setv("rematch", 3), start]),
        talk("duel-rematch-no", [mv("rematch", "==", 2)], [setv("rematch", 0)]),
        talk("duel-rematch-won", [mv("rematch", "==", 3), ["mod-flag", DUEL_WON, 1]], [setv("rematch", 0), ITEM_RECEIVED]),
        talk("duel-rematch-fled", [mv("rematch", "==", 3)], [setv("rematch", 0)]),
        talk("duel-after", [mf("duel-paid")], [ASK]),
        talk("duel-reward", [mf("duel-started"), ["mod-flag", DUEL_WON, 1]], [["give-money", 500], mf("duel-paid"), ITEM_RECEIVED]),
        talk("duel-fled", [mf("duel-started"), ["mod-flag", DUEL_PLAYED, 1]], [mf("duel-started", 0)]),
        talk("duel-offer", None, [mf("duel-started"), start]),
    ]


def actor(key, character, pos, direction, talks):
    """character: a number of the game's characters, or the name of a sprite of this mod (sprites/<name>/, docs/stages.md section 11)."""
    who = {"sprite": character} if isinstance(character, str) else {"character": character}
    return dict({"key": key}, **who, x=pos[0], y=pos[1], dir=direction, talks=talks)


# ---------------------------------------------------------------------------------------------- stage.json
LOWER_BATTLES = [
    {"area": 1, "rate": 3, "slots": [  # the grass: levels 8 to 11, a pair now and then
        "lower-coelamon", "lower-woodmon", "lower-triceramon", "lower-coelamon",
        "lower-apemon", "lower-woodmon", "lower-pair", "lower-triceramon"]},
    {"area": 2, "rate": 4, "slots": [  # the tall grass: a little harder and more often
        "lower-minotarumon", "lower-apemon", "lower-pair", "lower-minotarumon",
        "lower-triceramon", "lower-pair", "lower-minotarumon", "lower-woodmon"]},
]
UPPER_BATTLES = [
    {"area": 1, "rate": 3, "slots": [  # levels 13 to 16
        "upper-tyrannomon", "upper-apemon", "upper-redvegiemon", "upper-tyrannomon",
        "upper-apemon", "upper-redvegiemon", "upper-shogungekomon", "upper-trio"]},
    {"area": 2, "rate": 4, "slots": [
        "upper-trio", "upper-shogungekomon", "upper-trio", "upper-tyrannomon",
        "upper-apemon", "upper-shogungekomon", "upper-trio", "upper-redvegiemon"]},
]


def battles_json(table):
    out = []
    for area in table:
        out.append({
            "area": area["area"], "rate": area["rate"],
            "slots": [{"encounter": "training-field:" + name, "stage": FIGHT_STAGE, "music": FIGHT_MUSIC} for name in area["slots"]],
        })
    return out


def rect_json(r):
    return {"x": r[0], "y": r[1], "w": r[2], "h": r[3]}


# where the bobber of the fishing gauge is drawn from the player, by the way the player faces (the disc's gauge slots use these:
# wstag716.c:231-233 (72, 0), (72, -36), (0, -56); wstag351.c:397-398 (0, 80), (-48, 56)); u16 for a negative number
FISH_OFFSET = {2: (-72, 0), 4: (0, -56), 6: (72, 0), 0: (0, 80)}


def gauge_slot(key, area, facing):
    dx, dy = FISH_OFFSET[facing]
    return {"key": key, "type": "gauge", "arg": dx & 0xFFFF, "x": dy & 0xFFFF, "area": dict(rect_json(area), facing=facing)}


def stage_json(kind, start, s):
    doc = {
        "$schema": "../../../../../../docs/modding/schema/stage-new.schema.json",
        "format": "stage-new-v1",
        "about": None,
        "fallback": {"stage": 330, "x": 952, "y": 412, "dir": 0},
        "start": {"x": start[0], "y": start[1], "dir": 4},
        "textFile": 211,
        "soundBank": 9,
        "music": MUSIC,
        "banner": {"area": "%s:banner-area" % NAME, "place": "%s:banner-%s" % (NAME, kind)},
    }
    if kind == "lower":
        doc["about"] = ("The lower floor of the training field (1024 x 768 px, 8 x 6 tiles). Generated by tools/make_art.py together with its PNGs. "
                        "Slots: 0 'park' = the mat in the south wall, back to Central Park beside the guide; 1 'stairs-up' = the stone steps in the north-east, to the upper floor; "
                        "2 to 4 = the fishing spots of the lake (the pier and two shores). Battle areas: 1 = the grass, 2 = the tall grass; the encounters are the rows of tables/field_encounters.json (levels 8 to 11). "
                        "The plateau in the north has a ramp on each side: floor value 3 on the west one (walking east climbs) and 19 = 16 + 3 on the east one (mirrored: walking east goes down).")
        doc["slots"] = {"add": [
            {"key": "park", "stage": 330, "x": 952, "y": 412, "dir": 0, "area": rect_json(s.rects["park"])},
            {"key": "stairs-up", "stage": "training-field:upper", "x": 7 * CELL + 8, "y": 24 * CELL + 8, "dir": 4, "area": rect_json(s.rects["up"])},
            gauge_slot("fish-pier", s.rects["fish"]["pier"], 4),
            gauge_slot("fish-east", s.rects["fish"]["east"], 2),
            gauge_slot("fish-south", s.rects["fish"]["south"], 4),
        ]}
        doc["battles"] = {"add": battles_json(LOWER_BATTLES)}
        n = s.npcs
        doc["actors"] = {"add": [
            actor("ranger", 69, n["ranger"], 2, ranger_talks()),
            actor("navi-lake", "sign", n["navi-lake"], 0, navi_talks("navi-lake", "seen-lake")),
            actor("navi-hill", "sign", n["navi-hill"], 0, navi_talks("navi-hill", "seen-hill")),
        ]}
        doc["fishing"] = {
            "default": {"1": [result("lake-betamon", 3), result("lake-gizamon", 3), result("lake-cardmon", 2), {"item": "dw3:item/43"}, {"nothing": True}],
                        "2": [result("lake-shellmon", 3), result("lake-tortomon", 2), result("lower-coelamon", 2)]},
            "slots": [{"key": "fish-pier",
                       "1": [result("lake-gizamon", 2), result("lake-cardmon", 2), result("lake-shellmon", 2)],
                       "2": [result("lake-dolphmon", 3), result("lake-octomon", 2), result("lake-tortomon", 1)]}],
        }
        doc["hiddenSpots"] = {
            "add": [{"x": x, "y": y} for x, y in s.trees],
            "prize": {"common": [result("tree-cardmon", 3), result("tree-mojyamon", 2), result("tree-vegiemon", 2), result("tree-numemon", 1)],
                      "rare": [result("tree-giromon", 1), result("tree-mamemon", 1)]},
        }
    else:
        doc["about"] = ("The upper floor of the training field (768 x 512 px, 6 x 4 tiles). Generated by tools/make_art.py together with its PNGs. Slot 0 'stairs-down' = the stone steps in the south-west, back to the lower floor. "
                        "Battle areas: 1 = the grass, 2 = the tall grass; the encounters are the rows of tables/field_encounters.json (levels 13 to 16). "
                        "Characters: a card player (card battle against opponent 0x13, level 15), the champion of the ring (a once-per-save duel with its own trio) and an info Navi.")
        doc["slots"] = {"add": [
            {"key": "stairs-down", "stage": "training-field:lower", "x": 880, "y": 132, "dir": 0, "area": rect_json(s.rects["down"])},
        ]}
        doc["battles"] = {"add": battles_json(UPPER_BATTLES)}
        n = s.npcs
        doc["actors"] = {"add": [
            actor("card-tamer", 51, n["card"], 0, card_talks()),
            actor("ring-champion", 57, n["duel"], 0, duel_talks()),
            actor("navi-ring", "sign", n["navi-ring"], 0, navi_talks("navi-ring", "seen-ring")),
        ]}
        doc["hiddenSpots"] = {
            "add": [{"x": x, "y": y} for x, y in s.trees],
            "prize": {"common": [result("tree-upper-cardmon", 3), result("tree-kokatorimon", 2), result("tree-gekomon", 2), result("tree-sukamon", 1)],
                      "rare": [result("tree-flarerizamon", 1), result("tree-bluemeramon", 1)]},
        }
    return doc


# ---------------------------------------------------------------------------------------------- texts (named lines, docs/stages.md section 8)
# English is required; es-419 (neutral Latin American Spanish, localization/es-419/GLOSSARY.md: "Tamer", tú) and spa (Spain) are optional.
# The lines are original: nothing here comes from the game's text (ADR-001).
TEXTS = {
    # the area name banner shown when a floor is entered (stage.json "banner"): the big line, then the floor
    "banner-area": {"eng": "Training Field", "es-419": "Campo de Entrenamiento", "spa": "Campo de Entrenamiento"},
    "banner-lower": {"eng": "Lower Floor", "es-419": "Planta Baja", "spa": "Planta Baja"},
    "banner-upper": {"eng": "Upper Floor", "es-419": "Planta Alta", "spa": "Planta Alta"},
    "locked": {
        "eng": "Looking for a good place to train? Not yet, Tamer. Beat the Seiryu Leader first, then come back and I will take you there.",
        "es-419": "¿Buscas un buen lugar para entrenar? Todavía no, Tamer. Primero vence al Líder Seiryu y luego vuelve; yo te llevaré.",
        "spa": "¿Buscas un buen lugar para entrenar? Todavía no, Domador. Primero vence al Líder Seiryu y luego vuelve; yo te llevaré.",
    },
    "offer": {
        "eng": "So you beat the Seiryu Leader! Then you are ready for the Training Field. Hold on tight, here we go!",
        "es-419": "¡Venciste al Líder Seiryu! Entonces ya estás listo para el Campo de Entrenamiento. ¡Sujétate fuerte, allá vamos!",
        "spa": "¡Venciste al Líder Seiryu! Entonces ya estás listo para el Campo de Entrenamiento. ¡Sujétate fuerte, allá vamos!",
    },
    "ranger-offer-tools": {
        "eng": "Welcome, Tamer! I am the ranger here. Read the signs at the lake, on the hill and upstairs, and beat the card player up there. Report back to me. Take these tools, on the house!",
        "es-419": "¡Bienvenido, Tamer! Soy el guardabosques de aquí. Lee los letreros del lago, de la colina y del piso de arriba, y vence al jugador de cartas de allá. Luego vuelve conmigo. ¡Toma estas herramientas, van por la casa!",
        "spa": "¡Bienvenido, Domador! Soy el guarda de aquí. Lee los carteles del lago, de la colina y de la planta de arriba, y gana al jugador de cartas de allí. Luego vuelve conmigo. ¡Toma estas herramientas, invita la casa!",
    },
    "ranger-offer": {
        "eng": "Welcome, Tamer! I am the ranger here. Read the signs at the lake, on the hill and upstairs, and beat the card player up there. Then report back to me.",
        "es-419": "¡Bienvenido, Tamer! Soy el guardabosques de aquí. Lee los letreros del lago, de la colina y del piso de arriba, y vence al jugador de cartas de allá. Luego vuelve conmigo.",
        "spa": "¡Bienvenido, Domador! Soy el guarda de aquí. Lee los carteles del lago, de la colina y de la planta de arriba, y gana al jugador de cartas de allí. Luego vuelve conmigo.",
    },
    "ranger-progress": {
        "eng": "How is the survey going? Read the signs at the lake, on the hill and upstairs, and beat the card player up there.",
        "es-419": "¿Cómo va el recorrido? Lee los letreros del lago, de la colina y del piso de arriba, y vence al jugador de cartas de allá.",
        "spa": "¿Cómo va el recorrido? Lee los carteles del lago, de la colina y de la planta de arriba, y gana al jugador de cartas de allí.",
    },
    "ranger-complete": {
        "eng": "You read every sign and beat the card player! Here is your reward, Tamer. The field is yours to train in!",
        "es-419": "¡Leíste todos los letreros y venciste al jugador de cartas! Aquí tienes tu recompensa, Tamer. ¡El campo es tuyo para entrenar!",
        "spa": "¡Has leído todos los carteles y has ganado al jugador de cartas! Aquí tienes tu recompensa, Domador. ¡El campo es tuyo para entrenar!",
    },
    "ranger-done": {
        "eng": "Thanks again, Tamer. Keep training, and watch out for the tall grass!",
        "es-419": "Gracias otra vez, Tamer. Sigue entrenando y cuidado con el pasto alto.",
        "spa": "Gracias otra vez, Domador. Sigue entrenando y ten cuidado con la hierba alta.",
    },
    "navi-lake": {
        "eng": "This is Lake Meadow. You can fish from the pier and from the shores. Bring a Fishing Pole!",
        "es-419": "Esta es la Pradera del Lago. Puedes pescar desde el muelle y desde las orillas. ¡Trae una Caña de pescar!",
        "spa": "Esta es la Pradera del Lago. Puedes pescar desde el muelle y desde las orillas. ¡Trae una caña de pescar!",
    },
    "navi-hill": {
        "eng": "This is the Hilltop. Mind the trees up here: one of them hides a Digimon. Kick them to find out!",
        "es-419": "Esta es la Cima de la Colina. Cuidado con los árboles de aquí: uno esconde un Digimon. ¡Patéalos para averiguarlo!",
        "spa": "Esta es la Cima de la Colina. Cuidado con los árboles de aquí: uno esconde un Digimon. ¡Pátealos para averiguarlo!",
    },
    "navi-ring": {
        "eng": "This is the Training Ring. Tamers who want a card game or a duel can ask the ones standing around.",
        "es-419": "Este es el Anillo de Entrenamiento. Los Tamers que quieran un juego de cartas o un duelo pueden pedírselo a los que están por aquí.",
        "spa": "Este es el Anillo de Entrenamiento. Los Domadores que quieran una partida de cartas o un duelo pueden pedírselo a los que están por aquí.",
    },
    "card-offer": {
        "eng": "A card game, Tamer? I will not go easy on you!",
        "es-419": "¿Un juego de cartas, Tamer? ¡No me voy a contener!",
        "spa": "¿Una partida de cartas, Domador? ¡No voy a ser blando contigo!",
    },
    "card-lost": {
        "eng": "You beat me! Come back for a rematch whenever you like.",
        "es-419": "¡Me venciste! Vuelve a buscar la revancha cuando quieras.",
        "spa": "¡Me has ganado! Vuelve a por la revancha cuando quieras.",
    },
    "card-won": {
        "eng": "Ha! I won this time. Try again whenever you like.",
        "es-419": "¡Ja! Esta vez gané yo. Inténtalo otra vez cuando quieras.",
        "spa": "¡Ja! Esta vez he ganado yo. Inténtalo otra vez cuando quieras.",
    },
    "duel-offer": {
        "eng": "I am the champion of this ring. My three Digimon against yours, and no running away. Ready?",
        "es-419": "Soy el campeón de este anillo. Mis tres Digimon contra los tuyos, y nadie huye. ¿Listo?",
        "spa": "Soy el campeón de este anillo. Mis tres Digimon contra los tuyos, y nadie huye. ¿Listo?",
    },
    "duel-reward": {
        "eng": "You beat all three of my Digimon! What a fight! Here is the prize money. The ring is yours, champion.",
        "es-419": "¡Venciste a mis tres Digimon! ¡Qué pelea! Aquí tienes el dinero del premio. El anillo es tuyo, campeón.",
        "spa": "¡Has vencido a mis tres Digimon! ¡Qué pelea! Aquí tienes el dinero del premio. El anillo es tuyo, campeón.",
    },
    "duel-after": {
        "eng": "You are the champion now. Fancy a rematch against my three Digimon?",
        "es-419": "Ahora el campeón eres tú. ¿Quieres una revancha contra mis tres Digimon?",
        "spa": "Ahora el campeón eres tú. ¿Quieres una revancha contra mis tres Digimon?",
    },
    "duel-fled": {
        "eng": "Running away from the ring? Come back when you are ready to fight.",
        "es-419": "¿Huyendo del anillo? Vuelve cuando estés listo para pelear.",
        "spa": "¿Huyendo del anillo? Vuelve cuando estés listo para pelear.",
    },
    "duel-rematch-go": {
        "eng": "That is the spirit! Here we go!",
        "es-419": "¡Ese es el espíritu! ¡Allá vamos!",
        "spa": "¡Ese es el espíritu! ¡Allá vamos!",
    },
    "duel-rematch-no": {
        "eng": "No problem. Come back whenever you want to fight again.",
        "es-419": "Sin problema. Vuelve cuando quieras pelear otra vez.",
        "spa": "Sin problema. Vuelve cuando quieras pelear otra vez.",
    },
    "duel-rematch-won": {
        "eng": "You did it again! The ring is still yours, champion.",
        "es-419": "¡Lo hiciste otra vez! El anillo sigue siendo tuyo, campeón.",
        "spa": "¡Lo has hecho otra vez! El anillo sigue siendo tuyo, campeón.",
    },
    "duel-rematch-fled": {
        "eng": "Leaving in the middle of the fight? The ring will wait for you.",
        "es-419": "¿Te vas a media pelea? El anillo te esperará.",
        "spa": "¿Te vas a media pelea? El anillo te esperará.",
    },
    "card-again": {
        "eng": "Good game, Tamer! Do you want a rematch?",
        "es-419": "¡Buena partida, Tamer! ¿Quieres la revancha?",
        "spa": "¡Buena partida, Domador! ¿Quieres la revancha?",
    },
    "card-rematch-go": {
        "eng": "Here we go, then! Cards on the table!",
        "es-419": "¡Allá vamos, entonces! ¡Cartas sobre la mesa!",
        "spa": "¡Allá vamos, entonces! ¡Cartas sobre la mesa!",
    },
    "card-rematch-no": {
        "eng": "All right. I will be here whenever you feel like it.",
        "es-419": "Está bien. Aquí estaré cuando quieras.",
        "spa": "Vale. Aquí estaré cuando quieras.",
    },
}
SIGN_HEADER = "{speaker-of:TALK04:19}"
TEXT_ABOUT = {
    "eng": "The lines of the Training Field's characters (named texts, docs/stages.md section 8). English is required.",
    "es-419": "Las líneas de los personajes del Campo de Entrenamiento (es-419, texto propio de este mod).",
    "spa": "Las líneas de los personajes del Campo de Entrenamiento (español de España, texto propio de este mod; el es-419 tiene el suyo).",
}


def write_texts(base):
    for lang in ("eng", "es-419", "spa"):
        folder = os.path.join(base, "text", lang)
        os.makedirs(folder, exist_ok=True)
        # the signs' lines start with the game's own sign header (TALK04 entry 19, read from the player's disc at run time; docs/stages.md section 8)
        doc = {"format": "stage-text-v1", "about": TEXT_ABOUT[lang],
               "texts": {k: (SIGN_HEADER + v[lang] if k.startswith("navi-") else v[lang]) for k, v in sorted(TEXTS.items())}}
        with open(os.path.join(folder, "stages.json"), "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False)
            f.write("\n")


def write_tables(base):
    folder = os.path.join(base, "tables")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "field_encounters.json"), "w", encoding="utf-8") as f:
        json.dump({"format": "field_encounters-v1", "rows": {"%s:%s" % (NAME, k): v for k, v in encounter_rows().items()}}, f, indent=2, ensure_ascii=False)
        f.write("\n")




def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(MOD, "stages")
    for kind, builder in (("lower", build_lower), ("upper", build_upper)):
        s, start, n = builder()
        folder = os.path.join(out, kind)
        s.save(folder)
        with open(os.path.join(folder, "stage.json"), "w", encoding="utf-8") as f:
            json.dump(stage_json(kind, start, s), f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("%s: %d x %d px, %d colours, %d walkable 8 px blocks reached from the start %s" % (kind, s.w, s.h, len(s.pal.colors), n, start))
    base = os.path.dirname(os.path.abspath(out))
    write_tables(base)
    write_texts(base)
    write_sign(base)


if __name__ == "__main__":
    main()
