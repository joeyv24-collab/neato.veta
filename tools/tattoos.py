#!/usr/bin/env python3
"""Paints Neato's tattoos onto her body texture (upscaled 2x, nearest) and writes assets/neato_body.ntex.

The body texture is stacked twice (512x1024 after the 2x upscale): the top half is used by her left
side and centre, the bottom half by her right side (see fbx2nmdl.py), so a
tattoo only shows on the side it is painted on.  Each tattoo is a decal
projected onto the model's surface, and ink only lands on bare skin.

Usage: tattoos.py assets_src/neato_body.png assets/neato.nmdl assets/neato_body.ntex
"""
import sys, struct, json, os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 4                      # supersampling per texel
UP = 2                      # texture upscale so the linework stays readable
INK = np.array([32, 28, 38], np.float32)

# Each tattoo: decal image, centre on the body (game units, character faces +Z,
# her right side is -X), outward direction it is projected along, and width.
TATTOOS = [
    dict(name='bat (right hip)', image='tattoo_bat.json',
         centre=(-0.083, 1.088, 0.09), out=(-0.6, 0.0, 0.8), width=0.085),
    dict(name='hooded skull (left collarbone)', image='tattoo_skull.png',
         centre=(0.031, 1.397, 0.042), out=(0.12, 0.55, 0.83), width=0.032),
]


def load_nmdl(path):
    d = open(path, 'rb').read()
    nv, ni, ns, nb = struct.unpack_from('<IIII', d, 8)
    o = 28 + nb * 164
    V = np.frombuffer(d, np.dtype([('p', '<3f4'), ('n', '<3f4'), ('uv', '<2f4'),
                                   ('b', '4u1'), ('w', '4u1')]), nv, o)
    o += nv * 40
    subs = [struct.unpack_from('<III', d, o + 12 * i) for i in range(ns)]
    o += 12 * ns
    return V, subs, np.frombuffer(d, '<u2', ni, o)


def surface_maps(V, subs, I, W, H):
    """World position and normal for every (supersampled) texel of the body."""
    pos = np.full((H, W, 3), np.nan, np.float32)
    nrm = np.zeros((H, W, 3), np.float32)
    for first, cnt, slot in subs:
        if slot != 0:
            continue
        for t in range(first, first + cnt, 3):
            a, b, c = I[t:t + 3]
            uv = np.array([V['uv'][a], V['uv'][b], V['uv'][c]]) * (W, H)
            P = np.array([V['p'][a], V['p'][b], V['p'][c]])
            N = np.array([V['n'][a], V['n'][b], V['n'][c]])
            x0, y0 = np.maximum(np.floor(uv.min(0)).astype(int) - 1, 0)
            x1, y1 = np.minimum(np.ceil(uv.max(0)).astype(int) + 1, (W - 1, H - 1))
            if x1 < x0 or y1 < y0:
                continue
            (ax, ay), (bx, by), (cx, cy) = uv
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            gy, gx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
            l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
            l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
            l3 = 1 - l1 - l2
            m = (l1 >= -1e-3) & (l2 >= -1e-3) & (l3 >= -1e-3)
            L = np.stack([l1, l2, l3], -1)[m]
            ys, xs = (gy[m] - 0.5).astype(int), (gx[m] - 0.5).astype(int)
            pos[ys, xs] = L @ P
            nrm[ys, xs] = L @ N
    return pos, nrm


def decal_mask(spec, size=1024):
    """Decal coverage image: a .png mask (white = ink) or a .json polygon outline."""
    if spec.endswith('.png'):
        im = Image.open(os.path.join(HERE, '..', 'assets_src', spec)).convert('L')
        return np.asarray(im, np.float32) / 255.0
    d = json.load(open(os.path.join(HERE, '..', 'assets_src', spec)))
    pts = np.array(d['outline'], float)
    lo, hi = pts.min(0), pts.max(0)
    pad = d.get('line', 0.06) * (hi - lo)[0]
    sc = (size - 2 * pad) / (hi - lo)[0]
    h = int((hi - lo)[1] * sc + 2 * pad)
    img = Image.new('L', (size, h), 0)
    P = [tuple((p - lo) * sc + pad) for p in pts]
    lw = int(d.get('line', 0.06) * size)
    dr = ImageDraw.Draw(img)
    dr.line(P + [P[0], P[1]], fill=255, width=lw, joint='curve')
    for p in P:
        dr.ellipse([p[0] - lw / 2, p[1] - lw / 2, p[0] + lw / 2, p[1] + lw / 2], fill=255)
    return np.asarray(img, np.float32) / 255.0


def main(base_png, nmdl, out):
    src = Image.open(base_png).convert('RGBA')
    src = src.resize((src.width * UP, src.height * UP), Image.NEAREST)
    base = np.asarray(src, np.float32)
    tex = np.concatenate([base, base], 0)          # top: left/centre, bottom: right
    H, W = tex.shape[:2]
    V, subs, I = load_nmdl(nmdl)
    pos, nrm = surface_maps(V, subs, I, W * SS, H * SS)

    # bare skin = pale or shaded, unsaturated texels
    r, g, b, a = (tex[..., i] for i in range(4))
    skin = (r > 110) & (np.abs(r - b) < 30) & (np.abs(r - g) < 30) & (a > 0)

    cover = np.zeros((H * SS, W * SS), np.float32)
    for t in TATTOOS:
        m = decal_mask(t['image'])
        C = np.array(t['centre'], np.float32)
        n = np.array(t['out'], np.float32); n /= np.linalg.norm(n)
        R = np.cross(-n, [0, 1, 0]); R /= np.linalg.norm(R)
        U = np.cross(R, -n)
        w = t['width']; h = w * m.shape[0] / m.shape[1]
        d = pos - C
        s, u, dep = d @ R, d @ U, d @ n
        facing = nrm @ n
        ok = (np.abs(s) < w / 2) & (np.abs(u) < h / 2) & (np.abs(dep) < 0.04) & (facing > 0.15)
        ok &= ~np.isnan(pos[..., 0])
        ys, xs = np.nonzero(ok)
        mx = ((s[ys, xs] / w + 0.5) * (m.shape[1] - 1)).astype(int)
        my = ((0.5 - u[ys, xs] / h) * (m.shape[0] - 1)).astype(int)
        cover[ys, xs] = np.maximum(cover[ys, xs], m[my, mx])
        print(f"{t['name']}: {len(ys) // (SS * SS)} texels in range")

    cov = cover.reshape(H, SS, W, SS).mean((1, 3))
    cov = np.clip(cov * 1.25, 0, 1) * skin
    tex[..., :3] = tex[..., :3] * (1 - cov[..., None]) + INK * cov[..., None]
    im = Image.fromarray(tex.astype(np.uint8), 'RGBA')
    with open(out, 'wb') as f:
        f.write(b'NTEX' + struct.pack('<II', W, H) + im.tobytes())
    im.save('/tmp/neato_body_tattooed.png')


if __name__ == '__main__':
    main(*sys.argv[1:4])
