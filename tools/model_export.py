#!/usr/bin/env python3
"""Export Digimon World 3 battle models (EU disc) to glTF 2.0 (skinned, animated).

Usage:
  python3 -I tools/model_export.py "<disc>.bin" <fighter> <outdir> [--repo <root>] [--no-animations]
  python3 -I tools/model_export.py "<disc>.bin" --all <outdir> [--repo <root>] [--no-animations]
  python3 -I tools/model_export.py "<disc>.bin" <fighter> <outdir> --source [--unit-scale F]   (also with --all)

--source writes the layout the port's in-game export uses (<outdir>/<id>/model.gltf + .bin + .png + model.json, at Blender
scale 1/1000, ready for a mod's content/models/<key>/) and is the reference pc/shim/model_export.c is compared with.

<fighter> is the fighter number (e.g. 383 = Kotemon) or dw3:digimon/<id>. --repo is the
checkout that holds src/engine/file/file_table.c (default: the parent of tools/).

For each fighter it writes <outdir>/fighter_<id>.gltf + .bin + .png (one atlas with every
texture/CLUT combination of the mesh). Everything is read from the user's disc at run time and
written outside the repo: the output is derived retail data and must never be committed (ADR-001).

Pure Python 3 standard library (no numpy, no Pillow): the glTF JSON, the .bin and the PNG are
written by hand. The format is documented in docs/formats.md section 4.6; the exporter is
described in docs/modding/README.md.

Notes on the mapping (details in the docs):
  * axes: the game is PS1 (Y down); glTF is (x, y, z) -> (-x, -y, z), a proper rotation.
  * skeleton: one node per bone, rest pose = first frame of the idle motion (slot 0). Every vertex
    is weighted 1.0 to its bone. A bone whose scale is <= 48 is hidden by the game (a weapon, a
    limb); in glTF its scale is 0, and the inverse bind matrices use scale 1 for it so they stay
    invertible.
  * animations: one per non-empty motion slot (motion_00 ...), sampled at integer game frames,
    60 per second (one key frame per vsync). Rotations are rebuilt per sample as Rz*Ry*Rx from the
    interpolated raw angles and stored as quaternions.
"""
import bisect
import json
import math
import os
import struct
import sys
import zlib

FILE_FIGHTERS = 0x1CC          # FILE_FIGHTERS in EU
HIDDEN_SCALE = 48              # (sx | sy | sz) <= 48 -> the bone is not drawn
FPS = 60.0
UNIT_SCALE = 1 / 1000          # Blender size: 1000 game units are 1 m (Kotemon, 1886 units, is 1.9 m); model.json says "scale": 1000
C_AXES = (-1.0, -1.0, 1.0)     # PS1 -> glTF axis map, applied as a conjugation


# ---------------------------------------------------------------- small binary helpers

def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def s16s(b, o, n):
    return struct.unpack_from("<%dh" % n, b, o)


def rlen(d, t):
    """Decompress the RLEN block at t (see docs/formats.md section 3)."""
    if d[t:t + 4] != b"RLEN":
        raise ValueError("not an RLEN block")
    src = t + 8
    out = bytearray()
    while True:
        c = d[src]
        if c == 0:
            break
        if c & 0x80:
            out += bytes([d[src + 1]]) * (c & 0x7F)
            src += 2
        else:
            out += d[src + 1:src + 1 + c]
            src += 1 + c
    return bytes(out)


def parse_tim(b):
    """-> dict(mode, clut=(x, y, w, h, bytes) or None, img=(x, y, w, h, bytes))"""
    if u32(b, 0) != 0x10:
        raise ValueError("not a TIM")
    flag = u32(b, 4)
    p = 8
    clut = None
    if flag & 8:
        sz = u32(b, p)
        x, y, w, h = struct.unpack_from("<4H", b, p + 4)
        clut = (x, y, w, h, b[p + 12:p + sz])
        p += sz
    sz = u32(b, p)
    x, y, w, h = struct.unpack_from("<4H", b, p + 4)
    return dict(mode=flag & 7, clut=clut, img=(x, y, w, h, b[p + 12:p + sz]))


def parse_tim_archive(d, off):
    """The TIMs of a TIM archive at off (the table ends at a 0 entry); RLEN-packed ones are unpacked."""
    out = []
    i = 0
    while True:
        e = u32(d, off + 4 * i)
        if e == 0:
            break
        t = off + e
        raw = rlen(d, t) if d[t:t + 4] == b"RLEN" else d[t:]
        out.append(parse_tim(raw))
        i += 1
    return out


