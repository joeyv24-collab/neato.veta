#!/usr/bin/env python3
"""Builds Kesh, the game's original four-armed brawler, procedurally.

Kesh is a Thal'veri: a deep-trench people from the sea off Saltreach, tall and
broad-shouldered, teal-skinned with glowing tide-lines, a swept fin crest, and
two pairs of arms (an upper pair for striking, a lower pair for grabbing).

Writes assets/kesh.nmdl (same format as fbx2nmdl.py) and assets/kesh.ntex,
a small palette texture. Bone names match Neato's rig so the code-driven
animation works on her, plus a second (lower) pair of arms.

Usage: make_brawler.py assets/kesh.nmdl assets/kesh.ntex
"""
import sys, struct, math
import numpy as np
from PIL import Image

# ---------------------------------------------------------------- palette
SW = 8                      # swatch size in px, 8x8 swatches in a 64x64 texture
PALETTE = {
    'skin':    (58, 150, 158),
    'skin_d':  (38, 112, 124),
    'glow':    (130, 245, 230),
    'suit':    (40, 38, 62),
    'suit_l':  (62, 58, 92),
    'bronze':  (184, 132, 74),
    'bronze_d':(132, 90, 50),
    'crest':   (22, 92, 118),
    'crest_t': (70, 196, 196),
    'eye':     (255, 206, 80),
    'lip':     (28, 74, 92),
    'sash':    (160, 42, 62),
    'boot':    (30, 30, 38),
}
SLOTS = {}


def palette_texture():
    im = Image.new('RGBA', (64, 64), (255, 0, 255, 255))
    for i, (k, c) in enumerate(PALETTE.items()):
        x, y = (i % 8) * SW, (i // 8) * SW
        im.paste(Image.new('RGBA', (SW, SW), (*c, 255)), (x, y))
        SLOTS[k] = ((x + SW / 2) / 64, (y + SW / 2) / 64)
    return im


# ---------------------------------------------------------------- skeleton
H = 1.95
BONES = []                 # (name, parent, head, tail)


def bone(name, parent, head, tail):
    BONES.append((name, parent, np.array(head, float), np.array(tail, float)))


def build_skeleton():
    bone('DEF-spine', None, (0, 0.98, 0), (0, 1.10, 0))
    bone('DEF-spine.001', 'DEF-spine', (0, 1.10, 0), (0, 1.22, 0.01))
    bone('DEF-spine.002', 'DEF-spine.001', (0, 1.22, 0.01), (0, 1.38, 0.0))
    bone('DEF-spine.003', 'DEF-spine.002', (0, 1.38, 0.0), (0, 1.62, -0.01))
    bone('DEF-spine.004', 'DEF-spine.003', (0, 1.62, -0.01), (0, 1.69, 0.0))
    bone('DEF-spine.005', 'DEF-spine.004', (0, 1.69, 0.0), (0, 1.74, 0.01))
    bone('DEF-spine.006', 'DEF-spine.005', (0, 1.74, 0.01), (0, 1.96, 0.01))
    for s, sx in (('L', 1), ('R', -1)):
        bone(f'DEF-thigh.{s}', 'DEF-spine', (0.11 * sx, 0.96, 0), (0.12 * sx, 0.52, 0.01))
        bone(f'DEF-shin.{s}', f'DEF-thigh.{s}', (0.12 * sx, 0.52, 0.01), (0.12 * sx, 0.09, -0.01))
        bone(f'DEF-foot.{s}', f'DEF-shin.{s}', (0.12 * sx, 0.09, -0.01), (0.12 * sx, 0.02, 0.15))
        # upper (striking) arms in an A-pose, like Neato's rig
        bone(f'DEF-shoulder.{s}', 'DEF-spine.003', (0.05 * sx, 1.56, 0), (0.21 * sx, 1.57, -0.01))
        a0 = np.array((0.23 * sx, 1.56, -0.01))
        d = np.array((math.sin(math.radians(42)) * sx, -math.cos(math.radians(42)), 0))
        bone(f'DEF-upper_arm.{s}', f'DEF-shoulder.{s}', a0, a0 + d * 0.31)
        bone(f'DEF-forearm.{s}', f'DEF-upper_arm.{s}', a0 + d * 0.31, a0 + d * 0.58)
        bone(f'DEF-hand.{s}', f'DEF-forearm.{s}', a0 + d * 0.58, a0 + d * 0.68)
        # lower (grappling) arms, from the bottom of the ribcage
        b0 = np.array((0.16 * sx, 1.33, 0.03))
        e = np.array((math.sin(math.radians(36)) * sx, -math.cos(math.radians(36)), 0.22))
        e /= np.linalg.norm(e)
        bone(f'DEF-upper_arm2.{s}', 'DEF-spine.002', b0, b0 + e * 0.27)
        bone(f'DEF-forearm2.{s}', f'DEF-upper_arm2.{s}', b0 + e * 0.27, b0 + e * 0.51)
        bone(f'DEF-hand2.{s}', f'DEF-forearm2.{s}', b0 + e * 0.51, b0 + e * 0.6)


def bone_matrix(head, tail):
    y = tail - head
    y /= np.linalg.norm(y)
    ref = np.array((0, 0, 1.0)) if abs(y[2]) < 0.9 else np.array((1.0, 0, 0))
    x = np.cross(y, ref); x /= np.linalg.norm(x)
    z = np.cross(x, y)
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2], M[:3, 3] = x, y, z, head
    return M


