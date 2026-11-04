#!/usr/bin/env python3
"""Checks the host RLEN unpacker against the game's own decompressor.

Generates RLEN vectors with the packer this repository already uses
(tools/es419/menu_bank.py's pack_rlen), feeds each one to pc/build/rlen_tool
and compares the bytes it writes with the original data, byte for byte.

    make -C pc
    .venv/bin/python pc/check_rlen.py

The vectors are generated here, at check time: nothing packed is committed
(ADR-001). Exits non-zero on the first failing vector and reports each one.
"""
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "es419"))

from menu_bank import pack_rlen  # noqa: E402

TOOL = ROOT / "pc" / "build" / "rlen_tool"


def vectors():
    """(name, data) pairs: the run length encoding's cases, then junk."""
    cases = [
        ("empty", b""),
        ("one_byte", b"A"),
        ("two_same", b"\x5a\x5a"),  # the shortest run (min_run is 2)
        ("two_diff", b"\x5a\x5b"),
        ("literals_only", bytes(range(200))),  # longer than 127: two literal chunks
        ("zeros_run", b"\x00" * 10),  # a run of the terminator's own byte
        ("zero_between_literals", b"\x00A\x00B\x00"),
        ("run_127", b"z" * 127),  # exactly the run cap
        ("run_128", b"z" * 128),  # the packer splits it
        ("run_300", bytes([7]) * 300),
        ("run_then_literal", b"w" * 40 + b"tail"),
        ("literal_then_run", b"head" + b"w" * 40),
        ("all_256_bytes", bytes(range(256))),
        ("alternating", b"\xaa\xbb" * 100),
        ("mixed", b"ABCD" + b"E" * 5 + b"FGH" + b"\xff" * 200 + b"xyz"),
        ("text_like", b"RLEN host check " * 200),
    ]

    rnd = random.Random(0xD161)  # deterministic, rerunnable
    cases.append(("random_64k", bytes(rnd.randrange(256) for _ in range(65536))))

    # Random data with runs sprinkled in: exercises both coders at once.
    junk = bytearray()
    while len(junk) < 65536:
        if rnd.random() < 0.5:
            junk += bytes([rnd.randrange(256)]) * rnd.randrange(1, 400)
        else:
            junk += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 300)))
    cases.append(("random_with_runs", bytes(junk)))

    return cases


def run_case(name, data, tmp):
    packed = pack_rlen(data)
    if packed[:4] != b"RLEN":
        return f"{name}: pack_rlen did not produce an RLEN header"
    src = tmp / f"{name}.rlen"
    dst = tmp / f"{name}.out"
    src.write_bytes(packed)

    proc = subprocess.run([str(TOOL), str(src), str(dst)], capture_output=True, text=True)
    if proc.returncode != 0:
        return f"{name}: rlen_tool exited {proc.returncode}: {proc.stderr.strip()}"

    got = dst.read_bytes()
    if got != data:
        if len(got) != len(data):
            return f"{name}: {len(got)} bytes out, {len(data)} expected"
        i = next(k for k in range(len(data)) if got[k] != data[k])
        return f"{name}: first differing byte at {i}: {got[i]:#04x} != {data[i]:#04x}"

    print(f"ok   {name}: {len(data)} bytes unpacked")
    return None


def main():
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    failures = 0
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for name, data in vectors():
            error = run_case(name, data, tmp)
            if error is not None:
                print(f"FAIL {error}")
                failures += 1

    if failures != 0:
        sys.exit(f"{failures} vector(s) failed")
    print("all vectors unpacked byte for byte")


if __name__ == "__main__":
    main()
