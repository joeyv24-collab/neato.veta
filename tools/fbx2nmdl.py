#!/usr/bin/env python3
"""Convert a rigged binary FBX (Blender export) into the game's .nmdl format,
and PNG textures into raw .ntex files.

.nmdl layout (little endian)
  char[4] 'NMDL'  u32 version(1)
  u32 nverts  u32 nindices  u32 nsub  u32 nbones
  f32 height                       # model height in game units (metres)
  bones[nbones]:  char[32] name, i32 parent, f32[16] bindGlobal, f32[16] invBind  (column-major)
  verts[nverts]:  f32 pos[3], f32 nrm[3], f32 uv[2], u8 bone[4], u8 weight[4] (sum 255)
  subs[nsub]:     u32 firstIndex, u32 count, u32 slot   # texture slot, see SLOTS
  u16 indices[nindices]

.ntex: char[4] 'NTEX', u32 w, u32 h, RGBA8 pixels (top row first)

Usage: fbx2nmdl.py model.fbx out.nmdl --height 1.75
       fbx2nmdl.py --tex in.png out.ntex
"""
import sys, struct, math, argparse
import numpy as np
sys.path.insert(0, __import__('os').path.dirname(__file__))
from fbxparse import load, child, props70

SLOTS = {'body': 0, 'eye': 1, 'eyebrows': 2, 'mouth': 3, 'pupil': 4}

# Only DEF (deforming) bones are kept.  Rigify parents some DEF bones to
# non-deforming ORG/MCH bones, so give those a sensible DEF parent instead.
REPARENT = {
    'DEF-pelvis.L': 'DEF-spine', 'DEF-pelvis.R': 'DEF-spine',
    'DEF-thigh.L': 'DEF-spine', 'DEF-thigh.R': 'DEF-spine',
    'DEF-shoulder.L': 'DEF-spine.003', 'DEF-shoulder.R': 'DEF-spine.003',
    'DEF-upper_arm.L': 'DEF-shoulder.L', 'DEF-upper_arm.R': 'DEF-shoulder.R',
    'DEF-eye.l': 'DEF-spine.006', 'DEF-eye.r': 'DEF-spine.006',
}


def nm(n):
    return n[1][1].split('\x00')[0]


def mat(lst):
    # FBX matrices are stored column-major
    return np.array(lst, dtype=np.float64).reshape(4, 4).T


def euler_xyz(r):
    rx, ry, rz = [math.radians(a) for a in r]
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Z @ Y @ X


def model_matrix(model):
    p = props70(model)
    t = p.get('Lcl Translation', [0, 0, 0])[:3]
    r = p.get('Lcl Rotation', [0, 0, 0])[:3]
    s = p.get('Lcl Scaling', [1, 1, 1])[:3]
    M = np.eye(4)
    M[:3, :3] = euler_xyz(r) @ np.diag(s)
    M[:3, 3] = t
    return M


def tex_cmd(src, dst):
    from PIL import Image
    im = Image.open(src).convert('RGBA')
    with open(dst, 'wb') as f:
        f.write(b'NTEX' + struct.pack('<II', *im.size) + im.tobytes())