# ---------------------------------------------------------------- mesh
VERTS, TRIS = [], []       # vert: (pos, nrm, uv, {bone: w})


def vert(p, n, col, w):
    VERTS.append((np.array(p, float), np.array(n, float), SLOTS[col], w))
    return len(VERTS) - 1


def quad(a, b, c, d):
    TRIS.append((a, b, c)); TRIS.append((a, c, d))


def ring_frame(axis):
    axis = axis / np.linalg.norm(axis)
    ref = np.array((0, 0, 1.0)) if abs(axis[2]) < 0.9 else np.array((1.0, 0, 0))
    u = np.cross(axis, ref); u /= np.linalg.norm(u)
    v = np.cross(u, axis)
    return u, v


DETAIL = 1.45     # global detail multiplier: ~7,000 triangles


def tube(p0, p1, r0, r1, col, b0, b1=None, sides=6, scale=(1, 1), caps=True, col1=None,
         segs=1, profile=None, blend=0.25, front=0.0):
    """tapered, segmented tube from p0 to p1.  profile(t) multiplies the radius
    (muscle shape), 'front' pushes the front of the tube out (calves, biceps).
    Vertices near p1 blend towards bone b1 for smooth bending."""
    p0, p1 = np.array(p0, float), np.array(p1, float)
    sides = max(4, int(round(sides * DETAIL)))
    segs = max(1, int(round(segs * DETAIL)))
    axis = p1 - p0
    u, v = ring_frame(axis)
    rings = []
    for i in range(segs + 1):
        t = i / segs
        p = p0 + axis * t
        r = (r0 + (r1 - r0) * t) * (profile(t) if profile else 1.0)
        b1_ = b1 or b0
        if b1 and t > 1 - blend:
            k = (t - (1 - blend)) / blend * 0.5
            w = {b0: 1 - k, b1_: k}
        elif t < blend and i > 0 and False:
            w = {b0: 1.0}
        else:
            w = {b0: 1.0}
        c = (col1 or col) if t > 0.999 and col1 else col
        ids = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            f = 1.0 + front * max(0.0, math.cos(a)) * math.sin(math.pi * t)
            off = u * math.cos(a) * r * scale[0] * f + v * math.sin(a) * r * scale[1]
            ids.append(vert(p + off, off / (np.linalg.norm(off) + 1e-9), c, w))
        rings.append(ids)
    for r0_, r1_ in zip(rings, rings[1:]):
        for k in range(sides):
            k2 = (k + 1) % sides
            quad(r0_[k], r0_[k2], r1_[k2], r1_[k])
    if caps:
        an = axis / np.linalg.norm(axis)
        for end, ring, sgn in ((0, rings[0], -1), (1, rings[-1], 1)):
            c = vert(p0 if end == 0 else p1, an * sgn, col,
                     {b0 if end == 0 else (b1 or b0): 1.0})
            for k in range(sides):
                k2 = (k + 1) % sides
                if sgn > 0: TRIS.append((ring[k], ring[k2], c))
                else: TRIS.append((ring[k2], ring[k], c))


