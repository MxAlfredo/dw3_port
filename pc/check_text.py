#!/usr/bin/env python3
"""Checks the game's own text measurement (host) against an independent one.

Runs pc/build/text_measure over a language file's string indices: the harness
loads the file through the game's own file layer (milestone 1: FILE_TABLE and
FILE_CACHE over CD_READER), takes the string with getString and measures its
widest line in style 1 -- the area banner's style -- with FONT.decode and
measureText, the game's own text engine compiled for the host.

This check reads the same file from the same image, takes the same string and
measures it itself. The European glyph and icon tables are parsed out of
src/engine/text/text_tools.c (the #elif VERSION_EU blocks) and the string is
decoded with the charmap the overlay tools already use (tools/es419/lang.py).
For every index the string's bytes and its pixel width must match.

The indices are the place names the G-041 rule rewrote (SSSTNAME, SSAMTMAP
and SSASKMAP), plus short and long strings around them. The image is the
owner's own file: it is only read and nothing of it is written into the
repository (ADR-001).

    make -C pc
    .venv/bin/python pc/check_text.py <disc .bin>

Exits non-zero on the first failing index and reports each one.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "es419"))

import patch_disc  # noqa: E402
import gen_table  # noqa: E402
import lang  # noqa: E402

TOOL = ROOT / "pc" / "build" / "text_measure"
FONT_C = ROOT / "src" / "engine" / "text" / "text_tools.c"

# The framing of a Mode 2 Form 1 sector, as pc/check_file.py and the tools read it.
SECTOR = patch_disc.SECTOR
USER_DATA = patch_disc.USER_DATA
DATA_SIZE = patch_disc.DATA_SIZE

# FONT_CODE_LENGTHS (src/engine/text/font.c:74): the bytes each 0x02 code takes
CODE_LENGTHS = [1, 2, 3, 2, 5, 3, 3, 2, 3, 2, 0]

# (language file, string indices). SSSTNAME is the area banner's place name and
# SSAMTMAP/SSASKMAP the map label's: indices 30, 31, 71, 72 and 84 (SSSTNAME)
# and 27 and 29 (the maps) are the ones the G-041 rule rewrote; the rest are
# short and long strings around them (0 is the empty string).
CASES = [
    ("SSSTNAME.BIN", [0, 1, 4, 30, 31, 71, 72, 84, 128]),
    ("SSAMTMAP.BIN", [0, 1, 14, 21, 27, 29, 44]),
    ("SSASKMAP.BIN", [0, 1, 14, 21, 27, 29, 44]),
]

LINE = re.compile(r"index (\d+) width (-?\d+) bytes ([0-9a-f]*)")

# T9: the es-419 mode reads file layout (file, index, spanish_index, lba, sectors) from
# tools/es419/gen_table.py. The build emits it as build/eu/es419/es419_layout.json.
# The mode is opt-in (`--es419-layout <path>`); there is no default that silently
# skips. With ES419=1 the host's own file_table.c redirects the USA slot at 2+7k to
# the overlay's sectors in DUMMY, so the checker drives text_measure with each row's
# `index` (the USA slot), not with `spanish_index` (which would read the unpatched
# retail copy from the disc).
USAGE = (f"usage: {Path(__file__).name} <disc .bin> "
         f"[--es419-layout <es419_layout.json>]")


def eu_block(decl):
    """The #elif VERSION_EU ... #endif body of array `decl` in text_tools.c."""
    text = FONT_C.read_text(encoding="utf-8")
    m = re.search(r"\b" + re.escape(decl) + r"\s*\[[^\]]*\]\s*=\s*\{", text)
    assert m, f"{decl} not in {FONT_C}"
    start = m.end()
    els = text.index("#elif VERSION_EU", start)
    end = text.index("#endif", els)
    return text[els:end]


def table(decl, count):
    """The EU glyph/icon table `decl`: rows of its { ... } initializers."""
    rows = [[int(x, 0) for x in m.group(1).split(",")]
            for m in re.finditer(r"\{([^{}]*)\}", eu_block(decl))]
    if len(rows) != count:
        raise ValueError(f"{decl}: {len(rows)} rows, expected {count}")
    return rows


# The tables of the font's first style (FONT_STYLES[1], the banner's): the EU
# sheet has the 230 glyphs and the 114 icons. The game's glyphCount (234) is
# those 230 plus the 4 slots below the sheet; iconCount is the 114 icons.
GLYPHS = table("FONT_GLYPHS_1", 230)
ICONS = table("FONT_ICONS_1", 114)
GLYPH_COUNT = len(GLYPHS) + 4
ICON_COUNT = len(ICONS)