def convert(path, out, height):
    root = load(path)
    top = {n[0]: n for n in root}
    objs = {n[1][0]: n for n in top['Objects'][2]}
    conns = [(c[1][0], c[1][1], c[1][2]) for c in top['Connections'][2]]
    parent_of = {}
    children = {}
    for t, a, b in conns:
        if t == 'OO':
            parent_of.setdefault(a, []).append(b)
            children.setdefault(b, []).append(a)

    def kids(oid, typ):
        return [objs[k] for k in children.get(oid, []) if k in objs and objs[k][0] == typ]

    # ---- bones -------------------------------------------------------
    bind = {}          # bone name -> global bind matrix (FBX space)
    clusters = []      # (geom id, bone name, indexes, weights, meshTransform)
    for oid, n in objs.items():
        if n[0] == 'Deformer' and n[1][2] == 'Cluster':
            bone = [objs[c] for c in children.get(oid, []) if c in objs and objs[c][0] == 'Model']
            if not bone:
                continue
            bname = nm(bone[0])
            bind[bname] = mat(child(n, 'TransformLink')[1][0])
            skin = [p for p in parent_of.get(oid, []) if p in objs and objs[p][0] == 'Deformer']
            geom = [p for p in parent_of.get(skin[0], []) if p in objs and objs[p][0] == 'Geometry']
            idx = child(n, 'Indexes')
            if idx:
                clusters.append((geom[0], bname, idx[1][0], child(n, 'Weights')[1][0],
                                 mat(child(n, 'Transform')[1][0])))

    # FBX model hierarchy, used to find DEF parents
    model_by_name = {nm(n): oid for oid, n in objs.items() if n[0] == 'Model'}
    def fbx_parent(name):
        ps = [p for p in parent_of.get(model_by_name[name], []) if p in objs and objs[p][0] == 'Model']
        return nm(objs[ps[0]]) if ps else None

    bone_names = [b for b in bind if b.startswith('DEF-')]
    def def_parent(b):
        if b in REPARENT:
            return REPARENT[b]
        p = fbx_parent(b)
        while p and not p.startswith('DEF-'):
            p = fbx_parent(p)
        return p
    # order so that parents come first
    ordered = []
    def add(b):
        if b in ordered:
            return
        p = def_parent(b)
        if p:
            add(p)
        ordered.append(b)
    for b in bone_names:
        add(b)
    bidx = {b: i for i, b in enumerate(ordered)}

    # ---- geometry ----------------------------------------------------
    verts = []        # (pos(4), nrm(3), uv(2), weights dict)
    vkey = {}
    tris_by_slot = {s: [] for s in SLOTS.values()}
    for gid, g in objs.items():
        if g[0] != 'Geometry':
            continue
        model = [objs[p] for p in parent_of.get(gid, []) if p in objs and objs[p][0] == 'Model'][0]
        mats = [objs[c] for c in children.get(model_by_name[nm(model)], []) if c in objs and objs[c][0] == 'Material']
        # connection order defines material index
        mat_ids = [c[1] for c in conns if c[2] == model_by_name[nm(model)] and c[1] in objs and objs[c[1]][0] == 'Material']
        mat_names = [nm(objs[m]) for m in mat_ids]
        def slot_of(mname):
            for key in ('eyebrows', 'pupil', 'mouth', 'eye'):
                if mname.startswith(key):
                    return SLOTS[key]
            return SLOTS['body']

        V = np.array(child(g, 'Vertices')[1][0]).reshape(-1, 3)
        PI = child(g, 'PolygonVertexIndex')[1][0]
        uvl = child(g, 'LayerElementUV')
        UV = np.array(child(uvl, 'UV')[1][0]).reshape(-1, 2)
        UVI = child(uvl, 'UVIndex')[1][0]
        nl = child(g, 'LayerElementNormal')
        N = np.array(child(nl, 'Normals')[1][0]).reshape(-1, 3)
        NI = child(nl, 'NormalsIndex')
        NI = NI[1][0] if NI else None
        nmap = child(nl, 'MappingInformationType')[1][0]
        ml = child(g, 'LayerElementMaterial')
        MI = child(ml, 'Materials')[1][0]
        mmap = child(ml, 'MappingInformationType')[1][0]

        cl = [c for c in clusters if c[0] == gid]
        # Blender writes the cluster 'Transform' relative to the bone, so build
        # the mesh's global bind matrix from the model's own local transform.
        meshT = model_matrix(model)
        wts = [dict() for _ in range(len(V))]
        for _, bname, ids, ws, _t in cl:
            if bname not in bidx:
                continue
            for i, w in zip(ids, ws):
                if w > 1e-4:
                    wts[i][bidx[bname]] = wts[i].get(bidx[bname], 0) + w
        nrmM = np.linalg.inv(meshT[:3, :3]).T

        poly = []
        pnum = 0
        for i, raw in enumerate(PI):
            cp = raw if raw >= 0 else ~raw
            if nmap == 'ByPolygonVertex':
                ni = NI[i] if NI else i
            else:
                ni = NI[cp] if NI else cp
            key = (gid, cp, UVI[i], ni)
            if key not in vkey:
                p = meshT @ np.array([*V[cp], 1.0])
                n = nrmM @ N[ni]
                n = n / (np.linalg.norm(n) + 1e-9)
                vkey[key] = len(verts)
                verts.append((p[:3], n, UV[UVI[i]], wts[cp]))
            poly.append(vkey[key])
            if raw < 0:
                mi = MI[0] if mmap == 'AllSame' else MI[pnum]
                slot = slot_of(mat_names[mi] if mi < len(mat_names) else 'body')
                for j in range(1, len(poly) - 1):
                    tris_by_slot[slot].append((poly[0], poly[j], poly[j + 1]))
                poly = []
                pnum += 1

    P = np.array([v[0] for v in verts])
    lo, hi = P.min(0), P.max(0)
    # FBX from Blender is Y-up; character faces +Z.
    s = height / (hi[1] - lo[1])
    G = np.diag([s, s, s, 1.0])
    G[:3, 3] = [-(lo[0] + hi[0]) / 2 * s, -lo[1] * s, -(lo[2] + hi[2]) / 2 * s]

    out_v = bytearray()
    for p, n, uv, w in verts:
        gp = (G @ np.array([*p, 1]))[:3]
        top4 = sorted(w.items(), key=lambda kv: -kv[1])[:4]
        tot = sum(x[1] for x in top4)
        if tot <= 0:
            top4, tot = [(bidx['DEF-spine'], 1.0)], 1.0
        bi = [b for b, _ in top4] + [0] * (4 - len(top4))
        bw = [int(round(x / tot * 255)) for _, x in top4] + [0] * (4 - len(top4))
        bw[0] += 255 - sum(bw)
        out_v += struct.pack('<8f4B4B', *gp, *n, uv[0], 1.0 - uv[1], *bi, *bw)

    subs, idx = [], []
    for slot, tris in tris_by_slot.items():
        if not tris:
            continue
        subs.append((len(idx), len(tris) * 3, slot))
        for t in tris:
            idx += t

    out_b = bytearray()
    for b in ordered:
        Bg = G @ bind[b]
        p = def_parent(b)
        name = b.encode()[:31]
        out_b += name + b'\x00' * (32 - len(name))
        out_b += struct.pack('<i', bidx[p] if p else -1)
        out_b += struct.pack('<16f', *Bg.T.flatten())
        out_b += struct.pack('<16f', *np.linalg.inv(Bg).T.flatten())

    with open(out, 'wb') as f:
        f.write(b'NMDL' + struct.pack('<IIIIIf', 1, len(verts), len(idx), len(subs), len(ordered), height))
        f.write(out_b)
        f.write(out_v)
        for s_ in subs:
            f.write(struct.pack('<III', *s_))
        f.write(struct.pack('<%dH' % len(idx), *idx))
    print(f'{out}: {len(verts)} verts, {len(idx)//3} tris, {len(subs)} submeshes, {len(ordered)} bones')
    for b in ordered:
        print('  ', bidx[b], b, '<-', def_parent(b))


if __name__ == '__main__':
    if sys.argv[1] == '--tex':
        tex_cmd(sys.argv[2], sys.argv[3])
    else:
        ap = argparse.ArgumentParser()
        ap.add_argument('fbx'); ap.add_argument('out')
        ap.add_argument('--height', type=float, default=1.75)
        a = ap.parse_args()
        convert(a.fbx, a.out, a.height)
