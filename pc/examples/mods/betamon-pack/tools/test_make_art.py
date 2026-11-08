#!/usr/bin/env python3
"""Determinism and limits of make_art.py (needs the SpriteKiln venv: ~/.cache/spritekiln-venv/bin/python test_make_art.py)."""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def run(out):
    subprocess.run([sys.executable, os.path.join(HERE, "make_art.py"), "--out", out, "--river-out", out + "-river"], check=True,
                   capture_output=True)


def tree_hash(root):
    h = hashlib.sha256()
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            h.update(os.path.relpath(path, root).encode())
            with open(path, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


class MakeArt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.a = os.path.join(cls.tmp.name, "a")
        cls.b = os.path.join(cls.tmp.name, "b")
        cls.ra = cls.a + "-river"
        run(cls.a)
        run(cls.b)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_two_runs_are_identical(self):
        self.assertEqual(tree_hash(self.a), tree_hash(self.b))
        self.assertEqual(tree_hash(self.ra), tree_hash(self.b + "-river"))

    def test_portrait_icon_and_limits(self):
        from PIL import Image

        p = Image.open(os.path.join(self.a, "portrait.png"))
        self.assertEqual(p.mode, "P")
        self.assertEqual(p.height, 32)
        self.assertIn(p.width // 32, range(1, 9))
        self.assertEqual(p.width % 32, 0)
        self.assertEqual(Image.open(os.path.join(self.a, "icon.png")).size, (44, 28))
        for base, _, files in [*os.walk(self.a), *os.walk(self.ra)]:
            for name in files:
                if name.endswith(".png"):
                    im = Image.open(os.path.join(base, name))
                    self.assertEqual(im.mode, "P", name)
                    self.assertIn("transparency", im.info, name)
                    self.assertLessEqual(len(set(im.getdata())), 16, name)

    def test_training_and_field_json(self):
        with open(os.path.join(self.a, "field", "sprite.json")) as f:
            spr = json.load(f)
        ids = [x["id"] for x in spr["frames"]] + [x["id"] for x in spr["animations"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({s["id"] for s in spr["sets"]} >= {1, 5, 6}, True)
        names = sorted(os.listdir(os.path.join(self.a, "training")))
        self.assertEqual(len(names), 12)
        for n in names:
            with open(os.path.join(self.a, "training", n, "training.json")) as f:
                t = json.load(f)
            self.assertEqual(t["format"], "partners-training-v1")
            self.assertEqual({a["id"] for a in t["animations"]} >= {0, 1, 2, 3}, True)

    def test_field_follower_is_not_bigger_than_the_originals(self):
        """The original followers are about 20-26 x 23-31 px (docs/modding/README.md 6.9b): Betamon stays inside that box."""
        from PIL import Image

        for name in os.listdir(os.path.join(self.a, "field")):
            if name.endswith(".png"):
                w, h = Image.open(os.path.join(self.a, "field", name)).size
                self.assertLessEqual(w, 26, name)
                self.assertLessEqual(h, 31, name)

    def test_river_sprite(self):
        with open(os.path.join(self.ra, "sprite.json")) as f:
            spr = json.load(f)
        self.assertEqual(spr["format"], "sprite-v1")
        self.assertEqual(len(spr["frames"]), 40)  # 8 idle frames x 5 facings
        self.assertEqual(len(spr["directions"]), 8)
        for a in spr["animations"]:
            self.assertTrue(a["loop"])
            self.assertEqual(len(a["steps"]), 8)
        for fr in spr["frames"]:
            self.assertTrue(os.path.exists(os.path.join(self.ra, fr["image"])))


if __name__ == "__main__":
    unittest.main()
