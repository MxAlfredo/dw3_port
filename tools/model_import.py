#!/usr/bin/env python3
"""Bake a neutral model source (glTF 2.0 + PNG + metadata JSON) into a Digimon World 3 battle model file.

Usage:
  python3 -I tools/model_import.py <source.json> --out <mod dir> [--mod <mod name>] [--check]

<source.json> is a `model-src-v1` file (docs/modding/README.md, section "Hacer un modelo nuevo"). The blob is
written to <mod dir>/models/<name>.bin and a digimon.json snippet is printed. --check validates and reports
without writing. Pure Python 3 standard library; nothing from the disc is needed or read: the effect scripts
come from a donor fighter that the game resolves at run time (`"effects": "dw3:digimon/<id>"`).
A model.json with `"from": "dw3:model/<id>"` (the game completes it from an original at load) is rejected here: the tool
never reads the disc. Export the folder first (the game's "Export models" row), or let the game bake it.

What the game's format allows (docs/formats.md 4.6) and what this tool does about it:
  * rigid skin: each triangle goes to the bone that weighs most on it (a warning counts the split ones);
  * at most 256 vertices and 256 normals per bone mesh: a bigger bone gets child bones with identity keys;
  * one 256x240 texture of 4 bpp with 16 colour palettes (CLUT) embedded in the last rows: the UV islands are
    repacked into it, scaled down if needed, and each island gets a median-cut palette; colour 0 is transparent;
  * keys are sampled densely from the glTF animations (60 per second), the angles are unwrapped (the game
    interpolates the raw angle linearly) and the tracks are reduced with a small tolerance; every motion lives on
    one shared timeline under 30000 frames and has fewer than 0x640 steps;
  * a motion slot table of 62 slots written with the game's step format (loop, blend in, blend back to idle).
"""
import base64
import bisect
import json
import math
import os
import struct
import sys
import zlib
from urllib.parse import unquote

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model_export as me  # noqa: E402

FORMAT = "model-src-v1"
SENTINEL = 30000                # last key frame of every track
SLOTS = 62                      # motion slots of a fighter
MIN_SLOTS = (0, 1, 2, 3, 4, 10, 11, 12, 14)     # the safe minimum of docs/formats.md 4.6
MAX_STEPS = 0x640               # Model.keyframes capacity
TEX_W, TEX_H = 256, 240         # texel area; rows 240..255 hold the CLUTs (4 per row)
MAX_CLUTS = 64
MAX_VERTS = 256                 # per bone mesh (1 byte indices)
WARN_BONES, MAX_BONES = 40, 90  # the battle layer has 100 callbacks for every fighter on screen
WARN_POLYS = 3000
BLEND_STEPS = 10
C = me.C_AXES
PNG_SIG = b"\x89PNG\r\n\x1a\n"


class ModelImportError(Exception):
    """An input the tool cannot turn into a game model (the message says what to change)."""


# ---------------------------------------------------------------- PNG reader

