#!/usr/bin/env python3
"""Checks the country-select art in VRAM and the left panel's sprite.

Runs pc/build/boot with the art diagnostic armed (pc/boot.c's fourth argument,
pc/shim/gfx_engine.c's pcBootArtInit): the game's own main() runs, the country
select uploads its art through its own TIM loader, and at the armed frame the
harness reads the VRAM regions back with PsyCross's GR_ReadVRAM and prints them
together with the frame's sprites and DR_TPAGEs.

Three things are verified, all against an independent read of the same disc:

  * the VRAM region: this check parses CNTSELWN.BIN (file 0x8A3) on its own --
    the bank's two entries, the RLEN packing and the TIM's own CLUT and image
    blocks -- and compares the FNV-1a checksum of each block with the checksum
    the harness read back from VRAM, at the addresses country_select.c asks for
    (`setImagePos(0x280, 0)`, `setClutPos(0, 0x1F0)`);
  * the left panel's sprite: the harness prints every SPRT of the frame with
    its UV, size and CLUT, and this check recomputes them from the bank's own
    part table (the mode-1 part of frame 3, SPRITE_LEFT_PANEL) and the drawer's
    coordinates (setTexture(0x280, 0), setAltClut(0, 0x1F0), drawn at 0, 158);
  * the texture page that governs it: the same part's `getTPage(1, 0, 0x280 +
    u/2, 0)` must be the DR_TPAGE the chain walks just before that sprite.

The image is the owner's own file: it is only read and nothing of it is written
into the repository (ADR-001). The run opens the PsyCross window, so it needs a
display.

    make -C pc
    .venv/bin/python pc/check_country.py <disc .bin>

Exits non-zero on the first failing case and reports each one.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "es419"))

import patch_disc  # noqa: E402
import menu_bank  # noqa: E402

TOOL = ROOT / "pc" / "build" / "boot"

# The screen's bank on the European disc (country_select.h's CNTY_SEL_FILE).
BANK_PATH = "AAA/DAT/COUNTRY/CNTSELWN.BIN"
BANK_INDEX = 0x8A3

# Where country_select.c's TASK_INIT sends the bank's art (its own TIM loader),
# read back with GR_ReadVRAM.
IMAGE_X = 0x280
IMAGE_Y = 0
CLUT_X = 0
CLUT_Y = 0x1F0

# The frame the diagnostic is armed on: the panels are open and their art is up.
ART_FRAME = 40
FRAMES = ART_FRAME + 2

# The left panel's task (country_select.c): SPRITE_LEFT_PANEL of the bank, drawn
# at (0, 158) by a drawer with setTexture(0x280, 0) and setAltClut(0, 0x1F0).
LEFT_PANEL_FRAME = 3
LEFT_PANEL_X = 0
LEFT_PANEL_Y = 158
TPAGE_X = 0x280
TPAGE_Y = 0
ALT_CLUT_X = 0
ALT_CLUT_Y = 0x1F0

# spriteDrawerSetAltClut stores y - 0x100 (sprite_drawer.c), and the part's
# mode-1 CLUT row is added on top of it.
ALT_CLUT_BIAS = 0x100

LINE_VRAM = re.compile(r"art vram (\d+),(\d+) (\d+)x(\d+) \((\d+) bytes\) sum=([0-9a-f]+)")
LINE_TPAGE = re.compile(r"art tpage code=([0-9a-f]+) tpage=(\d+)")
LINE_SPRITE = re.compile(r"art sprite u=(-?\d+) v=(-?\d+) w=(\d+) h=(\d+) "
                         r"clut=(\d+) at (-?\d+),(-?\d+)")


def fnv1a(data):
    """FNV-1a 32-bit, the checksum the harness prints (pc/shim/gfx_engine.c)."""
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def u32(data, at):
    return int.from_bytes(data[at:at + 4], "little")


def u16(data, at):
    return int.from_bytes(data[at:at + 2], "little")


def s16(data, at):
    return int.from_bytes(data[at:at + 2], "little", signed=True)


def read_sectors(image, lba, count):
    """COUNT sectors of the open image, the 2048 data bytes of each."""
    out = bytearray()
    for i in range(count):
        image.seek((lba + i) * patch_disc.SECTOR + patch_disc.USER_DATA)
        out += image.read(patch_disc.DATA_SIZE)
    return bytes(out)


def rlen_decode(data):
    """The game's RLEN, as timLoaderLoadArchive and decompressor.c unpack it:
    a byte n < 0x80 copies the next n bytes, n | 0x80 repeats the next byte n
    times, a 0 ends. Returns (declared size, unpacked bytes)."""
    assert data[:4] == b"RLEN", f"not RLEN data: {data[:4]!r}"
    size = u32(data, 4)
    at = 8
    out = bytearray()
    while at < len(data) and data[at] != 0:
        code = data[at]
        if code & 0x80:
            out += bytes([data[at + 1]]) * (code & 0x7F)
            at += 2
        else:
            out += data[at + 1:at + 1 + code]
            at += 1 + code
    return size, bytes(out)


def get_clut(x, y):
    """PsyCross's getClut(x, y) (libgpu.h)."""
    return ((y << 6) | ((x >> 4) & 0x3F)) & 0xFFFF


