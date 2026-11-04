#!/usr/bin/env python3
"""Map the EU disc's file numbers (FILE_TABLE) to ISO9660 paths.

Usage: python3 -I tools/file_names.py "<disc>.bin" [--table src/engine/file/file_table.c]

Prints a TSV on stdout (number, first sector, sectors, status, path, bytes) and a
coverage summary on stderr. Nothing is written to disk; the output is derived from the
user's disc and must not be committed (ADR-001).

The table is read from the C source (the VERSION_EU block): FILE_SECTORS[n] is the
disc LBA where file n starts and FILE_SECTOR_COUNTS[n] its length in sectors. The
paths come from walking the ISO9660 directory tree, the same records text_data.c
reads for the text files. A number maps to a path when a file's extent equals the
number's first sector. Status values:
  ok        exactly one directory record starts at that LBA
  unused    the table has 0 sectors (no such file on the disc)
  ambiguous several records start at that LBA
  nofile    no record starts there (the number points inside a file or elsewhere)
  dup       another number has the same LBA and path (still listed, flagged)
"""
import re
import struct
import sys
from collections import defaultdict

FRAME = 2352
DATA_OFF = 24  # sync 12 + header 4 + XA subheader 8


def read_sector(f, lba, n=1):
    out = b""
    for i in range(n):
        f.seek((lba + i) * FRAME + DATA_OFF)
        out += f.read(2048)
    return out


def parse_table(path):
    src = open(path, encoding="utf-8").read()
    eu = src[src.index("#elif VERSION_EU"):]
    eu = eu[: eu.index("\n#endif")]

    def array(name):
        m = re.search(name + r"\[\d+\]\s*=\s*\{(.*?)\n\};", eu, re.S)
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        # ES419_SLOT_SECTOR(k, original) / ES419_SLOT_COUNT(k, original) -> original
        body = re.sub(r"ES419_SLOT_\w+\(\s*\d+\s*,\s*([^)]*)\)", r"\1", body)
        return [int(t, 0) for t in re.findall(r"0[xX][0-9a-fA-F]+|\d+", body)]

    return array("FILE_SECTORS"), array("FILE_SECTOR_COUNTS")


def walk(f):
    """All files, by scanning every directory of the path table.

    The root directory does not list AAA/ (the game's folder is hidden from it),
    so the path table, which has every directory, is the way in (text_data.c does
    the same).
    """
    pvd = read_sector(f, 16)
    if pvd[1:6] != b"CD001":
        sys.exit("not an ISO9660 image with 2352-byte frames")
    pt_size = struct.unpack_from("<I", pvd, 132)[0]
    pt_lba = struct.unpack_from("<I", pvd, 140)[0]
    pt = read_sector(f, pt_lba, (pt_size + 2047) // 2048)[:pt_size]
    dirs = []  # (name, extent, parent index 1-based)
    at = 0
    while at + 8 <= pt_size and pt[at]:
        idl = pt[at]
        ext = struct.unpack_from("<I", pt, at + 2)[0]
        par = struct.unpack_from("<H", pt, at + 6)[0]
        dirs.append((pt[at + 8:at + 8 + idl].decode("latin-1"), ext, par))
        at += 8 + idl + (idl & 1)

    def dir_path(i):
        name, _, par = dirs[i]
        if i == 0:
            return ""
        return dir_path(par - 1) + name + "/"

    files = []  # (path, lba, size)
    for i, (_, ext, _) in enumerate(dirs):
        first = read_sector(f, ext)
        size = struct.unpack_from("<I", first, 10)[0]
        data = read_sector(f, ext, (size + 2047) // 2048)[:size]
        off = 0
        prefix = dir_path(i)
        while off < size:
            rl = data[off]
            if rl == 0:
                off = (off // 2048 + 1) * 2048
                continue
            rec = data[off:off + rl]
            off += rl
            nl = rec[32]
            name = rec[33:33 + nl]
            if name in (b"\x00", b"\x01") or rec[25] & 2:
                continue
            files.append((prefix + name.decode("latin-1").split(";")[0],
                          struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]))
    return files


def main():
    args = sys.argv[1:]
    table = "src/engine/file/file_table.c"
    if "--table" in args:
        i = args.index("--table")
        table = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    sectors, counts = parse_table(table)
    with open(args[0], "rb") as f:
        files = walk(f)
    by_lba = defaultdict(list)
    for p, lba, sz in files:
        by_lba[lba].append((p, sz))
    stats = defaultdict(int)
    seen = {}
    used = set()
    print("number\tlba\tsectors\tstatus\tpath\tbytes")
    for n, (lba, cnt) in enumerate(zip(sectors, counts)):
        if lba == 0 or cnt == 0:
            st, path, sz = "unused", "", ""
        else:
            hits = by_lba.get(lba, [])
            if len(hits) == 1:
                path, sz = hits[0]
                st = "ok"
                if (lba, path) in seen:
                    st = "dup"
                seen.setdefault((lba, path), n)
                used.add(path)
            elif hits:
                st, path, sz = "ambiguous", "|".join(h[0] for h in hits), ""
            else:
                st, path, sz = "nofile", "", ""
        stats[st] += 1
        print(f"{n}\t{lba}\t{cnt}\t{st}\t{path}\t{sz}")
    sys.stderr.write(f"numbers in table: {len(sectors)}\n")
    for k in ("ok", "dup", "unused", "ambiguous", "nofile"):
        sys.stderr.write(f"  {k}: {stats[k]}\n")
    sys.stderr.write(f"disc files: {len(files)}; referenced by a number: {len(used)}\n")
    for p, lba, sz in files:
        if p not in used:
            sys.stderr.write(f"  not in table: {p} (lba {lba}, {sz} bytes)\n")


if __name__ == "__main__":
    main()
