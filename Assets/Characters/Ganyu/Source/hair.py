"""Hair: ribbon-like anime strands swept along splines, a scalp cap, ahoge and the
low ponytail. Also the horns and the hair-tie ornament (they share the head bone).

Strand UVs: u runs around the strand cross-section, v = arc length from the hair root
divided by HAIR_V_LEN, so one gradient texture colours every strand by absolute length
(short bangs stay light blue, the long ponytail ends go deep blue).
"""
import math

import numpy as np
import bmesh

from common import (nrm, smoothstep, catmull_rom, resample_polyline, polyline_tangents,
                    MeshBuilder, project_to_sdf, vertex_neighbors, laplacian_relax,
                    bm_levelset_cut, rot_axis)
import head as H
import body as B

HC = np.array([0.0, 0.010, 1.50])      # head centre used for directions / outward hints
HAIR_V_LEN = 1.25                        # metres of strand length mapped to v in [0, 1]
TIE = np.array([0.0, 0.094, 1.392])      # ponytail tie at the nape
RNG = np.random.default_rng(7)


def head_dir(phi, lam):
    p, l = math.radians(phi), math.radians(lam)
    return np.array([math.sin(p) * math.cos(l), -math.cos(p) * math.cos(l), math.sin(l)])


def head_point(phi, lam, offset=0.0, center=HC):
    """Point at signed distance `offset` from the head surface along a ray from center."""
    d = head_dir(phi, lam)
    lo, hi = 0.0, 0.25
    for _ in range(40):
        m = 0.5 * (lo + hi)
        v = H.head_sdf((center + d * m)[None])[0] - offset
        if v < 0:
            lo = m
        else:
            hi = m
    return center + d * 0.5 * (lo + hi)


def body_clear(P, clearance):
    """Push points out of the body until they are `clearance` away from it."""
    P = np.atleast_2d(np.array(P, dtype=np.float64))
    for _ in range(12):
        d = B.body_sdf(P)
        bad = d < clearance
        if not bad.any():
            break
        from common import sdf_grad
        g = nrm(sdf_grad(B.body_sdf, P[bad]))
        P[bad] += g * (clearance - d[bad])[:, None]
    return P


def outward_from_head(C):
    return nrm(C - HC)


class HairBuilder:
    def __init__(self):
        self.mb = MeshBuilder()
        self.strands = []   # (vertex index array, per-vertex s, centre line) for rigging

    def strand(self, ctrl, w0, thick=0.22, hint=outward_from_head, n_len=22, n_cross=8,
               bend=0.20, tip=0.65, root_v=0.0, twist=0.0, mat=0, min_thick=0.0015,
               w_profile=None, close_root=True, flat=1.0, v_len=None, tag="head"):
        pts = catmull_rom(np.asarray(ctrl, dtype=np.float64), 14)
        C, L = resample_polyline(pts, n_len + 1)
        s = np.linspace(0.0, 1.0, n_len + 1)
        T = polyline_tangents(C)
        Hh = hint(C)
        S = nrm(np.cross(T, Hh))
        for i in range(1, len(S)):
            if S[i] @ S[i - 1] < 0:
                S[i] = -S[i]
        # light smoothing of the side vector
        S2 = S.copy()
        S2[1:-1] = nrm(S[:-2] + 2 * S[1:-1] + S[2:])
        S = nrm(S2 - np.einsum('ij,ij->i', S2, T)[:, None] * T)
        N = np.cross(S, T)
        if twist:
            for i in range(len(C)):
                R = rot_axis(T[i], twist * s[i])
                S[i] = R @ S[i]
                N[i] = R @ N[i]
        if w_profile is None:
            w = w0 * (1.0 - s) ** tip * (0.80 + 0.80 * s)
        else:
            w = w0 * w_profile(s)
        t = np.maximum(w * thick * flat, min_thick * (1.0 - s))
        ang = 2 * np.pi * np.arange(n_cross) / n_cross
        ca, sa = np.cos(ang), np.sin(ang)
        rings = []
        allidx = []
        alls = []
        arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))])
        for i in range(n_len):
            x = 0.5 * w[i] * ca
            y = 0.5 * t[i] * sa - bend * w[i] * ca ** 2
            P = C[i] + np.outer(x, S[i]) + np.outer(y, N[i])
            idx = self.mb.add_verts(P)
            rings.append(idx)
            allidx.extend(idx)
            alls.extend([s[i]] * n_cross)
        tip_i = self.mb.add_verts(C[-1][None])[0]
        allidx.append(tip_i)
        alls.append(1.0)
        vv = root_v + arc / (HAIR_V_LEN if v_len is None else (arc[-1] if v_len == "unit" else v_len))
        for i in range(n_len - 1):
            a, b = rings[i], rings[i + 1]
            for k in range(n_cross):
                k1 = (k + 1) % n_cross
                u0, u1 = k / n_cross, (k + 1) / n_cross
                self.mb.add_face([a[k], a[k1], b[k1], b[k]],
                                 [(u0, vv[i]), (u1, vv[i]), (u1, vv[i + 1]), (u0, vv[i + 1])], mat)
        last = rings[-1]
        for k in range(n_cross):
            k1 = (k + 1) % n_cross
            u0, u1 = k / n_cross, (k + 1) / n_cross
            self.mb.add_face([last[k], last[k1], tip_i],
                             [(u0, vv[-2]), (u1, vv[-2]), (0.5 * (u0 + u1), vv[-1])], mat)
        if close_root:
            c = self.mb.add_verts(C[0][None])[0]
            allidx.append(c)
            alls.append(0.0)
            first = rings[0]
            for k in range(n_cross):
                k1 = (k + 1) % n_cross
                u0, u1 = k / n_cross, (k + 1) / n_cross
                self.mb.add_face([first[k1], first[k], c], [(u1, vv[0]), (u0, vv[0]), (0.5 * (u0 + u1), vv[0])], mat)
        self.strands.append((np.array(allidx), np.array(alls), C, tag))
        return C