def get_tpage(tp, abr, x, y):
    """PsyCross's getTPage(tp, abr, x, y) (libgpu.h)."""
    return (((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4)
            | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2)) & 0xFFFF


def parse_bank(data):
    """The bank's art, as tools/es419/menu_bank.py documents it: the sprite
    sheet (parts and frames) and the one RLEN-packed 8-bit TIM. Returns the
    parts, the frames, the TIM's CLUT and the TIM's image."""
    sheet_off, tim_off = int.from_bytes(data[0:4], "little"), int.from_bytes(data[4:8], "little")
    sheet = data[sheet_off:tim_off]
    po, io = u32(sheet, 0), u32(sheet, 4)
    parts = [tuple(s16(sheet, po + 14 * i + 2 * k) for k in range(7))
             for i in range((io - po) // 14)]
    count = (po - 8) // 4
    frames = []
    for i in range(count):
        at = u32(sheet, 8 + 4 * i)
        items = int.from_bytes(sheet[at:at + 2], "little", signed=True)
        frames.append([tuple(s16(sheet, at + 6 + 6 * k + 2 * j) for j in range(3))
                       for k in range(items)])
    declared, tim = rlen_decode(data[tim_off + 8:])
    if declared != len(tim) or u32(tim, 0) != 0x10:
        raise ValueError("the bank's TIM is not an RLEN-packed TIM")
    clut_size, cx, cy, cw, ch = (u32(tim, 8), u16(tim, 12), u16(tim, 14),
                                 u16(tim, 16), u16(tim, 18))
    clut = tim[8 + 12:8 + clut_size]
    img_at = 8 + clut_size
    img_size, ix, iy, iw, ih = (u32(tim, img_at), u16(tim, img_at + 4),
                                u16(tim, img_at + 6), u16(tim, img_at + 8),
                                u16(tim, img_at + 10))
    image = tim[img_at + 12:img_at + img_size]
    if (cw, ch) != (256, 1) or len(clut) != 512:
        raise ValueError(f"unexpected CLUT block {cw}x{ch}")
    if (iw, ih) != (64, 256) or len(image) != iw * ih * 2:
        raise ValueError(f"unexpected image block {iw}x{ih}")
    return parts, frames, clut, image, (cx, cy), (ix, iy)


def parse(stdout):
    """Everything the harness printed, in chain order."""
    out = {"vram": [], "chain": []}
    for line in stdout.splitlines():
        m = LINE_VRAM.search(line)
        if m:
            out["vram"].append((int(m.group(1)), int(m.group(2)), int(m.group(3)),
                                int(m.group(4)), int(m.group(5)), m.group(6)))
            continue
        m = LINE_TPAGE.search(line)
        if m:
            out["chain"].append(("tpage", int(m.group(1), 16), int(m.group(2))))
            continue
        m = LINE_SPRITE.search(line)
        if m:
            out["chain"].append(("sprite",) + tuple(int(m.group(i)) for i in range(1, 8)))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin>")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(sys.argv[1])
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    with image_path.open("rb") as image:
        files = patch_disc.files(image)
        if BANK_PATH not in files:
            sys.exit(f"{BANK_PATH} is not on {image_path}")
        lba, length = files[BANK_PATH]
        bank = read_sectors(image, lba, -(-length // patch_disc.DATA_SIZE))

    failures = 0

    def fail(what, detail):
        nonlocal failures
        print(f"FAIL {what}: {detail}")
        failures += 1

    parts, frames, clut, tim_image, clut_at, image_at = parse_bank(bank)

    # What country_select.c uploads, and where: the TIM's own CLUT and image
    # blocks, sent to (0, 0x1F0) and (0x280, 0) by its own loader.
    expected_vram = [
        (IMAGE_X, IMAGE_Y, 64, 256, len(tim_image), fnv1a(tim_image), "image"),
        (CLUT_X, CLUT_Y, 256, 1, len(clut), fnv1a(clut), "CLUT"),
    ]

    # The left panel's sprite, from the bank's own part table: the mode-1 part
    # of frame 3 (SPRITE_LEFT_PANEL), at the drawer's coordinates.
    panel = None
    for pi, x, y in frames[LEFT_PANEL_FRAME]:
        if parts[pi][6] != 0:
            panel = (parts[pi], x, y)
    if panel is None:
        sys.exit("the bank's left-panel frame holds no 8-bit part")
    part, part_x, part_y = panel
    want_sprite = (0 if part[6] == 0 else part[0] & 0x7F, part[1], part[2], part[3],
                   get_clut(ALT_CLUT_X + part[4],
                            ALT_CLUT_Y - ALT_CLUT_BIAS + part[5]),
                   LEFT_PANEL_X + part_x, LEFT_PANEL_Y + part_y)
    want_tpage = get_tpage(1, 0, TPAGE_X + part[0] // 2, TPAGE_Y)

    # PsyCross writes its own <app>.log in the working directory: run in a
    # throwaway one so a check run leaves nothing behind.
    with tempfile.TemporaryDirectory() as workdir:
        proc = subprocess.run([str(TOOL), str(image_path.resolve()), str(FRAMES),
                               str(Path(workdir) / "shot.png"), str(ART_FRAME)],
                              capture_output=True, text=True, cwd=workdir)
    if proc.returncode != 0:
        sys.exit(f"boot exited {proc.returncode}: {proc.stderr.strip()}")
    got = parse(proc.stdout)

    # The VRAM: the game's own loader left the disc's bytes where it asked.
    if not got["vram"]:
        fail("the art regions", "the harness printed no `art vram` line "
                                "(was boot run with the art frame?)")
    for i, expected in enumerate(expected_vram[:len(got["vram"])]):
        region = got["vram"][i]
        want = expected[:5]
        if abs(region[0] - expected[0]) > 0 or region[2] != expected[2] or \
                region[3] != expected[3]:
            fail(f"VRAM region {i} ({expected[6]})",
                 f"the harness has {region[:4]}, {want[:4]} expected")
        elif region[5] != f"{expected[5]:08x}":
            fail(f"VRAM region {i} ({expected[6]})",
                 f"checksum {region[5]}, {expected[5]:08x} of the disc's own "
                 f"{expected[6]} bytes expected")
        else:
            print(f"ok   VRAM {region[0]},{region[1]} {region[2]}x{region[3]} "
                  f"({region[4]} bytes) is the disc's {expected[6]} byte for byte")
    if len(got["vram"]) != len(expected_vram):
        fail("the art regions", f"{len(got['vram'])} regions from the harness, "
                                f"{len(expected_vram)} expected")

    # The left panel's sprite and the DR_TPAGE that governs it: in the chain,
    # the page the drawer sets just before the sprite is the sprite's own.
    chain = got["chain"]
    found = -1
    for i, node in enumerate(chain):
        if node[0] == "sprite" and node[3:8] == want_sprite[2:7]:
            found = i
            break
    if found < 0:
        fail("the left panel's sprite",
             f"no sprite {want_sprite[2:7]} in the frame's chain")
    else:
        node = chain[found]
        got_sprite = tuple(node[1:8])
        if got_sprite != want_sprite:
            fail("the left panel's sprite", f"{got_sprite}, {want_sprite} expected")
        else:
            page = None
            for j in range(found - 1, -1, -1):
                if chain[j][0] == "tpage":
                    page = chain[j][2]
                    break
            if page != want_tpage:
                fail("the left panel's texture page",
                     f"the DR_TPAGE the chain walks before the sprite is {page}, "
                     f"{want_tpage} expected")
            else:
                print(f"ok   the left panel's sprite u={got_sprite[0]} v={got_sprite[1]} "
                      f"w={got_sprite[2]} h={got_sprite[3]} clut={got_sprite[4]} "
                      f"at {got_sprite[5]},{got_sprite[6]} is the bank's 8-bit part, "
                      f"under texture page {page}")

    if failures != 0:
        sys.exit(f"{failures} check(s) failed")
    print("the country-select art is in VRAM with the disc's own bytes and the "
          "game's own left-panel sprite points at its 8-bit part's page and CLUT")


if __name__ == "__main__":
    main()
