#!/usr/bin/env python3
"""Checks the game's own drawing (host) against an independent expectation.

Runs pc/build/draw_order over a language file's string indices and inspects
the ordering table the game's own text window built. This is milestone 3's
measurement: not a screenshot, but the primitives themselves.

The harness (pc/draw_order.c) draws one string with the game's own code --
createTextWindow / textWindowSetString / textWindowDraw from
src/engine/text/text_window.c, compiled against PsyCross's libgpu -- into an
ordering table through PsyCross's addPrim. It prints the packet layout, the
counts by kind, and every primitive (code, length, size).

This check verifies the result structurally:

  * the packet sizes are PsyCross's host layout (P_TAG 16, OT_TAG 12, SPRT 28,
    POLY_FT4 48, DR_TPAGE 16, P_LEN 3), not the retail Psy-Q 4.7 layout
    (which would give P_TAG 8, SPRT 20, ...): the sizes are printed and
    compared here against the host values;
  * every primitive's size is (length + P_LEN) * 4 and its (code, length) is
    one this build can produce (0x64 SPRT, 0x2C POLY_FT4, 0xE1 DR_TPAGE);
  * the kinds and counts are the ones the game's own drawing path produces for
    that string, recomputed here from the string's bytes and the European
    glyph table parsed out of src/engine/text/text_tools.c: one SPRT per drawn
    character, and one DR_TPAGE per character plus the ones the game emits
    when the glyph's texture page changes and once at the end;
  * the byte total is the sum of the primitive sizes (the chain is contiguous);
  * the chain is well formed (the harness reports it).

The image is the owner's own file: it is only read and nothing of it is
written into the repository (ADR-001).

    make -C pc
    .venv/bin/python pc/check_draw.py <disc .bin>

Exits non-zero on the first failing case and reports each one.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "pc"))

# check_text already parses the European glyph/icon tables out of text_tools.c
# (the #elif VERSION_EU blocks) and reads a language file's strings from the
# image; its module level runs on import, its main does not.
import check_text as ct  # noqa: E402
import patch_disc  # noqa: E402

TOOL = ROOT / "pc" / "build" / "draw_order"

# PsyCross's packet layout on this host (sizeof, in bytes) and P_LEN. The
# retail Psy-Q 4.7 headers would give P_TAG 8, OT_TAG 8, SPRT 20, ... so a
# build that reached the wrong headers fails here.
HOST_PACKETS = {"P_TAG": 16, "OT_TAG": 12, "SPRT": 28, "POLY_FT4": 48, "DR_TPAGE": 16}
HOST_P_LEN = 3

# Discord's command codes and the length field each carries (setlen in
# src/engine/text/text_window.c and PsyCross's set* macros).
KINDS = {0x64: 4, 0x2C: 9, 0xE1: 1}

# Same language files as the text engine check; the empty string (0) plus
# plain names around the ones the G-041 rule rewrote. Index 14 of the map
# files carries a single line break (0x02 0x01), the only control code here.
CASES = [
    ("SSSTNAME.BIN", [0, 1, 4, 30, 31, 71, 72, 84, 128]),
    ("SSAMTMAP.BIN", [0, 1, 14, 21, 27, 29, 44]),
    ("SSASKMAP.BIN", [0, 1, 14, 21, 27, 29, 44]),
]

PACKET = re.compile(r"packet (\S+)=(-?\d+)(?: (\S+)=(-?\d+))?(?: (\S+)=(-?\d+))?"
                    r"(?: (\S+)=(-?\d+))?(?: (\S+)=(-?\d+))?(?: (\S+)=(-?\d+))?")
PACKET_NAMES = ("P_TAG", "OT_TAG", "SPRT", "POLY_FT4", "DR_TPAGE", "P_LEN")
OT = re.compile(r"ot prims=(-?\d+) sprt=(-?\d+) ft4=(-?\d+) tpage=(-?\d+) bytes=(-?\d+)")
PRIM = re.compile(r"prim 0x([0-9a-f]+) (-?\d+) (-?\d+)")


def page_of(kind, value):
    """The texture page of a drawn character, what the game's tpage keys off.

    processTextChar picks the glyph (or, for an icon, the icon) and
    textWindowDraw falls back to glyphs[0] when its page is 0xFF
    (the missing glyph). Only p in 0/1 appears in these strings.
    """
    if kind == 0:
        g = ct.GLYPHS[value - 4]
        return g[0] if g[0] != 0xFF else ct.GLYPHS[0][0]
    if kind == 1:
        g = ct.ICONS[value - 1] if 0 < value <= ct.ICON_COUNT else ct.GLYPHS[0]
        return g[0] if g[0] != 0xFF else ct.GLYPHS[0][0]
    return ct.GLYPHS[0][0]


def expected(raw):
    """(sprites, tpages) textWindowDraw produces for `raw`: one SPRT per drawn
    character and, per character, one DR_TPAGE; one extra DR_TPAGE when the
    glyph's page differs from the previous one (the first compares against the
    function's prevTpage 0), and one at the end. An empty string is never
    entered (textWindowDraw returns on a NULL buffer)."""
    drawn = [(k, v) for k, v, _ in ct.codes(raw) if k != 2]
    n = len(drawn)
    if n == 0:
        return 0, 0
    pages = [page_of(k, v) for k, v in drawn]
    changes = 1  # the first glyph's page against prevTpage == 0
    changes += sum(1 for a, b in zip(pages, pages[1:]) if a != b)
    return n, n + changes + 1


def controls_ok(raw):
    """True when the only control code is the line break the model handles."""
    return all(v in (1,) for k, v, _ in ct.codes(raw) if k == 2)


def parse(stdout):
    """(packet, summary, prims, chain) from the harness output, or raise."""
    packet = {}
    summary = None
    prims = []
    chain = None
    for line in stdout.splitlines():
        m = PACKET.match(line)
        if m and line.startswith("packet "):
            packet = {m.group(i): int(m.group(i + 1))
                      for i in (1, 3, 5, 7, 9, 11) if m.group(i)}
            continue
        m = OT.match(line)
        if m:
            summary = dict(zip(("prims", "sprt", "ft4", "tpage", "bytes"),
                               (int(x) for x in m.groups())))
            continue
        m = PRIM.match(line)
        if m:
            prims.append((int(m.group(1), 16), int(m.group(2)), int(m.group(3))))
            continue
        if line.startswith("chain "):
            chain = line.split()[1]
    return packet, summary, prims, chain


def run_tool(image_path, index, string):
    proc = subprocess.run([str(TOOL), str(image_path), str(index), str(string)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"file {index} string {string}: draw_order exited "
                         f"{proc.returncode}: {proc.stderr.strip()}")
    return parse(proc.stdout)


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin>")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(sys.argv[1])
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    failures = 0
    with image_path.open("rb") as image:
        ct.ON_DISC = patch_disc.files(image)
        for name, indices in CASES:
            path = f"AAA/DAT/COUNTRY/SPN/{name}"
            index = ct.gen_table.spanish_index(ct.gen_table.K_OF_FILE[name])
            raw = ct.strings(image, path)
            for i in indices:
                string = raw[i]
                if not controls_ok(string):
                    print(f"FAIL {name} index {i}: unsupported control code in the string")
                    failures += 1
                    continue
                packet, summary, prims, chain = run_tool(image_path, index, i)
                want_sprites, want_tpages = expected(string)

                bad = []
                if len(packet) != len(PACKET_NAMES) or \
                        any(packet.get(k) != v for k, v in HOST_PACKETS.items()) or \
                        packet.get("P_LEN") != HOST_P_LEN:
                    bad.append(f"packet layout {packet} is not PsyCross's host layout "
                               f"{HOST_PACKETS} P_LEN={HOST_P_LEN}")
                if summary is None:
                    bad.append("no `ot` summary line")
                else:
                    total = summary["prims"]
                    if total != len(prims):
                        bad.append(f"{total} prims summarised, {len(prims)} listed")
                    if total != summary["sprt"] + summary["ft4"] + summary["tpage"]:
                        bad.append("the kinds do not add up to the total")
                    if summary["sprt"] != want_sprites:
                        bad.append(f"{summary['sprt']} sprites, {want_sprites} expected")
                    if summary["tpage"] != want_tpages:
                        bad.append(f"{summary['tpage']} tpages, {want_tpages} expected")
                    if total != want_sprites + want_tpages:
                        bad.append(f"{total} prims, {want_sprites + want_tpages} expected")
                    if summary["bytes"] != sum(p[2] for p in prims):
                        bad.append(f"{summary['bytes']} bytes, {sum(p[2] for p in prims)} "
                                   f"in the primitive sizes")
                if chain != "ok":
                    bad.append(f"chain not ok ({chain!r})")
                for code, length, size in prims:
                    if code not in KINDS:
                        bad.append(f"primitive 0x{code:02x} is not a kind this path draws")
                    elif KINDS[code] != length:
                        bad.append(f"0x{code:02x} has length {length}, {KINDS[code]} expected")
                    if size != (length + HOST_P_LEN) * 4:
                        bad.append(f"0x{code:02x} has size {size}, "
                                   f"{(length + HOST_P_LEN) * 4} expected")
                    if size != HOST_PACKETS.get({0x64: "SPRT", 0x2C: "POLY_FT4",
                                                 0xE1: "DR_TPAGE"}.get(code, ""), size):
                        bad.append(f"0x{code:02x} size is not PsyCross's host size")

                if bad:
                    print(f"FAIL {name} index {i}")
                    for line in bad:
                        print(f"     {line}")
                    failures += 1
                else:
                    print(f"ok   {name} index {i}: {summary['prims']} prims "
                          f"({summary['sprt']} spr, {summary['ft4']} ft4, "
                          f"{summary['tpage']} tpage), {summary['bytes']} bytes")

    if failures != 0:
        sys.exit(f"{failures} string(s) failed")
    print("all draw orders built with the game's own drawing code and PsyCross's host layout")


if __name__ == "__main__":
    main()
