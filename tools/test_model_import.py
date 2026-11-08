"""Tests of tools/model_import.py on synthetic glTF (no disc data). Run: python3 -I tools/test_model_import.py

`python3 -I tools/test_model_import.py --demo <dir>` writes the demo model (a rigged robot made of boxes: robot.gltf,
robot.bin, robot.png and its robot.json metadata) that is the minimum example of a `model-src-v1` source.
`python3 -I tools/test_model_import.py --demo-partner <dir>` writes the same robot with the animations of a partner (idle, weak idle, dodge,
flinch, hit, knock out, cheer, blast, attack: partner.gltf with its buffer embedded, partner.png and partner.json, whose "motions" name the slot of each one).
"""
import base64
import copy
import json
import math
import os
import random
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import model_export as me  # noqa: E402
import model_import as mi  # noqa: E402

F32, U8, U16 = me.F32, me.U8, me.U16
IDENT = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]


# ---------------------------------------------------------------- synthetic glTF: a robot made of boxes

FACES = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
         ((0, 1, 0), (0, 0, 1), (1, 0, 0)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
         ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]


def box_faces(center, half):
    """24 corners (pos, normal, corner index) and 12 triangles (CCW seen from outside) of an axis aligned box."""
    verts, tris = [], []
    for n, u, v in FACES:
        base = len(verts)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [center[k] + n[k] * half[k] + su * u[k] * half[k] + sv * v[k] * half[k] for k in range(3)]
            verts.append((tuple(p), n, (su, sv)))
        tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return verts, tris


def quat_axis(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))


def png_rgba(w, h, pixel):
    return me.write_png(w, h, b"".join(bytes(pixel(x, y)) for y in range(h) for x in range(w)))


def make_robot(prefix_scale=1.0, prefix_yaw=0.0, many_verts=0, anim_extra=None, name="robot", box_scale=1.0, empty_hip=False):
    """-> (gltf dict, bin bytes, png bytes, info). Bones: root, torso, head, arm; one box each (+ legs on the root).
    The armature node above the joints has a uniform scale/yaw (to test the folding). Positions are metres."""
    gb = me.GltfBuilder()
    r = prefix_scale
    arm_q = quat_axis((0, 1, 0), prefix_yaw)
    A = (me.matmul(quat_matrix(arm_q), [[r, 0, 0], [0, r, 0], [0, 0, r]]), [0.0, 0.0, 0.0])
    # joint locals (metres) and boxes (centre, half) in the joint's local space
    joints = [dict(name="hip", parent=None, t=(0.0, 0.0, 0.0), box=((0.0, 0.2, 0.0), (0.25, 0.2, 0.15))),
              dict(name="torso", parent=0, t=(0.0, 0.4, 0.0), box=((0.0, 0.3, 0.0), (0.3, 0.3, 0.2))),
              dict(name="head", parent=1, t=(0.0, 0.6, 0.0), box=((0.0, 0.2, 0.0), (0.2, 0.2, 0.2))),
              dict(name="arm", parent=1, t=(0.4, 0.5, 0.0), box=((0.3, 0.0, 0.0), (0.3, 0.08, 0.08)))]
    nj = len(joints)
    # world bind (glTF space) of every joint, including the armature node
    world = []
    for i, j in enumerate(joints):
        loc = (IDENT, [x * box_scale for x in j["t"]])
        world.append(mi.compose(A, loc) if j["parent"] is None else mi.compose(world[j["parent"]], loc))
    # extra boxes to push a bone over 256 vertices
    pos, nrm, uv, jts, tris = [], [], [], [], []
    quad = [(0, 0), (1, 0), (0, 1), (1, 1)]
    for ji, j in enumerate(joints):
        boxes = [] if (empty_hip and ji == 0) else [j["box"]]
        if ji == 1:
            for k in range(many_verts):
                boxes.append(((0.003 * k - 0.06, 0.01 * (k % 7), 0.0), (0.03, 0.01, 0.03)))
        for bx in boxes:
            c = tuple(x * box_scale for x in bx[0])
            h = tuple(x * box_scale for x in bx[1])
            vs, ts = box_faces(c, h)
            base = len(pos)
            qx, qy = quad[ji % 4]
            for p, n, (su, sv) in vs:
                wp = [x + y for x, y in zip(me.matvec(world[ji][0], p), world[ji][1])]
                wn = me.matvec(world[ji][0], n)
                ln = mi.vlen(wn)
                pos.append(tuple(wp))
                nrm.append(tuple(x / ln for x in wn))
                uv.append(((qx * 32 + 6 + (su + 1) * 10) / 64.0, (qy * 32 + 6 + (sv + 1) * 10) / 64.0))
                jts.append(ji)
            tris += [(base + a, base + b, base + c_) for a, b, c_ in ts]
    nv = len(pos)
    ibm = []
    for ji in range(nj):
        wr, wt = world[ji]
        inv = me.inv3(wr)
        it = [-x for x in me.matvec(inv, wt)]
        for c in range(3):
            ibm += [inv[0][c], inv[1][c], inv[2][c], 0.0]
        ibm += [it[0], it[1], it[2], 1.0]
    nodes = [dict(name="Armature", children=[1], scale=[r, r, r], rotation=list(arm_q))]
    for ji, j in enumerate(joints):
        n = dict(name=j["name"], translation=[x * box_scale for x in j["t"]])
        if ji + 1 < nj + 0:
            pass
        nodes.append(n)
    for ji, j in enumerate(joints):
        if j["parent"] is not None:
            nodes[1 + j["parent"]].setdefault("children", []).append(1 + ji)
    nodes.append(dict(name="mesh", mesh=0, skin=0))
    ibm_acc = gb.add(me.pack_f(ibm), F32, nj, "MAT4")
    att = {"POSITION": gb.add(me.pack_f([x for p in pos for x in p]), F32, nv, "VEC3", 34962,
                              me.minmax_vec([list(p) for p in pos], 3)),
           "NORMAL": gb.add(me.pack_f([x for p in nrm for x in p]), F32, nv, "VEC3", 34962),
           "TEXCOORD_0": gb.add(me.pack_f([x for p in uv for x in p]), F32, nv, "VEC2", 34962),
           "JOINTS_0": gb.add(b"".join(struct.pack("<4B", j, 0, 0, 0) for j in jts), U8, nv, "VEC4", 34962),
           "WEIGHTS_0": gb.add(b"".join(struct.pack("<4f", 1.0, 0, 0, 0) for _ in jts), F32, nv, "VEC4", 34962)}
    ia = gb.add(struct.pack("<%dH" % (3 * len(tris)), *[i for t in tris for i in t]), U16, 3 * len(tris), "SCALAR", 34963)
    # animations
    times = [0.0, 0.375, 0.75, 1.125, 1.5]
    arm_q_keys = [quat_axis((0, 0, 1), d) for d in (0, 40, 0, -40, 0)]
    head_t = [[0.0, 0.6 + dy, 0.0] for dy in (0.0, 0.02, 0.0, 0.02, 0.0)]
    t_in = gb.add(me.pack_f(times), F32, len(times), "SCALAR", minmax=([0.0], [1.5]))
    samplers, channels = [], []

    def chan(node, path, values, atype, tin=t_in, n=None):
        acc = gb.add(me.pack_f([x for v in values for x in v]), F32, len(values), atype)
        samplers.append({"input": tin, "output": acc, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})

    chan(4, "rotation", arm_q_keys, "VEC4")
    chan(3, "translation", [[t[0] * box_scale, t[1] * box_scale, t[2] * box_scale] for t in head_t], "VEC3")
    anims = [{"name": "idle", "samplers": samplers, "channels": channels}]
    if anim_extra == "partner":
        anims += partner_animations(gb, box_scale)
    if anim_extra == "spin":
        s2, c2 = [], []
        ts = [0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0]
        tin2 = gb.add(me.pack_f(ts), F32, len(ts), "SCALAR", minmax=([0.0], [2.0]))
        qs = [quat_axis((0, 1, 0), 90 * k * 0.9999) for k in range(len(ts))]
        acc = gb.add(me.pack_f([x for v in qs for x in v]), F32, len(qs), "VEC4")
        s2.append({"input": tin2, "output": acc, "interpolation": "LINEAR"})
        c2.append({"sampler": 0, "target": {"node": 4, "path": "rotation"}})
        anims.append({"name": "spin", "samplers": s2, "channels": c2})
    png = png_rgba(64, 64, lambda x, y: [(220, 60, 60, 255), (60, 200, 80, 255), (70, 90, 230, 255), (230, 210, 60, 255)][(y // 32) * 2 + x // 32]
                   if (x + y) % 8 < 5 else [(120, 30, 30, 255), (30, 110, 40, 255), (30, 40, 140, 255), (140, 120, 30, 255)][(y // 32) * 2 + x // 32])
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, len(nodes) - 1]}], "nodes": nodes,
            "skins": [{"inverseBindMatrices": ibm_acc, "skeleton": 1, "joints": [1, 2, 3, 4]}],
            "meshes": [{"name": name, "primitives": [{"attributes": att, "indices": ia, "material": 0, "mode": 4}]}],
            "materials": [{"name": "m", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0}}],
            "textures": [{"source": 0}], "images": [{"uri": name + ".png"}],
            "buffers": [{"byteLength": len(gb.bin), "uri": name + ".bin"}], "bufferViews": gb.views, "accessors": gb.accessors,
            "animations": anims}
    info = dict(world=world, joints=joints, box_scale=box_scale, A=A, tris=len(tris), verts=nv)
    return gltf, bytes(gb.bin), png, info


