#!/usr/bin/env python3
"""Checks the font in VRAM and the glyphs the game's own text window draws.

Runs pc/build/font_frame over the owner's disc. That harness loads FILE_FONT
(0x287 on the European disc) with the game's own loadFont and its real TIM
loader, uploads the TIMs to VRAM through PsyCross's LoadImage, reads the region
back with GR_ReadVRAM, then gives the game's own text window a string of a disc
language file and draws it in style 1, the area banner's.

Two things are verified, both against an independent read of the same disc and
of the same C:

  * the VRAM region: this check parses FILE_FONT out of the disc on its own --
    the file's archive table, the game's RLEN decoder, the TIM blocks -- and
    compares the FNV-1a checksum of each TIM's image block with the checksum the
    harness read back from VRAM, at the address and size the loader uses
    (loadFont asks for 0x140 and timLoaderLoadArchive moves each next image 0x40
    halfwords right);
  * the glyph primitives: the harness prints every SPRT of the draw order with
    the texture page of the DR_TPAGE that governs it, and this check recomputes
    u, v, w, h, the CLUT (getClut(0x140 + clutX, clutY), the text window's
    texX/texY and palette 0) and the texture page (getTPage(0, 1, 0x140 + page *
    64, 0)) out of the European glyph table parsed from text_tools.c.

The image is the owner's own file: it is only read and nothing of it is written
into the repository (ADR-001). The run opens the PsyCross window, so it needs a
display.

    make -C pc
    .venv/bin/python pc/check_font.py <disc .bin>

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
sys.path.insert(0, str(ROOT / "pc"))

import patch_disc  # noqa: E402
import gen_table  # noqa: E402
import check_text as ct  # noqa: E402
import check_draw as cd  # noqa: E402

TOOL = ROOT / "pc" / "build" / "font_frame"

# The disc's own files: the font the game loads as FILE_FONT (0x287), and the
# language file the banner reads for the area names (the Spanish copy, which
# gen_table.spanish_index gives for its USA number).
FONT_FILE = 0x287
FONT_PATH = "AAA/DAT/COUNTRY/CMFONTTM.BIN"
LANG_PATH = "AAA/DAT/COUNTRY/SPN/SSSTNAME.BIN"
STRING = 71

# Where loadFont puts the font: (0x140, 0) and each next image 0x40 halfwords
# right (timLoaderLoadArchive). These are read back with GR_ReadVRAM.
VRAM_X = 0x140
VRAM_Y = 0
IMAGE_W = 0x40   # halfwords
IMAGE_H = 0x100  # rows

# The window's texture origin (createTextWindow's texX/texY) and its palette
# row: FONT_GLYPHS_1's CLUT columns are relative to these.
TEX_X = 0x140
TEX_Y = 0
PALETTE = 0

# The region the font's TIMs land in, and the loader's own buffer (bufferSize
# 0xA800): the first TIM's image block must fit in it.
TIM_LOADER_BUFFER = 0xA800

FRAMES = 2  # enough to exercise the frame loop; the summary counts them all

LINE_HEAD = re.compile(r"font file (\d+), (\d+) sectors, head ([0-9a-f]+)")
LINE_TEXT = re.compile(r"text file (\d+), (\d+) sectors")
LINE_VRAM = re.compile(r"vram (\d+),(\d+) (\d+)x(\d+) \((\d+) bytes\) sum=([0-9a-f]+)")
LINE_REGION = re.compile(r"vram region (\d+),(\d+) (\d+)x(\d+) \((\d+) bytes\) sum=([0-9a-f]+)")
LINE_GLYPH = re.compile(r"glyph (\d+) u=(\d+) v=(\d+) w=(\d+) h=(\d+) clut=(\d+) "
                        r"tpage=(\d+) at (-?\d+),(-?\d+)")
LINE_FRAME = re.compile(r"frame (\d+): (\d+) glyphs, (\d+) bytes")
LINE_TOTAL = re.compile(r"presented (\d+) frames, (\d+) glyph sprites")


def fnv1a(data):
    """FNV-1a 32-bit, the checksum the harness prints (pc/font_frame.c)."""
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def u32(data, at):
    return int.from_bytes(data[at:at + 4], "little")


def u16(data, at):
    return int.from_bytes(data[at:at + 2], "little")


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
    times, a 0 ends. Returns the unpacked bytes."""
    assert data[:4] == b"RLEN", f"not RLEN data: {data[:4]!r}"
    size = u32(data, 4)
    at = 8
    out = bytearray()
    while at < len(data) and data[at] != 0:
        code = data[at]
        if code & 0x80:
            n = code & 0x7F
            out += bytes([data[at + 1]]) * n
            at += 2
        else:
            out += data[at + 1:at + 1 + code]
            at += 1 + code
    return size, bytes(out)