def make_vram(tims):
    """The halfword rows the model texture occupies, relative to texPos (64 halfwords per TIM)."""
    width = 64 * max(1, len(tims))
    rows = [[0] * width for _ in range(512)]
    for i, t in enumerate(tims):
        x, y, wd, h, data = t["img"]
        n = min(wd * h, len(data) // 2)
        hw = struct.unpack_from("<%dH" % n, data, 0)
        for r in range(n // wd):
            rows[r][64 * i:64 * i + wd] = hw[r * wd:(r + 1) * wd]
    return rows


def c15(v):
    return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31)


# glTF alpha standing in for each PS1 blend mode (abr = mode + 1): 0 = 0.5B+0.5F, 1 = B+F, 2 = B-F, 3 = B+0.25F.
# Only the half blend is exact; the others are the closest standard alpha blend (see docs/formats.md 4.6).
ABR_ALPHA = {1: 0.5, 2: 0.5, 3: 0.5, 4: 0.25}


def variant_rgba(vram, page, mode, cx, cy, semi_alpha=None):
    """One 256x256 RGBA page (bytes) from the packed texture: 4/8 bit through the CLUT at (cx, cy), or 15 bit.
    The colour value 0 is fully transparent (never drawn). With semi_alpha (a polygon drawn semi-transparent)
    only the texels whose STP bit (bit 15) is set get that alpha; the others stay opaque, as on the GPU."""
    clut = vram[cy] if 0 <= cy < len(vram) else [0] * 64
    width = len(vram[0])

    def px(v):
        if v == 0:
            return b"\x00\x00\x00\x00"
        a = round(semi_alpha * 255) if semi_alpha is not None and v & 0x8000 else 255
        return bytes(c15(v)) + bytes([a])

    pal = None
    if mode == 0:
        pal = [px(clut[cx + i] if cx + i < width else 0) for i in range(16)]
    elif mode == 1:
        pal = [px(clut[cx + i] if cx + i < width else 0) for i in range(256)]
    rows = []
    for v in range(256):
        row = vram[v][page * 64:page * 64 + 256]
        row = row + [0] * (256 - len(row))
        if mode == 0:
            idx = [(hw >> s) & 15 for hw in row[:64] for s in (0, 4, 8, 12)]
            rows.append(b"".join([pal[i] for i in idx]))
        elif mode == 1:
            idx = [(hw >> s) & 255 for hw in row[:128] for s in (0, 8)]
            rows.append(b"".join([pal[i] for i in idx]))
        else:
            rows.append(b"".join([px(hw) for hw in row]))
    return b"".join(rows)


def poly_texels(vram, poly):
    """The 15 bit colour values (CLUT applied) of the texel box a textured polygon covers."""
    page, mode, cx, cy = poly["tex"]
    clut = vram[cy] if 0 <= cy < len(vram) else [0] * 64
    width = len(vram[0])
    us = [u for u, v in poly["uv"]]
    vs = [v for u, v in poly["uv"]]
    out = []
    for v in range(min(vs), max(vs) + 1):
        row = vram[v][page * 64:page * 64 + 256] if v < len(vram) else []
        row = row + [0] * (256 - len(row))
        for u in range(min(us), max(us) + 1):
            if mode == 0:
                i = (row[u >> 2] >> ((u & 3) * 4)) & 15
                out.append(clut[cx + i] if cx + i < width else 0)
            elif mode == 1:
                i = (row[u >> 1] >> ((u & 1) * 8)) & 255
                out.append(clut[cx + i] if cx + i < width else 0)
            else:
                out.append(row[u])
    return out


def poly_blends(vram, poly):
    """Whether a polygon has anything to blend: a semi-transparent flat polygon always; a textured one only
    if some texel in its box has the STP bit (a non-zero colour with bit 15)."""
    if not poly["abr"]:
        return False
    if poly["uv"] is None:
        return True
    return any(v & 0x8000 for v in poly_texels(vram, poly))


def write_png(width, height, rgba):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    stride = width * 4
    raw = b"".join(b"\x00" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------- model parsing

def parse_mesh(d, off):
    """Mesh archive at off (entries 0 vertices, 1 normals, 2 commands) -> dict(verts, norms, polys).

    A poly is dict(idx, nrm, uv, tex, quad, lit, gour, abr, color): idx/nrm index lists, uv a list of
    (u, v) with the texture base applied (or None), tex (page, mode, clutX, clutY) or None, color the
    RGB of the first flat-colour slot (see docs/formats.md 4.6, section 2)."""
    def e(i):
        return off + u32(d, off + 4 * i)

    v0 = e(0)
    n = s16s(d, v0, 1)[0]
    flat = s16s(d, v0 + 6, 3 * n)
    verts = [flat[3 * i:3 * i + 3] for i in range(n)]
    n0 = e(1)
    nn = s16s(d, n0, 1)[0]
    flat = s16s(d, n0 + 6, 3 * nn)
    norms = [flat[3 * i:3 * i + 3] for i in range(nn)]
    c = e(2)
    st = dict(quad=0, tex=0, f10=0, f11=0, lit=0, gour=0, abr=0)
    names = {8: "quad", 9: "tex", 10: "f10", 11: "f11", 12: "lit", 13: "gour", 14: "abr"}
    tex = (0, 0, 0, 0, 0, 0)  # u base, v base, page, clutX, clutY, mode
    color = [(128, 128, 128)] * 4
    polys = []
    while d[c] != 0xFF:
        op = d[c]
        hi, lo = op >> 4, op & 15
        if hi:
            if hi not in names:
                raise ValueError("mesh command %02x at %x" % (op, c))
            st[names[hi]] = lo
            c += 1
        elif lo == 1:
            tex = (d[c + 1], d[c + 3], d[c + 2], d[c + 4], d[c + 5], d[c + 6])
            c += 7
        elif 2 <= lo <= 5:
            color[lo - 2] = (d[c + 1], d[c + 2], d[c + 3])
            c += 4
        elif lo == 0:
            while d[c] == 0:
                c += 1
                k = st["quad"] + 3
                idx = list(d[c:c + k])
                c += k
                nrm = None
                if st["lit"]:
                    nrm = list(d[c:c + k])
                    c += k
                uv = None
                tkey = None
                if st["tex"]:
                    q = d[c:c + 2 * k]
                    c += 2 * k
                    uv = [((q[2 * i] + tex[0]) & 255, (q[2 * i + 1] + tex[1]) & 255) for i in range(k)]
                    tkey = (tex[2], tex[5], tex[3], tex[4])
                if max(idx) >= n or (nrm and max(nrm) >= nn):
                    raise ValueError("index out of range at %x" % c)
                polys.append(dict(idx=idx, nrm=nrm, uv=uv, tex=tkey, quad=st["quad"], lit=st["lit"],
                                  gour=st["gour"], abr=st["abr"], color=color[0]))
        else:
            raise ValueError("mesh command %02x at %x" % (op, c))
    return dict(verts=verts, norms=norms, polys=polys)


def parse_keys(d, ko):
    """Key archive at ko: three lists (position, rotation, scale) of (x, y, z, frame)."""
    a = [u32(d, ko + 4 * i) for i in range(4)]
    out = []
    for k in range(3):
        out.append([s16s(d, p, 4) for p in range(ko + a[k], ko + a[k + 1], 8)])
    return out


def parse_model(d, o):
    """Model entry at o of the fighter file d -> dict(bones, vram). Bone 0 is the mesh-less root."""
    def w(i):
        return u32(d, o + 4 * i)

    def entry(i):
        return u32(d, 4 * i)

    texf = w(0)
    count = w(1) + 1
    bones = [dict(parent=0, mesh=None, pos=[], rot=[], scl=[])]
    for i in range(1, count):
        p, mf, kf = w(2 + 3 * (i - 1)), w(3 + 3 * (i - 1)), w(4 + 3 * (i - 1))
        pos, rot, scl = parse_keys(d, entry(kf))
        bones.append(dict(parent=p, mesh=parse_mesh(d, entry(mf)), pos=pos, rot=rot, scl=scl))
    tims = parse_tim_archive(d, entry(texf)) if texf else []
    return dict(bones=bones, vram=make_vram(tims), tims=len(tims))


def parse_motions(d, o):
    """Motion archive at o -> list of slots; each a list of (index, count, frame, endFrame) steps or None."""
    n = u32(d, o) // 4
    out = []
    for m in range(n):
        eo = u32(d, o + 4 * m)
        if eo == 0:
            out.append(None)
            continue
        p = o + eo
        steps = []
        while True:
            s = s16s(d, p, 4)
            steps.append(s)
            p += 8
            if s[0] == 0x7FFF:
                break
        out.append(steps)
    return out


# ---------------------------------------------------------------- pose sampling (FIGHTSTG_poseBone / blendBone)

def sample_keys(keys, frame):
    """poseBone: the key at FRAME or linear interpolation between its neighbours (ints, like the GTE)."""
    frames = [k[3] for k in keys]
    i = bisect.bisect_left(frames, frame)
    if i >= len(keys):
        return tuple(keys[-1][:3])
    if frames[i] == frame or i == 0:
        return tuple(keys[i][:3])
    prev, cur = keys[i - 1], keys[i]
    t = ((frame - prev[3]) << 12) // (cur[3] - prev[3])
    return tuple(prev[j] + (((cur[j] - prev[j]) * t) >> 12) for j in range(3))


def first_key_at(keys, frame):
    """blendBone: the first key whose frame is >= FRAME (no interpolation)."""
    frames = [k[3] for k in keys]
    i = min(bisect.bisect_left(frames, frame), len(keys) - 1)
    return tuple(keys[i][:3])


def mix(prev, cur, w):
    return tuple(prev[j] + (((cur[j] - prev[j]) * w) >> 12) for j in range(3))


def pose_at(model, frame):
    """[(pos, rot, scale)] per bone (bone 0 gets identity-ish zeros) at an integer game frame."""
    out = [((0, 0, 0), (0, 0, 0), (4096, 4096, 4096))]
    for b in model["bones"][1:]:
        out.append((sample_keys(b["pos"], frame), sample_keys(b["rot"], frame), sample_keys(b["scl"], frame)))
    return out


def blend_pose(model, prev, target, w):
    """blendBone for every bone: PREV (a pose list) towards the keys at/after TARGET with weight W/4096."""
    out = [prev[0]]
    for i, b in enumerate(model["bones"][1:], 1):
        pp, pr, ps = prev[i]
        cp, cr, cs = first_key_at(b["pos"], target), first_key_at(b["rot"], target), first_key_at(b["scl"], target)
        if 0 in ps or 0 in cs:
            s = cs
        else:
            s = mix(ps, cs, w)
        out.append((mix(pp, cp, w), mix(pr, cr, w), s))
    return out


def expand_motion(steps, idle_frame):
    """Mirror FIGHTSTG_setMotion: the per-vsync samples of a motion slot.

    -> (samples, meta). A sample is ('f', frame) or ('b', source frame or None, target frame, weight).
    meta: loop_start (sample index the motion repeats from, or None) and ends (True when it finishes
    and the game goes back to idle)."""
    samples = []
    meta = dict(loop_start=None, ends=False)
    i = 0
    while steps[i][0] != 0x7FFF:
        index, count, frame, end = steps[i]
        if count == 0:
            if frame == -32768:          # 0x8000: repeat from sample 'end'
                meta["loop_start"] = end
            elif frame == -1:            # 0xFFFF: the motion is over
                meta["ends"] = True
            else:
                samples.append(("f", frame))
            break
        if end == 0:
            to = steps[i + 1][2] if i + 1 < len(steps) else 0
            if to == -1:
                to = idle_frame
                meta["ends"] = True
            src = steps[i - 1][3] if index != 0 else None
            for j in range(count):
                samples.append(("b", src, to, ((j + 1) << 12) // (count + 1)))
        else:
            for j in range(count):
                samples.append(("f", frame + j))
        i += 1
    return samples, meta


# ---------------------------------------------------------------- matrix helpers (3x3 as row lists)

def dsum(values):
    """Left-to-right float sum. Python 3.12+ sum() compensates (Neumaier); the C exporter (pc/shim/model_export.c) adds
    plainly, and both have to give the same bits."""
    t = 0.0
    for x in values:
        t += x
    return t


def matmul(a, b):
    return [[dsum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def matvec(a, v):
    return [dsum(a[i][k] * v[k] for k in range(3)) for i in range(3)]


def transpose(a):
    return [[a[j][i] for j in range(3)] for i in range(3)]


def det3(a):
    return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
            - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
            + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))


def inv3(a):
    d = det3(a)
    if abs(d) < 1e-12:
        raise ValueError("singular matrix")
    c = [[0.0] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            r = [x for x in range(3) if x != i]
            q = [x for x in range(3) if x != j]
            m = a[r[0]][q[0]] * a[r[1]][q[1]] - a[r[0]][q[1]] * a[r[1]][q[0]]
            c[j][i] = ((-1) ** (i + j)) * m / d
    return c


def rotzyx(r):
    """RotMatrixZYX_gte: Rz * Ry * Rx from raw angles (4096 = 360 degrees)."""
    a = [2 * math.pi * v / 4096.0 for v in r]
    sx, sy, sz = [math.sin(v) for v in a]
    cx, cy, cz = [math.cos(v) for v in a]
    return [[cz * cy, cz * sy * sx - cx * sz, cz * sy * cx + sx * sz],
            [sz * cy, sz * sy * sx + cx * cz, sz * sy * cx - sx * cz],
            [-sy, cy * sx, cy * cx]]


def conj(m):
    """C * M * C with C = diag(-1, -1, 1)."""
    return [[C_AXES[i] * C_AXES[j] * m[i][j] for j in range(3)] for i in range(3)]


def quat_from_matrix(m):
    """Unit quaternion (x, y, z, w) of a rotation matrix."""
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
    n = math.sqrt(dsum(x * x for x in q))
    return tuple(x / n for x in q)


def is_hidden(scale):
    return (scale[0] | scale[1] | scale[2]) <= HIDDEN_SCALE


def gl_scale(scale):
    return (0.0, 0.0, 0.0) if is_hidden(scale) else tuple(s / 4096.0 for s in scale)


def bind_scale(scale):
    """Scale for the bind pose: a hidden bone (or an axis that collapses) counts as 1.0 so the matrices invert."""
    if is_hidden(scale):
        return (1.0, 1.0, 1.0)
    return tuple(1.0 if abs(s) < 16 else s / 4096.0 for s in scale)


def world_matrices(model, pose, scale_fn):
    """[(R, t)] game-space world matrices (R includes scale) for a pose list."""
    out = []
    for i, b in enumerate(model["bones"]):
        if i == 0:
            out.append(([[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]], [0.0, 0.0, 0.0]))
            continue
        pos, rot, scl = pose[i]
        s = scale_fn(scl)
        r = rotzyx(rot)
        local = [[r[a][c] * s[c] for c in range(3)] for a in range(3)]
        pr, pt = out[b["parent"]]
        wr = matmul(pr, local)
        wt = [x + y for x, y in zip(matvec(pr, pos), pt)]
        out.append((wr, wt))
    return out


# ---------------------------------------------------------------- glTF writer

class GltfBuilder:
    """Collects the .bin, bufferViews and accessors."""

    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, view, ctype, count, atype, offset=0, minmax=None, normalized=False):
        a = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
        if offset:
            a["byteOffset"] = offset
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"], a["max"] = minmax
        self.accessors.append(a)
        return len(self.accessors) - 1

    def add(self, data, ctype, count, atype, target=None, minmax=None):
        return self.accessor(self.view(data, target), ctype, count, atype, minmax=minmax)


F32, U8, U16, U32 = 5126, 5121, 5123, 5125
COMP_SIZE = {F32: 4, U8: 1, U16: 2, U32: 4}
TYPE_N = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def pack_f(vals):
    return struct.pack("<%df" % len(vals), *vals)


def minmax_vec(rows, n):
    return ([min(r[i] for r in rows) for i in range(n)], [max(r[i] for r in rows) for i in range(n)])


def build_mesh_primitives(model, bind_world, vram_variants, unit_scale=1.0):
    """Triangulate every visible-at-bind bone mesh into primitives grouped by material.

    -> dict(material key -> primitive arrays). Material keys: (textured, blend). vram_variants maps
    (texture key, alpha or None) to an atlas page: a semi-transparent polygon with STP texels gets its own
    page where those texels carry the blend alpha. Triangles are wound counter-clockwise for glTF (the
    game's are the other way round, it culls the clockwise-on-screen ones with NCLIP)."""
    prims = {}
    for bi, b in enumerate(model["bones"]):
        if bi == 0 or not b["mesh"]:
            continue
        wr, wt = bind_world[bi]
        try:
            ninv = transpose(inv3(wr))
        except ValueError:
            ninv = wr
        me = b["mesh"]
        gpos = [conj_vec(matvec(wr, v), wt, unit_scale) for v in me["verts"]]
        for p in me["polys"]:
            k = p["idx"]
            blend = poly_blends(model["vram"], p)
            alpha = ABR_ALPHA.get(p["abr"], 0.5) if blend else None
            key = (p["uv"] is not None, blend)
            pr = prims.setdefault(key, dict(pos=[], nrm=[], uv=[], col=[], joints=[], idx=[], seen={}))
            if p["nrm"] is None:
                a = [me["verts"][k[1]][j] - me["verts"][k[0]][j] for j in range(3)]
                c = [me["verts"][k[2]][j] - me["verts"][k[0]][j] for j in range(3)]
                fn = [a[2] * c[1] - a[1] * c[2], a[0] * c[2] - a[2] * c[0], a[1] * c[0] - a[0] * c[1]]  # outward
            tvar = None
            if p["uv"] is not None:
                tvar = vram_variants.setdefault((p["tex"], alpha), len(vram_variants))
            corner = []
            for i in range(len(k)):
                nk = ("n", p["nrm"][i]) if p["nrm"] else ("f", tuple(fn))
                uvk = None
                if p["uv"] is not None:
                    uvk = (tvar,) + tuple(p["uv"][i])
                vk = (bi, k[i], nk, uvk, p["color"], alpha)
                vid = pr["seen"].get(vk)
                if vid is None:
                    vid = len(pr["pos"])
                    pr["seen"][vk] = vid
                    pr["pos"].append(gpos[k[i]])
                    nv = list(me["norms"][p["nrm"][i]]) if p["nrm"] else list(fn)
                    nn = conj_vec(matvec(ninv, nv), (0, 0, 0))
                    ln = math.sqrt(dsum(x * x for x in nn)) or 1.0
                    pr["nrm"].append([x / ln for x in nn])
                    pr["uv"].append(uvk)
                    pr["col"].append([c / 255.0 for c in p["color"]] + ([alpha] if blend else []))
                    pr["joints"].append(bi)
                corner.append(vid)
            pr["idx"] += ([corner[0], corner[2], corner[1]] if len(k) == 3 else
                          [corner[0], corner[2], corner[1], corner[2], corner[3], corner[1]])
    return prims


def conj_vec(v, t, k=1.0):
    return [C_AXES[i] * (v[i] + t[i]) * k for i in range(3)]


def build_gltf(model, motions, name, with_animations=True, idle_frame=None, unit_scale=1.0, uri_base=None):
    """Parsed model + motion slots -> (gltf dict, bin bytes, png bytes). See the module docstring.

    unit_scale multiplies every length (positions, bone translations, the inverse bind matrices' translations):
    UNIT_SCALE puts a fighter at Blender size; the model.json of the source layout then says "scale": 1000 and the
    importer undoes it. The files are named <uri_base or name>.bin / .png."""
    uri_base = uri_base or name
    bones = model["bones"]
    if idle_frame is None:
        idle_frame = 0
        if motions and motions[0]:
            idle_frame = motions[0][0][2]
    rest = pose_at(model, idle_frame)
    rest_world = world_matrices(model, rest, gl_scale)
    bind_world = world_matrices(model, rest, bind_scale)
    # glTF-space bind matrices C W C, inverted for the skin
    gb = GltfBuilder()
    nodes = []
    for i, b in enumerate(bones):
        pos, rot, scl = rest[i]
        n = {"name": "bone_%02d" % i}
        if i:
            n["translation"] = [C_AXES[j] * pos[j] * unit_scale for j in range(3)]
            n["rotation"] = list(quat_from_matrix(conj(rotzyx(rot))))
            n["scale"] = list(gl_scale(scl))
        nodes.append(n)
    for i, b in enumerate(bones):
        if i:
            par = nodes[b["parent"]]
            par.setdefault("children", []).append(i)
    mesh_node = len(nodes)
    ibm = []
    for i in range(len(bones)):
        wr, wt = bind_world[i]
        cr = conj(wr)
        ct = [C_AXES[j] * wt[j] * unit_scale for j in range(3)]
        inv = inv3(cr)
        it = [-x for x in matvec(inv, ct)]
        # column-major 4x4
        for c in range(3):
            ibm += [inv[0][c], inv[1][c], inv[2][c], 0.0]
        ibm += [it[0], it[1], it[2], 1.0]
    ibm_acc = gb.add(pack_f(ibm), F32, len(bones), "MAT4")

    variants = {}
    prims = build_mesh_primitives(model, bind_world, variants, unit_scale)
    nv = max(1, len(variants))
    cols = min(4, nv)
    rows_n = (nv + cols - 1) // cols
    aw, ah = cols * 256, rows_n * 256
    atlas = bytearray(aw * ah * 4)
    for (tk, alpha), vi in variants.items():
        page = variant_rgba(model["vram"], *tk, semi_alpha=alpha)
        ox, oy = (vi % cols) * 256, (vi // cols) * 256
        for y in range(256):
            o = ((oy + y) * aw + ox) * 4
            atlas[o:o + 1024] = page[y * 1024:(y + 1) * 1024]
    png = write_png(aw, ah, bytes(atlas))

    materials = []
    mat_index = {}

    def material(key):
        if key in mat_index:
            return mat_index[key]
        textured, blend = key
        m = {"name": ("tex" if textured else "flat") + ("_blend" if blend else ""),
             "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0,
                                      "baseColorFactor": [1.0, 1.0, 1.0, 1.0]},
             "alphaMode": "BLEND" if blend else ("MASK" if textured else "OPAQUE"), "doubleSided": False}
        if textured:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
        if textured and not blend:
            m["alphaCutoff"] = 0.5
        materials.append(m)
        mat_index[key] = len(materials) - 1
        return mat_index[key]

    primitives = []
    for key in sorted(prims):
        pr = prims[key]
        n = len(pr["pos"])
        if not n or not pr["idx"]:
            continue
        pos = [[float(x) for x in p] for p in pr["pos"]]
        att = {"POSITION": gb.add(pack_f([x for p in pos for x in p]), F32, n, "VEC3", 34962, minmax_vec(pos, 3)),
               "NORMAL": gb.add(pack_f([x for p in pr["nrm"] for x in p]), F32, n, "VEC3", 34962)}
        if key[0]:
            uvs = []
            for u in pr["uv"]:
                var, uu, vv = u
                uvs += [((var % cols) * 256 + uu + 0.5) / aw, ((var // cols) * 256 + vv + 0.5) / ah]
            att["TEXCOORD_0"] = gb.add(pack_f(uvs), F32, n, "VEC2", 34962)
        else:
            nc = 4 if key[1] else 3
            att["COLOR_0"] = gb.add(pack_f([x for c in pr["col"] for x in c]), F32, n, "VEC%d" % nc, 34962)
        att["JOINTS_0"] = gb.add(b"".join(struct.pack("<4B", j, 0, 0, 0) for j in pr["joints"]), U8, n, "VEC4", 34962)
        att["WEIGHTS_0"] = gb.add(b"".join(struct.pack("<4f", 1.0, 0, 0, 0) for _ in range(n)), F32, n, "VEC4", 34962)
        if n < 65536:
            ia = gb.add(struct.pack("<%dH" % len(pr["idx"]), *pr["idx"]), U16, len(pr["idx"]), "SCALAR", 34963)
        else:
            ia = gb.add(struct.pack("<%dI" % len(pr["idx"]), *pr["idx"]), U32, len(pr["idx"]), "SCALAR", 34963)
        primitives.append({"attributes": att, "indices": ia, "material": material(key), "mode": 4})

    nodes.append({"name": "mesh", "mesh": 0, "skin": 0})
    gltf = {
        "asset": {"version": "2.0", "generator": "dw3 model export",
                  "extras": {"bones": len(bones), "textureVariants": len(variants), "restFrame": idle_frame}},
        "scene": 0,
        "scenes": [{"name": name, "nodes": [0, mesh_node]}],
        "nodes": nodes,
        "skins": [{"name": "skeleton", "inverseBindMatrices": ibm_acc, "skeleton": 0,
                   "joints": list(range(len(bones)))}],
        "meshes": [{"name": name, "primitives": primitives}],
        "materials": materials,
        "buffers": [{"byteLength": 0, "uri": uri_base + ".bin"}],
    }
    gltf["images"] = [{"uri": uri_base + ".png", "name": "atlas"}]
    gltf["samplers"] = [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}]
    gltf["textures"] = [{"source": 0, "sampler": 0}]
    if with_animations:
        anims = build_animations(model, motions, gb, idle_frame, unit_scale)
        if anims:
            gltf["animations"] = anims
    gltf["bufferViews"] = gb.views
    gltf["accessors"] = gb.accessors
    gltf["buffers"][0]["byteLength"] = len(gb.bin)
    return gltf, bytes(gb.bin), png


def build_animations(model, motions, gb, idle_frame, unit_scale=1.0):
    """One glTF animation per non-empty motion slot, sampled at game frames (60 per second)."""
    bones = model["bones"]
    rest = pose_at(model, idle_frame)
    pose_cache = {}

    def frame_pose(f):
        if f not in pose_cache:
            pose_cache[f] = pose_at(model, f)
        return pose_cache[f]

    anims = []
    one_time = None
    for slot, steps in enumerate(motions or []):
        if not steps:
            continue
        samples, meta = expand_motion(steps, idle_frame)
        if not samples:
            continue
        poses = []
        for s in samples:
            if s[0] == "f":
                poses.append(frame_pose(s[1]))
            else:
                _, src, to, w = s
                prev = frame_pose(src) if src is not None else rest
                poses.append(blend_pose(model, prev, to, w))
        n = len(poses)
        times = [i / FPS for i in range(n)]
        data = bytearray()
        sections = []   # (accessor spec) appended into one view

        def put(vals, ctype, atype, count, minmax=None):
            while len(data) % 4:
                data.append(0)
            off = len(data)
            data.extend(pack_f(vals))
            sections.append((off, ctype, atype, count, minmax))
            return len(sections) - 1

        t_in = put(times, F32, "SCALAR", n, ([times[0]], [times[-1]]))
        z_in = put([0.0], F32, "SCALAR", 1, ([0.0], [0.0]))
        chans = []
        for bi in range(1, len(bones)):
            trs = []
            qs = []
            ss = []
            prevq = None
            for ps in poses:
                pos, rot, scl = ps[bi]
                trs.append([C_AXES[j] * pos[j] * unit_scale for j in range(3)])
                q = quat_from_matrix(conj(rotzyx(rot)))
                if prevq is not None and dsum(a * b for a, b in zip(q, prevq)) < 0:
                    q = tuple(-x for x in q)
                prevq = q
                qs.append(list(q))
                ss.append(list(gl_scale(scl)))
            for path, series in (("translation", trs), ("rotation", qs), ("scale", ss)):
                const = all(max(abs(a - b) for a, b in zip(series[0], r)) < 1e-6 for r in series)
                width = 4 if path == "rotation" else 3
                atype = "VEC4" if path == "rotation" else "VEC3"
                if const:
                    out = put(series[0], F32, atype, 1)
                    chans.append((bi, path, z_in, out))
                else:
                    out = put([x for r in series for x in r], F32, atype, n)
                    chans.append((bi, path, t_in, out))
        view = gb.view(bytes(data))
        acc_ids = [gb.accessor(view, ct, cnt, at, offset=off, minmax=mm) for off, ct, at, cnt, mm in sections]
        samplers = []
        channels = []
        for bi, path, tin, out in chans:
            samplers.append({"input": acc_ids[tin], "output": acc_ids[out], "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": bi, "path": path}})
        extras = {"slot": slot, "frames": n, "ends": meta["ends"]}
        if meta["loop_start"] is not None:
            extras["loopStartFrame"] = meta["loop_start"]
        anims.append({"name": "motion_%02d" % slot, "samplers": samplers, "channels": channels, "extras": extras})
    return anims


def validate_gltf(gltf, binary):
    """Structural self-check (the standard validator is not always at hand). Raises AssertionError."""
    assert gltf["asset"]["version"] == "2.0"
    assert sum(b["byteLength"] for b in gltf["buffers"]) == len(binary) == gltf["buffers"][0]["byteLength"]
    for v in gltf["bufferViews"]:
        assert v["byteOffset"] + v["byteLength"] <= len(binary), "bufferView outside the buffer"
        assert v["byteOffset"] % 4 == 0
    nviews = len(gltf["bufferViews"])
    for a in gltf["accessors"]:
        assert 0 <= a["bufferView"] < nviews
        v = gltf["bufferViews"][a["bufferView"]]
        size = COMP_SIZE[a["componentType"]] * TYPE_N[a["type"]]
        off = a.get("byteOffset", 0)
        assert off % COMP_SIZE[a["componentType"]] == 0
        assert off + a["count"] * size <= v["byteLength"], "accessor outside its view"
        assert a["count"] > 0
    nacc = len(gltf["accessors"])
    nnodes = len(gltf["nodes"])
    seen = set()
    for i, n in enumerate(gltf["nodes"]):
        for c in n.get("children", []):
            assert 0 <= c < nnodes and c not in seen, "bad or repeated child"
            seen.add(c)
    for s in gltf["scenes"]:
        for n in s["nodes"]:
            assert 0 <= n < nnodes and n not in seen, "scene root is also a child"
    for sk in gltf.get("skins", []):
        assert all(0 <= j < nnodes for j in sk["joints"])
        a = gltf["accessors"][sk["inverseBindMatrices"]]
        assert a["type"] == "MAT4" and a["count"] == len(sk["joints"])
    for n in gltf["nodes"]:
        if "skin" in n:
            assert n["skin"] < len(gltf["skins"])
    nmat = len(gltf.get("materials", []))
    for m in gltf["meshes"]:
        for p in m["primitives"]:
            att = p["attributes"]
            count = gltf["accessors"][att["POSITION"]]["count"]
            for k, ai in att.items():
                assert 0 <= ai < nacc and gltf["accessors"][ai]["count"] == count, k
            ia = gltf["accessors"][p["indices"]]
            assert ia["count"] % 3 == 0
            raw = binary[gltf["bufferViews"][ia["bufferView"]]["byteOffset"] + ia.get("byteOffset", 0):]
            fmt = {U16: "<H", U32: "<I"}[ia["componentType"]]
            sz = struct.calcsize(fmt)
            assert max(struct.unpack_from(fmt, raw, i * sz)[0] for i in range(ia["count"])) < count, "index out of range"
            ja = gltf["accessors"][att["JOINTS_0"]]
            jv = gltf["bufferViews"][ja["bufferView"]]
            jraw = binary[jv["byteOffset"] + ja.get("byteOffset", 0):]
            njoint = len(gltf["skins"][0]["joints"])
            assert all(jraw[i * 4] < njoint for i in range(count)), "joint out of range"
            assert 0 <= p["material"] < nmat
            mn, mx = gltf["accessors"][att["POSITION"]]["min"], gltf["accessors"][att["POSITION"]]["max"]
            assert len(mn) == len(mx) == 3
    for a in gltf.get("animations", []):
        for ch in a["channels"]:
            assert 0 <= ch["target"]["node"] < nnodes
            sm = a["samplers"][ch["sampler"]]
            ia, oa = gltf["accessors"][sm["input"]], gltf["accessors"][sm["output"]]
            assert ia["count"] == oa["count"], "sampler input/output count"
            assert "min" in ia and "max" in ia
            assert oa["type"] == ("VEC4" if ch["target"]["path"] == "rotation" else "VEC3")
    for t in gltf.get("textures", []):
        assert t["source"] < len(gltf["images"])
    return True


# ---------------------------------------------------------------- disc access

class BlobDisc:
    """The files by number: load(n) is the file's bytes. A subclass reads them from the disc."""

    def __init__(self, files=None):
        self.files = dict(files or {})

    def load(self, n):
        return self.files[n]

    def fighter_ids(self):
        d = self.load(FILE_FIGHTERS)
        ents = struct.unpack_from("<4I", d, 0)[1]
        ids = []
        o = ents
        while True:
            i = struct.unpack_from("<h", d, o)[0]
            if i == 0:
                return ids
            ids.append(i)
            o += 4

    def fighter(self, fid):
        d = self.load(FILE_FIGHTERS)
        _, ents, partners, enemies = struct.unpack_from("<4I", d, 0)
        o = ents
        while True:
            i, idx, kind = struct.unpack_from("<hBB", d, o)
            if i == 0:
                raise KeyError("no fighter %d" % fid)
            if i == fid:
                p = enemies + idx * 0x48 if kind >= 0x3A else partners + idx * 0xC4
                model, motions, effects, face = struct.unpack_from("<4I", d, p)
                return dict(id=fid, model=model, motions=motions, effects=effects, enemy=kind >= 0x3A,
                            distance=struct.unpack_from("<h", d, p + 0x10)[0], height=struct.unpack_from("<h", d, p + 0x18)[0])
            o += 4

    def entry(self, ref):
        """(file<<16)|entry -> (file bytes, offset of the entry)"""
        d = self.load(ref >> 16)
        return d, u32(d, (ref & 0xFFFF) * 4)


class Disc(BlobDisc):
    def __init__(self, path, repo):
        super().__init__()
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import file_names as fn
        self.fn = fn
        self.sectors, self.counts = fn.parse_table(os.path.join(repo, "src", "engine", "file", "file_table.c"))
        self.f = open(path, "rb")
        self.cache = {}

    def load(self, n):
        if n not in self.cache:
            if len(self.cache) > 4:
                self.cache = {k: v for k, v in self.cache.items() if k == FILE_FIGHTERS}
            self.cache[n] = self.fn.read_sector(self.f, self.sectors[n], self.counts[n])
        return self.cache[n]


def parse_fighter_spec(s):
    if s.startswith("dw3:digimon/"):
        s = s[len("dw3:digimon/"):]
    return int(s, 0)


def export_fighter(disc, fid, outdir, with_animations=True):
    f = disc.fighter(fid)
    d, o = disc.entry(f["model"])
    model = parse_model(d, o)
    md, mo = disc.entry(f["motions"])
    motions = parse_motions(md, mo)
    name = "fighter_%d" % fid
    gltf, binary, png = build_gltf(model, motions, name, with_animations)
    validate_gltf(gltf, binary)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, name + ".gltf"), "w") as fh:
        json.dump(gltf, fh, separators=(",", ":"))
    with open(os.path.join(outdir, name + ".bin"), "wb") as fh:
        fh.write(binary)
    with open(os.path.join(outdir, name + ".png"), "wb") as fh:
        fh.write(png)
    return dict(bones=len(model["bones"]), anims=len(gltf.get("animations", [])),
                tris=sum(gltf["accessors"][p["indices"]]["count"] // 3
                         for m in gltf["meshes"] for p in m["primitives"]))


def source_json(fid, fighter, motions, unit_scale=UNIT_SCALE):
    """The model.json (model-src-v1) next to the exported glTF: what pc/shim/model_export.c writes byte for byte.
    The glTF is at Blender size (UNIT_SCALE), so "scale" is its inverse; every motion slot maps to its motion_NN
    animation (the game's loop and end options are in the animation's extras, not here)."""
    lines = ['{', '  "format": "model-src-v1",', '  "name": "fighter_%d",' % fid, '  "gltf": "model.gltf",',
             '  "scale": %s,' % ("%d" % round(1 / unit_scale) if abs(1 / unit_scale - round(1 / unit_scale)) < 1e-9 else repr(1 / unit_scale)), '  "effects": "dw3:digimon/%d",' % fid,
             '  "template": "dw3:digimon/%d",' % fid, '  "info": {', '    "distance": %d,' % fighter["distance"],
             '    "height": %d' % fighter["height"], '  },', '  "motions": {']
    slots = [i for i, st in enumerate(motions or []) if st and expand_motion(st, 0)[0]]
    for n, i in enumerate(slots):
        lines.append('    "%d": "motion_%02d"%s' % (i, i, "," if n + 1 < len(slots) else ""))
    lines += ['  }', '}', '']
    return "\n".join(lines)


def export_source(disc, fid, outdir, unit_scale=UNIT_SCALE):
    """The in-game export's layout: <outdir>/model.gltf + model.bin + model.png + model.json, ready to drop into a
    mod's content/models/<key>/. Returns the counts like export_fighter."""
    f = disc.fighter(fid)
    d, o = disc.entry(f["model"])
    model = parse_model(d, o)
    md, mo = disc.entry(f["motions"])
    motions = parse_motions(md, mo)
    gltf, binary, png = build_gltf(model, motions, "fighter_%d" % fid, True, None, unit_scale, "model")
    validate_gltf(gltf, binary)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "model.gltf"), "w") as fh:
        json.dump(gltf, fh, separators=(",", ":"))
    with open(os.path.join(outdir, "model.bin"), "wb") as fh:
        fh.write(binary)
    with open(os.path.join(outdir, "model.png"), "wb") as fh:
        fh.write(png)
    with open(os.path.join(outdir, "model.json"), "w") as fh:
        fh.write(source_json(fid, f, motions, unit_scale))
    return dict(bones=len(model["bones"]), anims=len(gltf.get("animations", [])),
                tris=sum(gltf["accessors"][p["indices"]]["count"] // 3
                         for m in gltf["meshes"] for p in m["primitives"]))


def main(argv):
    args = list(argv)
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--repo" in args:
        i = args.index("--repo")
        repo = args[i + 1]
        del args[i:i + 2]
    source = "--source" in args
    if source:
        args.remove("--source")
    unit = None
    if "--unit-scale" in args:
        i = args.index("--unit-scale")
        unit = float(args[i + 1])
        del args[i:i + 2]
    anim = "--no-animations" not in args
    if not anim:
        args.remove("--no-animations")
    every = "--all" in args
    if every:
        args.remove("--all")
    if (every and len(args) != 2) or (not every and len(args) != 3):
        sys.exit(__doc__)
    disc = Disc(args[0], repo)
    if every:
        outdir = args[1]
        ok, bad = 0, []
        tris = 0
        for fid in disc.fighter_ids():
            try:
                if source:
                    r = export_source(disc, fid, os.path.join(outdir, str(fid)), UNIT_SCALE if unit is None else unit)
                else:
                    r = export_fighter(disc, fid, outdir, anim)
                ok += 1
                tris += r["tris"]
            except Exception as e:  # survey mode: report and go on
                bad.append((fid, repr(e)))
        print("exported %d fighters (%d triangles), %d failed" % (ok, tris, len(bad)))
        for fid, e in bad:
            print("  fighter %d: %s" % (fid, e))
        return 1 if bad else 0
    fid = parse_fighter_spec(args[1])
    if source:
        r = export_source(disc, fid, os.path.join(args[2], str(fid)), UNIT_SCALE if unit is None else unit)
    else:
        r = export_fighter(disc, fid, args[2], anim)
    print("fighter %d: %d bones, %d triangles, %d animations -> %s" % (fid, r["bones"], r["tris"], r["anims"], args[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