def s8(value):
    """A glyph's dx/dy: stored as a signed byte."""
    return value - 256 if value >= 128 else value


def advance(glyph):
    """A glyph's width in pixels: what it draws plus its bearing (measureText).

    Glyph.dx is the row's 8th byte and Glyph.advance the 10th: the fields come
    in the order page, u, v, clutX, clutY, w, h, dx, dy, advance, unusedA.
    """
    return glyph[9] + s8(glyph[7])


def codes(raw):
    """(kind, value, text) of each character of a string, from lang.py's charmap.

    Mirrors Font.decode (src/engine/text/font.c): kind 0 a glyph (value - 4
    indexes the sheet), 1 an icon, 2 a control code, 3 a missing glyph. `text`
    is the charmap's character (lang.SINGLE, lang.EXT, lang.CONTROL) or the
    {xx} token lang.py leaves for a code it does not name.
    """
    i = 0
    while i < len(raw):
        b = raw[i]
        nxt = raw[i + 1] if i + 1 < len(raw) else 0
        if b == 0x01:  # an icon, 0x01 xx; xx also names it in lang.EXT
            yield 1, nxt, lang.EXT.get(nxt, f"{{{b:02x}{nxt:02x}}}")
            i += 2
        elif b == 0x02:  # a control code, 0x02 xx
            yield 2, nxt, lang.CONTROL.get(nxt, f"{{{b:02x}{nxt:02x}}}")
            i += CODE_LENGTHS[nxt] if nxt < len(CODE_LENGTHS) else 2
        elif b == 0x03:  # s[0] < 4: a missing glyph, its second byte not taken
            yield 3, 0, f"{{{b:02x}}}"
            i += 1
        elif b in lang.SINGLE:
            yield 0, b, lang.SINGLE[b]
            i += 1
        else:  # a glyph the charmap does not name, or past the sheet
            if 4 <= b < GLYPH_COUNT:
                yield 0, b, f"{{{b:02x}}}"
            else:
                yield 3, 0, f"{{{b:02x}}}"
            i += 1


def decode(raw):
    """The string lang.py's charmap reads in `raw`, from the same walk."""
    return "".join(text for _, _, text in codes(raw))


def measure(raw):
    """measureText(raw, FONT_STYLES[1], 0): the widest line, in pixels."""
    width = 0
    widest = 0
    for kind, value, _ in codes(raw):
        if kind == 0:
            width += advance(GLYPHS[value - 4])
        elif kind == 1:
            if 0 < value <= ICON_COUNT:
                width += advance(ICONS[value - 1])
        elif kind == 2:
            if value in (1, 3):  # a line break: the widest line so far
                widest = max(widest, width)
                width = 0
        elif kind == 3:
            width += advance(GLYPHS[0])
    return max(width, widest)


def read_sectors(image, lba, count):
    """COUNT sectors of the open image, the 2048 data bytes of each."""
    out = bytearray()
    for i in range(count):
        image.seek((lba + i) * SECTOR + USER_DATA)
        out += image.read(DATA_SIZE)
    return bytes(out)