def quat_matrix(q):
    return mi.quat_matrix(q)


# name -> (duration in seconds, key times in 0..1, torso pitch in degrees, arm roll in degrees, hip offset (x, y, z) in metres)
PARTNER_POSES = {
    "idle_weak": (1.6, [0, 0.5, 1], [28, 34, 28], [-20, -10, -20], [(0, 0, 0)] * 3),
    "dodge": (0.6, [0, 0.4, 1], [0, -10, 0], [0, 20, 0], [(0, 0, 0), (0.35, 0, 0), (0, 0, 0)]),
    "flinch": (0.4, [0, 0.4, 1], [0, -18, 0], [0, 30, 0], [(0, 0, 0), (0, 0, -0.08), (0, 0, 0)]),
    "hit": (0.7, [0, 0.3, 1], [0, -30, 0], [0, 60, 0], [(0, 0, 0), (0, 0, -0.2), (0, 0, 0)]),
    "ko": (1.0, [0, 0.5, 1], [0, -60, -90], [0, 80, 80], [(0, 0, 0), (0, -0.1, -0.15), (0, -0.25, -0.2)]),
    "cheer": (1.2, [0, 0.3, 0.6, 1], [0, -8, 0, -8], [0, 150, 0, 150], [(0, 0, 0), (0, 0.1, 0), (0, 0, 0), (0, 0.1, 0)]),
    "blast": (1.2, [0, 0.5, 1], [0, 10, 0], [0, 100, 0], [(0, 0, 0), (0, 0.3, 0), (0, 0, 0)]),
    "attack": (0.8, [0, 0.35, 0.6, 1], [0, 25, 10, 0], [0, -70, 40, 0], [(0, 0, 0), (0, 0, 0.05), (0, 0, 0.35), (0, 0, 0)]),
}


def partner_animations(gb, box_scale):
    """The animations of a partner demo: one per pose of PARTNER_POSES, on the hip (translation), the torso (pitch) and the arm (roll)."""
    out = []
    for name, (dur, tk, torso, arm, hip) in PARTNER_POSES.items():
        times = [dur * t for t in tk]
        t_acc = gb.add(me.pack_f(times), F32, len(times), "SCALAR", minmax=([0.0], [times[-1]]))
        samplers, channels = [], []
        for node, path, vals, atype in (
                (1, "translation", [[x * box_scale for x in h] for h in hip], "VEC3"),
                (2, "rotation", [quat_axis((1, 0, 0), d) for d in torso], "VEC4"),
                (4, "rotation", [quat_axis((0, 0, 1), d) for d in arm], "VEC4")):
            acc = gb.add(me.pack_f([x for v in vals for x in v]), F32, len(vals), atype)
            samplers.append({"input": t_acc, "output": acc, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})
        out.append({"name": name, "samplers": samplers, "channels": channels})
    return out


def write_model(d, gltf, binary, png, meta, name="robot"):
    os.makedirs(d, exist_ok=True)
    Path(d, name + ".gltf").write_text(json.dumps(gltf))
    Path(d, name + ".bin").write_bytes(binary)
    Path(d, name + ".png").write_bytes(png)
    Path(d, name + ".json").write_text(json.dumps(meta, indent=2))


DEMO_META = {"format": "model-src-v1", "name": "robot", "gltf": "robot.gltf", "fitHeight": 1800,
             "effects": "dw3:digimon/383", "template": "dw3:digimon/4", "info": {"distance": -1500},
             "motions": {"0": "idle", "1": "idle"}, "fill": "idle"}


def write_demo(d):
    g, b, p, _ = make_robot()
    write_model(d, g, b, p, DEMO_META)


# a partner's model.json: the animations are named after what they do, "motions" says the slot each one plays (docs/formats.md 4.6, "Ranuras de
# movimiento"); the slots that no entry covers play "attack" ("fill"), the technique slots of the donor among them
PARTNER_META = {"format": "model-src-v1", "name": "partner", "gltf": "partner.gltf", "fitHeight": 1800,
                "effects": "dw3:digimon/383", "template": "dw3:digimon/383",
                "motions": {"0": "idle", "1": "idle_weak", "2": "dodge", "3": "flinch", "4": "hit", "5": 4, "6": 4,
                            "10": {"animation": "ko", "end": "hold"}, "11": 4, "12": "cheer", "13": "blast", "14": "attack",
                            "15": 14, "16": 14, "17": 14, "20": 14, "32": 14},
                "fill": "attack"}