def loft(sections, sides=10, sub=3):
    """torso: list of (y, half_w, half_d, zoff, colour, weights); 'sub' extra
    rings are interpolated (smoothstep) between each pair of sections"""
    sides = int(round(sides * DETAIL))
    dense = []
    for (a, b) in zip(sections, sections[1:]):
        for i in range(sub):
            t = i / sub
            ts = t * t * (3 - 2 * t)
            mix = lambda x, y: x + (y - x) * ts
            w = {}
            for k_, v_ in a[5].items(): w[k_] = w.get(k_, 0) + v_ * (1 - t)
            for k_, v_ in b[5].items(): w[k_] = w.get(k_, 0) + v_ * t
            dense.append((mix(a[0], b[0]), mix(a[1], b[1]), mix(a[2], b[2]), mix(a[3], b[3]),
                          a[4] if t < 0.5 else b[4], w))
    dense.append(sections[-1])
    rings = []
    for y, hw, hd, zo, col, w in dense:
        ids = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            # slightly squarer than an ellipse: reads as muscle rather than a tube
            sa, ca = math.sin(a), math.cos(a)
            x = math.copysign(abs(sa) ** 0.8, sa) * hw
            z = math.copysign(abs(ca) ** 0.85, ca) * hd
            n = np.array((math.sin(a) / hw, 0, math.cos(a) / hd)); n /= np.linalg.norm(n)
            ids.append(vert((x, y, z + zo), n, col, w))
        rings.append(ids)
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(sides):
            k2 = (k + 1) % sides
            quad(r0[k], r1[k], r1[k2], r0[k2])
    return rings


def ellipsoid(c, r, col, b, lat=6, lon=8, colfn=None, shape=None):
    c = np.array(c, float); r = np.array(r, float)
    lat = int(round(lat * DETAIL)); lon = int(round(lon * DETAIL))
    grid = []
    for i in range(lat + 1):
        th = math.pi * i / lat
        row = []
        for j in range(lon):
            ph = 2 * math.pi * j / lon
            n = np.array((math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph)))
            cc = colfn(n) if colfn else col
            rr = r * (shape(n) if shape else 1.0)
            row.append(vert(c + n * rr, n / r / np.linalg.norm(n / r), cc, {b: 1.0}))
        grid.append(row)
    for i in range(lat):
        for j in range(lon):
            j2 = (j + 1) % lon
            quad(grid[i][j], grid[i + 1][j], grid[i + 1][j2], grid[i][j2])


def box(c, h, col, b, rot=None):
    c = np.array(c, float); h = np.array(h, float)
    R = rot if rot is not None else np.eye(3)
    for n in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
        n = np.array(n, float)
        u = np.array((0, 1.0, 0)) if abs(n[1]) < 0.5 else np.array((1.0, 0, 0))
        v = np.cross(n, u)
        ids = []
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = c + R @ ((n + u * su + v * sv) * h)
            ids.append(vert(p, R @ n, col, {b: 1.0}))
        quad(*ids)