# ---------------------------------------------------------------------------
# layout
# ---------------------------------------------------------------------------

def jitter(scale):
    return RNG.normal(0.0, scale, 3)


def build_bangs(hb):
    # (phi, lam_tip, width, tip sway sideways in deg)
    # strands directly above the eyes stop at the upper lid; one thin strand falls between the eyes
    spec = [(-80, -54, 0.038, -4), (-67, -38, 0.044, -3), (-53, -26, 0.048, -3), (-39, -9, 0.048, 2),
            (-25, -6, 0.046, 0), (-12, -5, 0.044, 3), (0, -22, 0.026, -2), (12, -4, 0.044, 2),
            (25, -7, 0.046, -2), (39, -8, 0.048, 2), (53, -25, 0.048, 3), (67, -37, 0.044, 3),
            (80, -52, 0.038, 4), (88, -62, 0.034, 4)]
    # under layer (shorter, slightly closer to the forehead) to close gaps
    for phi in (-62, -36, -12, 12, 36, 62):
        ctrl = [head_point(phi * 0.30, 62, 0.002), head_point(phi * 0.62, 44, 0.013),
                head_point(phi * 0.90, 22, 0.014), head_point(phi * 0.98, 8, 0.011),
                head_point(phi, -2, 0.008)]
        hb.strand(ctrl, 0.040, thick=0.22, bend=0.20, tip=0.75, n_len=16)
    for phi, lam_tip, w, sway in spec:
        side = 1.0 if phi >= 0 else -1.0
        ctrl = [head_point(phi * 0.30, 64, 0.004),
                head_point(phi * 0.62, 46, 0.019),
                head_point(phi * 0.90, 24, 0.020),
                head_point(phi * 0.98, 6, 0.017),
                head_point(phi + sway, (6 + lam_tip) * 0.5, 0.012),
                head_point(phi + sway * 1.6, lam_tip, 0.009 + 0.004 * abs(phi) / 90.0)]
        hb.strand(ctrl, w, thick=0.24, bend=0.20, tip=0.75, n_len=20)