def write_partner_demo(d):
    """partner.gltf (its buffer embedded as a data URI: no .bin to carry), partner.png and partner.json."""
    g, b, p, _ = make_robot(anim_extra="partner", name="partner")
    g["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(b).decode()
    os.makedirs(d, exist_ok=True)
    Path(d, "partner.gltf").write_text(json.dumps(g))
    Path(d, "partner.png").write_bytes(p)
    Path(d, "partner.json").write_text(json.dumps(PARTNER_META, indent=2))


def make_glb(g, b, png):
    """The robot's glTF with its buffer and image embedded: .glb bytes."""
    g = copy.deepcopy(g)
    g["buffers"][0].pop("uri")
    view = len(g["bufferViews"])
    binary = bytearray(b)
    while len(binary) % 4:
        binary.append(0)
    g["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(png)})
    binary += png
    g["images"][0] = {"bufferView": view, "mimeType": "image/png"}
    g["buffers"][0]["byteLength"] = len(binary)
    js = json.dumps(g).encode()
    js += b" " * (-len(js) % 4)
    binary += b"\0" * (-len(binary) % 4)
    return struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(binary)) + struct.pack("<II", len(js), 0x4E4F534A) + js \
        + struct.pack("<II", len(binary), 0x004E4942) + bytes(binary)


def parity_cases():
    """{case: (gltf dict, bin, png, meta, glb bytes or None)}: sources the C baker (pc/modeltest.c) must bake to the same bytes."""
    cases = {}

    def add(name, g, b, png, meta, glb=None):
        cases[name] = (g, b, png, meta, glb)

    g, b, png, _ = make_robot(name="m")
    meta = copy.deepcopy(DEMO_META)
    meta["gltf"] = "m.gltf"
    add("robot", g, b, png, meta)
    g, b, png, _ = make_robot(name="m", prefix_scale=0.01, prefix_yaw=30)
    add("armature", g, b, png, dict(meta))
    g, b, png, _ = make_robot(name="m", many_verts=300)
    add("manyverts", g, b, png, dict(meta))
    g, b, png, _ = make_robot(name="m", empty_hip=True)
    add("emptyhip", g, b, png, dict(meta))
    g, b, png, _ = make_robot(name="m", anim_extra="spin")
    m2 = copy.deepcopy(meta)
    m2["motions"].update({"2": {"animation": "spin", "speed": 1.5, "blendIn": 4}, "3": {"animation": "spin", "loop": True, "loopStart": 2},
                          "4": {"animation": "spin", "end": "hold", "blendIn": 0}, "5": 2, "6": {"animation": "idle", "speed": 2.0, "blendOut": 4}})
    m2["tolerance"] = {"rotation": 1.0}
    add("spin", g, b, png, m2)
    g, b, png, _ = make_robot(name="m")
    gl = make_glb(g, b, png)
    m3 = copy.deepcopy(meta)
    m3["gltf"] = "m.glb"
    add("glb", g, b, png, m3, gl)
    g, b, png, _ = make_robot(name="m")
    g = copy.deepcopy(g)
    for prim in g["meshes"][0]["primitives"]:
        prim["attributes"].pop("NORMAL")
    m4 = copy.deepcopy(meta)
    m4["normals"] = "smooth"
    add("smooth", g, b, png, m4)
    m5 = copy.deepcopy(m4)
    m5["normals"] = "flat"
    add("flat", g, b, png, m5)
    g, b, png, _ = make_robot(name="m")
    w, h, rgba = mi.read_png(png)
    a = bytearray(rgba)
    for y in range(0, 32):
        for x in range(0, 32):
            if x % 4 == 0:
                a[(y * w + x) * 4 + 3] = 0
    g = copy.deepcopy(g)
    g["materials"][0]["alphaMode"] = "MASK"
    g["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = [0.9, 0.8, 1.0, 1.0]
    add("masked", g, b, me.write_png(w, h, bytes(a)), dict(meta))
    g, b, png, _ = make_robot(name="m")
    big = png_rgba(512, 512, lambda x, y: [(220, 60, 60, 255), (60, 200, 80, 255), (70, 90, 230, 255), (230, 210, 60, 255)][(y // 256) * 2 + x // 256]
                   if (x * 7 + y * 3) % 11 < 8 else (x % 256, y % 256, (x + y) % 256, 255))
    add("bigtexture", g, b, big, dict(meta))
    g, b, png, _ = make_robot(name="m")
    g = copy.deepcopy(g)
    g["materials"][0]["pbrMetallicRoughness"] = {"baseColorFactor": [0.2, 0.4, 0.8, 1.0]}
    add("untextured", g, b, png, dict(meta))
    g, b, png, _ = make_robot(name="m")
    g = copy.deepcopy(g)
    for sm in g["animations"][0]["samplers"]:
        sm["interpolation"] = "STEP"
    add("step", g, b, png, dict(meta))
    # many materials of flat colours: more palettes than CLUT slots
    g, b, png, _ = make_robot(name="m", many_verts=120)
    g = copy.deepcopy(g)
    prim = g["meshes"][0]["primitives"][0]
    ia = g["accessors"][prim["indices"]]
    view = g["bufferViews"][ia["bufferView"]]
    idx = list(struct.unpack_from("<%dH" % ia["count"], b, view["byteOffset"]))
    binary = bytearray(b)
    prims, mats = [], []
    per = len(idx) // 3 // 100 + 1
    for k in range(100):
        tri = idx[3 * k * per:3 * (k + 1) * per * 1]
        tri = tri[:len(tri) // 3 * 3]
        if not tri:
            break
        while len(binary) % 4:
            binary.append(0)
        g["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": 2 * len(tri), "target": 34963})
        binary += struct.pack("<%dH" % len(tri), *tri)
        g["accessors"].append({"bufferView": len(g["bufferViews"]) - 1, "componentType": 5123, "count": len(tri), "type": "SCALAR"})
        mats.append({"pbrMetallicRoughness": {"baseColorFactor": [(k * 7 % 256) / 255, (k * 13 % 256) / 255, (k * 29 % 256) / 255, 1.0]}})
        prims.append(dict(prim, indices=len(g["accessors"]) - 1, material=k))
    g["meshes"][0]["primitives"] = prims
    g["materials"] = mats
    g["buffers"][0]["byteLength"] = len(binary)
    add("manycolors", g, bytes(binary), png, dict(meta))
    return cases


def write_parity(d):
    """Every parity case into <d>/<case>/ (model.json + glTF + PNG + expected.bin, the Python tool's blob)."""
    for name, (g, b, png, meta, glb) in parity_cases().items():
        cd = os.path.join(d, name)
        os.makedirs(cd, exist_ok=True)
        if glb is not None:
            Path(cd, "m.glb").write_bytes(glb)
        else:
            Path(cd, "m.gltf").write_text(json.dumps(g))
            Path(cd, "m.bin").write_bytes(b)
            Path(cd, "m.png").write_bytes(png)
        Path(cd, "model.json").write_text(json.dumps(meta, indent=2))
        Path(cd, "expected.bin").write_bytes(mi.bake(copy.deepcopy(meta), cd)["blob"])


def bake_dir(meta, gltf, binary, png, partner=False):
    with tempfile.TemporaryDirectory() as d:
        write_model(d, gltf, binary, png, meta, name=meta["name"] if meta["name"] in ("partner",) else "robot")
        return mi.bake(meta, d, partner)


def parse(blob):
    model = me.parse_model(blob, me.u32(blob, 4 * 1))
    motions = me.parse_motions(blob, me.u32(blob, 0))
    return model, motions


def game_world_points(model, motions, frame):
    """Game-space world positions of every mesh corner at a frame (keys of the blob)."""
    pose = me.pose_at(model, frame)
    wm = me.world_matrices(model, pose, lambda s: tuple(x / 4096.0 for x in s))
    pts = []
    for bi, b in enumerate(model["bones"]):
        if bi == 0 or not b["mesh"]:
            continue
        R, t = wm[bi]
        for p in b["mesh"]["polys"]:
            for k in p["idx"]:
                w = me.matvec(R, b["mesh"]["verts"][k])
                pts.append(tuple(w[j] + t[j] for j in range(3)))
    return pts


# ---------------------------------------------------------------- tests

class PngTests(unittest.TestCase):
    def test_rgba_round_trip(self):
        px = [(x * 10 % 256, y * 7 % 256, 99, (x + y) * 5 % 256) for y in range(6) for x in range(9)]
        data = me.write_png(9, 6, b"".join(bytes(p) for p in px))
        w, h, rgba = mi.read_png(data)
        self.assertEqual((w, h), (9, 6))
        self.assertEqual(list(rgba), [c for p in px for c in p])

    def _png(self, w, h, ctype, depth, rows, extra=b""):
        def chunk(tag, d):
            return struct.pack(">I", len(d)) + tag + d + struct.pack(">I", zlib.crc32(tag + d) & 0xFFFFFFFF)
        raw = b"".join(b"\0" + r for r in rows)
        return (mi.PNG_SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0)) + extra
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

    def test_palette_and_grey_and_filters(self):
        def chunk(tag, d):
            return struct.pack(">I", len(d)) + tag + d + struct.pack(">I", zlib.crc32(tag + d) & 0xFFFFFFFF)
        plte = chunk(b"PLTE", bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9])) + chunk(b"tRNS", bytes([255, 255, 255, 0]))
        data = self._png(4, 1, 3, 2, [bytes([0b00011011])], plte)
        w, h, rgba = mi.read_png(data)
        self.assertEqual(list(rgba), [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 9, 9, 9, 0])
        data = self._png(3, 1, 0, 8, [bytes([10, 20, 30])])
        self.assertEqual(list(mi.read_png(data)[2][:8]), [10, 10, 10, 255, 20, 20, 20, 255])
        # filters: Sub, Up, Average, Paeth over a 3x3 RGB image written by hand
        rgb = [[(10, 20, 30), (40, 50, 60), (70, 80, 90)], [(15, 25, 35), (45, 55, 65), (75, 85, 95)],
               [(1, 2, 3), (4, 5, 6), (7, 8, 9)]]
        prev = bytes(9)
        rows = []
        for ft, row in zip((1, 2, 4), rgb):
            flat = bytes(c for p in row for c in p)
            enc = bytearray()
            for i, v in enumerate(flat):
                a = flat[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                if ft == 1:
                    pr = a
                elif ft == 2:
                    pr = b
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                enc.append((v - pr) & 255)
            rows.append((ft, bytes(enc)))
            prev = flat
        raw = b"".join(bytes([ft]) + e for ft, e in rows)

        def chunk2(tag, d):
            return struct.pack(">I", len(d)) + tag + d + struct.pack(">I", zlib.crc32(tag + d) & 0xFFFFFFFF)
        data = (mi.PNG_SIG + chunk2(b"IHDR", struct.pack(">IIBBBBB", 3, 3, 8, 2, 0, 0, 0)) + chunk2(b"IDAT", zlib.compress(raw))
                + chunk2(b"IEND", b""))
        out = mi.read_png(data)[2]
        self.assertEqual([out[4 * i + k] for i in range(9) for k in range(3)], [c for row in rgb for p in row for c in p])

    def test_rejects_non_png(self):
        with self.assertRaises(mi.ModelImportError):
            mi.read_png(b"\xff\xd8\xff")


class MathTests(unittest.TestCase):
    def test_euler_round_trip(self):
        rnd = random.Random(7)
        cases = [(rnd.uniform(-3, 3), rnd.uniform(-1.5, 1.5), rnd.uniform(-3, 3)) for _ in range(200)]
        cases += [(0.3, math.pi / 2, 0.4), (0.3, -math.pi / 2, 0.4), (0.0, 0.0, 0.0), (math.pi, 0.2, math.pi)]
        for ang in cases:
            m = me.rotzyx([a * 4096 / (2 * math.pi) for a in ang])
            raw = mi.raw_angles(m, None)
            m2 = me.rotzyx(raw)
            for i in range(3):
                for j in range(3):
                    self.assertAlmostEqual(m[i][j], m2[i][j], places=5, msg=repr(ang))

    def test_unwrap_follows_previous(self):
        prev = None
        out = []
        for deg in range(0, 721, 10):
            q = quat_axis((0, 0, 1), deg)
            raw = mi.raw_angles(me.conj(mi.quat_matrix(q)), prev)
            prev = raw
            out.append(raw[2] if deg else raw[2])
        # a rotation about Z of 720 degrees: the raw Z angle keeps growing in one direction (conjugation flips the sign)
        diffs = [b - a for a, b in zip(out, out[1:])]
        self.assertTrue(all(abs(d - diffs[0]) < 1.0 for d in diffs), diffs[:5])
        self.assertAlmostEqual(abs(out[-1] - out[0]), 8192, delta=2)

    def test_slerp_and_samplers(self):
        a, b = quat_axis((0, 0, 1), 0), quat_axis((0, 0, 1), 90)
        q = mi.slerp(a, b, 0.5)
        self.assertAlmostEqual(q[2], math.sin(math.radians(22.5)), places=6)
        sm = ([0.0, 1.0], [(0, 0, 0), (2, 4, 6)], "LINEAR")
        self.assertEqual(mi.eval_sampler(sm, 0.5, False), (1.0, 2.0, 3.0))
        self.assertEqual(mi.eval_sampler(sm, 9.0, False), (2, 4, 6))
        step = ([0.0, 1.0], [(0, 0, 0), (2, 4, 6)], "STEP")
        self.assertEqual(mi.eval_sampler(step, 0.9, False), (0, 0, 0))
        cub = ([0.0, 1.0], [(0,), (0,), (0,), (0,), (1,), (0,)], "CUBICSPLINE")
        self.assertAlmostEqual(mi.eval_sampler(cub, 0.5, False)[0], 0.5)

    def test_reduce_track(self):
        frames = list(range(0, 101))
        line = [(f * 2.0, 0, 0) for f in frames]
        self.assertEqual(mi.reduce_track(line, frames, 0.1), [0, 100])
        bent = [(f * 2.0 if f < 50 else 100 - (f - 50) * 4.0, 0, 0) for f in frames]
        self.assertEqual(mi.reduce_track(bent, frames, 0.1), [0, 50, 100])

    def test_median_cut(self):
        hist = {mi.key15(r, g, 0): 1 for r in range(0, 256, 17) for g in range(0, 256, 51)}
        pal = mi.median_cut(hist, 16)
        self.assertLessEqual(len(pal), 16)
        few = {mi.key15(255, 0, 0): 5, mi.key15(0, 0, 255): 5}
        self.assertEqual(sorted(mi.median_cut(few, 16)), sorted(few))

    def test_archive_layout(self):
        a = mi.archive([b"abc", None, b"defgh"])
        offs = struct.unpack_from("<3I", a, 0)
        self.assertEqual(offs[0], 16)
        self.assertEqual(offs[1], 0)
        self.assertEqual(offs[2], 24)
        self.assertEqual(a[offs[0]:offs[0] + 3], b"abc")
        self.assertEqual(a[offs[2]:offs[2] + 5], b"defgh")
        self.assertEqual(len(a) % 8, 0)

    def test_rlen_round_trip(self):
        raw = bytes([0] * 300 + list(range(50)) + [7] * 10 + [1, 2, 3, 3, 3, 4])
        self.assertEqual(me.rlen(mi.rlen_encode(raw), 0), raw)


class BakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gltf, cls.bin, cls.png, cls.info = make_robot()
        cls.meta = copy.deepcopy(DEMO_META)
        cls.res = bake_dir(cls.meta, cls.gltf, cls.bin, cls.png)
        cls.model, cls.motions = parse(cls.res["blob"])

    def test_layout_and_snippet(self):
        blob = self.res["blob"]
        # the table is pointed at by the first word; motions at entry 0 and the model at entry 1
        n = self.res["entries"]
        self.assertEqual(me.u32(blob, 0), (4 * n + 7) // 8 * 8)     # the first word is the table size
        self.assertEqual(len(self.motions), 62)
        self.assertEqual(len(self.model["bones"]), 5)       # root + hip, torso, head, arm
        self.assertEqual(self.model["bones"][0]["mesh"], None)
        self.assertEqual([b["parent"] for b in self.model["bones"][1:]], [0, 1, 2, 2])
        self.assertEqual(self.res["snippet"]["model"]["model"], {"file": "<mod>:digimon/robot/model.bin", "entry": 1})
        self.assertEqual(self.res["snippet"]["model"]["effects"], "dw3:digimon/383")
        self.assertEqual(self.res["stats"]["polys"], self.info["tris"])

    def test_rest_pose_matches_gltf(self):
        """World positions of the baked model at the first key frame equal the glTF bind pose (x -> -x, y -> -y, scaled)."""
        s = 1800.0 / 1.4       # fitHeight / rest height (the head box ends at 1.4 m)
        pts = game_world_points(self.model, self.motions, self.motions[0][0][2])
        self.assertEqual(len(pts), 3 * self.info["tris"])
        ys = [p[1] for p in pts]
        self.assertAlmostEqual(max(ys) - min(ys), 1800, delta=3)
        self.assertLess(max(ys), 3)           # standing on y = 0 of the glTF -> y down in the game: up is negative
        # compare one by one against the glTF vertices at t = 0 (the idle animation starts at the rest pose)
        want = []
        j = self.gltf["accessors"][self.gltf["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
        view = self.gltf["bufferViews"][j["bufferView"]]
        raw = struct.unpack_from("<%df" % (3 * j["count"]), self.bin, view["byteOffset"] + j.get("byteOffset", 0))
        verts = [(raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]) for i in range(j["count"])]
        ia = self.gltf["accessors"][self.gltf["meshes"][0]["primitives"][0]["indices"]]
        iv = self.gltf["bufferViews"][ia["bufferView"]]
        idx = struct.unpack_from("<%dH" % ia["count"], self.bin, iv["byteOffset"])
        got = sorted(tuple(round(c) for c in p) for p in pts)
        for i in idx:
            v = verts[i]
            want.append((round(-v[0] * s), round(-v[1] * s), round(v[2] * s)))
        want.sort()
        worst = max(max(abs(a - b) for a, b in zip(g, w)) for g, w in zip(got, want))
        self.assertLessEqual(worst, 3)

    def test_idle_steps_and_keys(self):
        slot0 = self.motions[0]
        n = slot0[0][1]
        self.assertEqual(slot0[0][0], 0)
        self.assertEqual(slot0[0][3], slot0[0][2] + n - 1)
        self.assertEqual(slot0[1], (n, 0, -32768, 0))           # loops from the start
        self.assertEqual(slot0[2][0], 0x7FFF)
        self.assertEqual(n, 90)                                 # 1.5 s at 60 per second, the last sample dropped
        for b in self.model["bones"][1:]:
            for track in (b["pos"], b["rot"], b["scl"]):
                self.assertEqual(track[0][3], 0)
                self.assertEqual(track[-1][3], 30000)
        arm = self.model["bones"][4]
        self.assertGreater(len(arm["rot"]), 3)                  # animated
        self.assertEqual(len(self.model["bones"][1]["rot"]), 2) # the hip is constant: rest key + sentinel
        # the arm rotates +-40 degrees about Z: raw angles reach 40/360*4096 = 455
        zs = [k[2] for k in arm["rot"]]
        self.assertAlmostEqual(max(abs(z) for z in zs), 455, delta=12)

    def test_non_loop_slots_and_fill(self):
        sl = self.motions[2]
        self.assertEqual(sl[0], (0, 10, 0, 0))                  # blend in
        self.assertEqual(sl[1][:2], (10, 91))                   # 1.5 s: 91 samples, the last one kept
        self.assertEqual(sl[2], (101, 10, 0, 0))                # blend back to idle
        self.assertEqual(sl[3], (111, 0, -1, 0))
        # slot 0 and 1 share one clip (same animation and speed): same frame range
        self.assertEqual(self.motions[0][0][2], self.motions[1][0][2])
        self.assertTrue(all(m is not None for m in self.motions))   # "fill" covers every slot

    def test_islands_are_the_connected_cells(self):
        # the robot's UV charts are 4 quadrants of the texture; each chart is one island however the triangles are ordered
        self.assertEqual(self.res["stats"]["islands"], 4)
        self.assertEqual(self.res["stats"]["palettes"], 4)

    def test_texture_is_valid_tim(self):
        blob = self.res["blob"]
        tex = me.parse_tim_archive(blob, me.u32(blob, 4 * 2))
        self.assertEqual(len(tex), 1)
        t = tex[0]
        self.assertEqual((t["mode"], t["img"][2], t["img"][3]), (2, 64, 256))
        vram = me.make_vram(tex)
        # every textured polygon: UVs inside 256x240, a CLUT at x multiple of 16 in rows 240..255
        for b in self.model["bones"][1:]:
            for p in b["mesh"]["polys"]:
                page, mode, cx, cy = p["tex"]
                self.assertEqual((page, mode), (0, 0))
                self.assertIn(cx, (0, 16, 32, 48))
                self.assertTrue(240 <= cy <= 255)
                for u, v in p["uv"]:
                    self.assertLess(v, 240)
        # the first colours of a CLUT row decode (non-zero = visible)
        polys = [p for b in self.model["bones"][1:] for p in b["mesh"]["polys"]]
        cols = set()
        for p in polys:
            for v in me.poly_texels(vram, p):
                cols.add(v)
        self.assertGreater(len(cols), 3)
        self.assertLess(len(cols), 40)

    def test_texture_colours_survive(self):
        """The hip box is red with darker stripes: its polygons sample (near) those two colours."""
        blob = self.res["blob"]
        vram = me.make_vram(me.parse_tim_archive(blob, me.u32(blob, 4 * 2)))
        hip = self.model["bones"][1]
        rgb = set()
        for p in hip["mesh"]["polys"]:
            for v in me.poly_texels(vram, p):
                if v:
                    rgb.add(me.c15(v))
        reds = [c for c in rgb if c[0] > c[1] + 40 and c[0] > c[2] + 40]
        self.assertGreaterEqual(len(reds), 1)
        self.assertTrue(all(c[0] >= c[1] and c[0] >= c[2] for c in rgb), rgb)

    def test_bounds_and_commands(self):
        blob = self.res["blob"]
        d = blob
        mo = me.u32(d, 4 * 1)
        words = [me.u32(d, mo + 4 * i) for i in range(2 + 3 * 4)]
        for i in range(4):
            mesh = me.u32(d, 4 * words[3 + 3 * i])
            e = lambda k: mesh + me.u32(d, mesh + 4 * k)       # noqa: E731
            nv = me.s16s(d, e(0), 1)[0]
            verts = [me.s16s(d, e(0) + 6 + 6 * k, 3) for k in range(nv)]
            bounds = me.s16s(d, e(5), 30)
            self.assertEqual(bounds[0], 9)
            xs, ys, zs = [v[0] for v in verts], [v[1] for v in verts], [v[2] for v in verts]
            self.assertEqual(tuple(bounds[3:6]), ((min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2, (min(zs) + max(zs)) // 2))
            self.assertEqual(tuple(bounds[6:9]), (max(xs), max(ys), max(zs)))
            cmds = d[e(2):]
            self.assertEqual(list(cmds[:7]), [0xD1, 0x80, 0x91, 0xC1, 0xE0, 0xA0, 0xB0])

    def test_winding_matches_gltf(self):
        """The game culls polygons whose NCLIP <= 0: the cross product of a game triangle points against its normal."""
        good = bad = 0
        for b in self.model["bones"][1:]:
            me_ = b["mesh"]
            for p in me_["polys"]:
                v = [me_["verts"][k] for k in p["idx"]]
                a = [v[1][j] - v[0][j] for j in range(3)]
                c = [v[2][j] - v[0][j] for j in range(3)]
                cr = (a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0])
                n = me_["norms"][p["nrm"][0]]
                if sum(x * y for x, y in zip(cr, n)) < 0:
                    good += 1
                else:
                    bad += 1
        self.assertEqual(bad, 0)
        self.assertGreater(good, 20)


class FoldingTests(unittest.TestCase):
    def check(self, **kw):
        g, b, png, info = make_robot(**kw)
        meta = copy.deepcopy(DEMO_META)
        meta["fitHeight"] = 1800
        res = bake_dir(meta, g, b, png)
        model, motions = parse(res["blob"])
        pts = game_world_points(model, motions, motions[0][0][2])
        ys = [p[1] for p in pts]
        return res, model, motions, pts, ys

    def test_armature_scale_is_folded(self):
        res, model, motions, pts, ys = self.check(prefix_scale=0.01)
        self.assertAlmostEqual(max(ys) - min(ys), 1800, delta=4)
        self.assertEqual(len(model["bones"]), 5)
        self.assertLessEqual(max(abs(k[i]) for bn in model["bones"][1:] for k in bn["scl"] for i in range(3)), 4096)
        base = self.check()[3]
        worst = max(max(abs(a - b) for a, b in zip(p, q)) for p, q in zip(sorted(pts), sorted(base)))
        self.assertLessEqual(worst, 4)

    def test_armature_yaw_rotates_the_model(self):
        res, model, motions, pts, ys = self.check(prefix_yaw=90.0)
        base = self.check()[3]
        # a +90 degree turn about the vertical axis: (x, z) -> (z, -x) in glTF space, (-x, z) -> (-z, ...) in the game
        mapped = sorted(tuple(round(c) for c in (p[0], p[1], p[2])) for p in pts)
        self.assertEqual(len(mapped), len(base))
        self.assertAlmostEqual(max(ys) - min(ys), 1800, delta=4)
        xs = [p[0] for p in pts]
        zs = [p[2] for p in pts]
        bx, bz = [p[0] for p in base], [p[2] for p in base]
        self.assertAlmostEqual(max(xs) - min(xs), max(bz) - min(bz), delta=6)
        self.assertAlmostEqual(max(zs) - min(zs), max(bx) - min(bx), delta=6)

    def test_yaw_option(self):
        g, b, png, info = make_robot()
        meta = copy.deepcopy(DEMO_META)
        meta["yaw"] = 180
        res = bake_dir(meta, g, b, png)
        model, motions = parse(res["blob"])
        pts = game_world_points(model, motions, motions[0][0][2])
        base = self.check()[3]
        self.assertAlmostEqual(sum(p[2] for p in pts) / len(pts), -sum(p[2] for p in base) / len(base), delta=3)

    def test_split_bone_over_256_vertices(self):
        g, b, png, info = make_robot(many_verts=40)      # 24 + 40 * 24 vertices on the torso
        res = bake_dir(copy.deepcopy(DEMO_META), g, b, png)
        model, motions = parse(res["blob"])
        self.assertGreater(len(model["bones"]), 5)
        for bn in model["bones"][1:]:
            self.assertLessEqual(len(bn["mesh"]["verts"]), 256)
            self.assertLessEqual(len(bn["mesh"]["norms"]), 256)
        extra = model["bones"][5:]
        self.assertTrue(all(bn["parent"] == 2 for bn in extra))
        for bn in extra:
            self.assertEqual(bn["pos"], [(0, 0, 0, 0), (0, 0, 0, 30000)])
            self.assertEqual(bn["scl"], [(4096, 4096, 4096, 0), (4096, 4096, 4096, 30000)])
        self.assertEqual(sum(len(bn["mesh"]["polys"]) for bn in model["bones"][1:]), info["tris"])
        self.assertTrue(any("extra child bones" in s for s in res["info"]))

    def test_bone_without_polygons_keeps_the_hierarchy(self):
        g, b, png, info = make_robot(empty_hip=True)
        g["nodes"][1]["translation"] = [0.0, 0.0, 0.001]       # not an identity: it cannot be merged into the root
        res = bake_dir(copy.deepcopy(DEMO_META), g, b, png)
        model, motions = parse(res["blob"])
        self.assertEqual(len(model["bones"]), 5)
        hip = model["bones"][1]
        self.assertEqual((len(hip["mesh"]["verts"]), len(hip["mesh"]["polys"])), (0, 0))
        self.assertEqual([bn["parent"] for bn in model["bones"][1:]], [0, 1, 2, 2])
        pts = game_world_points(model, motions, motions[0][0][2])
        self.assertEqual(len(pts), 3 * (info["tris"]))

    def test_identity_root_without_polygons_becomes_the_root(self):
        g, b, png, info = make_robot(empty_hip=True)
        res = bake_dir(copy.deepcopy(DEMO_META), g, b, png)
        model, motions = parse(res["blob"])
        self.assertEqual([bn["parent"] for bn in model["bones"][1:]], [0, 1, 1])

    def test_spin_keys_reproduce_the_rotation(self):
        g, b, png, info = make_robot(anim_extra="spin")
        meta = copy.deepcopy(DEMO_META)
        meta["motions"]["5"] = {"animation": "spin", "blendIn": 0, "end": "hold"}
        res = bake_dir(meta, g, b, png)
        model, motions = parse(res["blob"])
        steps = motions[5]
        f0, n = steps[0][2], steps[0][1]
        arm = model["bones"][4]
        worst = 0.0
        for f in range(f0, f0 + n):
            rot = me.sample_keys(arm["rot"], f)
            m = me.rotzyx(rot)
            t = (f - f0) / (n - 1) * 2.0
            q = quat_axis((0, 1, 0), 90 * (t / 0.25) * 0.9999)
            want = me.conj(mi.quat_matrix(q))
            worst = max(worst, max(abs(m[i][j] - want[i][j]) for i in range(3) for j in range(3)))
        self.assertLess(worst, 0.02)


class PartnerSlotTests(unittest.TestCase):
    """--partner: one warning per motion slot a partner's battle code plays and the model leaves empty."""

    def bake(self, meta_mutate=None, partner=True):
        g, b, png, _ = make_robot(anim_extra="partner", name="partner")
        meta = copy.deepcopy(PARTNER_META)
        if meta_mutate:
            meta_mutate(meta)
        return bake_dir(meta, g, b, png, partner=partner)

    def test_partner_demo_has_no_warning(self):
        res = self.bake()
        self.assertEqual(res["warnings"], [])
        self.assertEqual(res["stats"]["slots"], mi.SLOTS)

    def test_each_missing_slot_is_a_line(self):
        def mut(m):
            del m["fill"]
            for k in ("3", "13", "20", "32"):
                del m["motions"][k]
        res = self.bake(meta_mutate=mut)
        slots = [int(w.split()[2]) for w in res["warnings"] if "has no animation" in w]
        self.assertEqual(slots, [3, 13, 20, 32], res["warnings"])
        self.assertTrue(all(w.startswith("motion slot ") for w in res["warnings"]))
        self.assertIn("(flinch)", res["warnings"][0])
        self.assertFalse(any("are not mapped" in w for w in res["warnings"]))

    def test_without_the_flag_the_generic_line_stays(self):
        def mut(m):
            del m["fill"]
            del m["motions"]["3"]
        res = self.bake(meta_mutate=mut, partner=False)
        self.assertTrue(any("motion slots [3]" in w for w in res["warnings"]), res["warnings"])

    def test_tables_are_sorted_and_cover_the_generic_minimum(self):
        for table in (mi.ENEMY_NEEDS, mi.PARTNER_NEEDS):
            slots = [t[0] for t in table]
            self.assertEqual(slots, sorted(set(slots)))
            self.assertTrue(set(mi.MIN_SLOTS) <= set(slots))
        self.assertEqual([t[0] for t in mi.ENEMY_NEEDS], list(mi.MIN_SLOTS))

    def test_check_flag_on_the_command_line(self):
        with tempfile.TemporaryDirectory() as d:
            write_partner_demo(d)
            self.assertEqual(mi.main([os.path.join(d, "partner.json"), "--check", "--partner"]), 0)


class ValidationTests(unittest.TestCase):
    def bake(self, mutate=None, meta_mutate=None):
        g, b, png, _ = make_robot()
        meta = copy.deepcopy(DEMO_META)
        if mutate:
            mutate(g)
        if meta_mutate:
            meta_mutate(meta)
        return bake_dir(meta, g, b, png)

    def test_format_and_name(self):
        with self.assertRaises(mi.ModelImportError):
            self.bake(meta_mutate=lambda m: m.update(format="model-src-v2"))
        with self.assertRaises(mi.ModelImportError):
            self.bake(meta_mutate=lambda m: m.update(name="bad name!"))

    def test_missing_idle_slot_is_an_error(self):
        def mut(m):
            del m["motions"]["1"]
            del m["fill"]
        with self.assertRaisesRegex(mi.ModelImportError, "slot 1"):
            self.bake(meta_mutate=mut)

    def test_unknown_animation(self):
        with self.assertRaisesRegex(mi.ModelImportError, "no animation 'walk'"):
            self.bake(meta_mutate=lambda m: m["motions"].update({"3": "walk"}))

    def test_missing_minimum_slots_warn(self):
        def mut(m):
            del m["fill"]
        res = self.bake(meta_mutate=mut)
        self.assertTrue(any("motion slots [2, 3, 4, 10, 11, 12, 14]" in w for w in res["warnings"]), res["warnings"])

    def test_scale_and_fit_are_exclusive(self):
        with self.assertRaises(mi.ModelImportError):
            self.bake(meta_mutate=lambda m: m.update(scale=10))

    def test_tiny_model_warns_without_scale(self):
        def mut(m):
            del m["fitHeight"]
        res = self.bake(meta_mutate=mut)
        self.assertTrue(any("scale" in w for w in res["warnings"]))

    def test_vertex_out_of_range(self):
        with self.assertRaisesRegex(mi.ModelImportError, "16 bit"):
            self.bake(meta_mutate=lambda m: (m.pop("fitHeight"), m.update(scale=1e6)))

    def test_non_uniform_armature_scale(self):
        def mut(g):
            g["nodes"][0]["scale"] = [1.0, 2.0, 1.0]
        with self.assertRaisesRegex(mi.ModelImportError, "non-uniform"):
            self.bake(mutate=mut)

    def test_slot_copy_and_range(self):
        res = self.bake(meta_mutate=lambda m: m["motions"].update({"3": 0}))
        model, motions = parse(res["blob"])
        self.assertEqual(motions[3], motions[0])
        with self.assertRaises(mi.ModelImportError):
            self.bake(meta_mutate=lambda m: m["motions"].update({"99": "idle"}))
        with self.assertRaises(mi.ModelImportError):
            self.bake(meta_mutate=lambda m: m["motions"].update({"7": 8}))

    def test_motion_options(self):
        def mut(m):
            m["motions"]["4"] = {"animation": "idle", "loop": True, "blendIn": 6, "loopStart": 3}
            m["motions"]["5"] = {"animation": "idle", "end": "hold", "blendIn": 0}
            m["motions"]["6"] = {"animation": "idle", "speed": 2.0, "blendIn": 0, "blendOut": 4}
        res = self.bake(meta_mutate=mut)
        model, motions = parse(res["blob"])
        self.assertEqual(motions[4][0], (0, 6, 0, 0))
        self.assertEqual(motions[4][2], (6 + motions[4][1][1], 0, -32768, 6 + 3))
        self.assertEqual(motions[5][1], (91, 0, -1, 0))
        self.assertEqual(motions[6][0][1], 46)                  # twice as fast: half the frames
        self.assertEqual(motions[6][1], (46, 4, 0, 0))

    def test_motions_default_to_motion_nn_names(self):
        def mut(g):
            g["animations"][0]["name"] = "motion_00"
            g["animations"].append(dict(g["animations"][0], name="motion_01"))

        def meta(m):
            del m["motions"]
            del m["fill"]
        res = self.bake(mutate=mut, meta_mutate=meta)
        model, motions = parse(res["blob"])
        self.assertIsNotNone(motions[0])
        self.assertIsNotNone(motions[1])
        self.assertIsNone(motions[2])

    def test_missing_texture_file(self):
        g, b, png, _ = make_robot()
        with tempfile.TemporaryDirectory() as d:
            write_model(d, g, b, png, DEMO_META)
            os.remove(os.path.join(d, "robot.png"))
            with self.assertRaisesRegex(mi.ModelImportError, "texture file not found"):
                mi.bake(copy.deepcopy(DEMO_META), d)

    def test_from_is_rejected_with_the_way_out(self):
        g, b, png, _ = make_robot()
        meta = copy.deepcopy(DEMO_META)
        meta["from"] = "dw3:model/383"
        with tempfile.TemporaryDirectory() as d:
            write_model(d, g, b, png, DEMO_META)
            with self.assertRaisesRegex(mi.ModelImportError, "export the folder first"):
                mi.bake(meta, d)

    def test_untextured_material_becomes_a_patch(self):
        def mut(g):
            g["materials"][0]["pbrMetallicRoughness"] = {"baseColorFactor": [0.2, 0.4, 0.8, 1.0]}
        res = self.bake(mutate=mut)
        self.assertTrue(any("no texture" in w for w in res["warnings"]))
        model, motions = parse(res["blob"])
        blob = res["blob"]
        vram = me.make_vram(me.parse_tim_archive(blob, me.u32(blob, 4 * 2)))
        cols = {me.c15(v) for b in model["bones"][1:] for p in b["mesh"]["polys"] for v in me.poly_texels(vram, p) if v}
        self.assertEqual(len(cols), 1)
        r, g_, b_ = next(iter(cols))
        self.assertTrue(b_ > g_ > r)

    def test_alpha_cutout_uses_colour_zero(self):
        g, b, png, _ = make_robot()
        w, h, rgba = mi.read_png(png)
        a = bytearray(rgba)
        for y in range(0, 32):
            for x in range(0, 32):
                if x % 4 == 0:
                    a[(y * w + x) * 4 + 3] = 0
        png2 = me.write_png(w, h, bytes(a))
        meta = copy.deepcopy(DEMO_META)
        g["materials"][0]["alphaMode"] = "MASK"
        res = bake_dir(meta, g, b, png2)
        model, motions = parse(res["blob"])
        blob = res["blob"]
        vram = me.make_vram(me.parse_tim_archive(blob, me.u32(blob, 4 * 2)))
        zero = sum(1 for bn in model["bones"][1:2] for p in bn["mesh"]["polys"] for v in me.poly_texels(vram, p) if v == 0)
        self.assertGreater(zero, 0)

    def test_large_texture_is_downscaled(self):
        g, b, png, _ = make_robot()
        big = png_rgba(512, 512, lambda x, y: [(220, 60, 60, 255), (60, 200, 80, 255), (70, 90, 230, 255), (230, 210, 60, 255)][(y // 256) * 2 + x // 256])
        res = bake_dir(copy.deepcopy(DEMO_META), g, b, big)
        self.assertLess(res["stats"]["scale"], 1.0)
        self.assertLessEqual(res["stats"]["palettes"], 64)

    def test_many_islands_merge_into_64_cluts(self):
        # a 1x1 flat material per triangle with distinct colours -> more palettes than CLUT slots
        polys = []
        for i in range(100):
            col = (i * 7 % 256, i * 13 % 256, i * 29 % 256, 255)
            mat = (None, tuple(c / 255 for c in col), "OPAQUE", 0.5)
            polys.append(dict(mat=mat, corners=[((0, 0, 0), (0, 0, 1), (0.5, 0.5))] * 3))
        rows, st = mi.build_texture(polys, None, lambda s: None, lambda s: None)
        self.assertEqual(st["palettes"], 64)
        self.assertTrue(all(p["clut"][1] in range(240, 256) and p["clut"][0] in (0, 16, 32, 48) for p in polys))
        self.assertEqual(len(rows), 256)

    def test_round_trip_through_the_exporter(self):
        """bake -> export to glTF with the exporter -> bake again: the same geometry and the same motion slots."""
        res = bake_dir(copy.deepcopy(DEMO_META), *make_robot()[:3])
        model, motions = parse(res["blob"])
        gltf, binary, png = me.build_gltf(model, motions, "again")
        me.validate_gltf(gltf, binary)
        with tempfile.TemporaryDirectory() as d:
            Path(d, "again.gltf").write_text(json.dumps(gltf))
            Path(d, "again.bin").write_bytes(binary)
            Path(d, "again.png").write_bytes(png)
            motion_map = {}
            for a in gltf["animations"]:
                ex = a["extras"]
                spec = {"animation": a["name"], "blendIn": 0}
                if "loopStartFrame" in ex:
                    spec.update(loop=True, loopStart=ex["loopStartFrame"])
                else:
                    spec.update(loop=False, end="hold")
                motion_map[str(ex["slot"])] = spec
            meta = {"format": "model-src-v1", "name": "again", "gltf": "again.gltf", "motions": motion_map,
                    "effects": "dw3:digimon/383"}
            res2 = mi.bake(meta, d)
        model2, motions2 = parse(res2["blob"])
        self.assertEqual(len(model2["bones"]), len(model["bones"]))
        p1 = sorted(tuple(round(c) for c in p) for p in game_world_points(model, motions, motions[0][0][2]))
        p2 = sorted(tuple(round(c) for c in p) for p in game_world_points(model2, motions2, motions2[0][0][2]))
        self.assertEqual(len(p1), len(p2))
        self.assertLessEqual(max(max(abs(a - b) for a, b in zip(u, v)) for u, v in zip(p1, p2)), 2)
        self.assertEqual([m is not None for m in motions], [m is not None for m in motions2])
        self.assertEqual(motions2[0][1][:3][2], -32768)


class CliTests(unittest.TestCase):
    def test_check_and_write(self):
        with tempfile.TemporaryDirectory() as d:
            write_demo(d)
            out = os.path.join(d, "mymod")
            import io
            from contextlib import redirect_stdout
            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = mi.main([os.path.join(d, "robot.json"), "--out", out])
            self.assertEqual(rc, 0)
            blob = Path(out, "digimon", "robot", "model.bin").read_bytes()
            text = buf.getvalue()
            self.assertIn('"file": "mymod:digimon/robot/model.bin"', text)
            self.assertIn("digimon-digimon-v1", text)
            model, motions = parse(blob)
            self.assertEqual(len(model["bones"]), 5)
            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = mi.main([os.path.join(d, "robot.json"), "--check"])
            self.assertEqual(rc, 0)

    def test_glb_input(self):
        g, b, png, _ = make_robot()
        g = copy.deepcopy(g)
        # embed the buffer and the image in a .glb
        g["buffers"][0].pop("uri")
        view = len(g["bufferViews"])
        binary = bytearray(b)
        while len(binary) % 4:
            binary.append(0)
        g["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(png)})
        binary += png
        g["images"][0] = {"bufferView": view, "mimeType": "image/png"}
        g["buffers"][0]["byteLength"] = len(binary)
        js = json.dumps(g).encode()
        js += b" " * (-len(js) % 4)
        binary += b"\0" * (-len(binary) % 4)
        glb = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(binary)) + struct.pack("<II", len(js), 0x4E4F534A) + js \
            + struct.pack("<II", len(binary), 0x004E4942) + bytes(binary)
        with tempfile.TemporaryDirectory() as d:
            Path(d, "robot.glb").write_bytes(glb)
            meta = copy.deepcopy(DEMO_META)
            meta["gltf"] = "robot.glb"
            res = mi.bake(meta, d)
        self.assertEqual(res["stats"]["bones"], 5)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--demo":
        write_demo(sys.argv[2])
        print("demo model written to %s" % sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--demo-partner":
        write_partner_demo(sys.argv[2])
        print("partner demo model written to %s" % sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--parity":
        write_parity(sys.argv[2])
        print("parity cases written to %s" % sys.argv[2])
    else:
        unittest.main()
