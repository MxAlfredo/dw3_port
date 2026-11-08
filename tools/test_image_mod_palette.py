"""Tests of tools/image_mod_palette.py on synthetic PNGs (no game data). Run: python3 -I tools/test_image_mod_palette.py"""
import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import image_mod_palette as imp  # noqa: E402

BLUE = (0, 0, 255)


def make_png(path, pal):
    """A 2x2 indexed PNG (index 0 transparent) with the palette `pal`."""
    def chunk(typ, body):
        return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes([i % len(pal) for i in (0, 1)]) for _ in range(2))
    data = (imp.SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 3, 0, 0, 0)) + chunk(b"PLTE", b"".join(bytes(c) for c in pal))
            + chunk(b"tRNS", b"\x00") + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Path(path).write_bytes(data)


def plte(path):
    for typ, body in imp.chunks(Path(path).read_bytes()):
        if typ == b"PLTE":
            return [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]


class ModPalette(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.data = os.path.join(self.tmp.name, "data")
        self.mod = os.path.join(self.tmp.name, "mod")
        for rel in ("menus/stshop/f000p00.png", "menus/stshop/f001p00.png", "sprites/p001play/000.png", "cards/c001/face.png",
                    "partners/kotemon/portrait.png", "images/screen/stwmp0tm/00_00.png"):
            make_png(os.path.join(self.data, rel), [(0, 0, 0), BLUE])

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = imp.main(["--data", self.data, "--mod", self.mod, *argv])
        return rc, out.getvalue()

    def test_default_is_the_shop_menu_pieces_at_the_same_path(self):
        rc, _ = self.run_tool()
        self.assertEqual(rc, 0)
        for n in ("f000p00.png", "f001p00.png"):
            self.assertNotEqual(plte(os.path.join(self.mod, "menus/stshop", n))[1], BLUE)  # recoloured (gold)
        self.assertEqual(plte(os.path.join(self.mod, "menus/stshop/f000p00.png"))[0], (0, 0, 0))
        self.assertFalse(os.path.exists(os.path.join(self.mod, "images")))

    def test_sprite_reads_sprites_and_writes_sprites(self):
        rc, _ = self.run_tool("--sprite", "P001PLAY", "--to", "0")
        self.assertEqual(rc, 0)
        self.assertEqual(plte(os.path.join(self.mod, "sprites/p001play/000.png"))[1], (255, 0, 0))  # blue -> red
        self.assertFalse(os.path.exists(os.path.join(self.mod, "images")))

    def test_any_topic_path_and_folder(self):
        rc, _ = self.run_tool("cards/c001", "partners/kotemon/portrait.png", "images/screen/stwmp0tm")
        self.assertEqual(rc, 0)
        for rel in ("cards/c001/face.png", "partners/kotemon/portrait.png", "images/screen/stwmp0tm/00_00.png"):
            self.assertTrue(os.path.exists(os.path.join(self.mod, rel)), rel)

    def test_list_names_every_topic_png(self):
        rc, out = self.run_tool("--list")
        self.assertEqual(rc, 0)
        self.assertEqual(sorted(out.split()), ["cards/c001/face.png", "images/screen/stwmp0tm/00_00.png", "menus/stshop/f000p00.png",
                                               "menus/stshop/f001p00.png", "partners/kotemon/portrait.png", "sprites/p001play/000.png"])

    def test_the_old_images_path_is_not_a_name(self):
        rc, out = self.run_tool("field/sprt/p001play")
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.exists(self.mod))


if __name__ == "__main__":
    unittest.main()
