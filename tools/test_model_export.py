"""Tests of tools/model_export.py on synthetic bytes (no disc data). Run: python3 -I tools/test_model_export.py"""
import copy
import math
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import model_export as me  # noqa: E402


def archive(parts):
    """A table of u32 offsets (relative to the archive start) followed by the parts, 4-aligned."""
    off = 4 * len(parts)
    table, body = [], b""
    for p in parts:
        table.append(off + len(body))
        body += p + b"\0" * (-len(p) % 4)
    return struct.pack("<%dI" % len(parts), *table) + body


def mesh_archive(verts, norms, cmds):
    vb = struct.pack("<3h", len(verts), 0, 0) + b"".join(struct.pack("<3h", *v) for v in verts)
    nb = struct.pack("<3h", len(norms), 0, 0) + b"".join(struct.pack("<3h", *n) for n in norms)
    return archive([vb, nb, bytes(cmds), b"", b"", struct.pack("<27h", *([0] * 27))])


def keys_archive(pos, rot, scl):
    lists = [b"".join(struct.pack("<4h", *k) for k in ks) for ks in (pos, rot, scl)]
    head = 16
    offs = [head]
    for ls in lists:
        offs.append(offs[-1] + len(ls))
    return struct.pack("<4I", *offs) + b"".join(lists)


def tim_archive():
    """One mode-2 TIM, 64x256 halfwords: pixel (u=1, v=0) has index 5; CLUT at row 255, x 0: index 5 = red."""
    img = [0] * (64 * 256)
    img[0] = 5 << 4
    img[255 * 64 + 5] = 0x001F
    data = struct.pack("<%dH" % len(img), *img)
    tim = struct.pack("<II", 0x10, 2) + struct.pack("<IHHHH", 12 + len(data), 0, 0, 64, 256) + data
    return struct.pack("<2I", 8, 0) + tim


SENTINEL = 30000


def synthetic_file():
    """Model entry + a 2-bone skeleton: bone 1 visible (a lit textured triangle + a flat quad), bone 2 hidden."""
    cmds1 = [0x01, 0, 0, 0, 0, 255, 0,            # texture: u0, page 0, v0, clut (0, 255), mode 0
             0x91, 0xC1, 0x80,                    # textured, lit, triangles
             0x00, 0, 1, 2, 0, 0, 0, 1, 0, 2, 0, 0, 3,   # poly: idx, normals, uv (1,0) (2,0) (0,3)
             0x90, 0xC0, 0x81, 0x02, 10, 20, 30,  # untextured, unlit, quads, colour slot 0
             0x00, 0, 1, 2, 3,
             0xFF]
    verts1 = [(0, 0, 0), (100, 0, 0), (0, 100, 0), (100, 100, 0)]
    mesh1 = mesh_archive(verts1, [(0, 0, 4096)], cmds1)
    mesh2 = mesh_archive([(0, 0, 0), (10, 0, 0), (0, 10, 0)], [(0, 0, 4096)],
                         [0x90, 0xC0, 0x80, 0x02, 1, 2, 3, 0x00, 0, 1, 2, 0xFF])
    keys1 = keys_archive([(0, 50, 0, 10), (0, 50, 0, SENTINEL)],
                         [(0, 0, 0, 10), (0, 0, 1024, 12), (0, 0, 1024, SENTINEL)],
                         [(4096, 4096, 4096, 10), (4096, 4096, 4096, SENTINEL)])
    keys2 = keys_archive([(5, 0, 0, 10), (5, 0, 0, SENTINEL)], [(0, 0, 0, 10), (0, 0, 0, SENTINEL)],
                         [(0, 0, 0, 10), (0, 0, 0, SENTINEL)])
    model = struct.pack("<8I", 1, 2, 0, 2, 3, 1, 4, 5)
    motions = bytearray(archive([
        # slot 0: three playback frames 10..12 then repeat from the start
        struct.pack("<4h", 0, 3, 10, 13) + struct.pack("<4h", 1, 0, -32768, 0) + struct.pack("<4h", 0x7FFF, 0, 0, 0),
        b"",
        # slot 2: blend (2 steps) towards frame 12, then play 12..13, then end
        struct.pack("<4h", 0, 2, 0, 0) + struct.pack("<4h", 1, 2, 12, 13) + struct.pack("<4h", 2, 0, -1, 0)
        + struct.pack("<4h", 0x7FFF, 0, 0, 0),
    ]))
    motions[4:8] = b"\0\0\0\0"   # slot 1 is empty: offset 0
    parts = [model, tim_archive(), mesh1, keys1, mesh2, keys2, motions]
    return archive(parts)


