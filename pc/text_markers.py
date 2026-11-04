#!/usr/bin/env python3
"""Numbered-only fact tool: text entry markers and animation set lengths.

Works straight from the disc image the owner supplies, through the game's own
file table (FILE_SECTORS / FILE_SECTOR_COUNTS of src/engine/file/file_table.c,
the VERSION_EU block): no retail text of the disc ever lands in the output.
Everything printed is counts, offsets and byte values.

Formats
-------
    text:  s32 offsets from the file's start; entry 0 of the field's event text
           file is the string table; getString (text_tools.c): the count word
           sits at table[0] and string i at table + table[i + 1], NUL-terminated.
    anim:  s32 offsets from the file's start; the actor's fileEntry points to a
           set-list block of 6-s16 rows [set, dir0..dir4] (completion read,
           terminated by a 0 set, FIELDSTG_animateActor); the dirptr rows are
           s16 indices into the same file's s32 offset table (animateActor puts
           the file id over their bits: setAnims[i] = entry[i+1] | file<<16);
           each dir points at frames of s32 quads [duration, image|file<<16,
           a, b]; -1 plays once and sets animDone, 0 loops back. Script
           directions 5-8 map to index 8-dir (actors face mirrored sides).

    pc/text_markers.py IMAGE [text|anim] [...]
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import patch_disc  # noqa: E402

FILE_TABLE_C = ROOT / "src" / "engine" / "file" / "file_table.c"

SECTOR = patch_disc.SECTOR
USER_DATA = patch_disc.USER_DATA
DATA_SIZE = patch_disc.DATA_SIZE

# FONT_CODE_LENGTHS (src/engine/text/font.c:74): bytes of each 0x02 code
CODE_LENGTHS = [1, 2, 3, 2, 5, 3, 3, 2, 3, 2, 0]

# TEXT_WAIT_BUTTONS (src/engine/text/text_window.c:887), PAD_* bit indices are
# not needed here: this table maps the arg byte to the handler's step index.
WAIT_BUTTON_NAMES = ["cross", "circle", "cross", "triangle", "square"]

USAGE = ("usage: text_markers.py IMAGE text <file-id> <entry-index>*\n"
         "       text_markers.py IMAGE anim <file-id> <entry-index>* [--set N --dir D]*\n"
         "       text_markers.py IMAGE probe <file-id>*")


def eu_file_table():
    """(FILE_SECTORS, FILE_SECTOR_COUNTS) of the VERSION_EU block.

    Reads the two arrays out of file_table.c as literals: the ES419_SLOT_*
    macros are taken as the original value headings them, because the disc
    build they describe is the retail one. Returns (sectors, counts), each
    indexed by file id.
    """
    text = FILE_TABLE_C.read_text(encoding="utf-8", errors="replace")

    def block(name):
        import re
        m = re.search(rf"\b{name}\[2382\]\s*=\s*\{{", text)
        if m is None:
            raise SystemExit(f"{name} not found in {FILE_TABLE_C}")
        start = m.end() - 1  # at the array's opening brace
        depth = 0
        end = start
        for end, ch in enumerate(text[start:], start):
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    break
        body = re.sub(r"/\*.*?\*/", "", text[start + 1:end], flags=re.DOTALL)
        out = []
        toks = [t.strip() for t in body.split(",")]
        i = 0
        while i < len(toks):
            tok = toks[i]
            if tok.startswith("ES419_SLOT_"):
                # the original value is the macro's second argument
                tok = toks[i + 1].rstrip(")")
                i += 2
                out.append(int(tok, 0))
                continue
            if tok:
                out.append(int(tok.rstrip(")"), 0))
            i += 1
        return out

    sectors = block("FILE_SECTORS")
    counts = block("FILE_SECTOR_COUNTS")
    return sectors, counts


def read_file(image, sectors, counts, file_id):
    """One file's bytes off the disc, from its LBA and its sector count."""
    if file_id >= len(sectors) or file_id >= len(counts):
        raise SystemExit(f"file {file_id:#x}: past the file table ({len(sectors)} entries)")
    lba = sectors[file_id]
    n = counts[file_id]
    if n == 0:
        return b"", lba, 0
    out = bytearray()
    for i in range(n):
        image.seek((lba + i) * SECTOR + USER_DATA)
        out += image.read(DATA_SIZE)
    return bytes(out[:n * DATA_SIZE]), lba, n


def s32(data, off):
    import struct
    return struct.unpack_from("<i", data, off)[0]


def strings_of_text_file(data):
    """Entry 0 of a field event text file: (count, [offsets]).

    The offset table sits at entry 0 of the file's s32 archive; the count word
    and the offsets are relative to that table, as getString reads them.
    """
    table = s32(data, 0)
    base = table
    count = s32(data, base)
    offs = [s32(data, base + 4 * (i + 1)) for i in range(count)]
    return base, count, offs