def _unfilter(raw, h, stride, bpp):
    out = bytearray(h * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        elif ft != 0:
            raise ModelImportError("PNG: unknown filter %d" % ft)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out


def read_png(data):
    """-> (width, height, rgba bytes). 8/16-bit grey, grey+alpha, RGB, RGBA and 1-8 bit palette; no interlace."""
    if data[:8] != PNG_SIG:
        raise ModelImportError("the texture is not a PNG (convert it to PNG)")
    pos, idat, plte, trns, hdr = 8, bytearray(), None, None, None
    while pos + 8 <= len(data):
        ln, tag = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if tag == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    if hdr is None:
        raise ModelImportError("PNG without header")
    w, h, depth, ctype, _, _, interlace = hdr
    if interlace:
        raise ModelImportError("interlaced PNG is not supported (save it without interlacing)")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if chans is None or depth not in (1, 2, 4, 8, 16):
        raise ModelImportError("PNG colour type %d / depth %d is not supported" % (ctype, depth))
    bits = depth * chans
    stride = (w * bits + 7) // 8
    rows = _unfilter(zlib.decompress(bytes(idat)), h, stride, max(1, bits // 8))
    out = bytearray(w * h * 4)
    for y in range(h):
        line = rows[y * stride:(y + 1) * stride]
        if depth == 16:
            line = line[0::2]
        if depth < 8:
            mask = (1 << depth) - 1
            vals = []
            for b in line:
                for s in range(8 - depth, -1, -depth):
                    vals.append((b >> s) & mask)
            line = vals[:w]
            if ctype == 0:
                line = [v * 255 // mask for v in line]
        o = y * w * 4
        if ctype == 6:
            out[o:o + w * 4] = bytes(line[:w * 4])
        elif ctype == 2:
            for x in range(w):
                out[o + 4 * x:o + 4 * x + 4] = bytes((line[3 * x], line[3 * x + 1], line[3 * x + 2], 255))
        elif ctype == 0:
            for x in range(w):
                g = line[x]
                out[o + 4 * x:o + 4 * x + 4] = bytes((g, g, g, 255))
        elif ctype == 4:
            for x in range(w):
                g = line[2 * x]
                out[o + 4 * x:o + 4 * x + 4] = bytes((g, g, g, line[2 * x + 1]))
        else:
            if plte is None:
                raise ModelImportError("palette PNG without PLTE")
            for x in range(w):
                i = line[x]
                a = trns[i] if trns and i < len(trns) else 255
                out[o + 4 * x:o + 4 * x + 4] = bytes((plte[3 * i], plte[3 * i + 1], plte[3 * i + 2], a))
    return w, h, out


# ---------------------------------------------------------------- glTF reader

TYPE_N = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
COMP_FMT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
COMP_NORM = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


class Gltf:
    def __init__(self, js, buffers, base):
        self.js, self.buffers, self.base = js, buffers, base
        self._images = {}

    def view_bytes(self, vi):
        v = self.js["bufferViews"][vi]
        off = v.get("byteOffset", 0)
        return self.buffers[v["buffer"]][off:off + v["byteLength"]]

    def accessor(self, ai):
        """-> list of tuples (one per element); normalized integers become floats."""
        a = self.js["accessors"][ai]
        if "sparse" in a:
            raise ModelImportError("sparse accessors are not supported (export without sparse storage)")
        n, nc, ct = a["count"], TYPE_N[a["type"]], a["componentType"]
        fmt = COMP_FMT[ct]
        size = struct.calcsize(fmt)
        if "bufferView" not in a:
            return [(0.0,) * nc for _ in range(n)]
        v = self.js["bufferViews"][a["bufferView"]]
        buf = self.buffers[v["buffer"]]
        off = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = v.get("byteStride") or nc * size
        norm = COMP_NORM.get(ct) if a.get("normalized") else None
        out = []
        f = "<%d%s" % (nc, fmt)
        for i in range(n):
            t = struct.unpack_from(f, buf, off + i * stride)
            if norm:
                t = tuple(max(x / norm, -1.0) for x in t)
            out.append(t)
        return out

    def image(self, ii):
        """-> (w, h, rgba) of the glTF image ii."""
        if ii not in self._images:
            im = self.js["images"][ii]
            if "uri" in im:
                uri = im["uri"]
                if uri.startswith("data:"):
                    data = base64.b64decode(uri.split(",", 1)[1])
                else:
                    path = os.path.join(self.base, unquote(uri))
                    if not os.path.isfile(path):
                        raise ModelImportError("texture file not found: %s" % path)
                    with open(path, "rb") as fh:
                        data = fh.read()
            else:
                data = bytes(self.view_bytes(im["bufferView"]))
            self._images[ii] = read_png(data)
        return self._images[ii]


def load_gltf(path):
    with open(path, "rb") as fh:
        data = fh.read()
    base = os.path.dirname(os.path.abspath(path))
    return parse_gltf(data, base)


def parse_gltf(data, base):
    """A .gltf (JSON) or .glb (binary) byte string -> Gltf."""
    binchunk = None
    if data[:4] == b"glTF":
        length = struct.unpack_from("<I", data, 8)[0]
        off, js = 12, None
        while off < length:
            clen, ctype = struct.unpack_from("<II", data, off)
            chunk = data[off + 8:off + 8 + clen]
            off += 8 + clen
            if ctype == 0x4E4F534A:
                js = json.loads(chunk.decode("utf-8"))
            elif ctype == 0x004E4942:
                binchunk = chunk
        if js is None:
            raise ModelImportError("glb without a JSON chunk")
    else:
        try:
            js = json.loads(data.decode("utf-8"))
        except ValueError as e:
            raise ModelImportError("the glTF is not valid JSON: %s" % e)
    if js.get("asset", {}).get("version", "2.0")[:1] != "2":
        raise ModelImportError("only glTF 2.0 is supported")
    buffers = []
    for b in js.get("buffers", []):
        uri = b.get("uri")
        if uri is None:
            buffers.append(binchunk or b"")
        elif uri.startswith("data:"):
            buffers.append(base64.b64decode(uri.split(",", 1)[1]))
        else:
            path = os.path.join(base, unquote(uri))
            if not os.path.isfile(path):
                raise ModelImportError("buffer file not found: %s" % path)
            with open(path, "rb") as fh:
                buffers.append(fh.read())
    return Gltf(js, buffers, base)


# ---------------------------------------------------------------- small math

def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def fsum(it):
    """Left-to-right float sum. (The built-in sum() of Python 3.12+ compensates its rounding; the C baker adds plainly, and
    both must give the same bits.)"""
    t = 0.0
    for x in it:
        t += x
    return t


def matmul(a, b):
    return [[fsum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def matvec(a, v):
    return [fsum(a[i][k] * v[k] for k in range(3)) for i in range(3)]


def quat_from_matrix(m):
    """Unit quaternion (x, y, z, w) of a rotation matrix (model_export's, with a plain sum)."""
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    n = math.sqrt(fsum(x * x for x in q))
    return tuple(x / n for x in q)


def vlen(v):
    return math.sqrt(fsum(c * c for c in v))


def trs_matrix(t, q, s):
    r = quat_matrix(q)
    return [[r[i][j] * s[j] for j in range(3)] for i in range(3)], list(t)


def compose(a, b):
    """(M1, t1) * (M2, t2) as affine maps."""
    m1, t1 = a
    m2, t2 = b
    return matmul(m1, m2), [x + y for x, y in zip(matvec(m1, t2), t1)]


def decompose_matrix(m16):
    """Column-major 4x4 -> (t, quat, scale)."""
    t = (m16[12], m16[13], m16[14])
    cols = [(m16[0], m16[1], m16[2]), (m16[4], m16[5], m16[6]), (m16[8], m16[9], m16[10])]
    s = [vlen(c) or 1.0 for c in cols]
    rot = [[cols[j][i] / s[j] for j in range(3)] for i in range(3)]
    if me.det3(rot) < 0:
        s[0] = -s[0]
        rot = [[cols[j][i] / s[j] for j in range(3)] for i in range(3)]
    return t, quat_from_matrix(rot), tuple(s)


def euler_candidates(m):
    """The two (rx, ry, rz) radian triples with rotzyx == m."""
    sy = max(-1.0, min(1.0, -m[2][0]))
    b = math.asin(sy)
    if abs(sy) < 0.999999:
        a = math.atan2(m[2][1], m[2][2])
        c = math.atan2(m[1][0], m[0][0])
        return [(a, b, c), (a + math.pi, math.pi - b, c + math.pi)]
    return [(0.0, b, math.atan2(-m[0][1], m[1][1]))]


RAW = 4096.0 / (2 * math.pi)


def raw_angles(m, prev):
    """Raw (4096 = 360 degrees) angles of rotation matrix m, the equivalent triple closest to prev (or in +-180)."""
    best, bd = None, None
    for cand in euler_candidates(m):
        out = []
        for i, ang in enumerate(cand):
            r = ang * RAW
            ref = prev[i] if prev is not None else 0.0
            r += 4096.0 * round((ref - r) / 4096.0)
            out.append(r)
        d = fsum(abs(out[i] - (prev[i] if prev is not None else 0.0)) for i in range(3))
        if bd is None or d < bd:
            best, bd = out, d
    return best


def slerp(a, b, u):
    d = fsum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = [-x for x in b], -d
    if d > 0.9995:
        q = [x + (y - x) * u for x, y in zip(a, b)]
    else:
        th = math.acos(d)
        sa, sb = math.sin((1 - u) * th), math.sin(u * th)
        q = [(x * sa + y * sb) / math.sin(th) for x, y in zip(a, b)]
    n = vlen(q) or 1.0
    return tuple(x / n for x in q)


def eval_sampler(sm, t, is_quat):
    times, vals, interp = sm
    cub = interp == "CUBICSPLINE"

    def val(i):
        return vals[3 * i + 1] if cub else vals[i]

    n = len(times)
    if n == 1 or t <= times[0]:
        return val(0)
    if t >= times[-1]:
        return val(n - 1)
    i = bisect.bisect_right(times, t) - 1
    dt = times[i + 1] - times[i]
    u = (t - times[i]) / dt
    if interp == "STEP":
        return val(i)
    if cub:
        v0, v1 = val(i), val(i + 1)
        m0, m1 = vals[3 * i + 2], vals[3 * (i + 1)]
        s2, s3 = u * u, u * u * u
        q = tuple((2 * s3 - 3 * s2 + 1) * v0[k] + (s3 - 2 * s2 + u) * dt * m0[k]
                  + (-2 * s3 + 3 * s2) * v1[k] + (s3 - s2) * dt * m1[k] for k in range(len(v0)))
        if is_quat:
            n2 = vlen(q) or 1.0
            q = tuple(x / n2 for x in q)
        return q
    a, b = val(i), val(i + 1)
    if is_quat:
        return slerp(a, b, u)
    return tuple(x + (y - x) * u for x, y in zip(a, b))


def conj_vec(v):
    return (C[0] * v[0], C[1] * v[1], C[2] * v[2])


# ---------------------------------------------------------------- scene: nodes, skins, polygons

class Scene:
    """The glTF scene reduced to what the game needs: game bones, rigid triangles and animations."""

    def __init__(self, g, meta, warn):
        self.g, self.meta, self.warn = g, meta, warn
        js = g.js
        self.nodes = js.get("nodes", [])
        self.parent = {}
        for i, n in enumerate(self.nodes):
            for c in n.get("children", []):
                self.parent[c] = i
        roots = None
        if "scenes" in js and js["scenes"]:
            roots = js["scenes"][js.get("scene", 0)].get("nodes", [])
        if roots is None:
            roots = [i for i in range(len(self.nodes)) if i not in self.parent]
        self.reach = set()
        stack = list(roots)
        while stack:
            i = stack.pop()
            if i in self.reach:
                continue
            self.reach.add(i)
            stack += self.nodes[i].get("children", [])
        self.rest = [self.rest_trs(i) for i in range(len(self.nodes))]
        self.animated = set()
        for a in js.get("animations", []):
            for ch in a.get("channels", []):
                if ch["target"].get("path") in ("translation", "rotation", "scale") and "node" in ch["target"]:
                    self.animated.add(ch["target"]["node"])

    def rest_trs(self, i):
        n = self.nodes[i]
        if "matrix" in n:
            return decompose_matrix(n["matrix"])
        q = tuple(n.get("rotation", (0, 0, 0, 1)))
        ql = vlen(q) or 1.0
        return (tuple(n.get("translation", (0, 0, 0))), tuple(x / ql for x in q), tuple(n.get("scale", (1, 1, 1))))

    def local_matrix(self, i):
        t, q, s = self.rest[i]
        return trs_matrix(t, q, s)

    def ancestors(self, i):
        out = []
        while i in self.parent:
            i = self.parent[i]
            out.append(i)
        return out[::-1]

    def world_rest(self, i):
        m = ([[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]], [0.0, 0.0, 0.0])
        for a in self.ancestors(i) + [i]:
            m = compose(m, self.local_matrix(a))
        return m


def read_primitives(g, scene, warn):
    """Every triangle primitive of the scene: list of dicts(node, skin, pos, nrm, uv, infl, tris, mat)."""
    js = g.js
    prims = []
    for ni in sorted(scene.reach):
        n = scene.nodes[ni]
        if "mesh" not in n:
            continue
        mesh = js["meshes"][n["mesh"]]
        for pi, p in enumerate(mesh["primitives"]):
            mode = p.get("mode", 4)
            if mode != 4:
                raise ModelImportError("mesh %r primitive %d: only triangle lists (mode 4) are supported" % (mesh.get("name"), pi))
            at = p["attributes"]
            pos = [tuple(v) for v in g.accessor(at["POSITION"])]
            nrm = [tuple(v) for v in g.accessor(at["NORMAL"])] if "NORMAL" in at else None
            uv = [tuple(v) for v in g.accessor(at["TEXCOORD_0"])] if "TEXCOORD_0" in at else None
            if "COLOR_0" in at:
                warn("vertex colours (COLOR_0) are ignored: the game lights textures only")
            if uv is None:
                warn("a primitive has no TEXCOORD_0: it gets one texel of its material colour")
            if "indices" in p:
                idx = [t[0] for t in g.accessor(p["indices"])]
            else:
                idx = list(range(len(pos)))
            if len(idx) % 3:
                raise ModelImportError("a primitive has %d indices (not a multiple of 3)" % len(idx))
            infl = None
            skin = n.get("skin")
            if skin is not None and "JOINTS_0" in at:
                infl = [[] for _ in pos]
                for k in (0, 1):
                    jn, wn = "JOINTS_%d" % k, "WEIGHTS_%d" % k
                    if jn not in at:
                        break
                    js_, ws_ = g.accessor(at[jn]), g.accessor(at[wn])
                    for vi in range(len(pos)):
                        for j, w in zip(js_[vi], ws_[vi]):
                            if w > 0:
                                infl[vi].append((int(j), w))
            elif skin is not None:
                skin = None
            prims.append(dict(node=ni, skin=skin, pos=pos, nrm=nrm, uv=uv, infl=infl,
                              tris=[tuple(idx[i:i + 3]) for i in range(0, len(idx), 3)], mat=p.get("material")))
    if not prims:
        raise ModelImportError("the glTF scene has no triangle meshes")
    return prims


# ---------------------------------------------------------------- the build

class Bone:
    def __init__(self, node, parent):
        self.node, self.parent = node, parent
        self.polys = []
        self.children = []
        self.top = False
        self.k = 1.0            # uniform scale of the nodes folded above the subtree


class Builder:
    def __init__(self, meta, g, warn, info):
        self.meta, self.g, self.warn, self.info = meta, g, warn, info
        self.scene = Scene(g, meta, warn)

    # ---- skeleton
    def skeleton(self, prims):
        sc = self.scene
        js = self.g.js
        self.skin_joints, self.skin_ibm = {}, {}
        J = set()
        for p in prims:
            if p["skin"] is None:
                J.add(p["node"])
        for si in {p["skin"] for p in prims if p["skin"] is not None}:
            sk = js["skins"][si]
            joints = sk["joints"]
            if "inverseBindMatrices" in sk:
                ibm = [list(m) for m in self.g.accessor(sk["inverseBindMatrices"])]
            else:
                ibm = [[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]] * len(joints)
            self.skin_joints[si] = joints
            self.skin_ibm[si] = {j: ibm[k] for k, j in enumerate(joints)}
            J.update(joints)
        if not J:
            raise ModelImportError("no joints and no mesh nodes")
        desc = set()        # nodes with a J member at or below them
        for j in J:
            desc.add(j)
            desc.update(sc.ancestors(j))
        # a bone: a joint, an animated node, or a node between joints; the static nodes above all of them are folded
        inc = {x for x in desc if x in J or x in sc.animated or any(a in J for a in sc.ancestors(x))}
        self.included = inc
        # included nodes in DFS order from the topmost ones (nodes whose included ancestors are none)
        tops = [x for x in sorted(inc) if not any(a in inc for a in sc.ancestors(x))]
        bones = [Bone(None, 0)]
        idx = {}

        def add(n, par):
            b = Bone(n, par)
            bones.append(b)
            idx[n] = len(bones) - 1
            bones[par].children.append(idx[n])
            for c in nested_children(n):
                add(c, idx[n])

        def nested_children(n):
            """Included descendants of n whose nearest included ancestor is n."""
            out, stack = [], list(sc.nodes[n].get("children", []))
            while stack:
                c = stack.pop(0)
                if c in inc:
                    out.append(c)
                else:
                    stack = list(sc.nodes[c].get("children", [])) + stack
            return out

        for t in tops:
            add(t, 0)
        self.bones, self.node_bone = bones, idx

    # ---- polygons
    def polygons(self, prims):
        sc, warn = self.scene, self.warn
        mats = self.g.js.get("materials", [])
        self.raw_polys = []
        split_v = total_v = split_t = 0
        double = False
        for p in prims:
            pos, nrm, uv, infl = p["pos"], p["nrm"], p["uv"], p["infl"]
            skin = p["skin"]
            if nrm is None or self.meta.get("normals") in ("smooth", "flat"):
                nrm = self.make_normals(p, flat=self.meta.get("normals") == "flat")
            mat = mats[p["mat"]] if p["mat"] is not None else {}
            if mat.get("doubleSided"):
                double = True
            matkey = self.material_key(mat)
            for vi in range(len(pos)):
                if infl:
                    total_v += 1
                    if len(infl[vi]) > 1 and max(w for _, w in infl[vi]) < 0.999:
                        split_v += 1
            for tri in p["tris"]:
                if skin is None:
                    node = p["node"]
                    bi = self.node_bone[node]
                else:
                    wsum = {}
                    for vi in tri:
                        for j, w in (infl[vi] if infl else []):
                            nd = self.skin_joints[skin][j]
                            wsum[nd] = wsum.get(nd, 0.0) + w
                    if not wsum:
                        warn("a vertex has no joint weights: it follows the first joint")
                        wsum = {self.skin_joints[skin][0]: 1.0}
                    node = max(sorted(wsum), key=lambda n_: wsum[n_])
                    bi = self.node_bone[node]
                    owners = {max(infl[vi], key=lambda jw: jw[1])[0] for vi in tri if infl and infl[vi]}
                    if len(owners) > 1:
                        split_t += 1
                ibm = self.skin_ibm[skin][node] if skin is not None else None
                corners = []
                for vi in tri:
                    corners.append((self.to_local(ibm, pos[vi]), self.to_local_n(ibm, nrm[vi]),
                                    uv[vi] if uv else (0.0, 0.0)))
                # glTF is counter-clockwise, the game's order is (0, 2, 1) of it
                corners = [corners[0], corners[2], corners[1]]
                poly = dict(bone=bi, corners=corners, mat=matkey)
                self.raw_polys.append(poly)
                if mat.get("doubleSided") and self.meta.get("doubleSided") == "duplicate":
                    back = [corners[0], corners[2], corners[1]]
                    back = [(c[0], tuple(-x for x in c[1]), c[2]) for c in back]
                    self.raw_polys.append(dict(bone=bi, corners=back, mat=matkey))
        if double and self.meta.get("doubleSided") != "duplicate":
            warn("a material is double sided: the game draws one side only (set \"doubleSided\": \"duplicate\" to add the back faces)")
        if split_v:
            warn("%d of %d skinned vertices have split weights: each triangle follows its dominant bone (rigid skin)"
                 % (split_v, total_v))
        if split_t:
            warn("%d triangles span bones: they follow the bone that weighs most (rigid skin)" % split_t)

    def make_normals(self, p, flat):
        pos = p["pos"]
        acc = {}
        faces = []
        for a, b, c in p["tris"]:
            u = [pos[b][k] - pos[a][k] for k in range(3)]
            v = [pos[c][k] - pos[a][k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            faces.append(n)
            for vi in (a, b, c):
                key = tuple(round(x, 5) for x in pos[vi])
                s = acc.setdefault(key, [0.0, 0.0, 0.0])
                for k in range(3):
                    s[k] += n[k]
        out = [(0.0, 1.0, 0.0)] * len(pos)
        if flat:
            # one normal per corner is not expressible per vertex: use the face normal of the first triangle seen
            for (a, b, c), n in zip(p["tris"], faces):
                for vi in (a, b, c):
                    out[vi] = n
        else:
            for vi in range(len(pos)):
                out[vi] = tuple(acc[tuple(round(x, 5) for x in pos[vi])])
        return out

    @staticmethod
    def to_local(ibm, v):
        if ibm is None:
            return v
        return (ibm[0] * v[0] + ibm[4] * v[1] + ibm[8] * v[2] + ibm[12],
                ibm[1] * v[0] + ibm[5] * v[1] + ibm[9] * v[2] + ibm[13],
                ibm[2] * v[0] + ibm[6] * v[1] + ibm[10] * v[2] + ibm[14])

    @staticmethod
    def to_local_n(ibm, n):
        if ibm is None:
            return n
        m = [[ibm[0], ibm[4], ibm[8]], [ibm[1], ibm[5], ibm[9]], [ibm[2], ibm[6], ibm[10]]]
        try:
            mi = me.transpose(me.inv3(m))
        except ValueError:
            return n
        return tuple(matvec(mi, n))

    def material_key(self, mat):
        pbr = mat.get("pbrMetallicRoughness", {})
        fac = tuple(round(x, 3) for x in pbr.get("baseColorFactor", (1, 1, 1, 1)))
        tex = pbr.get("baseColorTexture")
        img = None
        if tex is not None:
            img = self.g.js["textures"][tex["index"]]["source"]
            if "extensions" in tex and "KHR_texture_transform" in tex["extensions"]:
                self.warn("KHR_texture_transform is ignored (bake the UV offset/scale in Blender)")
        else:
            self.warn("material %r has no texture: it becomes a flat colour patch" % mat.get("name"))
        return (img, fac, mat.get("alphaMode", "OPAQUE"), mat.get("alphaCutoff", 0.5))


# ---------------------------------------------------------------- texture: islands, packing, palettes

def shelf_pack(sizes, W, H):
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0], i))
    pos = [None] * len(sizes)
    x = y = sh = 0
    for i in order:
        w, h = sizes[i]
        if w > W or h > H:
            return None
        if x + w > W:
            y += sh
            x = sh = 0
        if y + h > H:
            return None
        pos[i] = (x, y)
        x += w
        sh = max(sh, h)
    return pos


def pack_scaled(rects, W, H):
    """Largest scale f <= 1 at which the rects (w, h in source texels) pack into W x H -> (f, sizes, positions)."""
    def at(f):
        sizes = [(max(1, round(w * f)), max(1, round(h * f))) for w, h in rects]
        return sizes, shelf_pack(sizes, W, H)

    sizes, pos = at(1.0)
    if pos is not None:
        return 1.0, sizes, pos
    lo, hi = 0.0, 1.0
    best = None
    for _ in range(18):
        mid = (lo + hi) / 2
        sizes, pos = at(mid)
        if pos is None:
            hi = mid
        else:
            lo, best = mid, (mid, sizes, pos)
    if best is None:
        raise ModelImportError("the texture islands do not fit in %dx%d even at 1/50 scale: too many UV islands" % (W, H))
    return best


def key15(r, g, b):
    return (min(31, round(r * 31 / 255.0))) | (min(31, round(g * 31 / 255.0)) << 5) | (min(31, round(b * 31 / 255.0)) << 10)


def comp15(k):
    return (k & 31, (k >> 5) & 31, (k >> 10) & 31)


WEIGHTS = (2.0, 3.0, 1.0)       # r, g, b: green counts most


def dist15(a, b):
    ar, ag, ab = comp15(a)
    br, bg, bb = comp15(b)
    return WEIGHTS[0] * (ar - br) ** 2 + WEIGHTS[1] * (ag - bg) ** 2 + WEIGHTS[2] * (ab - bb) ** 2


def median_cut(hist, n):
    """hist: {colour key: count} -> up to n colour keys."""
    if len(hist) <= n:
        return sorted(hist)
    boxes = [[(k, c) for k, c in sorted(hist.items())]]
    while len(boxes) < n:
        best, bs = None, 0.0
        for bi, b in enumerate(boxes):
            if len(b) < 2:
                continue
            for ch in range(3):
                vals = [comp15(k)[ch] for k, _ in b]
                sc = (max(vals) - min(vals)) * WEIGHTS[ch] * math.sqrt(sum(c for _, c in b))
                if sc > bs:
                    best, bs = (bi, ch), sc
        if best is None:
            break
        bi, ch = best
        b = boxes.pop(bi)
        b.sort(key=lambda kc: comp15(kc[0])[ch])
        half, acc, cut = sum(c for _, c in b) / 2.0, 0, 1
        for i, (_, c) in enumerate(b):
            acc += c
            if acc >= half:
                cut = max(1, min(len(b) - 1, i + 1))
                break
        boxes += [b[:cut], b[cut:]]
    pal = []
    for b in boxes:
        tot = sum(c for _, c in b)
        r = sum(comp15(k)[0] * c for k, c in b) / tot
        g = sum(comp15(k)[1] * c for k, c in b) / tot
        bl = sum(comp15(k)[2] * c for k, c in b) / tot
        pal.append(round(r) | (round(g) << 5) | (round(bl) << 10))
    return sorted(set(pal))


class UnionFind:
    def __init__(self):
        self.p = {}

    def find(self, x):
        self.p.setdefault(x, x)
        while self.p[x] != x:
            self.p[x] = self.p[self.p[x]]
            x = self.p[x]
        return x

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[rb] = ra


def tri_cells(pts, cell, gw, gh):
    """Cells of a (gw x gh) grid a triangle (pts in texels) covers: sampled at the 5 points of each cell."""
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    cx0, cx1 = max(0, int(min(xs) // cell)), min(gw - 1, int(max(xs) // cell))
    cy0, cy1 = max(0, int(min(ys) // cell)), min(gh - 1, int(max(ys) // cell))
    (ax, ay), (bx, by), (cx_, cy_) = pts
    den = (by - cy_) * (ax - cx_) + (cx_ - bx) * (ay - cy_)
    out = set()
    for p in pts:
        out.add((min(gw - 1, max(0, int(p[0] // cell))), min(gh - 1, max(0, int(p[1] // cell)))))
    for cy in range(cy0, cy1 + 1):
        for cx in range(cx0, cx1 + 1):
            if abs(den) < 1e-12:
                continue
            for ox, oy in ((0.5, 0.5), (0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)):
                px, py = (cx + ox) * cell, (cy + oy) * cell
                l1 = ((by - cy_) * (px - cx_) + (cx_ - bx) * (py - cy_)) / den
                l2 = ((cy_ - ay) * (px - cx_) + (ax - cx_) * (py - cy_)) / den
                if l1 >= -1e-9 and l2 >= -1e-9 and 1 - l1 - l2 >= -1e-9:
                    out.add((cx, cy))
                    break
    return out


def resample(img, x0, y0, x1, y1, rw, rh, factor, opaque, cutoff):
    """Box-filter the source rect (texels) into rw x rh. -> list of colour keys (-1 = transparent)."""
    w, h, rgba = img
    out = []
    fr, fg, fb, fa = factor
    for dy in range(rh):
        sy0 = y0 + (y1 - y0) * dy // rh
        sy1 = max(sy0 + 1, y0 + -(-(y1 - y0) * (dy + 1) // rh))
        for dx in range(rw):
            sx0 = x0 + (x1 - x0) * dx // rw
            sx1 = max(sx0 + 1, x0 + -(-(x1 - x0) * (dx + 1) // rw))
            sr = sg = sb = sa = 0
            n = 0
            for sy in range(sy0, min(sy1, h)):
                base = sy * w * 4
                for sx in range(sx0, min(sx1, w)):
                    o = base + sx * 4
                    a = rgba[o + 3]
                    sr += rgba[o] * a
                    sg += rgba[o + 1] * a
                    sb += rgba[o + 2] * a
                    sa += a
                    n += 1
            if n == 0:
                out.append(-1)
                continue
            alpha = sa / n * fa
            if opaque:
                alpha = 255
            if alpha < 255 * cutoff:
                out.append(-1)
                continue
            if sa == 0:
                out.append(-1)
                continue
            out.append(key15(min(255, sr / sa * fr), min(255, sg / sa * fg), min(255, sb / sa * fb)))
    return out


def build_texture(polys, gltf, warn, info):
    """Repack the UV islands of every polygon into one 256x240 page of 4 bpp indices with CLUTs.

    Sets poly['uv'] (3 (u, v) byte pairs) and poly['clut'] (x, y). -> (halfword rows 256 x 64, stats)."""
    images = {}

    def image_of(mat):
        if mat not in images:
            ii = mat[0]
            if ii is None:
                f = mat[1]
                images[mat] = (1, 1, bytes((round(f[0] * 255), round(f[1] * 255), round(f[2] * 255), round(f[3] * 255))))
                # the factor is already in the pixel
            else:
                images[mat] = gltf.image(ii)
        return images[mat]

    def factor_of(mat):
        return (1.0, 1.0, 1.0, 1.0) if mat[0] is None else mat[1]

    uf = UnionFind()
    clamped = False
    for pi, p in enumerate(polys):
        w, h, _ = image_of(p["mat"])
        cell = max(1, math.ceil(max(w, h) / 128))
        gw, gh = -(-w // cell), -(-h // cell)
        pts = []
        for _, _, uv in p["corners"]:
            u, v = uv
            if u < -1e-4 or u > 1 + 1e-4 or v < -1e-4 or v > 1 + 1e-4:
                clamped = True
            pts.append((min(max(u, 0.0), 1.0) * w, min(max(v, 0.0), 1.0) * h))
        p["px"] = pts
        cells = tri_cells(pts, cell, gw, gh)
        first = None
        for c in cells:
            key = (p["mat"], c)
            uf.find(key)
            if first is None:
                first = key
            else:
                uf.union(first, key)
        p["first"] = first
    # the island of a polygon is the component its cells end up in, after every polygon has merged its own (the root can move
    # while later polygons are merged, so it is read only now)
    for p in polys:
        p["isl"] = uf.find(p["first"])
    if clamped:
        warn("some UVs are outside 0..1 (tiling is not supported): they were clamped to the border")
    isl_ids, isl_polys = {}, []
    for pi, p in enumerate(polys):
        k = p["isl"]
        if k not in isl_ids:
            isl_ids[k] = len(isl_polys)
            isl_polys.append([])
        isl_polys[isl_ids[k]].append(pi)
    rects = []
    for ids in isl_polys:
        xs = [x for pi in ids for x, _ in polys[pi]["px"]]
        ys = [y for pi in ids for _, y in polys[pi]["px"]]
        x0, y0 = int(math.floor(min(xs) + 1e-6)), int(math.floor(min(ys) + 1e-6))
        x1, y1 = int(math.ceil(max(xs) - 1e-6)), int(math.ceil(max(ys) - 1e-6))
        x1, y1 = max(x1, x0 + 1), max(y1, y0 + 1)
        rects.append((x0, y0, x1, y1))
    f, sizes, place = pack_scaled([(r[2] - r[0], r[3] - r[1]) for r in rects], TEX_W, TEX_H)
    texels = [None] * len(rects)
    used = [set() for _ in rects]
    hists = [dict() for _ in rects]
    has_t = [False] * len(rects)
    for ii, ids in enumerate(isl_polys):
        x0, y0, x1, y1 = rects[ii]
        rw, rh = sizes[ii]
        rx, ry = place[ii]
        mat = polys[ids[0]]["mat"]
        w, h, _ = image_of(mat)
        opaque = mat[2] == "OPAQUE" or mat[0] is None
        texels[ii] = resample(image_of(mat), x0, y0, x1, y1, rw, rh, factor_of(mat), opaque, mat[3])
        for pi in ids:
            p = polys[pi]
            uvs = []
            for px, py in p["px"]:
                u = rx + (px - x0) * rw / (x1 - x0) - 0.5
                v = ry + (py - y0) * rh / (y1 - y0) - 0.5
                uvs.append((min(max(int(round(u)), rx), rx + rw - 1), min(max(int(round(v)), ry), ry + rh - 1)))
            p["uv"] = uvs
            us, vs = [a for a, _ in uvs], [b for _, b in uvs]
            for ty in range(min(vs), max(vs) + 1):
                for tx in range(min(us), max(us) + 1):
                    used[ii].add((tx - rx, ty - ry))
    for ii in range(len(rects)):
        rw, rh = sizes[ii]
        for (tx, ty) in used[ii]:
            k = texels[ii][ty * rw + tx]
            if k < 0:
                has_t[ii] = True
            else:
                hists[ii][k] = hists[ii].get(k, 0) + 1
    # one palette per island, identical ones shared, then merged down to MAX_CLUTS
    groups = []     # dict(isl=[...], hist=..., t=bool, pal=[...])

    def palette(hist, t):
        return median_cut(hist, 15 if t else 16)

    sig = {}
    for ii in range(len(rects)):
        pal = palette(hists[ii], has_t[ii])
        key = (tuple(pal), has_t[ii])
        if key in sig:
            g = groups[sig[key]]
            g["isl"].append(ii)
            for k, c in hists[ii].items():
                g["hist"][k] = g["hist"].get(k, 0) + c
        else:
            sig[key] = len(groups)
            groups.append(dict(isl=[ii], hist=dict(hists[ii]), t=has_t[ii], pal=pal))

    def cost(a, b):
        tot = 0.0
        for x, y in ((a, b), (b, a)):
            for k, c in x["hist"].items():
                tot += c * min(dist15(k, q) for q in y["pal"])
        return tot

    merged = 0
    while len(groups) > MAX_CLUTS:
        groups.sort(key=lambda g: sum(g["hist"].values()))
        a = groups.pop(0)
        bi = min(range(len(groups)), key=lambda i: cost(a, groups[i]))
        b = groups[bi]
        b["isl"] += a["isl"]
        for k, c in a["hist"].items():
            b["hist"][k] = b["hist"].get(k, 0) + c
        b["t"] = b["t"] or a["t"]
        b["pal"] = palette(b["hist"], b["t"])
        merged += 1
    # indices, CLUTs, error
    atlas = bytearray(256 * 256)
    cluts = []
    err_sum = err_n = 0
    max_distinct = 0
    isl_clut = {}
    for gi, g in enumerate(groups):
        pal = list(g["pal"])
        if g["t"]:
            pal = [0] + pal
        cluts.append([(0 if (g["t"] and i == 0) else (v if v else 0x0421 & 0x7FFF)) for i, v in enumerate(pal)])
        cx, cy = 16 * (gi % 4), 240 + gi // 4
        for ii in g["isl"]:
            isl_clut[ii] = (cx, cy)
        cache = {}
        offset = 1 if g["t"] else 0
        for ii in g["isl"]:
            rw, rh = sizes[ii]
            rx, ry = place[ii]
            max_distinct = max(max_distinct, len(hists[ii]))
            for ty in range(rh):
                for tx in range(rw):
                    k = texels[ii][ty * rw + tx]
                    if k < 0 and g["t"]:
                        idx = 0
                    else:
                        if k < 0:
                            k = g["pal"][0]
                        if k not in cache:
                            cache[k] = min(range(len(g["pal"])), key=lambda i: dist15(k, g["pal"][i])) + offset
                        idx = cache[k]
                        if (tx, ty) in used[ii]:
                            err_sum += dist15(k, g["pal"][idx - offset])
                            err_n += 1
                    atlas[(ry + ty) * 256 + rx + tx] = idx
    for pi, p in enumerate(polys):
        p["clut"] = isl_clut[isl_ids[p["isl"]]]
    rows = []
    for y in range(256):
        row = []
        for c in range(64):
            if y >= 240:
                gi = (y - 240) * 4 + c // 16
                e = c % 16
                v = 0
                if gi < len(cluts) and e < len(cluts[gi]):
                    v = cluts[gi][e]
                row.append(v)
            else:
                b = y * 256 + c * 4
                row.append(atlas[b] | (atlas[b + 1] << 4) | (atlas[b + 2] << 8) | (atlas[b + 3] << 12))
        rows.append(row)
    rms = math.sqrt(err_sum / err_n / sum(WEIGHTS)) * 255 / 31 if err_n else 0.0
    stats = dict(islands=len(rects), palettes=len(groups), merged=merged, scale=f, rms=rms, max_colors=max_distinct)
    if f < 0.999:
        info("texture: %d UV islands scaled to %.0f%% to fit %dx%d" % (len(rects), f * 100, TEX_W, TEX_H))
    if merged:
        warn("%d palettes were merged to stay within %d CLUTs" % (merged, MAX_CLUTS))
    if rms > 14:
        warn("texture quantization error is high (rms %.0f of 255): the game keeps 16 colours per UV island" % rms)
    return rows, stats


# ---------------------------------------------------------------- encoders (the game's file formats)

def pad(b, n=8):
    return b + b"\0" * (-len(b) % n)


def encode_bounds(verts):
    if not verts:
        return struct.pack("<30h", 9, *([0] * 29))
    xs, ys, zs = [v[0] for v in verts], [v[1] for v in verts], [v[2] for v in verts]
    x0, x1, y0, y1, z0, z1 = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    pts = [((x0 + x1) // 2, (y0 + y1) // 2, (z0 + z1) // 2)]
    for z in (z1, z0):
        for y in (y1, y0):
            for x in (x1, x0):
                pts.append((x, y, z))
    return struct.pack("<30h", 9, 0, 0, *[c for p in pts for c in p][:27])


def encode_cmds(polys):
    """polys: dicts(idx[3], nrm[3], uv[3], clut) in draw order; lit Gouraud textured triangles."""
    if not polys:
        return b"\xff"
    out = bytearray([0xD1, 0x80, 0x91, 0xC1, 0xE0, 0xA0, 0xB0])
    cur = None
    for p in polys:
        if p["clut"] != cur:
            out += bytes([1, 0, 0, 0, p["clut"][0], p["clut"][1], 0])
            cur = p["clut"]
        out.append(0)
        out += bytes(p["idx"]) + bytes(p["nrm"])
        for u, v in p["uv"]:
            out += bytes([u, v])
    out.append(0xFF)
    return bytes(out)


def encode_mesh(verts, norms, polys):
    vb = struct.pack("<3h", len(verts), 0, 0) + b"".join(struct.pack("<3h", *v) for v in verts)
    nb = struct.pack("<3h", len(norms), 0, 0) + b"".join(struct.pack("<3h", *v) for v in norms)
    parts = [pad(vb, 4), pad(encode_bounds(verts), 4), pad(nb, 4), pad(encode_cmds(polys), 4)]
    offs, o = [], 24
    for p in parts:
        offs.append(o)
        o += len(p)
    head = struct.pack("<6I", offs[0], offs[2], offs[3], 0, 0, offs[1])
    return pad(head + b"".join(parts), 8)


def encode_keys(pos, rot, scl):
    lists = [b"".join(struct.pack("<4h", *k) for k in lst) for lst in (pos, rot, scl)]
    o, offs = 16, []
    for lst in lists:
        offs.append(o)
        o += len(lst)
    return pad(struct.pack("<4I", *offs, o) + b"".join(lists), 8)


def rlen_encode(raw):
    out = bytearray(b"RLEN" + struct.pack("<I", len(raw)))
    i, n = 0, len(raw)
    while i < n:
        j = i
        while j < n and raw[j] == raw[i] and j - i < 127:
            j += 1
        if j - i >= 3:
            out += bytes([0x80 | (j - i), raw[i]])
            i = j
            continue
        k = i
        while k < n and k - i < 127 and not (k + 2 < n and raw[k] == raw[k + 1] == raw[k + 2]):
            k += 1
        if k == i:
            k = i + 1
        out += bytes([k - i]) + raw[i:k]
        i = k
    out.append(0)
    return bytes(out)


def encode_texture_archive(rows):
    data = b"".join(struct.pack("<64H", *r) for r in rows)
    tim = struct.pack("<IIIHHHH", 0x10, 2, len(data) + 12, 0, 0, 64, 256) + data
    return pad(struct.pack("<2I", 8, 0) + rlen_encode(tim), 8)


def encode_model_entry(tex_entry, bones):
    w = [tex_entry, len(bones)]
    for p, m, k in bones:
        w += [p, m, k]
    return pad(struct.pack("<%dI" % len(w), *w), 8)


def archive(entries):
    """A table of u32 offsets (the first one is the table size) followed by the entries, 8-aligned; None = offset 0."""
    n = len(entries)
    start = len(pad(b"\0" * (4 * n)))
    out, offs = bytearray(), []
    for e in entries:
        offs.append(start + len(out) if e is not None else 0)
        if e is not None:
            out += pad(e)
    return pad(struct.pack("<%dI" % n, *offs)) + bytes(out)


# ---------------------------------------------------------------- animation

class Clip:
    """One animation sampled on the game's timeline."""

    def __init__(self, name, speed, n, t0, dur, base):
        self.name, self.speed, self.n, self.t0, self.dur, self.base = name, speed, n, t0, dur, base
        self.poses = []         # per frame: list per game bone of (pos, rot, scl)

    @property
    def first(self):
        return self.base

    @property
    def last(self):
        return self.base + self.n - 1


def reduce_track(vals, frames, tol):
    """Douglas-Peucker on a track of 3-vectors with linear interpolation. -> sorted kept indices."""
    n = len(vals)
    if n <= 2:
        return list(range(n))
    keep = {0, n - 1}
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        fa, span = frames[a], frames[b] - frames[a]
        worst, wi = -1.0, -1
        va, vb = vals[a], vals[b]
        for i in range(a + 1, b):
            t = (frames[i] - fa) / span
            vi = vals[i]
            err = max(abs(vi[j] - (va[j] + (vb[j] - va[j]) * t)) for j in range(3))
            if err > worst:
                worst, wi = err, i
        if worst > tol:
            keep.add(wi)
            stack += [(a, wi), (wi, b)]
    return sorted(keep)


def clamp16(v, what):
    r = int(round(v))
    if r < -32768 or r > 32767:
        raise ModelImportError("%s = %d does not fit the game's 16 bit values (lower \"scale\"/\"fitHeight\" or move the root)" % (what, r))
    return r


# ---------------------------------------------------------------- the driver

def bake(meta, base_dir):
    """Validate and convert one `model-src-v1` description. -> dict(blob, entries, snippet, info, warnings, stats)."""
    infos, warns = [], []
    seen = set()

    def info(s):
        infos.append(s)

    def warn(s):
        if s not in seen:
            seen.add(s)
            warns.append(s)

    if "from" in meta:
        raise ModelImportError('"from" needs the original model from the disc, which this tool never reads: export the folder '
                               'first ("Export models" in the game) and bake that, or let the game bake this model.json')
    if meta.get("format") != FORMAT:
        raise ModelImportError('"format" must be "%s"' % FORMAT)
    name = meta.get("name")
    if not isinstance(name, str) or not name or not all(c.isalnum() or c in "-_" for c in name):
        raise ModelImportError('"name" is required: letters, digits, - and _')
    if "gltf" not in meta:
        raise ModelImportError('"gltf": path of the .gltf/.glb (relative to this file) is required')
    g = load_gltf(os.path.join(base_dir, meta["gltf"]))
    b = Builder(meta, g, warn, info)
    sc = b.scene
    prims = read_primitives(g, sc, warn)
    b.skeleton(prims)
    b.polygons(prims)
    bones = b.bones

    # polygons per bone, prune empty leaves, merge a pass-through root
    for p in b.raw_polys:
        bones[p["bone"]].polys.append(p)

    def has_content(i):
        return bool(bones[i].polys) or any(has_content(c) for c in bones[i].children)

    keep = [True] * len(bones)
    for i in range(1, len(bones)):
        if not has_content(i):
            keep[i] = False
    if not any(keep[1:]):
        raise ModelImportError("no polygon is left: the model is empty")
    pruned = sum(1 for i in range(1, len(bones)) if not keep[i])
    # children lists of kept bones, with a pass-through top (static, identity, no mesh) merged into the root
    kept_children = {i: [c for c in bones[i].children if keep[c]] for i in range(len(bones))}

    def passthrough(i):
        bi = bones[i]
        n = bi.node
        if n in sc.animated or bi.polys:
            return False
        t, q, s = sc.rest[n]
        if any(abs(x) > 1e-9 for x in t) or any(abs(x - y) > 1e-9 for x, y in zip(q, (0, 0, 0, 1))) \
                or any(abs(x - 1) > 1e-9 for x in s):
            return False
        return True

    changed = True
    while changed:
        changed = False
        for c in list(kept_children[0]):
            if passthrough(c):
                kept_children[0].remove(c)
                kept_children[0] += kept_children[c]
                for cc in kept_children[c]:
                    bones[cc].parent = 0
                keep[c] = False
                pruned += 1
                changed = True
                break
    order = []

    def walk(i):
        order.append(i)
        for c in kept_children[i]:
            walk(c)

    walk(0)
    newidx = {old: new for new, old in enumerate(order)}
    final = []
    for old in order:
        bn = bones[old]
        bn.parent = newidx[bn.parent] if old else 0
        final.append(bn)
    bones = final
    for i, bn in enumerate(bones):
        bn.top = i > 0 and bn.parent == 0
    if pruned:
        info("skeleton: %d bones without polygons were dropped" % pruned)

    # unit scale: scale or fitHeight against the rest pose height
    yaw = math.radians(float(meta.get("yaw", 0.0)))
    ps, pc = math.sin(yaw), math.cos(yaw)
    P = [[pc, 0, ps], [0, 1.0, 0], [-ps, 0, pc]]
    rest_y = []
    for p in prims:
        wr = {}
        for v_i, v in enumerate(p["pos"]):
            if p["skin"] is None:
                m = sc.world_rest(p["node"])
                w = [x + y for x, y in zip(matvec(m[0], v), m[1])]
            else:
                inf = p["infl"][v_i] if p["infl"] else []
                if not inf:
                    continue
                j = b.skin_joints[p["skin"]][max(inf, key=lambda jw: jw[1])[0]]
                if j not in wr:
                    m = sc.world_rest(j)
                    ib = b.skin_ibm[p["skin"]][j]
                    wr[j] = (m, ib)
                m, ib = wr[j]
                loc = Builder.to_local(ib, v)
                w = [x + y for x, y in zip(matvec(m[0], loc), m[1])]
            rest_y.append(w[1])
    height = (max(rest_y) - min(rest_y)) if rest_y else 0.0
    if "scale" in meta and "fitHeight" in meta:
        raise ModelImportError('use "scale" or "fitHeight", not both')
    if "fitHeight" in meta:
        if height <= 0:
            raise ModelImportError("cannot fit the height: the rest pose has no height")
        s = float(meta["fitHeight"]) / height
    else:
        s = float(meta.get("scale", 1.0))
        if "scale" not in meta and (height < 50 or height > 20000):
            warn("the rest pose is %.4g units high: set \"scale\" or \"fitHeight\" (original fighters are 1300-10000 game units)" % height)
    info("model: %d bones, %d polygons, rest height %.4g glTF units, scale %.5g -> %.0f game units high"
         % (len(bones), len(b.raw_polys), height, s, height * s))

    # fold the static nodes above each top bone
    for bn in bones[1:]:
        if not bn.top:
            continue
        m = ([[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]], [0.0, 0.0, 0.0])
        for a in sc.ancestors(bn.node):
            m = compose(m, sc.local_matrix(a))
        m = compose((P, [0.0, 0.0, 0.0]), m)
        cols = [[m[0][i][j] for i in range(3)] for j in range(3)]
        lens = [vlen(c) for c in cols]
        r = fsum(lens) / 3.0
        ok = r > 1e-9 and all(abs(x - r) < 1e-3 * r for x in lens)
        if ok:
            for a_ in range(3):
                for b_ in range(a_ + 1, 3):
                    if abs(fsum(cols[a_][k] * cols[b_][k] for k in range(3))) > 1e-3 * r * r:
                        ok = False
        if not ok:
            raise ModelImportError("the nodes above the skeleton have a non-uniform scale or a shear: apply the scale in Blender (Ctrl+A) and re-export")
        bn.fold = ([[m[0][i][j] / r for j in range(3)] for i in range(3)], m[1], r)
        bn.k = r
    for i, bn in enumerate(bones):
        if i and not bn.top:
            bn.k = bones[bn.parent].k

    return finish(meta, b, bones, prims, s, height, infos, warns, name, base_dir, info, warn)


def finish(meta, b, bones, prims, s, height, infos, warns, name, base_dir, info, warn):
    """Textures, animations, encoding."""
    sc, g = b.scene, b.g
    # ---- polygons -> quantized local geometry
    polys_all = []
    for bi, bn in enumerate(bones):
        for p in bn.polys:
            p["bone"] = bi
            polys_all.append(p)
    kk = [bn.k for bn in bones]
    for p in polys_all:
        k = kk[p["bone"]] * s
        ps, ns = [], []
        for pos, nrm, _ in p["corners"]:
            lv = conj_vec(pos)
            ps.append(tuple(clamp16(c * k, "vertex") for c in lv))
            nn = conj_vec(nrm)
            ln = vlen(nn)
            if ln < 1e-12:
                nn, ln = (0.0, -1.0, 0.0), 1.0
            ns.append(tuple(int(round(c / ln * 4096)) for c in nn))
        p["pos"], p["nrm"] = ps, ns
    rows, tstats = build_texture(polys_all, g, warn, info)
    # ---- animations
    rest_raw, rest_pose = [], []
    prev_rot = {}

    def local_game(i, t, q, sc_):
        """Game-space (pos floats, rotation matrix, scale floats) of bone i from a glTF local TRS."""
        bn = bones[i]
        if bn.top:
            fr, ft, r = bn.fold
            tt = [ft[j] + r * matvec(fr, t)[j] for j in range(3)]
            rm = matmul(fr, quat_matrix(q))
            pos = conj_vec([c * s for c in tt])
        else:
            rm = quat_matrix(q)
            pos = conj_vec([c * s * bn.k for c in t])
        return pos, me.conj(rm), tuple(x * 4096.0 for x in sc_)

    n0 = len(bones)
    for i in range(n0):
        if i == 0:
            rest_pose.append(None)
            continue
        t, q, sc_ = sc.rest[bones[i].node]
        pos, rm, scl = local_game(i, t, q, sc_)
        rot = raw_angles(rm, None)
        rest_pose.append((pos, rot, scl))
    tol_pos = float(meta.get("tolerance", {}).get("position", max(0.5, 0.001 * height * s)))
    tol_rot = float(meta.get("tolerance", {}).get("rotation", 4.0))
    tol_scl = float(meta.get("tolerance", {}).get("scale", 16.0))

    # animations by name
    anims = {}
    for ai, a in enumerate(g.js.get("animations", [])):
        anims[a.get("name") or "animation_%d" % ai] = a
    specs = parse_motions(meta, anims)
    clips = {}
    next_base = [10]
    channels_cache = {}

    def channels_of(a):
        key = id(a)
        if key in channels_cache:
            return channels_cache[key]
        out, tmin, tmax = {}, None, None
        for ch in a["channels"]:
            tg = ch["target"]
            if "node" not in tg or tg.get("path") not in ("translation", "rotation", "scale"):
                continue
            sm = a["samplers"][ch["sampler"]]
            times = [t[0] for t in g.accessor(sm["input"])]
            vals = [tuple(v) for v in g.accessor(sm["output"])]
            interp = sm.get("interpolation", "LINEAR")
            if not times:
                continue
            out.setdefault(tg["node"], {})[tg["path"]] = (times, vals, interp)
            tmin = times[0] if tmin is None else min(tmin, times[0])
            tmax = times[-1] if tmax is None else max(tmax, times[-1])
        channels_cache[key] = (out, tmin or 0.0, tmax or 0.0)
        return channels_cache[key]

    def pose_of(chs, t):
        res = [None]
        for i in range(1, n0):
            node = bones[i].node
            tr, q, sc_ = sc.rest[node]
            c = chs.get(node)
            if c:
                if "translation" in c:
                    tr = eval_sampler(c["translation"], t, False)
                if "rotation" in c:
                    q = eval_sampler(c["rotation"], t, True)
                if "scale" in c:
                    sc_ = eval_sampler(c["scale"], t, False)
            res.append((tr, q, sc_))
        return res

    def get_clip(spec, loop):
        key = (spec["animation"], spec["speed"], loop)
        if key in clips:
            return clips[key]
        a = anims[spec["animation"]]
        chs, tmin, tmax = channels_of(a)
        dur = tmax - tmin
        n = max(1, int(round(dur * 60.0 / spec["speed"])) + 1)
        poses = []
        prevs = [r[1] if r else None for r in rest_pose]
        for f in range(n):
            t = tmin + (dur * f / (n - 1) if n > 1 else 0.0)
            p = pose_of(chs, t)
            row = [None]
            for i in range(1, n0):
                tr, q, sc_ = p[i]
                pos, rm, scl = local_game(i, tr, q, sc_)
                rot = raw_angles(rm, prevs[i])
                prevs[i] = rot
                row.append((pos, rot, scl))
            poses.append(row)
        if loop and n > 2:
            def flat(row, i):
                return tuple(row[i][0]) + tuple(row[i][1]) + tuple(row[i][2])

            same = all(max(abs(x - y) for x, y in zip(flat(poses[0], i), flat(poses[-1], i))) < 1.0
                       for i in range(1, n0))
            if same:
                poses.pop()
                n -= 1
        clip = Clip(spec["animation"], spec["speed"], n, tmin, dur, next_base[0])
        clip.poses = poses
        next_base[0] += n + 2
        if next_base[0] >= SENTINEL - 10:
            raise ModelImportError("the motions need more than %d frames on the shared timeline: shorten or speed up some (\"speed\")" % SENTINEL)
        clips[key] = clip
        return clip

    # ---- motion slot table
    slot_steps = {}
    for slot in sorted(specs):
        sp = specs[slot]
        clip = get_clip(sp, sp["loop"])
        bi_, bo_ = sp["blendIn"], sp["blendOut"]
        steps, idx = [], 0
        if bi_:
            steps.append((idx, bi_, 0, 0))
            idx += bi_
        steps.append((idx, clip.n, clip.first, clip.last))
        loop_to = idx + min(max(sp["loopStart"], 0), clip.n - 1)
        idx += clip.n
        if sp["loop"]:
            steps.append((idx, 0, -32768, loop_to))
        elif sp["end"] == "hold" or bo_ <= 0:
            steps.append((idx, 0, -1, 0))
        else:
            steps.append((idx, bo_, 0, 0))
            idx += bo_
            steps.append((idx, 0, -1, 0))
        if idx + 1 > MAX_STEPS:
            raise ModelImportError("motion slot %d has %d steps (the game keeps %d): speed it up or shorten it" % (slot, idx, MAX_STEPS))
        if idx > 1500:
            warn("motion slot %d has %d steps (limit %d)" % (slot, idx, MAX_STEPS))
        slot_steps[slot] = steps
    for slot in (0, 1):
        if slot not in slot_steps:
            raise ModelImportError("motion slot %d (idle) is required: map an animation to it" % slot)
    missing = [x for x in MIN_SLOTS if x not in slot_steps]
    if missing:
        warn("motion slots %s are not mapped: fighters need %s (the effect scripts of the donor play them; see \"fill\")"
             % (missing, list(MIN_SLOTS)))
    # ---- keys per bone
    allclips = sorted(clips.values(), key=lambda c: c.base)
    pos_tracks = []
    for i in range(1, n0):
        rp, rr, rs = rest_pose[i]
        tracks = []
        for comp, rest, tol, rnd in ((0, rp, tol_pos, "position"), (1, rr, tol_rot, "rotation"), (2, rs, tol_scl, "scale")):
            keys = [(rest[0], rest[1], rest[2], 0)]
            const = True
            for c in allclips:
                vals = [c.poses[f][i][comp] for f in range(c.n)]
                if any(max(abs(v[j] - rest[j]) for j in range(3)) > tol for v in vals):
                    const = False
                frames = [c.base + f for f in range(c.n)]
                kept = reduce_track(vals, frames, tol)
                for k in kept:
                    keys.append((vals[k][0], vals[k][1], vals[k][2], frames[k]))
            if const:
                keys = [(rest[0], rest[1], rest[2], 0)]
            keys.append((keys[-1][0], keys[-1][1], keys[-1][2], SENTINEL))
            tracks.append([(clamp16(k[0], "%s of bone %d" % (rnd, i)), clamp16(k[1], rnd), clamp16(k[2], rnd), k[3]) for k in keys])
        pos_tracks.append(tracks)
        for sx in tracks[2]:
            if 0 < max(abs(sx[0]), abs(sx[1]), abs(sx[2])) <= me.HIDDEN_SCALE and (sx[0] | sx[1] | sx[2]) > 0 and 0 not in sx[:3]:
                warn("bone %d has a scale of %.4f or less: the game hides such a bone" % (i, me.HIDDEN_SCALE / 4096.0))
                break
    # ---- meshes: chunk per bone, extra child bones
    identity = [[(0, 0, 0, 0), (0, 0, 0, SENTINEL)], [(0, 0, 0, 0), (0, 0, 0, SENTINEL)],
                [(4096, 4096, 4096, 0), (4096, 4096, 4096, SENTINEL)]]
    bone_meshes = [None]
    parents = [0]
    tracks_all = [None]
    for i in range(1, n0):
        parents.append(bones[i].parent)
        tracks_all.append(pos_tracks[i - 1])
        bone_meshes.append(None)
    extra = 0
    chunks_of = {}
    for i in range(1, n0):
        chunks = []
        cur, vs, ns = [], set(), set()
        for p in bones[i].polys:
            nv = vs | set(p["pos"])
            nn = ns | set(p["nrm"])
            if cur and (len(nv) > MAX_VERTS or len(nn) > MAX_VERTS):
                chunks.append(cur)
                cur, nv, nn = [], set(p["pos"]), set(p["nrm"])
            cur.append(p)
            vs, ns = nv, nn
        chunks.append(cur)
        chunks_of[i] = chunks
    mesh_bytes = {}
    for i in range(1, n0):
        for ci, ch in enumerate(chunks_of[i]):
            verts, vmap, norms, nmap, out = [], {}, [], {}, []
            for p in sorted(ch, key=lambda q: q["clut"]):
                idx, nidx = [], []
                for v, n_ in zip(p["pos"], p["nrm"]):
                    if v not in vmap:
                        vmap[v] = len(verts)
                        verts.append(v)
                    if n_ not in nmap:
                        nmap[n_] = len(norms)
                        norms.append(n_)
                    idx.append(vmap[v])
                    nidx.append(nmap[n_])
                out.append(dict(idx=idx, nrm=nidx, uv=p["uv"], clut=p["clut"]))
            mb = encode_mesh(verts, norms, out)
            if ci == 0:
                mesh_bytes[i] = mb
            else:
                parents.append(i)
                tracks_all.append(identity)
                bone_meshes.append(mb)
                extra += 1
    for i in range(1, n0):
        bone_meshes[i] = mesh_bytes[i]
    nbones = len(parents)
    if extra:
        info("%d extra child bones carry the polygons of bones with more than %d vertices or normals" % (extra, MAX_VERTS))
    total_polys = len(polys_all)
    if nbones > MAX_BONES:
        raise ModelImportError("%d bones: the battle layer has 100 callbacks for all fighters on screen (limit %d)" % (nbones, MAX_BONES))
    if nbones > WARN_BONES:
        warn("%d bones: the 100 callbacks of the battle layer are shared by every fighter on screen" % nbones)
    if total_polys > WARN_POLYS:
        warn("%d polygons: the biggest original has 1334" % total_polys)
    # ---- file layout: 0 motions, 1 model, 2 texture, then (mesh, keys) per bone
    entries = [None, None, encode_texture_archive(rows)]
    bone_tuples = []
    for i in range(1, nbones):
        mesh_e = len(entries)
        entries.append(bone_meshes[i])
        keys_e = len(entries)
        entries.append(encode_keys(*tracks_all[i]))
        bone_tuples.append((parents[i], mesh_e, keys_e))
    entries[1] = encode_model_entry(2, bone_tuples)
    steps_bytes = {s_: b"".join(struct.pack("<4h", *st) for st in steps) + struct.pack("<4h", 0x7FFF, 0, 0, 0)
                   for s_, steps in slot_steps.items()}
    entries[0] = archive([steps_bytes.get(k) for k in range(SLOTS)])
    blob = archive(entries)
    # ---- snippet
    fight = {}
    mod = meta.get("mod", "<mod>")
    ref = lambda e: {"file": "%s:models/%s.bin" % (mod, name), "entry": e}   # noqa: E731
    fight["from"] = meta.get("template")
    fight["model"] = ref(1)
    fight["motions"] = ref(0)
    if meta.get("effects"):
        fight["effects"] = meta["effects"]
    else:
        warn('no "effects" donor: the snippet has no effects (the fighter needs "effects": "dw3:digimon/<id>")')
    for k in ("distance", "height"):
        if k in meta.get("info", {}):
            fight[k] = meta["info"][k]
    fight = {k: v for k, v in fight.items() if v is not None}
    snippet = {"format": "digimon-v1", "new": {name: {"base": meta.get("template", "dw3:digimon/4"),
                                                      "name": {"eng": meta.get("displayName", name)},
                                                      "model": fight}}}
    stats = dict(bones=nbones, polys=total_polys, bytes=len(blob), clips=len(clips), slots=len(slot_steps),
                 height=height * s, **tstats)
    return dict(blob=blob, entries=len(entries), snippet=snippet, info=infos, warnings=warns, stats=stats,
                model_entry=1, motions_entry=0)


def parse_motions(meta, anims):
    """The `motions` map -> {slot: spec} with defaults filled; integers copy another slot, "fill" covers the rest."""
    raw = meta.get("motions")
    if raw is None:
        raw = {}
        for nm in anims:
            if nm.startswith("motion_") and nm[7:].isdigit():
                raw[str(int(nm[7:]))] = nm
        if not raw:
            raise ModelImportError('"motions" is required (slot -> animation) or name the animations motion_NN')
    out = {}

    def make(slot, v):
        if isinstance(v, str):
            v = {"animation": v}
        if not isinstance(v, dict) or "animation" not in v:
            raise ModelImportError('motion slot %s: expected an animation name or {"animation": ...}' % slot)
        if v["animation"] not in anims:
            raise ModelImportError("motion slot %s: the glTF has no animation %r (it has: %s)"
                                   % (slot, v["animation"], ", ".join(sorted(anims)) or "none"))
        idle = slot in (0, 1)
        sp = dict(animation=v["animation"], speed=float(v.get("speed", 1.0)), loop=bool(v.get("loop", idle)),
                  end=v.get("end", "idle"), blendIn=int(v.get("blendIn", 0 if idle else BLEND_STEPS)),
                  blendOut=int(v.get("blendOut", BLEND_STEPS)), loopStart=int(v.get("loopStart", 0)))
        if sp["speed"] <= 0:
            raise ModelImportError("motion slot %s: speed must be positive" % slot)
        if sp["end"] not in ("idle", "hold"):
            raise ModelImportError('motion slot %s: "end" is "idle" or "hold"' % slot)
        return sp

    pending = {}
    for k, v in raw.items():
        try:
            slot = int(k)
        except ValueError:
            raise ModelImportError("motion slot %r is not a number" % k)
        if not 0 <= slot < SLOTS:
            raise ModelImportError("motion slot %d is outside 0..%d" % (slot, SLOTS - 1))
        if isinstance(v, int) and not isinstance(v, bool):
            pending[slot] = v
        else:
            out[slot] = make(slot, v)
    for slot, src in pending.items():
        if src not in out:
            raise ModelImportError("motion slot %d copies slot %d, which is not defined" % (slot, src))
        out[slot] = dict(out[src])
    if "fill" in meta:
        for slot in range(SLOTS):
            if slot not in out:
                f = meta["fill"]
                sp = make(slot, f)
                sp["loop"] = False
                out[slot] = sp
    return out


def main(argv):
    args = list(argv)
    check = "--check" in args
    if check:
        args.remove("--check")
    mod = None
    if "--mod" in args:
        i = args.index("--mod")
        mod = args[i + 1]
        del args[i:i + 2]
    out = None
    if "--out" in args:
        i = args.index("--out")
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or (out is None and not check):
        sys.exit(__doc__)
    path = os.path.abspath(args[0])
    try:
        with open(path, "r", encoding="utf-8") as fh:
            meta = json.load(fh)
    except (OSError, ValueError) as e:
        sys.exit("cannot read %s: %s" % (path, e))
    if mod is None and out is not None:
        mod = os.path.basename(os.path.abspath(out))
    if mod:
        meta["mod"] = mod
    try:
        r = bake(meta, os.path.dirname(path))
    except ModelImportError as e:
        sys.exit("error: %s" % e)
    for s in r["info"]:
        print(s)
    for s in r["warnings"]:
        print("warning: %s" % s)
    st = r["stats"]
    print("blob: %d bytes, %d entries, %d bones, %d polygons, %d motions in %d clips; texture: %d islands, %d palettes, rms %.1f"
          % (st["bytes"], r["entries"], st["bones"], st["polys"], st["slots"], st["clips"], st["islands"], st["palettes"], st["rms"]))
    if not check:
        dest = os.path.join(out, "models", meta["name"] + ".bin")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(r["blob"])
        print("wrote %s" % dest)
    print("digimon.json snippet (rename the key, pick \"base\"/\"from\" among the original fighters of the same kind):")
    print(json.dumps(r["snippet"], indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