class TestParsers(unittest.TestCase):
    def setUp(self):
        self.d = synthetic_file()
        self.model = me.parse_model(self.d, me.u32(self.d, 0))

    def test_model_structure(self):
        b = self.model["bones"]
        self.assertEqual(len(b), 3)
        self.assertEqual([x["parent"] for x in b], [0, 0, 1])
        self.assertEqual(self.model["tims"], 1)

    def test_mesh_commands(self):
        polys = self.model["bones"][1]["mesh"]["polys"]
        self.assertEqual(len(polys), 2)
        tri, quad = polys
        self.assertEqual(tri["idx"], [0, 1, 2])
        self.assertEqual(tri["nrm"], [0, 0, 0])
        self.assertEqual(tri["uv"], [(1, 0), (2, 0), (0, 3)])
        self.assertEqual(tri["tex"], (0, 0, 0, 255))
        self.assertEqual(len(quad["idx"]), 4)
        self.assertIsNone(quad["uv"])
        self.assertEqual(quad["color"], (10, 20, 30))

    def test_bad_mesh_index(self):
        bad = mesh_archive([(0, 0, 0)] * 2, [(0, 0, 4096)], [0x80, 0x00, 0, 1, 2, 0xFF])
        with self.assertRaises(ValueError):
            me.parse_mesh(bad, 0)

    def test_keys_and_sampling(self):
        k = self.model["bones"][1]["rot"]
        self.assertEqual(k[0], (0, 0, 0, 10))
        self.assertEqual(me.sample_keys(k, 10), (0, 0, 0))
        self.assertEqual(me.sample_keys(k, 11), (0, 0, 512))   # halfway
        self.assertEqual(me.sample_keys(k, 12), (0, 0, 1024))
        self.assertEqual(me.sample_keys(k, 5), (0, 0, 0))      # before the first key: first key
        self.assertEqual(me.first_key_at(k, 11), (0, 0, 1024))

    def test_tim_variant_and_png(self):
        page = me.variant_rgba(self.model["vram"], 0, 0, 0, 255)
        self.assertEqual(len(page), 256 * 256 * 4)
        self.assertEqual(page[4:8], bytes([255, 0, 0, 255]))   # pixel (1, 0): CLUT 5 = red
        self.assertEqual(page[0:4], b"\0\0\0\0")                # colour 0 is transparent
        png = me.write_png(256, 256, page)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        idat = png[png.index(b"IDAT") + 4:png.index(b"IEND") - 8]
        raw = zlib.decompress(idat)
        self.assertEqual(len(raw), 256 * (1 + 1024))
        self.assertEqual(raw[1 + 4:1 + 8], bytes([255, 0, 0, 255]))

    def test_stp_alpha_per_texel(self):
        vram = copy.deepcopy(self.model["vram"])
        vram[255][5] = 0x801F                                   # red with the STP bit
        opaque = me.variant_rgba(vram, 0, 0, 0, 255)
        semi = me.variant_rgba(vram, 0, 0, 0, 255, semi_alpha=0.5)
        self.assertEqual(opaque[4:8], bytes([255, 0, 0, 255]))  # not semi-transparent: STP ignored
        self.assertEqual(semi[4:8], bytes([255, 0, 0, 128]))    # STP texel: blended
        self.assertEqual(semi[0:4], b"\0\0\0\0")               # colour 0 stays fully transparent
        vram[255][5] = 0x001F                                   # no STP bit: opaque even when semi-transparent
        self.assertEqual(me.variant_rgba(vram, 0, 0, 0, 255, semi_alpha=0.5)[4:8], bytes([255, 0, 0, 255]))

    def test_poly_blends(self):
        tri = copy.deepcopy(self.model["bones"][1]["mesh"]["polys"][0])
        vram = copy.deepcopy(self.model["vram"])
        self.assertFalse(me.poly_blends(vram, tri))             # abr 0
        tri["abr"] = 1
        self.assertFalse(me.poly_blends(vram, tri))             # semi-transparent but no STP texel
        vram[255][5] = 0x801F
        self.assertTrue(me.poly_blends(vram, tri))
        flat = dict(tri, uv=None, tex=None)
        self.assertTrue(me.poly_blends(vram, flat))             # untextured: always blended

    def test_rlen(self):
        blk = b"RLEN" + b"\0\0\0\0" + bytes([2, 7, 8, 0x83, 9, 0])
        self.assertEqual(me.rlen(blk, 0), bytes([7, 8, 9, 9, 9]))

    def test_parse_motions(self):
        m = me.parse_motions(self.d, me.u32(self.d, 6 * 4))
        self.assertEqual(len(m), 3)
        self.assertIsNone(m[1])
        self.assertEqual(m[0][0], (0, 3, 10, 13))

    def test_fighter_spec(self):
        self.assertEqual(me.parse_fighter_spec("383"), 383)
        self.assertEqual(me.parse_fighter_spec("dw3:digimon/25"), 25)