def strings(image, path):
    """(index -> bytes) of every string in the language file at `path`."""
    lba, length = ON_DISC[path]
    data = read_sectors(image, lba, -(-length // DATA_SIZE))
    if not lang.is_lang(data):
        raise SystemExit(f"{path}: not a language file")
    count = int.from_bytes(data[:4], "little")
    offsets = [int.from_bytes(data[4 + 4 * i:8 + 4 * i], "little") for i in range(count)]
    out = {}
    for i, off in enumerate(offsets):
        out[i] = data[off:data.index(b"\x00", off)]
    return out


# --- es-419 mode (T9) -----------------------------------------------------
#
# Mirrors the layout gen_table.py emits (tools/es419/gen_table.py:95-99):
#   {"first_lba": int, "last_lba": int, "files": [...]}
# where each row has file, index (usa_slot(k)=2+7k), spanish_index (7+7k),
# lba, sectors.
#
# The es-419 image is the EU disc patched with tools/es419/make_disc.py; the
# host's text_measure is built with ES419=1 (see pc/Makefile's GAME_DEFS). Under
# that #if, file_table.c (file_table.c:10-16, 852...) substitutes
# ES419_SLOT_SECTOR(k, ...) into FILE_SECTORS at the USA positions 2+7k, so the
# host's FILE_CACHE.load(usa_slot(k)) returns the overlay's sectors and not the
# retail copy. The checker therefore drives text_measure with each row's
# `index` (USA slot). `spanish_index` is kept as informational data, not used
# for measurement.


def read_layout(path):
    """Read an es-419 layout JSON from `path` (raises FileNotFoundError)."""
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def read_layout_dict(rows):
    """Build the layout dict directly from rows (used by tests, no disk)."""
    first = rows[0]["lba"]
    last = rows[-1]["lba"] + rows[-1]["sectors"] - 1
    return {"first_lba": first, "last_lba": last, "files": rows}


def validate_layout(layout):
    """Reject layouts missing required fields or referencing unknown files.

    Same keys tools/es419/gen_table.py:95-99 emits: file, index (usa_slot),
    spanish_index, lba, sectors. Files must live in gen_table.K_OF_FILE and
    spanish_index must match gen_table.spanish_index(k) for that file.
    """
    required = {"file", "index", "spanish_index", "lba", "sectors"}
    seen = set()
    for row in layout["files"]:
        missing = required - set(row)
        if missing:
            raise ValueError(f"{row.get('file', '<no file>')}: missing {sorted(missing)}")
        if row["sectors"] <= 0:
            raise ValueError(f"{row['file']}: sectors={row['sectors']} (<= 0)")
        if row["file"] not in gen_table.K_OF_FILE:
            raise ValueError(f"{row['file']}: not in the es-419 text file set")
        k = gen_table.K_OF_FILE[row["file"]]
        if row["spanish_index"] != gen_table.spanish_index(k):
            raise ValueError(
                f"{row['file']}: spanish_index={row['spanish_index']} != "
                f"gen_table.spanish_index({k})={gen_table.spanish_index(k)}")
        if row["index"] != gen_table.usa_slot(k):
            raise ValueError(
                f"{row['file']}: index={row['index']} != gen_table.usa_slot({k})={gen_table.usa_slot(k)}")
        if row["file"] in seen:
            raise ValueError(f"{row['file']}: duplicate row in layout")
        seen.add(row["file"])


def read_sectors_at(image, lba, sectors):
    """Read `sectors` data blocks (DATA_SIZE bytes each) starting at LBA `lba`.

    A pure LBA-driven sector reader for the es-419 mode: the row already
    carries its LBA and sector count, so we read them straight from the image
    without going through the CD file table. The framing (USER_DATA bytes
    into each SECTOR-byte frame, then DATA_SIZE bytes) is the same one the EU
    mode uses; `image` is anything with seek/read, matching read_sectors.
    """
    out = bytearray()
    for i in range(sectors):
        image.seek((lba + i) * SECTOR + USER_DATA)
        out += image.read(DATA_SIZE)
    return bytes(out)


def cases_from_layout(layout, cases):
    """Map [(name, [indices])] to layout-driven cases for the es-419 mode.

    Each returned row carries the layout's per-file entries plus a
    `measurement_slot` that the checker hands to `text_measure`. With
    `ES419=1`, file_table.c redirects FILE_SECTORS at the USA positions
    `usa_slot(k) = 2+7k` (file_table.c:852...) to the overlay's sectors in
    DUMMY; the host's FILE_CACHE.load() therefore resolves the layout
    row's `index` (USA slot) to the es-419 overlay, while `spanish_index`
    would still read the unpatched retail copy from the disc. The checker
    keeps `spanish_index` as informational data and drives the tool with
    `measurement_slot`.
    """
    by_file = {r["file"]: r for r in layout["files"]}
    out = []
    for name, indices in cases:
        if name not in by_file:
            raise ValueError(f"{name}: not in the es-419 layout")
        row = by_file[name]
        out.append({
            "name": name,
            "indices": list(indices),
            "file": row["file"],
            "file_table_index": row["index"],
            "spanish_index": row["spanish_index"],
            "measurement_slot": row["index"],
            "lba": row["lba"],
            "sectors": row["sectors"],
        })
    return out


def run_tool(image_path, index, indices):
    """(string index -> (width, bytes)) as text_measure prints them."""
    proc = subprocess.run(
        [str(TOOL), str(image_path), str(index)] + [str(i) for i in indices],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"file {index}: text_measure exited {proc.returncode}: {proc.stderr.strip()}")
    out = {}
    for line in proc.stdout.splitlines():
        m = LINE.match(line)
        if m is None:
            raise SystemExit(f"file {index}: unexpected line from text_measure: {line!r}")
        out[int(m.group(1))] = (int(m.group(2)), bytes.fromhex(m.group(3)))
    return out


def parse_args(argv):
    """Parse argv into (image_path, layout_path | None).

    Pure: same USAGE line as the program's, raises SystemExit on misuse with
    that line as the message. The es-419 mode is opt-in: without
    `--es419-layout` the checker only runs the EU base mode; the path
    coming after the flag must be present (no implicit default that silently
    skips, so a stray flag is a hard failure).
    """
    positional = []
    layout = None
    saw_layout_flag = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--es419-layout":
            saw_layout_flag = True
            if i + 1 >= len(argv):
                sys.exit(USAGE)
            layout = argv[i + 1]
            i += 2
            continue
        if a in ("-h", "--help"):
            sys.exit(USAGE)
        positional.append(a)
        i += 1
    if saw_layout_flag and layout is None:
        # Already covered above when --es419-layout is the last arg, but a
        # second pass keeps the invariant clear in tests.
        sys.exit(USAGE)
    if len(positional) != 1:
        sys.exit(USAGE)
    return positional[0], layout


def main():
    image_str, layout_str = parse_args(sys.argv[1:])
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(image_str)
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    global ON_DISC
    failures = 0
    with image_path.open("rb") as image:
        ON_DISC = patch_disc.files(image)
        for name, indices in CASES:
            path = f"AAA/DAT/COUNTRY/SPN/{name}"
            index = gen_table.spanish_index(gen_table.K_OF_FILE[name])
            expected = strings(image, path)
            got = run_tool(image_path, index, indices)
            for i in indices:
                if i not in expected or i not in got:
                    print(f"FAIL index {index}/{i}: missing from the disc or the tool")
                    failures += 1
                    continue
                raw = expected[i]
                if decode(raw) != lang.decode_text(raw):
                    print(f"FAIL index {index}/{i}: the charmap walk and lang.decode_text disagree")
                    failures += 1
                    continue
                width, bytes_out = got[i]
                want = measure(raw)
                if bytes_out != raw:
                    print(f"FAIL index {index}/{i}: {len(bytes_out)} bytes out, {len(raw)} expected")
                    failures += 1
                elif width != want:
                    print(f"FAIL index {index}/{i}: {width} px from the tool, {want} px expected")
                    failures += 1
                else:
                    print(f"ok   {name} index {i}: {width} px, {len(raw)} bytes: {lang.decode_text(raw)!r}")

        if layout_str is not None:
            layout_path = Path(layout_str)
            if not layout_path.is_file():
                sys.exit(f"--es419-layout: {layout_path} is not a readable file")
            try:
                layout = read_layout(layout_path)
            except (OSError, ValueError) as e:
                sys.exit(f"--es419-layout: cannot read {layout_path}: {e}")
            try:
                validate_layout(layout)
            except ValueError as e:
                sys.exit(f"--es419-layout: invalid layout: {e}")
            mapped = cases_from_layout(layout, CASES)
            for row in mapped:
                data = read_sectors_at(image, row["lba"], row["sectors"])
                if not lang.is_lang(data):
                    sys.exit(f"es-419 {row['file']}: not a language file at "
                             f"LBA {row['lba']}, {row['sectors']} sectors")
                count = int.from_bytes(data[:4], "little")
                offs = [int.from_bytes(data[4 + 4 * i:8 + 4 * i], "little")
                        for i in range(count)]
                expected = {i: data[off:data.index(b"\x00", off)]
                            for i, off in enumerate(offs)}
                got = run_tool(image_path, row["measurement_slot"], row["indices"])
                for i in row["indices"]:
                    if i not in expected or i not in got:
                        print(f"FAIL es-419 {row['file']} index {row['measurement_slot']}/{i}: "
                              f"missing from the layout region or the tool")
                        failures += 1
                        continue
                    raw = expected[i]
                    if decode(raw) != lang.decode_text(raw):
                        print(f"FAIL es-419 {row['file']} index {row['measurement_slot']}/{i}: "
                              f"charmap walk and lang.decode_text disagree")
                        failures += 1
                        continue
                    width, bytes_out = got[i]
                    want = measure(raw)
                    if bytes_out != raw:
                        print(f"FAIL es-419 {row['file']} index {row['measurement_slot']}/{i}: "
                              f"{len(bytes_out)} bytes out, {len(raw)} expected")
                        failures += 1
                    elif width != want:
                        print(f"FAIL es-419 {row['file']} index {row['measurement_slot']}/{i}: "
                              f"{width} px from the tool, {want} px expected")
                        failures += 1
                    else:
                        print(f"ok   es-419 {row['file']} index {i}: "
                              f"{width} px, {len(raw)} bytes: {lang.decode_text(raw)!r}")

    if failures != 0:
        sys.exit(f"{failures} string(s) failed")
    print("all strings measured the same through the game's own text engine")


if __name__ == "__main__":
    main()