def build_side_locks(hb):
    """Long locks in front of the ears falling to the chest."""
    for side in (1, -1):
        for j, (dphi, dz, w, ex) in enumerate(((0, 0.0, 0.034, 0.0), (8, 0.014, 0.027, 0.012),
                                               (-6, -0.008, 0.024, -0.010))):
            top = head_point(side * (84 + dphi), 34, 0.004)
            mid = head_point(side * (88 + dphi), 4, 0.016)
            # soft S-wave down the lock, the tip curling outward (fluffy, like the reference art)
            pts = [top, mid,
                   np.array([side * (0.074 + ex * 0.3), -0.030, 1.425 + dz]),
                   np.array([side * (0.096 + ex * 0.4), -0.044, 1.365 + dz]),
                   np.array([side * (0.084 + ex * 0.6), -0.070, 1.300 + dz]),
                   np.array([side * (0.104 + ex), -0.096, 1.246 + dz]),
                   np.array([side * (0.106 + ex), -0.110, 1.208 + dz]),
                   np.array([side * (0.118 + ex), -0.106, 1.196 + dz])]
            pts[2:] = list(body_clear(np.array(pts[2:]), 0.016))

            def hint(C, side=side):
                o = nrm(C - HC)
                h = np.array([side * 0.6, -0.8, 0.0])
                f = smoothstep(1.45, 1.38, C[:, 2])[:, None]
                return nrm(o * (1 - f) + h * f)
            hb.strand(pts, w, thick=0.24, bend=0.16, tip=0.55, n_len=28, hint=hint, twist=0.4 * side, tag="lock",
                      root_v=0.22 / HAIR_V_LEN)


def build_side_hair(hb):
    """Strands over the ears, falling a little below the jaw."""
    for side in (1, -1):
        for phi, lam0, lam1, w, drop in ((98, 50, -42, 0.036, 0.00), (108, 48, -46, 0.038, 0.012),
                                         (120, 46, -40, 0.038, 0.0), (134, 44, -34, 0.036, 0.0),
                                         (92, 42, -56, 0.030, 0.02), (146, 40, -28, 0.034, 0.0)):
            ctrl = [head_point(side * phi * 0.8, lam0 + 15, 0.008),
                    head_point(side * phi, lam0, 0.030),
                    head_point(side * phi, 10, 0.042),
                    head_point(side * (phi + 3), (10 + lam1) / 2, 0.044),
                    head_point(side * (phi + 6), lam1, 0.038) + np.array([0, 0, -drop])]
            hb.strand(ctrl, w, thick=0.24, bend=0.20, tip=0.7, n_len=20)


def build_back_head(hb):
    """Strands from the crown and the back of the scalp gathered into the tie."""
    for phi in (-160, -138, -116, -96, 96, 116, 138, 160, 180):
        for lam_root, wmul in ((68, 1.0), (40, 1.0), (14, 0.9)):
            if lam_root == 14 and abs(phi) < 110:
                continue
            side = 1.0 if phi > 0 else -1.0
            if phi == 180:
                side = 0.0
            root = head_point(phi * (0.85 if lam_root > 60 else 1.0), lam_root, 0.006)
            mid = head_point(phi, max(lam_root - 25, -10), 0.030)
            end = TIE + np.array([side * 0.012, 0.004, 0.004])
            near = 0.5 * (mid + end) + nrm(0.5 * (mid + end) - HC) * 0.010
            hb.strand([root, mid, near, end], 0.040 * wmul, thick=0.20, bend=0.22, tip=0.3, n_len=16, n_cross=6,
                      w_profile=lambda s: 1.0 - 0.55 * s)


def ponytail_frame(z):
    """Centre line (y) and half sizes (wx, wy) of the ponytail bundle at height z."""
    zs = [1.392, 1.33, 1.24, 1.14, 1.04, 0.95, 0.86, 0.79]
    ys = [0.094, 0.110, 0.122, 0.128, 0.140, 0.180, 0.240, 0.300]
    wx = [0.016, 0.050, 0.066, 0.072, 0.074, 0.078, 0.088, 0.104]
    wy = [0.012, 0.024, 0.029, 0.031, 0.032, 0.034, 0.038, 0.046]
    zz = np.asarray(z, dtype=np.float64)
    f = lambda arr: np.interp(-zz, -np.asarray(zs), arr)
    return f(ys), f(wx), f(wy)


