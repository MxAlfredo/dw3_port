"""Tests of tools/techcompat.py on synthetic bytes (no disc data)."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import techcompat  # noqa: E402


def archive(present):
    """A file with one entry: a table of 18 script offsets (0 = empty), then one dummy script word per present slot."""
    table_at = 4  # entry 0 -> offset 4
    body = bytearray()
    offs = []
    for i in range(18):
        offs.append(0 if i not in present else 18 * 4 + len(body))
        if i in present:
            body += struct.pack("<I", 0xFFFF)
    # the table sits at `table_at`; the first slot's offset must be 72 (table end) for the count: force slot 0 nonzero-or-72
    first = offs[0] if offs[0] else 72
    table = struct.pack("<18I", *([first] + offs[1:]))
    # a fighter with an empty slot 0 would read count from the first offset: keep it >= 72 by construction
    return struct.pack("<I", table_at) + table + bytes(body)


def fighters_file(entries, partner_effects, enemy_effects):
    """entries: [(id, index, kind)]; partner/enemy FighterInfo with `effects` (+8) = (file << 16) | 0."""
    entries_at = 16
    partners_at = entries_at + 4 * (len(entries) + 1)
    enemies_at = partners_at + techcompat.PARTNER_INFO * len(partner_effects)
    out = bytearray(enemies_at + techcompat.ENEMY_INFO * len(enemy_effects))
    struct.pack_into("<III", out, 4, entries_at, partners_at, enemies_at)
    for i, (fid, idx, kind) in enumerate(entries):
        struct.pack_into("<hBB", out, entries_at + 4 * i, fid, idx, kind)
    for i, fnum in enumerate(partner_effects):
        struct.pack_into("<I", out, partners_at + techcompat.PARTNER_INFO * i + 8, fnum << 16)
    for i, fnum in enumerate(enemy_effects):
        struct.pack_into("<I", out, enemies_at + techcompat.ENEMY_INFO * i + 8, fnum << 16)
    return bytes(out)


FULL = set(range(18))
FILES = {10: archive(FULL), 11: archive({0, 1, 2, 3, 4, 5})}


class Parse(unittest.TestCase):
    def setUp(self):
        self.f = fighters_file([(3, 0, 0x10), (4, 0, 0x40), (5, 1, 0x41), (6, 1, 0x11)], [10, 0], [11, 11])
        self.fighters = techcompat.parse_fighters(self.f, FILES.__getitem__)

    def test_kinds_and_slots(self):
        self.assertFalse(self.fighters[3]["enemy"])
        self.assertTrue(self.fighters[4]["enemy"])
        self.assertEqual([i for i, on in enumerate(self.fighters[3]["slots"]) if on], list(range(18)))
        self.assertEqual([i for i, on in enumerate(self.fighters[4]["slots"]) if on], [0, 1, 2, 3, 4, 5])

    def test_no_archive_means_no_slots(self):
        self.assertFalse(any(self.fighters[6]["slots"]))  # partner index 1 has effects 0

    def test_analyze(self):
        scripts = [5, 5, 3, 12, 0]
        s = techcompat.analyze(self.fighters, scripts)
        self.assertEqual((s["fighters"], s["partners"], s["enemies"]), (4, 2, 2))
        self.assertEqual(s["without_archive"], [6])
        by = {r["script"]: r for r in s["scripts"]}
        self.assertEqual((by[12]["partners_ok"], by[12]["partners"], by[12]["enemies_ok"]), (1, 1, 0))
        self.assertEqual((by[3]["partners_ok"], by[3]["enemies_ok"], by[3]["enemies"]), (1, 2, 2))
        # script 5 on an enemy only needs slot 5; a partner needs 6 and 8 too (the full one has them)
        self.assertEqual((by[5]["partners_ok"], by[5]["enemies_ok"]), (1, 2))

    def test_needed_slots(self):
        self.assertEqual(techcompat.needed_slots(5, True), [5, 6, 8])
        self.assertEqual(techcompat.needed_slots(5, False), [5])
        self.assertEqual(techcompat.needed_slots(6, True), [6, 8])
        self.assertEqual(techcompat.needed_slots(9, True), [9])

    def test_truncated_input_does_not_crash(self):
        short = techcompat.parse_fighters(self.f[:60], FILES.__getitem__)
        self.assertIsInstance(short, dict)


if __name__ == "__main__":
    unittest.main()