def tim_payloads(tim):
    """The image blocks of a TIM: (mode, [(x, y, w, h, payload)]) -- the CLUT
    block, when the flag has it, and then the image."""
    assert u32(tim, 0) == 0x10, "not a TIM"
    flag = u32(tim, 4)
    at = 8
    blocks = []
    if flag & 8:
        size = u32(tim, at)
        blocks.append(("clut", u16(tim, at + 4), u16(tim, at + 6),
                       u16(tim, at + 8), u16(tim, at + 10), tim[at + 12:at + size]))
        at += size
    size = u32(tim, at)
    blocks.append(("image", u16(tim, at + 4), u16(tim, at + 6),
                   u16(tim, at + 8), u16(tim, at + 10), tim[at + 12:at + size]))
    return flag & 7, blocks


def get_clut(x, y):
    """PsyCross's getClut(x, y) (libgpu.h)."""
    return ((y << 6) | ((x >> 4) & 0x3F)) & 0xFFFF


def get_tpage(tp, abr, x, y):
    """PsyCross's getTPage(tp, abr, x, y) (libgpu.h)."""
    return (((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4)
            | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2)) & 0xFFFF


def glyph_of(kind, value):
    """The glyph textWindowDraw draws for a character, as processTextChar picks
    it: kind 0 glyphs[value - 4], kind 1 the value'th icon (or the missing
    glyph), kind 3 the missing glyph; and the missing glyph (page 0xFF) falls
    back to glyphs[0]."""
    if kind == 0:
        g = ct.GLYPHS[value - 4]
    elif kind == 1:
        g = ct.ICONS[value - 1] if 0 < value <= ct.ICON_COUNT else ct.GLYPHS[0]
    else:
        g = ct.GLYPHS[0]
    return ct.GLYPHS[0] if g[0] == 0xFF else g


def expected_glyphs(raw):
    """The SPRT fields textWindowDraw writes, in drawing order:
    (u, v, w, h, clut, tpage) per drawn character."""
    out = []
    for kind, value, _ in ct.codes(raw):
        if kind == 2:
            continue  # a control code draws nothing
        g = glyph_of(kind, value)
        out.append((g[1], g[2], g[5], g[6],
                    get_clut(TEX_X + g[3], PALETTE + TEX_Y + g[4]),
                    get_tpage(0, 1, TEX_X + (g[0] << 6), TEX_Y)))
    return out