def build_ponytail(hb, n=30):
    zs = np.linspace(1.392, 0.79, 14)
    for i in range(n):
        a = 2 * np.pi * (i + 0.5 * (i % 2)) / n + RNG.uniform(-0.15, 0.15)
        rad = 0.55 + 0.45 * ((i * 7) % n) / n
        ox, oy = math.cos(a) * rad, math.sin(a) * rad
        length = 0.70 + 0.30 * RNG.random()
        zend = 1.392 - (1.392 - 0.79) * length
        zz = zs[zs >= zend - 1e-6]
        if zz[-1] > zend + 0.01:
            zz = np.append(zz, zend)
        cy, wx, wy = ponytail_frame(zz)
        t = (1.392 - zz) / (1.392 - 0.79)
        phase = RNG.uniform(0, 2 * np.pi)
        wave = 0.010 * np.sin(phase + t * 7.0) * smoothstep(0.1, 0.5, t)
        flare = 1.0 + 0.6 * smoothstep(0.75, 1.0, t)
        X = ox * wx * flare + wave
        Y = cy + oy * wy * flare
        ctrl = np.stack([X, Y, zz], 1)
        ctrl[0] = TIE + np.array([ox * 0.012, oy * 0.008, 0.0])
        ctrl = body_clear(ctrl, 0.012)

        def hint(C, ox=ox, oy=oy):
            cy_, _, _ = ponytail_frame(C[:, 2])
            r = np.stack([C[:, 0], C[:, 1] - cy_, np.zeros(len(C))], 1)
            r = r + np.array([0.0, 0.03, 0.0])
            return nrm(r)
        w0 = 0.036 + 0.010 * RNG.random()
        hb.strand(ctrl, w0, thick=0.20, bend=0.18, tip=0.9, n_len=26, hint=hint, root_v=0.20 / HAIR_V_LEN,
                  w_profile=lambda s: (0.45 + 0.75 * smoothstep(0.0, 0.25, s)) * (1 - s) ** 0.8,
                  twist=RNG.uniform(-0.6, 0.6), tag="pony")


def build_ahoge(hb):
    root = head_point(0, 86, 0.0)
    ctrl = [root, root + [0.000, -0.004, 0.022], root + [0.004, 0.010, 0.042],
            root + [0.008, 0.032, 0.048], root + [0.010, 0.050, 0.040]]
    hb.strand(ctrl, 0.012, thick=0.35, bend=0.10, tip=0.8, n_len=16,
              hint=lambda C: np.tile(np.array([1.0, 0.0, 0.0]), (len(C), 1)))
    # small tuft on the crown, like the reference's top knot
    root2 = head_point(150, 70, 0.0)
    ctrl = [root2, root2 + [0.0, 0.006, 0.020], root2 + [0.0, 0.026, 0.024], root2 + [0.0, 0.034, 0.008]]
    hb.strand(ctrl, 0.020, thick=0.40, bend=0.15, tip=0.8, n_len=12,
              hint=lambda C: np.tile(np.array([1.0, 0.0, 0.0]), (len(C), 1)))


def build_hair_strands():
    hb = HairBuilder()
    build_bangs(hb)
    build_side_locks(hb)
    build_side_hair(hb)
    build_back_head(hb)
    build_ponytail(hb)
    build_ahoge(hb)
    return hb

# ---------------------------------------------------------------------------
# scalp cap
# ---------------------------------------------------------------------------

def hairline_fn(P):
    """Positive on the scalp side of the hairline."""
    rel = P - HC
    phi = np.degrees(np.arctan2(rel[:, 0], -rel[:, 1]))
    a = np.abs(phi)
    zl = np.interp(a, [0, 30, 60, 90, 120, 150, 180], [1.565, 1.556, 1.530, 1.455, 1.405, 1.385, 1.380])
    return P[:, 2] - zl


def build_cap(offset=0.015, seg=48, rings=30):
    C = HC
    R = np.array([0.10, 0.11, 0.11])
    th = np.pi * np.arange(1, rings + 1) / (rings + 1)
    ph = 2 * np.pi * np.arange(seg) / seg
    TH, PH = np.meshgrid(th, ph, indexing='ij')
    P = np.stack([R[0] * np.sin(TH) * np.sin(PH), -R[1] * np.sin(TH) * np.cos(PH), R[2] * np.cos(TH)], -1)
    V = np.vstack([P.reshape(-1, 3) + C, (C + [0, 0, R[2]])[None]])
    top = len(V) - 1
    F = []
    for k in range(rings - 1):
        for i in range(seg):
            i1 = (i + 1) % seg
            F.append([k * seg + i, k * seg + i1, (k + 1) * seg + i1, (k + 1) * seg + i])
    for i in range(seg):
        F.append([top, (i + 1) % seg, i])
    f = lambda Q: H.head_sdf(Q) - offset
    V = project_to_sdf(f, V, iters=10)
    V = laplacian_relax(V, vertex_neighbors(F, len(V)), f, iters=20, lam=0.5)
    # keep the scalp part
    bm = bmesh.new()
    for p in V:
        bm.verts.new(p)
    bm.verts.ensure_lookup_table()
    for fc in F:
        bm.faces.new([bm.verts[i] for i in fc])
    bm_levelset_cut(bm, hairline_fn)
    dead = [fc for fc in bm.faces if hairline_fn(np.array(fc.calc_center_median()[:])[None])[0] < 0]
    bmesh.ops.delete(bm, geom=dead, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context='VERTS')
    return bm

