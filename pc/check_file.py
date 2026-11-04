#!/usr/bin/env python3
"""Checks the host file layer against an independent read of the disc.

Runs pc/build/file_dump over a file index: the harness resolves the index
through the game's own file table and file cache (src/engine/file/,
unmodified) and writes the file's sectors out. This check reads the same file
from the same image itself and compares the bytes, sector by sector.

The file names and their LBAs come from the tools the repository already has
(tools/es419/gen_table.py for the text file indices, tools/patch_disc.py for
the ISO 9660 walk and the sector framing); no third reader is written here.

    make -C pc
    .venv/bin/python pc/check_file.py <disc .bin>

The image is the owner's own file: it is only read, and nothing of it is
written into the repository (ADR-001). Exits non-zero on the first failing
file and reports each one.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "es419"))

import patch_disc  # noqa: E402
import gen_table  # noqa: E402

TOOL = ROOT / "pc" / "build" / "file_dump"

# The framing of a Mode 2 Form 1 sector, as tools/patch_disc.py and
# tools/extract_disc.py already read it (sync + header + subheader, then data).
SECTOR = patch_disc.SECTOR
USER_DATA = patch_disc.USER_DATA
DATA_SIZE = patch_disc.DATA_SIZE


def slots():
    """(file table index, path on the disc) for every text file: the
    Spanish copy the file table holds at 7 + 7k and the USA copy at 2 + 7k
    (tools/es419/gen_table.py)."""
    for name, k in sorted(gen_table.K_OF_FILE.items()):
        yield gen_table.spanish_index(k), f"AAA/DAT/COUNTRY/SPN/{name}"
        yield gen_table.usa_slot(k), f"AAA/DAT/COUNTRY/USA/{name.replace('SS', 'US', 1)}"


def read_sectors(image, lba, count):
    """COUNT sectors of the open image, the 2048 data bytes of each.

    The frames are 2352 bytes apart, so each sector is sought again: reading
    DATA_SIZE after DATA_SIZE would walk into the next frame's sync."""
    out = bytearray()
    for i in range(count):
        image.seek((lba + i) * SECTOR + USER_DATA)
        out += image.read(DATA_SIZE)
    return bytes(out)


def check(index, path, image, image_path, on_disc, out):
    if path not in on_disc:
        return f"index {index} ({path}): not on the disc"
    lba, length = on_disc[path]
    count = -(-length // DATA_SIZE)  # the file's sectors on the disc
    expected = read_sectors(image, lba, count)

    proc = subprocess.run(
        [str(TOOL), str(image_path), str(index), str(out)],
        capture_output=True, text=True)
    if proc.returncode != 0:
        return f"index {index} ({path}): file_dump exited {proc.returncode}: {proc.stderr.strip()}"

    got = out.read_bytes()
    if got != expected:
        if len(got) != len(expected):
            return (f"index {index} ({path}): {len(got)} bytes out, {len(expected)} "
                    f"expected ({count} sectors at {lba})")
        i = next(k for k in range(len(expected)) if got[k] != expected[k])
        return f"index {index} ({path}): first differing byte at {i}: {got[i]:#04x} != {expected[i]:#04x}"

    print(f"ok   index {index}: {path} {count} sectors at {lba}, {length} bytes")
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin>")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image = Path(sys.argv[1])
    if not image.is_file():
        sys.exit(f"no such image: {image}")

    failures = 0
    with image.open("rb") as f:
        on_disc = patch_disc.files(f)
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "out.bin"
            for index, path in slots():
                error = check(index, path, f, image, on_disc, out)
                if error is not None:
                    print(f"FAIL {error}")
                    failures += 1

    if failures != 0:
        sys.exit(f"{failures} file(s) failed")
    print("all files read byte for byte through the game's own file layer")


if __name__ == "__main__":
    main()