def parse(stdout):
    """Everything the harness printed, as a dict of lists. The glyph sprites
    come between one frame's summary and the next, so they are grouped."""
    out = {"vram": [], "frames": [], "head": None, "text": None,
           "total": None, "region": None}
    glyphs = []
    for line in stdout.splitlines():
        m = LINE_HEAD.search(line)
        if m:
            out["head"] = (int(m.group(1)), int(m.group(2)), m.group(3))
            continue
        m = LINE_TEXT.search(line)
        if m:
            out["text"] = (int(m.group(1)), int(m.group(2)))
            continue
        m = LINE_VRAM.search(line)
        if m:
            out["vram"].append(tuple(int(m.group(i)) for i in range(1, 6))
                               + (m.group(6),))
            continue
        m = LINE_REGION.search(line)
        if m:
            out["region"] = tuple(int(m.group(i)) for i in range(1, 6)) + (m.group(6),)
            continue
        m = LINE_GLYPH.search(line)
        if m:
            glyphs.append(tuple(int(m.group(i)) for i in range(1, 10)))
            continue
        m = LINE_FRAME.search(line)
        if m:
            out["frames"].append((int(m.group(1)), int(m.group(2)), int(m.group(3)),
                                  glyphs))
            glyphs = []
            continue
        m = LINE_TOTAL.search(line)
        if m:
            out["total"] = (int(m.group(1)), int(m.group(2)))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin>")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(sys.argv[1])
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    lang_index = gen_table.spanish_index(gen_table.K_OF_FILE["SSSTNAME.BIN"])

    with image_path.open("rb") as image:
        files = patch_disc.files(image)
        if FONT_PATH not in files:
            sys.exit(f"{FONT_PATH} is not on {image_path}")
        lba, length = files[FONT_PATH]
        font = read_sectors(image, lba, -(-length // patch_disc.DATA_SIZE))
        ct.ON_DISC = files
        raw = ct.strings(image, LANG_PATH)[STRING]

    failures = 0

    # The font's archive, unpacked and parsed independently: getFileEntry/SIM
    # returns the file start plus its first word (the archive's own table), and
    # getArchiveEntry walks the table's offsets until one of them is zero.
    base = u32(font, 0)
    entries = []
    at = base
    while len(entries) < 64:
        offset = u32(font, at)
        if offset == 0:
            break
        entries.append(base + offset)
        at += 4

    images = []
    for number, start in enumerate(entries):
        declared, tim = rlen_decode(font[start:])
        if declared != len(tim):
            print(f"FAIL {FONT_PATH} entry {number}: {len(tim)} bytes unpacked, "
                  f"{declared} declared")
            failures += 1
            continue
        mode, blocks = tim_payloads(tim)
        image = [b for b in blocks if b[0] == "image"][0]
        _, x, y, w, h, pixels = image
        if w * h * 2 != len(pixels):
            print(f"FAIL {FONT_PATH} entry {number}: TIM {w}x{h} is {len(pixels)} "
                  f"bytes of image, not {w * h * 2}")
            failures += 1
            continue
        if len(pixels) > TIM_LOADER_BUFFER:
            print(f"FAIL {FONT_PATH} entry {number}: {len(pixels)} bytes of image, "
                  f"past the loader's {TIM_LOADER_BUFFER}-byte buffer")
            failures += 1
            continue
        images.append((number, mode, x, y, w, h, pixels))

    if not images:
        sys.exit("the font file holds no usable TIM")

    # What the loader does with them: at (0x140, 0), each next one 0x40
    # halfwords right, w halfwords a row (loadFont, timLoaderLoadArchive).
    expected_vram = []
    for i, (number, mode, x, y, w, h, pixels) in enumerate(images):
        if (w, h) != (IMAGE_W, IMAGE_H):
            print(f"FAIL {FONT_PATH} entry {number}: the TIM's image is {w}x{h} "
                  f"(mode {mode}), {IMAGE_W}x{IMAGE_H} expected")
            failures += 1
            continue
        expected_vram.append((VRAM_X + i * IMAGE_W, VRAM_Y, IMAGE_W, IMAGE_H,
                              len(pixels), fnv1a(pixels)))
    expected_region = (VRAM_X, VRAM_Y, IMAGE_W * len(expected_vram), IMAGE_H,
                       sum(len(p) for *_, p in images),
                       fnv1a(b"".join(p for *_, p in images)))

    # The harness opens the PsyCross window, and PsyCross always writes its own
    # <app name>.log in the working directory: it is run in a throwaway one so
    # that a check run leaves nothing behind (the disc is passed by its
    # absolute path).
    with tempfile.TemporaryDirectory() as workdir:
        proc = subprocess.run([str(TOOL), str(image_path.resolve()), str(lang_index),
                               str(STRING), str(FRAMES)],
                              capture_output=True, text=True, cwd=workdir)
    if proc.returncode != 0:
        sys.exit(f"font_frame exited {proc.returncode}: {proc.stderr.strip()}")
    got = parse(proc.stdout)

    def fail(what, detail):
        nonlocal failures
        print(f"FAIL {what}: {detail}")
        failures += 1

    # The file the game loaded as FILE_FONT is the font this check read.
    if got["head"] is None:
        fail("the font file", "no `font file` line in the output")
    else:
        index, sectors, head = got["head"]
        if index != FONT_FILE:
            fail("the font file", f"the harness reports file {index}, {FONT_FILE} expected")
        if sectors != -(-length // patch_disc.DATA_SIZE):
            fail("the font file", f"{sectors} sectors, {-(-length // patch_disc.DATA_SIZE)} expected")
        if head != font[:32].hex():
            fail("the font file", f"head {head} is not {FONT_PATH}'s first 32 bytes")
        else:
            print(f"ok   the game loads {FONT_PATH} as file {FONT_FILE}, "
                  f"{sectors} sectors, byte for byte")

    # The two TIMs landed in VRAM where the loader puts them, with exactly the
    # bytes the TIM holds.
    if got["text"] is None or got["text"][0] != lang_index:
        fail("the language file", f"the harness drew file {got['text']}, {lang_index} expected")
    if len(got["vram"]) != len(expected_vram):
        fail("the VRAM region", f"{len(got['vram'])} regions from the harness, "
                                f"{len(expected_vram)} expected")
    for i, expected in enumerate(expected_vram[:len(got["vram"])]):
        region = got["vram"][i]
        if region[:5] != expected[:5]:
            fail(f"VRAM region {i}",
                 f"the harness has {region[:5]}, {expected[:5]} expected")
        elif region[5] != f"{expected[5]:08x}":
            fail(f"VRAM region {i}",
                 f"checksum {region[5]}, {expected[5]:08x} of the TIM's own bytes expected")
        else:
            print(f"ok   VRAM {region[0]},{region[1]} {region[2]}x{region[3]} "
                  f"({region[4]} bytes) is TIM {i} byte for byte")

    if got["region"] is None:
        fail("the VRAM region", "no `vram region` line in the output")
    elif got["region"][:5] != expected_region[:5] or \
            got["region"][5] != f"{expected_region[5]:08x}":
        fail("the VRAM region", f"{got['region']} is not the two TIMs' bytes at "
                                f"{expected_region[:5]} sum={expected_region[5]:08x}")
    else:
        print(f"ok   VRAM {expected_region[0]},{expected_region[1]} "
              f"{expected_region[2]}x{expected_region[3]} "
              f"({expected_region[4]} bytes) is the font's two TIMs")

    # The draw order's sprites reference the glyph table: u, v, w, h, the CLUT
    # and the texture page. The window adds each primitive to the ordering
    # table as it goes, so the chain DrawOTag walks is the drawing order
    # reversed.
    want = list(reversed(expected_glyphs(raw)))
    frames = got["frames"]
    if not cd.controls_ok(raw):
        fail("the string", "it has a control code this model does not handle")
    elif not frames:
        fail("the draw order", "no frame was printed")
    elif any(count != len(want) for _, count, _, _ in frames):
        fail("the draw order", f"the frames drew {[count for _, count, _, _ in frames]} "
                               f"glyph sprites, {len(want)} expected")
    else:
        bad = 0
        for number, _, _, glyphs in frames:
            for i, (glyph, want_glyph) in enumerate(zip(glyphs, want)):
                _, u, v, w, h, clut, tpage = glyph[:7]
                if (u, v, w, h, clut, tpage) != want_glyph:
                    fail(f"glyph sprite {i} of frame {number}",
                         f"u={u} v={v} w={w} h={h} clut={clut} tpage={tpage}, "
                         f"{want_glyph} expected")
                    bad += 1
        if bad == 0 and not any(glyphs for _, _, _, glyphs in frames):
            fail("the draw order", "no frame listed its glyph sprites")
        elif bad == 0:
            u, v, w, h, clut, tpage = want[0]
            print(f"ok   {len(want)} glyph sprites per frame reference the glyph "
                  f"table (first: u={u} v={v} w={w} h={h} clut={clut} tpage={tpage})")

    if frames and got["total"] is not None and \
            got["total"] != (FRAMES, FRAMES * len(want)):
        fail("the run", f"{got['total']} frames/glyphs presented, "
                        f"{(FRAMES, FRAMES * len(want))} expected")

    if failures != 0:
        sys.exit(f"{failures} check(s) failed")
    print("the font's TIMs are in VRAM with the disc's own bytes and the game's own "
          "text window draws the glyph table's UV and CLUT")


if __name__ == "__main__":
    main()