# ---------------------------------------------------------------------------
# horns (dark with a red band), hair tie
# ---------------------------------------------------------------------------

def horn_path(side):
    s = side
    return np.array([[s * 0.040, -0.024, 1.570], [s * 0.056, 0.004, 1.595], [s * 0.074, 0.044, 1.606],
                     [s * 0.095, 0.086, 1.597], [s * 0.112, 0.116, 1.574], [s * 0.124, 0.130, 1.543],
                     [s * 0.128, 0.128, 1.512], [s * 0.126, 0.118, 1.492]])


def build_horn(hb, side, mat=0):
    ctrl = horn_path(side)

    def hint(C):
        return np.tile(np.array([0.0, 0.0, 1.0]), (len(C), 1))
    # w = height of the horn section (vertical), thick = sideways
    hb.strand(ctrl, 0.030, thick=0.62, bend=0.04, tip=1.0, n_len=26, n_cross=12, hint=hint, mat=mat,
              w_profile=lambda s: (1.0 - s) ** 0.85 * (1.0 + 0.15 * np.sin(np.pi * s)),
              twist=-0.5 * side, min_thick=0.002)


def build_horns(material, collection):
    hb = HairBuilder()
    for side in (1, -1):
        ctrl = horn_path(side)
        hb.strand(ctrl, 0.036, thick=1.10, bend=0.03, tip=1.0, n_len=32, n_cross=12,
                  hint=lambda C: np.tile(np.array([0.0, 0.0, 1.0]), (len(C), 1)),
                  w_profile=lambda s: (1.0 - s) ** 0.85 * (1.0 + 0.15 * np.sin(np.pi * s)),
                  twist=-0.5 * side, min_thick=0.002, v_len="unit")
    ob = hb.mb.build("Horns", materials=[material], collection=collection)
    from common import recalc_normals
    recalc_normals(ob)
    return ob


def build_hair(material, collection):
    """Returns the hair object (strands + scalp cap) and the strand records for rigging."""
    import bpy
    from common import recalc_normals
    hb = build_hair_strands()
    ob = hb.mb.build("Hair", materials=[material], collection=collection)
    # scalp cap as a second object joined into the hair
    bm = build_cap()
    me = bpy.data.meshes.new("HairCap")
    bm.to_mesh(me)
    bm.free()
    cap = bpy.data.objects.new("HairCap", me)
    collection.objects.link(cap)
    uvl = me.uv_layers.new(name="UVMap")
    co = np.array([v.co[:] for v in me.vertices])
    rel = co - HC
    az = np.arctan2(rel[:, 0], -rel[:, 1]) / (2 * np.pi) + 0.5
    pol = np.arccos(np.clip(rel[:, 2] / np.linalg.norm(rel, axis=1), -1, 1))
    vv = pol * 0.10 / HAIR_V_LEN
    uvs = []
    for p in me.polygons:
        idx = [me.loops[li].vertex_index for li in p.loop_indices]
        us = az[idx].copy()
        if us.max() - us.min() > 0.5:
            us[us < 0.5] += 1.0
        # 6 repeats around the head so the strand lines stay fine
        uvs.extend([(u * 6.0, vv[i]) for u, i in zip(us, idx)])
    uvl.data.foreach_set("uv", np.asarray(uvs, np.float32).ravel())
    me.materials.append(material)
    me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
    recalc_normals(cap)
    from common import orient_outward
    orient_outward(cap, HC)
    recalc_normals(ob)
    from common import join_objects
    ob = join_objects([ob, cap], "Hair")
    return ob, hb