def main(argv):
    if len(argv) < 3:
        sys.exit(USAGE)
    image_path = Path(argv[0])
    mode = argv[1]
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    sectors, counts = eu_file_table()
    with image_path.open("rb") as image:
        if mode == "probe":
            ids = [int(a, 0) for a in argv[2:]]
            for fid in ids:
                data, lba, n = read_file(image, sectors, counts, fid)
                if len(data) < 8:
                    print(f"file {fid:#x} lba={lba} sectors={n} too-small")
                    continue
                first = s32(data, 0)
                print(f"file {fid:#x} lba={lba} sectors={n} bytes={len(data)} "
                      f"table[0]={first}")
            return

        if mode == "text":
            fid = int(argv[2], 0)
            data, lba, n = read_file(image, sectors, counts, fid)
            base, count, offs = strings_of_text_file(data)
            print(f"file {fid:#x} lba={lba} sectors={n} bytes={len(data)} "
                  f"table@0={base:#x} entries={count}")
            for e in [int(a, 0) for a in argv[3:]]:
                if e >= count:
                    print(f"e{e}: out of range")
                    continue
                off = base + offs[e]
                end = data.index(b"\x00", off)
                raw = data[off:end]
                # the walk: mirror FONT.decode+processTextChar (non-sjis), byte
                # 0x01 = icon (2), 0x02 xx = control code, 0x03 alone = junk.
                typed = codes = pages = newlines = 0
                markers = []
                others = []
                i = 0
                ln = len(raw)
                while i < ln:
                    b = raw[i]
                    if b == 0x01:
                        codes += 1
                        others.append(("icon", i, raw[i + 1], 2))
                        i += 2
                        continue
                    if b == 0x02:
                        c = raw[i + 1]
                        clen = CODE_LENGTHS[c] if c < len(CODE_LENGTHS) else 2
                        if c == 2:  # textCodeWaitButton: arg at i+2
                            arg = raw[i + 2]
                            btn = WAIT_BUTTON_NAMES[arg] if 0 <= arg <= 4 else f"skip({arg})"
                            markers.append((i + 2, arg, btn))
                            codes += 1
                        elif c == 1:
                            newlines += 1
                            codes += 1
                        elif c == 3:
                            pages += 1
                            codes += 1
                        elif c in (5, 8):
                            codes += 1
                            others.append((f"code{c}", i, raw[i + 2], clen))
                        # typed glyphs/icons only: codes never come out
                        else:
                            codes += 1
                            others.append((f"code{c}", i, raw[i + 2], clen))
                        i += clen
                        continue
                    if b == 0x03:
                        codes += 1
                        i += 1
                        continue
                    typed += 1
                    i += 1
                print(f"e{e}: off={off:#x} len={len(raw)} typed={typed} "
                      f"codes={codes} newlines={newlines} pages={pages} "
                      f"markers={[(m[0], m[1], m[2]) for m in markers]} "
                      f"others={others}")
            return

        if mode == "anim":
            fid = int(argv[2], 0)
            row = int(argv[3], 0)
            data, lba, n = read_file(image, sectors, counts, fid)
            print(f"file {fid:#x} lba={lba} sectors={n} bytes={len(data)}")
            # the set list: the file's s32 table row `row` points at its block
            base = s32(data, 4 * row)
            rows = []
            p = base
            while True:
                r = struct.unpack_from("<6h", data, p)
                if r[0] == 0:
                    break
                rows.append(r)
                p += 12
            print(f"entry {row}: setlist@{base:#x} rows={len(rows)}")
            # --set N / --dir D pairs: per set, the frame walk; --dir (twice) is
            # the script's direction, mapped as animateActor maps it (5-8/8-dir)
            targets = []
            q = 4
            while q < len(argv):
                if argv[q] == "--set":
                    s = int(argv[q + 1], 0)
                    d = None
                    if q + 3 < len(argv) and argv[q + 2] == "--dir":
                        d = int(argv[q + 3], 0)
                        q += 2
                    targets.append((s, d))
                    q += 2
                    continue
                q += 1
            for s, d in [["ALL", None]] + targets:
                for r in rows:
                    if s != "ALL" and r[0] != s:
                        continue
                    ptrs = r[1:]
                    print(f"set {r[0]}: dirptrs={list(ptrs)}")
                    if s == "ALL":
                        continue
                    dirlist = range(5) if d is None else [8 - d if d >= 5 else d]
                    for di in dirlist:
                        ptr = ptrs[di]
                        fbase = s32(data, 4 * ptr)
                        quads = []
                        total = 0
                        p = fbase
                        while True:
                            dur, imgid, a, b = struct.unpack_from("<4i", data, p)
                            if dur == 0:
                                break
                            if dur < 0:
                                break
                            quads.append(dur)
                            total += dur
                            p += 16
                        end = struct.unpack_from("<i", data, p)[0]
                        tag = "once(-1)" if end == -1 else f"loop({end})"
                        print(f"  dir {di} ptr {ptr}: frames@{fbase:#x} quads={len(quads)} "
                              f"durations={quads} total={total} {tag}")
            return

        sys.exit(USAGE)


if __name__ == "__main__":
    main(sys.argv[1:])