def build_mesh():
    J = {name: (head, tail) for name, _, head, tail in BONES}
    sp = lambda a, b_: {a: 0.5, b_: 0.5}
    # torso: athletic, broad shoulders, narrow waist; suit on the trunk, sash at the hips
    loft([
        (0.86, 0.13, 0.10, 0.0, 'suit', {'DEF-spine': 1.0}),
        (0.92, 0.165, 0.12, -0.005, 'suit', {'DEF-spine': 1.0}),
        (0.99, 0.18, 0.125, 0.0, 'sash', {'DEF-spine': 1.0}),               # hips
        (1.06, 0.165, 0.115, 0.005, 'sash', sp('DEF-spine', 'DEF-spine.001')),
        (1.13, 0.135, 0.10, 0.01, 'skin', {'DEF-spine.001': 1.0}),          # bare, toned midriff
        (1.19, 0.125, 0.095, 0.012, 'skin', {'DEF-spine.001': 1.0}),        # waist
        (1.26, 0.14, 0.10, 0.012, 'skin', sp('DEF-spine.001', 'DEF-spine.002')),
        (1.32, 0.16, 0.11, 0.012, 'suit', {'DEF-spine.002': 1.0}),
        (1.38, 0.175, 0.13, 0.025, 'suit', sp('DEF-spine.002', 'DEF-spine.003')),
        (1.43, 0.18, 0.14, 0.032, 'suit', sp('DEF-spine.002', 'DEF-spine.003')),   # chest
        (1.48, 0.185, 0.125, 0.02, 'suit_l', {'DEF-spine.003': 1.0}),
        (1.53, 0.2, 0.115, 0.008, 'skin', {'DEF-spine.003': 1.0}),
        (1.575, 0.21, 0.105, 0.0, 'skin', {'DEF-spine.003': 1.0}),          # shoulders
        (1.605, 0.14, 0.085, -0.005, 'skin', {'DEF-spine.003': 1.0}),
        (1.63, 0.075, 0.065, -0.01, 'skin', sp('DEF-spine.003', 'DEF-spine.004')),
    ], sides=14, sub=3)
    # neck + head
    tube((0, 1.61, -0.01), (0, 1.71, 0.0), 0.058, 0.05, 'skin', 'DEF-spine.004', 'DEF-spine.005', sides=8, segs=3)
    # head: narrower jaw and chin, fuller cranium
    def head_shape(n):
        k = 1.0
        if n[1] < 0: k *= 1.0 - 0.28 * (-n[1]) * (0.5 + 0.5 * abs(n[0]))   # tapering jaw
        if n[2] > 0.6 and n[1] < -0.3: k *= 1.06                          # chin
        if n[1] > 0.2 and n[2] < 0: k *= 1.05                             # back of skull
        return k
    ellipsoid((0, 1.81, 0.015), (0.095, 0.118, 0.108), 'skin', 'DEF-spine.006', lat=8, lon=10,
              colfn=lambda n: 'skin_d' if n[1] < -0.62 else 'skin', shape=head_shape)
    # cheekbone tide-lines
    for sx in (1, -1):
        tube((0.062 * sx, 1.8, 0.075), (0.085 * sx, 1.775, 0.03), 0.006, 0.004, 'glow', 'DEF-spine.006', sides=4, segs=2)
    # eyes: two gold almond shapes; glowing tide-lines under them
    for sx in (1, -1):
        c = np.array((0.04 * sx, 1.825, 0.112))
        for (dx, dy, w, hh, col) in ((0, 0, 0.026, 0.014, 'eye'), (0.004 * sx, -0.032, 0.02, 0.005, 'glow')):
            p = c + np.array((dx, dy, 0.006 if col == 'eye' else 0.004))
            a = vert(p + (-w, 0, 0), (0, 0, 1), col, {'DEF-spine.006': 1})
            b_ = vert(p + (0, -hh, 0), (0, 0, 1), col, {'DEF-spine.006': 1})
            c2 = vert(p + (w, 0, 0), (0, 0, 1), col, {'DEF-spine.006': 1})
            d = vert(p + (0, hh, 0), (0, 0, 1), col, {'DEF-spine.006': 1})
            quad(a, b_, c2, d)
    # lips
    m0 = vert((-0.022, 1.755, 0.104), (0, 0, 1), 'lip', {'DEF-spine.006': 1})
    m1 = vert((0, 1.748, 0.108), (0, 0, 1), 'lip', {'DEF-spine.006': 1})
    m2 = vert((0.022, 1.755, 0.104), (0, 0, 1), 'lip', {'DEF-spine.006': 1})
    m3 = vert((0, 1.76, 0.108), (0, 0, 1), 'lip', {'DEF-spine.006': 1})
    quad(m0, m1, m2, m3)
    # swept fin crest: long tapering fins sweeping back from the brow
    for i, (x, y0, length, rise, w) in enumerate(((0, 1.925, 0.40, 0.02, 0.06), (0.045, 1.9, 0.34, -0.03, 0.05),
                                                 (-0.045, 1.9, 0.34, -0.03, 0.05), (0.075, 1.85, 0.28, -0.09, 0.04),
                                                 (-0.075, 1.85, 0.28, -0.09, 0.04))):
        p0 = np.array((x, y0, 0.05)); p1 = np.array((x * 1.25, y0 + rise - 0.1, -0.05 - length))
        tube(p0, p1, w, 0.012, 'crest' if i else 'crest_t', 'DEF-spine.006', sides=4, scale=(0.45, 1.0),
             segs=4, profile=lambda t: 1.0 + 0.4 * math.sin(math.pi * t))
    # legs: suit leggings into heavy boots, bronze knee guards
    for s in ('L', 'R'):
        th, sh, ft = J[f'DEF-thigh.{s}'], J[f'DEF-shin.{s}'], J[f'DEF-foot.{s}']
        tube(th[0], th[1], 0.09, 0.058, 'suit', f'DEF-thigh.{s}', f'DEF-shin.{s}', sides=8, segs=4,
             profile=lambda t: 1.0 + 0.12 * math.sin(math.pi * t * 0.9), front=0.12)
        tube(sh[0], sh[0] + (sh[1] - sh[0]) * 0.45, 0.058, 0.052, 'suit', f'DEF-shin.{s}', sides=8, segs=3,
             profile=lambda t: 1.0 + 0.15 * math.sin(math.pi * t))
        tube(sh[0] + (sh[1] - sh[0]) * 0.45, sh[1], 0.064, 0.056, 'boot', f'DEF-shin.{s}', f'DEF-foot.{s}', sides=8, segs=3)
        box(sh[0] + np.array((0, -0.02, 0.05)), (0.045, 0.05, 0.02), 'bronze', f'DEF-shin.{s}')
        box((ft[0] + ft[1]) / 2 + np.array((0, -0.01, 0.0)), (0.055, 0.04, 0.11), 'boot', f'DEF-foot.{s}')
        tube(th[0] + (th[1] - th[0]) * 0.55, th[0] + (th[1] - th[0]) * 0.6, 0.083, 0.082, 'glow', f'DEF-thigh.{s}', sides=8, caps=False)
    # arms: upper pair (bronze pauldrons, bracers), lower pair (lighter, wraps)
    for s in ('L', 'R'):
        sh = J[f'DEF-shoulder.{s}']
        tube(sh[0], sh[1], 0.065, 0.07, 'skin', f'DEF-shoulder.{s}', sides=8, segs=2)
        for tag, r_up, r_fore, pauldron in (('', 0.07, 0.06, True), ('2', 0.058, 0.05, False)):
            ua, fa, hd = J[f'DEF-upper_arm{tag}.{s}'], J[f'DEF-forearm{tag}.{s}'], J[f'DEF-hand{tag}.{s}']
            tube(ua[0], ua[1], r_up, r_up * 0.85, 'skin', f'DEF-upper_arm{tag}.{s}', f'DEF-forearm{tag}.{s}', sides=8, segs=4,
                 profile=lambda t: 1.0 + 0.16 * math.sin(math.pi * t), front=0.15)
            tube(fa[0], fa[1], r_fore, r_fore * 0.8, 'skin', f'DEF-forearm{tag}.{s}', f'DEF-hand{tag}.{s}', sides=8, segs=4,
                 profile=lambda t: 1.0 + 0.14 * math.sin(math.pi * t * 0.7))
            mid = fa[0] + (fa[1] - fa[0]) * 0.55
            tube(mid, fa[1], r_fore * 1.12, r_fore * 0.95, 'bronze' if pauldron else 'suit_l', f'DEF-forearm{tag}.{s}', sides=6)
            tube(fa[0] + (fa[1] - fa[0]) * 0.3, fa[0] + (fa[1] - fa[0]) * 0.36, r_fore * 1.05, r_fore * 1.05, 'glow',
                 f'DEF-forearm{tag}.{s}', sides=6, caps=False)
            # fist
            hc = (hd[0] + hd[1]) / 2
            hs = 1.0 if pauldron else 0.85
            hdir = (hd[1] - hd[0]) / np.linalg.norm(hd[1] - hd[0])
            ellipsoid(hc, np.array((0.048, 0.05, 0.042)) * hs, 'skin_d', f'DEF-hand{tag}.{s}', lat=4, lon=6)
            for f in range(4):                                      # curled fingers
                off = (f - 1.5) * 0.024 * hs
                fp = hc + hdir * 0.035 * hs + np.array((0, 0, off))
                tube(fp, fp + hdir * 0.03 * hs + np.array((0, 0, off * 0.1)), 0.012 * hs, 0.011 * hs, 'skin_d',
                     f'DEF-hand{tag}.{s}', sides=5, segs=2)
            tp = hc + np.array((0, 0, 0.04 * hs))
            tube(tp, tp + hdir * 0.03 * hs + np.array((0, 0, 0.012)), 0.013 * hs, 0.011 * hs, 'skin_d',
                 f'DEF-hand{tag}.{s}', sides=5, segs=2)
            if pauldron:
                box(ua[0] + np.array((0, 0.03, 0)), (0.075, 0.035, 0.07), 'bronze', f'DEF-upper_arm{tag}.{s}')
                box(ua[0] + np.array((0, -0.01, 0)), (0.07, 0.02, 0.065), 'bronze_d', f'DEF-upper_arm{tag}.{s}')
    # bronze collar and belt buckle
    box((0, 1.47, 0.135), (0.07, 0.04, 0.015), 'bronze', 'DEF-spine.003')
    box((0, 1.02, 0.125), (0.05, 0.035, 0.015), 'bronze', 'DEF-spine')
    # sash tails at the back
    tube((0.05, 1.0, -0.12), (0.09, 0.62, -0.17), 0.03, 0.02, 'sash', 'DEF-spine', sides=4, scale=(1.6, 0.4), segs=4)
    tube((-0.03, 1.0, -0.125), (-0.06, 0.7, -0.16), 0.025, 0.016, 'sash', 'DEF-spine', sides=4, scale=(1.6, 0.4), segs=3)