class TestMotion(unittest.TestCase):
    def test_playback_and_loop(self):
        s, meta = me.expand_motion([(0, 3, 10, 13), (1, 0, -32768, 0), (0x7FFF, 0, 0, 0)], 10)
        self.assertEqual(s, [("f", 10), ("f", 11), ("f", 12)])
        self.assertEqual(meta, dict(loop_start=0, ends=False))

    def test_blend_to_idle_and_end(self):
        steps = [(0, 2, 0, 0), (1, 2, 12, 13), (2, 0, -1, 0), (0x7FFF, 0, 0, 0)]
        s, meta = me.expand_motion(steps, 10)
        self.assertEqual(s[0], ("b", None, 12, 1365))
        self.assertEqual(s[1], ("b", None, 12, 2730))
        self.assertEqual(s[2:], [("f", 12), ("f", 13)])
        self.assertTrue(meta["ends"])
        steps = [(0, 1, 0, 0), (1, 0, -1, 0), (0x7FFF, 0, 0, 0)]   # blend towards -1: the idle frame
        s, meta = me.expand_motion(steps, 10)
        self.assertEqual(s, [("b", None, 10, 2048)])

    def test_blend_pose(self):
        model = {"bones": [dict(), dict(pos=[(0, 0, 0, 0), (100, 0, 0, 20)], rot=[(0, 0, 0, 0)],
                                         scl=[(4096, 4096, 4096, 0)])]}
        prev = me.pose_at(model, 0)
        out = me.blend_pose(model, prev, 20, 2048)
        self.assertEqual(out[1][0], (50, 0, 0))
        model["bones"][1]["scl"] = [(0, 4096, 4096, 0)]
        self.assertEqual(me.blend_pose(model, me.pose_at(model, 0), 20, 2048)[1][2], (0, 4096, 4096))


class TestMath(unittest.TestCase):
    def test_quaternion_matches_matrix(self):
        for rot in [(0, 0, 0), (100, 200, 300), (1024, 0, 0), (0, 2048, 0), (3000, 1500, 4000), (0, 0, 2048)]:
            m = me.conj(me.rotzyx(rot))
            x, y, z, w = me.quat_from_matrix(m)
            q = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
            for i in range(3):
                for j in range(3):
                    self.assertAlmostEqual(q[i][j], m[i][j], places=6, msg=str(rot))

    def test_rotation_is_zyx(self):
        # 90 degrees about Z sends +X to +Y; about X sends +Y to +Z
        r = me.rotzyx((0, 0, 1024))
        self.assertAlmostEqual(r[1][0], 1.0)
        r = me.rotzyx((1024, 0, 0))
        self.assertAlmostEqual(r[2][1], 1.0)

    def test_inverse(self):
        a = [[2.0, 0, 1], [0, 3, 0], [1, 0, 1]]
        p = me.matmul(a, me.inv3(a))
        for i in range(3):
            for j in range(3):
                self.assertAlmostEqual(p[i][j], 1.0 if i == j else 0.0)

    def test_hidden(self):
        self.assertTrue(me.is_hidden((48, 0, 0)))
        self.assertTrue(me.is_hidden((-4096, 4096, 4096)))   # a negative value is <= 48 in the game too
        self.assertFalse(me.is_hidden((4096, 49, 0)))
        self.assertEqual(me.gl_scale((0, 0, 0)), (0.0, 0.0, 0.0))


