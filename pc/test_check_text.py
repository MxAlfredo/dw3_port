"""Unit tests for the es-419 mode of pc/check_text.py.

These tests use synthetic data and do not require a disc. T9's patched-disc
end-to-end proof was run separately with artifacts under ignored build/; this
suite covers the layout-parser/row-validation/LBA-sector-reading/mapping layer.

Run with:
    .venv/bin/python -m unittest pc.test_check_text
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# pc/ is not a package (no __init__.py) by repo convention; the canonical
# verify command is `python -m unittest pc.test_check_text`, so we put the
# directory itself on sys.path to make the `check_text` importable here.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_text  # noqa: E402
import gen_table  # noqa: E402


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


# A two-row layout that exercises the parser against the schema documented in
# tools/es419/gen_table.py:95-99 (file, index=usa_slot(k), spanish_index, lba,
# sectors). Uses well-known k values from gen_table.K_OF_FILE.
def _layout_json(first_lba=1000, rows=None):
    rows = rows if rows is not None else [
        {"file": "SSSTNAME.BIN", "k": 26,
         "index": gen_table.usa_slot(26), "spanish_index": gen_table.spanish_index(26),
         "lba": 1000, "sectors": 2},
        {"file": "SSAMTMAP.BIN", "k": 0,
         "index": gen_table.usa_slot(0), "spanish_index": gen_table.spanish_index(0),
         "lba": 1002, "sectors": 1},
    ]
    last_lba = rows[-1]["lba"] + rows[-1]["sectors"] - 1
    return json.dumps({"first_lba": first_lba, "last_lba": last_lba, "files": rows})


def _write(tmp: Path, body: str) -> Path:
    p = tmp / "es419_layout.json"
    p.write_text(body, encoding="utf-8")
    return p


class ReadLayout(unittest.TestCase):
    def test_missing_file_raises(self):
        with self.assertRaises(FileNotFoundError):
            check_text.read_layout(HERE / "missing-es419_layout.json")

    def test_parses_top_level_keys(self):
        rows = [
            {"file": "SSSTNAME.BIN", "k": 26,
             "index": gen_table.usa_slot(26), "spanish_index": gen_table.spanish_index(26),
             "lba": 1000, "sectors": 2},
        ]
        path = _write(HERE, _layout_json(first_lba=1000, rows=rows))
        try:
            layout = check_text.read_layout(path)
            self.assertEqual(layout["first_lba"], 1000)
            self.assertEqual(layout["last_lba"], 1001)
            self.assertEqual(len(layout["files"]), 1)
        finally:
            path.unlink()

    def test_rows_carry_file_spanish_index_lba_sectors(self):
        rows = [
            {"file": "SSSTNAME.BIN", "k": 26,
             "index": gen_table.usa_slot(26), "spanish_index": gen_table.spanish_index(26),
             "lba": 1000, "sectors": 2},
            {"file": "SSAMTMAP.BIN", "k": 0,
             "index": gen_table.usa_slot(0), "spanish_index": gen_table.spanish_index(0),
             "lba": 1002, "sectors": 1},
        ]
        path = _write(HERE, _layout_json(rows=rows))
        try:
            layout = check_text.read_layout(path)
            self.assertEqual(layout["files"][0]["file"], "SSSTNAME.BIN")
            self.assertEqual(layout["files"][0]["spanish_index"], gen_table.spanish_index(26))
            self.assertEqual(layout["files"][0]["lba"], 1000)
            self.assertEqual(layout["files"][0]["sectors"], 2)
        finally:
            path.unlink()


class ValidateLayout(unittest.TestCase):
    def setUp(self):
        self.good_rows = [
            {"file": "SSSTNAME.BIN", "k": 26,
             "index": gen_table.usa_slot(26), "spanish_index": gen_table.spanish_index(26),
             "lba": 1000, "sectors": 2},
            {"file": "SSAMTMAP.BIN", "k": 0,
             "index": gen_table.usa_slot(0), "spanish_index": gen_table.spanish_index(0),
             "lba": 1002, "sectors": 1},
        ]

    def test_good_layout_passes(self):
        check_text.validate_layout(_layout_json_obj(self.good_rows))

    def test_missing_file_field_rejected(self):
        rows = [dict(self.good_rows[0])]
        rows[0].pop("file")
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))

    def test_missing_spanish_index_rejected(self):
        rows = [dict(self.good_rows[0])]
        rows[0].pop("spanish_index")
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))

    def test_zero_or_negative_sectors_rejected(self):
        rows = [dict(self.good_rows[0])]
        rows[0]["sectors"] = 0
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))

    def test_unknown_file_rejected(self):
        rows = [dict(self.good_rows[0])]
        rows[0]["file"] = "NOSUCH.BIN"
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))

    def test_mismatched_spanish_index_rejected(self):
        rows = [dict(self.good_rows[0])]
        rows[0]["spanish_index"] = 999
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))

    def test_duplicate_file_rejected(self):
        rows = [dict(self.good_rows[0]), dict(self.good_rows[0])]
        with self.assertRaises(ValueError):
            check_text.validate_layout(_layout_json_obj(rows))


class LbaSectorReading(unittest.TestCase):
    """`read_sectors_at` is the LBA-driven sector reader the es-419 mode needs.

    It accepts any object exposing seek(offset)/read(n) and returns exactly
    sectors * DATA_SIZE bytes starting at (lba * SECTOR + USER_DATA), the same
    framing check_text.read_sectors uses.
    """

    SECTOR = check_text.SECTOR
    USER_DATA = check_text.USER_DATA
    DATA_SIZE = check_text.DATA_SIZE

    def _make_image(self, sector_count: int) -> bytes:
        """A multi-sector image whose data area is recognizably indexed by LBA.

        Each data byte at LBA s, offset b is 0x80 + s; the surrounding framing
        (sync, header, subheader, EDC/ECC) is left at 0. read_sectors_at must
        follow the framing; reading contiguously would smear that into the
        results, so any framing bug surfaces as the wrong byte in out[].
        """
        body = bytearray(self.SECTOR * sector_count)
        for s in range(sector_count):
            base = s * self.SECTOR + self.USER_DATA
            for b in range(self.DATA_SIZE):
                body[base + b] = (0x80 + s) & 0xFF
        return bytes(body)

    def test_skip_precedes_data(self):
        """Sector 0: leading SECTOR bytes are framing, then DATA_SIZE data bytes."""
        body = self._make_image(2)
        img = _BytesImage(body)
        out = check_text.read_sectors_at(img, lba=0, sectors=1)
        self.assertEqual(len(out), self.DATA_SIZE)
        # All bytes must come from sector 0's data area (0x80).
        self.assertEqual(set(out), {0x80})

    def test_reads_exact_data_bytes_per_sector(self):
        body = self._make_image(4)
        img = _BytesImage(body)
        out = check_text.read_sectors_at(img, lba=2, sectors=2)
        self.assertEqual(len(out), 2 * self.DATA_SIZE)
        # First half = sector 2 data (0x80 + 2 = 0x82), second = sector 3 (0x83).
        self.assertEqual(set(out[:self.DATA_SIZE]), {0x82})
        self.assertEqual(set(out[self.DATA_SIZE:]), {0x83})

    def test_concat_multi_sector(self):
        body = self._make_image(3)
        img = _BytesImage(body)
        out = check_text.read_sectors_at(img, lba=0, sectors=3)
        self.assertEqual(len(out), 3 * self.DATA_SIZE)
        # Each sector is its own byte.
        for s in range(3):
            chunk = out[s * self.DATA_SIZE:(s + 1) * self.DATA_SIZE]
            self.assertEqual(set(chunk), {(0x80 + s) & 0xFF})


class CaseToLayoutMapping(unittest.TestCase):
    """The hard cases -> es-419 row mapping check_text exposes.

    The es-419 layout covers the 50 file-table indices of the Spanish text
    files (K_OF_FILE). Our existing CASES (SSSTNAME/SSAMTMAP/SSASKMAP) all
    live in K_OF_FILE; cases_from_layout returns the row keyed by file name
    and the `measurement_slot` the checker hands to `text_measure`.
    """

    def setUp(self):
        self.rows = [
            {"file": "SSSTNAME.BIN", "k": 26,
             "index": gen_table.usa_slot(26), "spanish_index": gen_table.spanish_index(26),
             "lba": 1000, "sectors": 2},
            {"file": "SSAMTMAP.BIN", "k": 0,
             "index": gen_table.usa_slot(0), "spanish_index": gen_table.spanish_index(0),
             "lba": 1002, "sectors": 1},
            {"file": "SSASKMAP.BIN", "k": 1,
             "index": gen_table.usa_slot(1), "spanish_index": gen_table.spanish_index(1),
             "lba": 1003, "sectors": 1},
        ]

    def test_returns_row_per_case(self):
        layout = check_text.read_layout_dict(self.rows)
        cases = [("SSSTNAME.BIN", [0, 30, 71, 84]),
                 ("SSAMTMAP.BIN", [0, 1, 14, 21, 27, 29, 44]),
                 ("SSASKMAP.BIN", [0, 1, 14, 21, 27, 29, 44])]
        mapped = check_text.cases_from_layout(layout, cases)
        self.assertEqual(len(mapped), 3)
        self.assertEqual(mapped[0]["name"], "SSSTNAME.BIN")
        self.assertEqual(mapped[0]["indices"], [0, 30, 71, 84])
        # The measurement slot is the USA one (file_table.c's redirect at
        # ES419=1); spanish_index stays as informational only.
        self.assertEqual(mapped[0]["measurement_slot"], gen_table.usa_slot(26))
        self.assertEqual(mapped[0]["lba"], 1000)
        self.assertEqual(mapped[0]["sectors"], 2)

    def test_unknown_case_file_raises(self):
        layout = check_text.read_layout_dict(self.rows)
        cases = [("NOPE.BIN", [0, 1])]
        with self.assertRaises(ValueError):
            check_text.cases_from_layout(layout, cases)

    def test_measurement_slot_is_usa_slot(self):
        """Regression guard: text_measure must be driven by the USA slot.

        file_table.c (file_table.c:10-16, 852...) compiles ES419_SLOT_SECTOR(k, ...)
        into FILE_SECTORS at the USA positions 2+7k when ES419=1. The host
        FILE_CACHE.load() therefore returns the overlay's sectors when asked by
        usa_slot(k), not by spanish_index. The checker must drive text_measure
        with the layout row's `index` (USA slot); spanish_index is what the
        unpatched retail disc carried and would measure the wrong file.
        """
        layout = check_text.read_layout_dict(self.rows)
        cases = [("SSSTNAME.BIN", [0])]
        mapped = check_text.cases_from_layout(layout, cases)
        self.assertEqual(mapped[0]["measurement_slot"], gen_table.usa_slot(26))
        # Sanity: spanish_index and usa_slot are different for every k.
        self.assertNotEqual(mapped[0]["measurement_slot"], gen_table.spanish_index(26))


class ArgvParsing(unittest.TestCase):
    """parse_args(argv) is the pure function the checker uses for its CLI.

    The checker has two modes: the EU base mode (image only, default), and
    the explicit es-419 mode (image plus `--es419-layout <path>`). The es-419
    mode is opt-in only -- no implicit default that silently skips -- so an
    explicit but missing or malformed path is a hard failure (SystemExit).
    """

    DISC = "disc.bin"

    def test_eu_only_returns_no_layout(self):
        image, layout = check_text.parse_args([self.DISC])
        self.assertEqual(image, self.DISC)
        self.assertIsNone(layout)

    def test_explicit_layout_returns_path(self):
        image, layout = check_text.parse_args([self.DISC, "--es419-layout", "layout.json"])
        self.assertEqual(image, self.DISC)
        self.assertEqual(layout, "layout.json")

    def test_layout_without_value_exits(self):
        with self.assertRaises(SystemExit):
            check_text.parse_args([self.DISC, "--es419-layout"])

    def test_no_args_exits(self):
        with self.assertRaises(SystemExit):
            check_text.parse_args([])

    def test_help_exits_clean(self):
        with self.assertRaises(SystemExit):
            check_text.parse_args(["--help"])

    def test_too_many_positionals_exits(self):
        with self.assertRaises(SystemExit):
            check_text.parse_args([self.DISC, "extra", "--es419-layout", "layout.json"])


class HostBuildConfig(unittest.TestCase):
    def test_es419_build_includes_generated_table_directory_only_when_enabled(self):
        # Include the real Makefile, but invoke a tiny explicit target so no
        # host binaries or generated disc artifacts are needed.
        with tempfile.TemporaryDirectory() as tmp:
            probe = Path(tmp) / "probe.mk"
            probe.write_text(
                f"include {ROOT / 'pc/Makefile'}\n"
                ".PHONY: print-game-inc\n"
                "print-game-inc:\n"
                "\t@printf '%s\\n' '$(GAME_INC)'\n",
                encoding="utf-8",
            )
            values = {}
            for enabled in ("0", "1"):
                result = subprocess.run(
                    ["make", "--no-print-directory", "-s", "-f", str(probe),
                     f"ES419={enabled}", "print-game-inc"],
                    cwd=ROOT / "pc", capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                values[enabled] = result.stdout.strip()

        generated_dir = str(ROOT / "build/eu/es419")
        self.assertNotIn(generated_dir, values["0"])
        self.assertIn(f"-I{generated_dir}", values["1"])


# --- helpers ---------------------------------------------------------------


# --- helpers ---------------------------------------------------------------


def _layout_json_obj(rows):
    """Return the layout dict the way tools/es419/gen_table.py:95-99 emits it."""
    first = rows[0]["lba"]
    last = rows[-1]["lba"] + rows[-1]["sectors"] - 1
    return {"first_lba": first, "last_lba": last, "files": rows}


class _BytesImage:
    """Minimal image-like object for read_sectors_at: seek/read against bytes."""

    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0

    def seek(self, offset: int) -> int:
        self._pos = offset
        return self._pos

    def read(self, n: int) -> bytes:
        out = self._data[self._pos:self._pos + n]
        self._pos += len(out)
        return out


if __name__ == "__main__":
    unittest.main()