def write(out_mdl, out_tex):
    tex = palette_texture()
    build_skeleton()
    build_mesh()
    names = [b[0] for b in BONES]
    bidx = {n: i for i, n in enumerate(names)}
    with open(out_tex, 'wb') as f:
        f.write(b'NTEX' + struct.pack('<II', *tex.size) + tex.tobytes())
    ob = bytearray()
    for name, parent, head, tail in BONES:
        B = bone_matrix(head, tail)
        nm = name.encode()[:31]
        ob += nm + b'\x00' * (32 - len(nm))
        ob += struct.pack('<i', bidx[parent] if parent else -1)
        ob += struct.pack('<16f', *B.T.flatten())
        ob += struct.pack('<16f', *np.linalg.inv(B).T.flatten())
    ov = bytearray()
    for p, n, uv, w in VERTS:
        items = sorted(w.items(), key=lambda kv: -kv[1])[:4]
        bi = [bidx[b] for b, _ in items] + [0] * (4 - len(items))
        tot = sum(x for _, x in items)
        bw = [int(round(x / tot * 255)) for _, x in items] + [0] * (4 - len(items))
        bw[0] += 255 - sum(bw)
        ov += struct.pack('<8f4B4B', *p, *n, uv[0], uv[1], *bi, *bw)
    idx = [i for t in TRIS for i in t]
    with open(out_mdl, 'wb') as f:
        f.write(b'NMDL' + struct.pack('<IIIIIf', 1, len(VERTS), len(idx), 1, len(BONES), H))
        f.write(ob); f.write(ov)
        f.write(struct.pack('<III', 0, len(idx), 0))
        f.write(struct.pack('<%dH' % len(idx), *idx))
    print(f'{out_mdl}: {len(VERTS)} verts, {len(TRIS)} tris, {len(BONES)} bones')


if __name__ == '__main__':
    write(sys.argv[1] if len(sys.argv) > 1 else 'assets/kesh.nmdl',
          sys.argv[2] if len(sys.argv) > 2 else 'assets/kesh.ntex')