class TestGltf(unittest.TestCase):
    def setUp(self):
        d = synthetic_file()
        self.model = me.parse_model(d, me.u32(d, 0))
        self.motions = me.parse_motions(d, me.u32(d, 6 * 4))
        self.gltf, self.bin, self.png = me.build_gltf(self.model, self.motions, "t")

    def test_validates(self):
        self.assertTrue(me.validate_gltf(self.gltf, self.bin))

    def test_skeleton(self):
        g = self.gltf
        self.assertEqual(len(g["skins"][0]["joints"]), 3)
        self.assertEqual(g["nodes"][1]["translation"], [0.0, -50.0, 0.0])    # (x, y, z) -> (-x, -y, z)
        self.assertEqual(g["nodes"][2]["translation"], [-5.0, 0.0, 0.0])
        self.assertEqual(g["nodes"][2]["scale"], [0.0, 0.0, 0.0])             # hidden bone
        self.assertEqual(g["nodes"][1]["scale"], [1.0, 1.0, 1.0])
        self.assertIn(2, g["nodes"][1]["children"])

    def test_rest_joint_matrices_are_identity_for_visible_bones(self):
        """node world * inverse bind = identity at the rest pose, so the mesh shows in its bind pose."""
        import struct as st
        g = self.gltf
        ibm = g["accessors"][g["skins"][0]["inverseBindMatrices"]]
        v = g["bufferViews"][ibm["bufferView"]]
        vals = st.unpack_from("<48f", self.bin, v["byteOffset"])
        n = g["nodes"][1]
        q = n["rotation"]
        x, y, z, w = q
        r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
             [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
             [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        world = [[r[i][j] * n["scale"][j] for j in range(3)] + [n["translation"][i]] for i in range(3)]
        m = vals[16:32]   # column-major
        ibm4 = [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        world4 = world + [[0.0, 0.0, 0.0, 1.0]]
        for i in range(4):
            for j in range(4):
                s = sum(world4[i][k] * ibm4[k][j] for k in range(4))
                self.assertAlmostEqual(s, 1.0 if i == j else 0.0, places=4)

    def test_materials_and_primitives(self):
        g = self.gltf
        kinds = {m["name"] for m in g["materials"]}
        self.assertEqual(kinds, {"tex", "flat"})
        tex = [m for m in g["materials"] if m["name"] == "tex"][0]
        self.assertEqual(tex["alphaMode"], "MASK")
        tris = sum(g["accessors"][p["indices"]]["count"] // 3 for p in g["meshes"][0]["primitives"])
        self.assertEqual(tris, 1 + 2 + 1)   # triangle, quad -> 2, hidden bone's triangle
        self.assertEqual(len(self.png) > 8, True)

    def _with_abr(self, abr, stp):
        model = copy.deepcopy(self.model)
        for p in model["bones"][1]["mesh"]["polys"]:
            p["abr"] = abr
        if stp:
            model["vram"][255][5] = 0x801F
        return me.build_gltf(model, self.motions, "t")[0]

    def test_windings_are_counter_clockwise(self):
        """The game keeps the polygons that are clockwise on screen (NCLIP); glTF fronts are counter-clockwise."""
        g = self.gltf
        prim = [p for p in g["meshes"][0]["primitives"] if g["materials"][p["material"]]["name"] == "tex"][0]
        acc = g["accessors"]
        pos = acc[prim["attributes"]["POSITION"]]
        pv = g["bufferViews"][pos["bufferView"]]
        iv = g["bufferViews"][acc[prim["indices"]]["bufferView"]]
        i0, i1, i2 = struct.unpack_from("<3H", self.bin, iv["byteOffset"])
        P = [struct.unpack_from("<3f", self.bin, pv["byteOffset"] + 12 * i) for i in (i0, i1, i2)]
        a = [P[1][j] - P[0][j] for j in range(3)]
        c = [P[2][j] - P[0][j] for j in range(3)]
        cross = [a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0]]
        self.assertLess(cross[2], 0)   # reversed against the game's order; the rest is checked on the disc data

    def test_materials_single_sided_and_blend_split(self):
        for m in self.gltf["materials"]:
            self.assertFalse(m["doubleSided"])                  # the game culls back faces
        # semi-transparent without STP texels: stays an opaque mask material
        g = self._with_abr(1, False)
        self.assertEqual({m["name"] for m in g["materials"]}, {"tex", "flat_blend", "flat"})
        # with an STP texel the textured polygon becomes blended, on its own atlas page
        g = self._with_abr(2, True)
        names = {m["name"]: m for m in g["materials"]}
        self.assertEqual(set(names), {"tex_blend", "flat_blend", "flat"})
        self.assertEqual(names["tex_blend"]["alphaMode"], "BLEND")
        self.assertNotIn("alphaCutoff", names["tex_blend"])
        self.assertEqual(g["asset"]["extras"]["textureVariants"], 1)

    def test_animations(self):
        a = {x["name"]: x for x in self.gltf["animations"]}
        self.assertEqual(sorted(a), ["motion_00", "motion_02"])      # the empty slot 1 is skipped
        self.assertEqual(a["motion_00"]["extras"]["frames"], 3)
        self.assertEqual(a["motion_02"]["extras"]["frames"], 4)
        g = self.gltf
        # bone 1 rotation: 3 keys at 60 fps, 0, 45 and 90 degrees about Z; the scale is constant: a single key
        by = {(c["target"]["node"], c["target"]["path"]): a["motion_00"]["samplers"][c["sampler"]]
              for c in a["motion_00"]["channels"]}
        rot = by[(1, "rotation")]
        self.assertEqual(g["accessors"][rot["output"]]["count"], 3)
        self.assertEqual(g["accessors"][rot["input"]]["max"], [2 / 60.0])
        self.assertEqual(g["accessors"][by[(1, "scale")]["output"]]["count"], 1)
        self.assertEqual(g["accessors"][by[(1, "scale")]["input"]]["count"], 1)
        v = g["bufferViews"][g["accessors"][rot["output"]]["bufferView"]]
        off = v["byteOffset"] + g["accessors"][rot["output"]].get("byteOffset", 0)
        qs = [struct.unpack_from("<4f", self.bin, off + 16 * i) for i in range(3)]
        for p, q in zip(qs, qs[1:]):
            self.assertGreaterEqual(sum(a * b for a, b in zip(p, q)), 0)   # sign continuity
        self.assertAlmostEqual(qs[1][2], math.sin(math.radians(45) / 2), places=4)

    def test_no_animations(self):
        g, _, _ = me.build_gltf(self.model, self.motions, "t", with_animations=False)
        self.assertNotIn("animations", g)

    def test_validator_catches_errors(self):
        bad = copy.deepcopy(self.gltf)
        bad["accessors"][0]["count"] += 10_000
        with self.assertRaises(AssertionError):
            me.validate_gltf(bad, self.bin)
        bad = copy.deepcopy(self.gltf)
        bad["skins"][0]["joints"].append(99)
        with self.assertRaises(AssertionError):
            me.validate_gltf(bad, self.bin)
        with self.assertRaises(AssertionError):
            me.validate_gltf(self.gltf, self.bin[:-4])


class TestSourceLayout(unittest.TestCase):
    """The in-game export's layout (I7): Blender scale, model.json, fighters by number."""

    def test_unit_scale_multiplies_every_length(self):
        base = TestGltf()
        base.setUp()
        g1, _, _ = me.build_gltf(base.model, base.motions, "t")
        g2, _, _ = me.build_gltf(base.model, base.motions, "t", unit_scale=0.001)
        a1 = g1["accessors"][g1["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
        a2 = g2["accessors"][g2["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
        for i in range(3):
            self.assertAlmostEqual(a2["max"][i], a1["max"][i] * 0.001, places=9)
        self.assertEqual(g2["nodes"][1]["scale"], g1["nodes"][1]["scale"])      # scales and rotations stay
        self.assertEqual(g2["nodes"][1]["rotation"], g1["nodes"][1]["rotation"])
        self.assertAlmostEqual(g2["nodes"][1]["translation"][0], g1["nodes"][1]["translation"][0] * 0.001, places=12)

    def test_source_json_and_uri(self):
        base = TestGltf()
        base.setUp()
        g, _, _ = me.build_gltf(base.model, base.motions, "fighter_7", True, None, me.UNIT_SCALE, "model")
        self.assertEqual(g["buffers"][0]["uri"], "model.bin")
        self.assertEqual(g["images"][0]["uri"], "model.png")
        text = me.source_json(7, dict(distance=-1500, height=321), base.motions)
        meta = __import__("json").loads(text)
        self.assertEqual(meta["format"], "model-src-v1")
        self.assertEqual(meta["scale"], 1000)
        self.assertEqual(meta["effects"], "dw3:digimon/7")
        self.assertEqual(meta["info"], {"distance": -1500, "height": 321})
        self.assertTrue(all(v == "motion_%02d" % int(k) for k, v in meta["motions"].items()) and meta["motions"])

    def test_plain_sums(self):
        self.assertEqual(me.dsum([0.1] * 10), sum_plain([0.1] * 10))


def sum_plain(v):
    t = 0.0
    for x in v:
        t += x
    return t


def png_pixels(data):
    """(width, height, raw rows) of a PNG made of IHDR + IDAT + IEND chunks, whatever the deflate stream."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    p, idat, ihdr = 8, b"", None
    while p < len(data):
        n = struct.unpack_from(">I", data, p)[0]
        tag = data[p + 4:p + 8]
        body = data[p + 8:p + 8 + n]
        assert struct.unpack_from(">I", data, p + 8 + n)[0] == zlib.crc32(tag + body) & 0xFFFFFFFF, "bad chunk crc"
        if tag == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat += body
        p += 12 + n
    return ihdr, zlib.decompress(idat)


def parity(folder):
    """The C exporter (modeltest writes <folder>/blobs/<n>.bin and <folder>/c/d<nnn>/model/model.*) against this tool: the .gltf, .bin
    and .json bytes are the same, the PNG has the same pixels (its deflate stream is the port's own). Returns the failures."""
    import tempfile
    root = Path(folder)
    files = {int(f.stem): f.read_bytes() for f in (root / "blobs").glob("*.bin")}
    disc = me.BlobDisc(files)
    bad = []
    ids = sorted(int(d.name[1:]) for d in (root / "c").iterdir() if d.is_dir())
    for fid in ids:
        with tempfile.TemporaryDirectory() as tmp:
            me.export_source(disc, fid, tmp)
            for name in ("model.gltf", "model.bin", "model.json"):
                a, b = (Path(tmp) / name).read_bytes(), (root / "c" / ("d%03d" % fid) / "model" / name).read_bytes()
                if a != b:
                    first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                    bad.append("%d/%s differs (python %d bytes, C %d bytes, first difference at %d)" % (fid, name, len(a), len(b), first))
            if png_pixels((Path(tmp) / "model.png").read_bytes()) != png_pixels((root / "c" / ("d%03d" % fid) / "model" / "model.png").read_bytes()):
                bad.append("%d/model.png has other pixels" % fid)
    return ids, bad


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--parity":
        done, problems = parity(sys.argv[2])
        for line in problems:
            print("parity: FAIL " + line)
        print("parity: %d fighter(s) compared, %d difference(s)" % (len(done), len(problems)))
        sys.exit(1 if problems or not done else 0)
    unittest.main()
