"""
create_ganyu_pbr.py -- Ganyu-style character built entirely with bpy (Blender 4.2+ / 5.x).

Run it from Blender:  Scripting workspace > Open > Run Script
or from a shell:      blender --python create_ganyu_pbr.py

Everything is created inside the collection "Ganyu_PBR" (re-running the script replaces it).

Modelling
  * Body and head are modelled on one side only (x >= 0) as quad cages with a Mirror (X,
    clipping, merge) and a Subdivision Surface (level 2) modifier.  The cage vertices are
    iteratively corrected so that the *subdivided limit surface* lands on the target shape,
    so the smoothing does not melt the forms.
  * Face: cheekbones, jaw line, nose bridge and eye sockets carry real depth.  The eye
    openings are concentric edge loops (inset) with lid thickness, wrapped around separate
    UV-sphere eyeballs.
  * Hair: extruded strand clumps lifted off the scalp in layers (scalp shell, inner layer,
    outer layer, bangs, side locks, ponytail).
  * Tights: a separate shell 1.2 mm above the skin.  Its shader lets the subsurface-scattered
    skin show through, depending on how stretched the knit is and on the viewing angle.

Materials use the Principled BSDF (skin: subsurface weight 0.18, radius (1.0, 0.4, 0.2),
roughness 0.5; eyes: roughness 0.05).
"""
import math

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Vector

COLL_NAME = "Ganyu_PBR"
SUBSURF_LEVELS = 2
FIT_ITERS = 4

# =============================================================================
# small math / SDF toolkit (numpy, vectorised over (N, 3) point arrays)
# =============================================================================


def nrm(v, eps=1e-12):
    v = np.asarray(v, dtype=np.float64)
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), eps)


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)


def rot_axis(axis, a):
    x, y, z = nrm(axis)
    c, s = math.cos(a), math.sin(a)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


def sd_ellipsoid(P, c, r, R=None):
    q = P - np.asarray(c, dtype=np.float64)
    if R is not None:
        q = q @ np.asarray(R)
    r = np.asarray(r, dtype=np.float64)
    k0 = np.linalg.norm(q / r, axis=1)
    k1 = np.linalg.norm(q / (r * r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-12)


def sd_round_cone(P, a, b, r1, r2):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    ba = b - a
    l2 = float(ba @ ba)
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = P - a
    y = pa @ ba
    z = y - l2
    xv = pa * l2 - y[:, None] * ba
    x2 = np.einsum('ij,ij->i', xv, xv)
    y2 = y * y * l2
    z2 = z * z * l2
    k = math.copysign(1.0, rr) * rr * rr * x2
    body = (np.sqrt(np.maximum(x2 * a2 * il2, 0.0)) + y * rr) * il2 - r1
    return np.where(np.sign(z) * a2 * z2 > k, np.sqrt(x2 + z2) * il2 - r2,
                    np.where(np.sign(y) * a2 * y2 < k, np.sqrt(x2 + y2) * il2 - r1, body))


class Profile:
    """Monotone (PCHIP) interpolation of several columns against a key."""

    def __init__(self, keys, cols):
        self.x = np.asarray(keys, dtype=np.float64)
        self.Y = np.asarray(cols, dtype=np.float64)
        self.M = np.stack([self._slopes(self.x, self.Y[:, j]) for j in range(self.Y.shape[1])], 1)

    @staticmethod
    def _slopes(x, y):
        h = np.diff(x)
        d = np.diff(y) / h
        m = np.zeros_like(y)
        for k in range(1, len(x) - 1):
            if d[k - 1] * d[k] > 0:
                w1, w2 = 2 * h[k] + h[k - 1], h[k] + 2 * h[k - 1]
                m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k])
        m[0], m[-1] = d[0], d[-1]
        return m

    def __call__(self, xq):
        xq = np.clip(np.asarray(xq, dtype=np.float64), self.x[0], self.x[-1])
        i = np.clip(np.searchsorted(self.x, xq) - 1, 0, len(self.x) - 2)
        h = self.x[i + 1] - self.x[i]
        t = (xq - self.x[i]) / h
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1)[:, None] * self.Y[i] + ((t3 - 2 * t2 + t) * h)[:, None] * self.M[i]
                + (-2 * t3 + 3 * t2)[:, None] * self.Y[i + 1] + ((t3 - t2) * h)[:, None] * self.M[i + 1])


class Loft:
    """Stack of horizontal superellipse sections: rows (z, cx, cy, rx, ry_front, ry_back, n)."""

    def __init__(self, rows, mirror=False):
        rows = np.asarray(rows, dtype=np.float64)
        rows = rows[np.argsort(rows[:, 0])]
        if mirror:
            rows[:, 1] *= -1.0
        self.zmin, self.zmax = rows[0, 0], rows[-1, 0]
        self.prof = Profile(rows[:, 0], rows[:, 1:])

    def params(self, z):
        return self.prof(np.atleast_1d(z))

    def point(self, z, ang):
        """Point on the section at height z, polar angle ang (0 = front -Y, +90deg = +X)."""
        p = self.params(z)
        cx, cy, rx, ryf, ryb, n = p.T
        s, c = np.sin(ang), -np.cos(ang)
        ry = np.where(c < 0, ryf, ryb)
        r = 1.0 / ((np.abs(s) / rx) ** n + (np.abs(c) / ry) ** n) ** (1.0 / n)
        return np.stack([cx + r * s, cy + r * c, np.broadcast_to(z, r.shape)], -1)

    def sdf(self, P):
        z = P[:, 2]
        cx, cy, rx, ryf, ryb, n = self.params(z).T
        dx, dy = P[:, 0] - cx, P[:, 1] - cy
        ry = np.where(dy < 0, ryf, ryb)
        u = np.abs(dx / rx) + 1e-12
        v = np.abs(dy / ry) + 1e-12
        f = (u ** n + v ** n) ** (1.0 / n)
        g = np.sqrt((f ** (1 - n) * u ** (n - 1) / rx) ** 2 + (f ** (1 - n) * v ** (n - 1) / ry) ** 2)
        return np.maximum((f - 1.0) / np.maximum(g, 1e-9), np.maximum(self.zmin - z, z - self.zmax))


def sdf_grad(f, P, h=2e-4):
    g = np.empty_like(P)
    for i in range(3):
        e = np.zeros(3)
        e[i] = h
        g[:, i] = (f(P + e) - f(P - e)) / (2 * h)
    return g


def project_to_sdf(f, P, iters=6, max_step=0.03):
    P = np.array(P, dtype=np.float64)
    for _ in range(iters):
        d = f(P)
        g = sdf_grad(f, P)
        step = (d / np.maximum(np.einsum('ij,ij->i', g, g), 1e-10))[:, None] * g
        ln = np.linalg.norm(step, axis=1, keepdims=True)
        P -= step * np.minimum(1.0, max_step / np.maximum(ln, 1e-12))
    return P


def catmull_rom(points, n_per_seg=12):
    P = np.asarray(points, dtype=np.float64)
    ext = np.vstack([2 * P[0] - P[1], P, 2 * P[-1] - P[-2]])
    out = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        t = np.linspace(0, 1, n_per_seg, endpoint=False)[:, None]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t ** 2
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    out.append(P[-1][None])
    return np.vstack(out)


def resample(P, n):
    P = np.asarray(P, dtype=np.float64)
    s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))])
    t = np.linspace(0, s[-1], n)
    return np.stack([np.interp(t, s, P[:, i]) for i in range(3)], 1), s[-1]


def tangents(P):
    T = np.zeros_like(P)
    T[1:-1] = P[2:] - P[:-2]
    T[0] = P[1] - P[0]
    T[-1] = P[-1] - P[-2]
    return nrm(T)

# =============================================================================
# Blender helpers
# =============================================================================


def get_collection():
    old = bpy.data.collections.get(COLL_NAME)
    if old is not None:
        for ob in list(old.all_objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        bpy.data.collections.remove(old)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.curves):
        for item in list(block):
            if item.users == 0:
                block.remove(item)
    coll = bpy.data.collections.new(COLL_NAME)
    bpy.context.scene.collection.children.link(coll)
    return coll


def mesh_object(name, V, F, coll, mirror=False, subsurf=0, smooth=True, viewport_levels=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in V], [], [list(map(int, f)) for f in F])
    me.update()
    if smooth:
        me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    if mirror:
        m = ob.modifiers.new("Mirror", 'MIRROR')
        m.use_axis = (True, False, False)
        m.use_clip = True
        m.use_mirror_merge = True
        m.merge_threshold = 1e-4
    if subsurf:
        s = ob.modifiers.new("Subdivision", 'SUBSURF')
        s.levels = subsurf if viewport_levels is None else viewport_levels
        s.render_levels = subsurf
        s.quality = 3
    return ob


def recalc_normals(ob, inside=False):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if inside:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()


def get_co(ob):
    a = np.zeros(len(ob.data.vertices) * 3)
    ob.data.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def set_co(ob, P):
    ob.data.vertices.foreach_set("co", np.asarray(P, dtype=np.float32).ravel())
    ob.data.update()


def limit_positions(ob):
    """Positions of the cage vertices on the subdivided (limit) surface.
    Mirror and Subsurf both keep the original vertices first in their output."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(ob.data.vertices)
    a = np.zeros(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    ev.to_mesh_clear()
    return a.reshape(-1, 3)[:n]


def edges_from_faces(F):
    E = set()
    for f in F:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            E.add((min(a, b), max(a, b)))
    return np.array(sorted(E), dtype=np.int64)


def relax_on_sdf(P, F, sdf, iters=30, lam=0.5, movable=None):
    """Even out a cage on the target surface: tangential Laplacian steps + re-projection.
    Vertices on the mirror plane stay on x = 0."""
    P = np.array(P, dtype=np.float64)
    E = edges_from_faces(F)
    n = len(P)
    deg = np.maximum(np.bincount(E.ravel(), minlength=n), 1)[:, None]
    center = np.abs(P[:, 0]) < 1e-6
    mov = np.ones(n, bool) if movable is None else movable
    for _ in range(iters):
        acc = np.zeros_like(P)
        np.add.at(acc, E[:, 0], P[E[:, 1]])
        np.add.at(acc, E[:, 1], P[E[:, 0]])
        D = acc / deg - P
        D[center, 0] = -P[center, 0]
        g = nrm(sdf_grad(sdf, P))
        D -= np.einsum('ij,ij->i', D, g)[:, None] * g
        D[~mov] = 0.0
        P += lam * D
        P[mov] = project_to_sdf(sdf, P[mov], iters=3)
        P[center, 0] = 0.0
    return P


def cage_edges(ob):
    e = np.zeros(len(ob.data.edges) * 2, dtype=np.int64)
    ob.data.edges.foreach_get("vertices", e)
    return e.reshape(-1, 2)


def fit_cage(ob, sdf, iters=8, movable=None, gain=0.8, max_step=0.006, smooth=0.5):
    """Move cage vertices so their subdivision limit positions lie on the zero set of `sdf`.
    The correction field is smoothed over the cage graph and clamped each step, so the cage
    stays regular where it is too coarse to follow small features."""
    P = get_co(ob)
    n = len(P)
    center = np.abs(P[:, 0]) < 1e-6
    E = cage_edges(ob)
    deg = np.maximum(np.bincount(E.ravel(), minlength=n), 1)[:, None]
    for _ in range(iters):
        L = limit_positions(ob)
        D = project_to_sdf(sdf, L, iters=5) - L
        acc = np.zeros_like(D)
        np.add.at(acc, E[:, 0], D[E[:, 1]])
        np.add.at(acc, E[:, 1], D[E[:, 0]])
        D = (1.0 - smooth) * D + smooth * acc / deg
        ln = np.linalg.norm(D, axis=1, keepdims=True)
        D *= gain * np.minimum(1.0, max_step / np.maximum(ln, 1e-12))
        if movable is not None:
            D[~movable] = 0.0
        P = P + D
        P[center, 0] = 0.0
        set_co(ob, P)
    return P


def cap_ring(ring_ids, V, extra_out):
    """Quad grid cap for an even-length closed ring (Coons patch). Returns faces.
    New interior vertices are appended to extra_out (list of positions)."""
    N = len(ring_ids)
    m = 2 if N >= 8 else 1
    n = N // 2 - m
    # boundary walked as: bottom (n+1), right side (m-1), top (n+1), left side (m-1)
    bottom = ring_ids[0:n + 1]
    right = ring_ids[n + 1:n + m]
    top = ring_ids[n + m:2 * n + m + 1][::-1]
    left = ring_ids[2 * n + m + 1:][::-1]
    grid = [[None] * (n + 1) for _ in range(m + 1)]
    for i in range(n + 1):
        grid[0][i] = bottom[i]
        grid[m][i] = top[i]
    for j in range(1, m):
        grid[j][n] = right[j - 1]
        grid[j][0] = left[j - 1]
    base = len(V) + len(extra_out)
    for j in range(1, m):
        for i in range(1, n):
            u, v = i / n, j / m
            P = ((1 - v) * V[grid[0][i]] + v * V[grid[m][i]] + (1 - u) * V[grid[j][0]] + u * V[grid[j][n]]
                 - ((1 - u) * (1 - v) * V[grid[0][0]] + u * (1 - v) * V[grid[0][n]]
                    + (1 - u) * v * V[grid[m][0]] + u * v * V[grid[m][n]]))
            grid[j][i] = base
            extra_out.append(P)
            base += 1
    F = []
    for j in range(m):
        for i in range(n):
            F.append([grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]])
    return F

# =============================================================================
# target shapes (metres, Z up, the character faces -Y, her left is +X)
# =============================================================================

TORSO_ROWS = [  # z, cx, cy, rx, ry_front, ry_back, n
    (0.775, 0, 0.012, 0.030, 0.025, 0.030, 2.0), (0.80, 0, 0.012, 0.085, 0.052, 0.062, 2.2),
    (0.83, 0, 0.012, 0.130, 0.068, 0.078, 2.4), (0.87, 0, 0.012, 0.150, 0.076, 0.084, 2.4),
    (0.91, 0, 0.010, 0.147, 0.079, 0.082, 2.4), (0.95, 0, 0.008, 0.135, 0.078, 0.075, 2.3),
    (0.99, 0, 0.007, 0.117, 0.075, 0.067, 2.2), (1.03, 0, 0.006, 0.101, 0.073, 0.066, 2.2),
    (1.07, 0, 0.004, 0.103, 0.072, 0.068, 2.2), (1.11, 0, 0.001, 0.110, 0.074, 0.074, 2.2),
    (1.15, 0, -0.002, 0.116, 0.076, 0.080, 2.3), (1.19, 0, -0.003, 0.119, 0.075, 0.084, 2.3),
    (1.23, 0, -0.002, 0.118, 0.071, 0.085, 2.3), (1.27, 0, 0.001, 0.110, 0.063, 0.079, 2.3),
    (1.30, 0, 0.005, 0.090, 0.053, 0.068, 2.2), (1.325, 0, 0.010, 0.060, 0.044, 0.054, 2.0),
    (1.35, 0, 0.014, 0.043, 0.040, 0.042, 2.0), (1.38, 0, 0.014, 0.038, 0.036, 0.037, 2.0),
    (1.46, 0, 0.012, 0.036, 0.034, 0.035, 2.0),
]
LEG_ROWS = [  # left leg
    (0.95, 0.075, 0.010, 0.040, 0.040, 0.040, 2.0), (0.90, 0.082, 0.010, 0.066, 0.065, 0.075, 2.0),
    (0.84, 0.086, 0.008, 0.073, 0.071, 0.079, 2.0), (0.78, 0.086, 0.004, 0.071, 0.071, 0.071, 2.0),
    (0.70, 0.083, 0.000, 0.066, 0.069, 0.064, 2.0), (0.62, 0.079, -0.004, 0.060, 0.064, 0.058, 2.0),
    (0.55, 0.074, -0.006, 0.053, 0.057, 0.050, 2.0), (0.50, 0.071, -0.007, 0.048, 0.050, 0.045, 2.0),
    (0.455, 0.068, -0.008, 0.046, 0.048, 0.043, 2.0), (0.41, 0.067, -0.006, 0.043, 0.043, 0.048, 2.0),
    (0.37, 0.066, -0.004, 0.045, 0.040, 0.058, 2.0), (0.33, 0.065, -0.002, 0.045, 0.038, 0.061, 2.0),
    (0.28, 0.064, 0.000, 0.042, 0.036, 0.055, 2.0), (0.22, 0.063, 0.003, 0.035, 0.032, 0.041, 2.0),
    (0.17, 0.062, 0.005, 0.030, 0.029, 0.031, 2.0), (0.13, 0.062, 0.007, 0.030, 0.029, 0.029, 2.0),
    (0.09, 0.062, 0.008, 0.028, 0.026, 0.028, 2.0),
]
TORSO = Loft(TORSO_ROWS)
LEG = Loft(LEG_ROWS)

ARM_ANGLE = math.radians(40.0)            # A-pose
SHOULDER = np.array([0.128, 0.005, 1.282])
ARM_DIR = np.array([math.cos(ARM_ANGLE), 0.0, -math.sin(ARM_ANGLE)])
UPPER_ARM = 0.255
WRIST_T = 0.485
ANKLE = np.array([0.062, 0.010, 0.112])
FOOT_YAW = math.radians(6.0)


def arm_sdf(P):
    S, d = SHOULDER, ARM_DIR
    E = S + UPPER_ARM * d
    W = S + WRIST_T * d
    delt = sd_round_cone(P, S - 0.010 * d + np.array([0, 0, -0.006]), S + 0.10 * d, 0.039, 0.031)
    upper = sd_round_cone(P, S, E, 0.034, 0.028)
    fore1 = sd_round_cone(P, E, E + 0.07 * d, 0.029, 0.031)
    fore2 = sd_round_cone(P, E + 0.07 * d, W + 0.02 * d, 0.031, 0.021)
    return smin(smin(smin(delt, upper, 0.02), fore1, 0.015), fore2, 0.015)


def foot_local(P):
    return (P - ANKLE) @ rot_z(-FOOT_YAW).T + ANKLE


def foot_world(Q):
    return (np.asarray(Q) - ANKLE) @ rot_z(FOOT_YAW).T + ANKLE


def foot_sdf(P):
    Q = foot_local(P)
    heel = sd_ellipsoid(Q, (0.062, 0.030, 0.078), (0.025, 0.027, 0.028))
    mid = sd_ellipsoid(Q, (0.061, -0.045, 0.064), (0.031, 0.050, 0.024), rot_x(math.radians(28)))
    ball = sd_ellipsoid(Q, (0.058, -0.105, 0.030), (0.037, 0.030, 0.018))
    toes = sd_ellipsoid(Q, (0.056, -0.140, 0.022), (0.033, 0.030, 0.014))
    return smin(smin(smin(heel, mid, 0.02), ball, 0.02), toes, 0.015)


def leg_sdf(P):
    d = LEG.sdf(P)
    d = smin(d, sd_ellipsoid(P, (0.068, -0.040, 0.455), (0.024, 0.012, 0.022)), 0.012)
    return smin(d, foot_sdf(P), 0.025)


def body_sdf(P, arms=True):
    """Left half of the body (x >= 0); the mirror makes the other side."""
    Q = P.copy()
    Q[:, 0] = np.abs(Q[:, 0])
    d = TORSO.sdf(Q)
    bust = sd_ellipsoid(Q, (0.060, -0.070, 1.176), (0.064, 0.056, 0.060), rot_z(math.radians(10)) @ rot_x(math.radians(8)))
    bust_r = sd_ellipsoid(Q * [-1, 1, 1], (0.060, -0.070, 1.176), (0.064, 0.056, 0.060),
                          rot_z(math.radians(10)) @ rot_x(math.radians(8)))
    d = smin(d, smin(bust, bust_r, 0.03), 0.035)
    glute = sd_ellipsoid(Q, (0.062, 0.050, 0.855), (0.070, 0.062, 0.075))
    glute_r = sd_ellipsoid(Q * [-1, 1, 1], (0.062, 0.050, 0.855), (0.070, 0.062, 0.075))
    d = smin(d, smin(glute, glute_r, 0.04), 0.035)
    d = smin(d, sd_round_cone(Q, (0.035, 0.016, 1.322), (0.120, 0.008, 1.292), 0.027, 0.030), 0.035)
    d = smin(d, leg_sdf(Q), 0.03)
    if arms:
        d = smin(d, arm_sdf(Q), 0.025)
    return d

# =============================================================================
# body cage (left half, mirrored)
# =============================================================================

NTH = 13         # half torso ring: front centre .. back centre (24 around after mirroring)
NL = 16          # leg ring (13 torso-half vertices + 3 on the crotch line)
NA = 10          # arm ring
TORSO_Z = [0.86, 0.895, 0.93, 0.965, 1.00, 1.035, 1.07, 1.10, 1.13, 1.155, 1.18, 1.205, 1.23, 1.255,
           1.28, 1.305, 1.33, 1.36, 1.395, 1.44]
KA, KB = 11, 14  # arm hole rings
CA, CB = 5, 7    # arm hole columns (column 6 = side)


class Cage:
    def __init__(self):
        self.V = []
        self.F = []
        self.part = []
        self.mat = []

    def add(self, p, part):
        self.V.append(np.asarray(p, dtype=np.float64))
        self.part.append(part)
        return len(self.V) - 1

    def face(self, f, mat=0):
        self.F.append([int(i) for i in f])
        self.mat.append(mat)

    def bridge(self, a, b, closed=True, mat=0):
        n = len(a)
        for m in range(n if closed else n - 1):
            m1 = (m + 1) % n
            self.face([a[m], a[m1], b[m1], b[m]], mat)


def unwrap_angles(P, center, e1, e2):
    """Angles of a loop of points measured around the loop's own centroid in the (e1, e2) plane.
    (Measuring around the arm axis fails: in an A-pose the axis does not pass through the hole.)"""
    n = np.cross(e1, e2)
    rel = P - center
    rel = rel - np.outer(rel @ n, n)
    rel = rel - rel.mean(0)
    return np.unwrap(np.arctan2(rel @ e2, rel @ e1))


def build_body_cage():
    cg = Cage()
    K = len(TORSO_Z)
    ang = np.pi * np.arange(NTH) / (NTH - 1)

    def hole_vert(k, c):
        return KA < k < KB and CA < c < CB

    def hole_cell(k, c):
        return KA <= k < KB and CA <= c < CB
    T = -np.ones((K, NTH), dtype=int)
    for k, z in enumerate(TORSO_Z):
        P = TORSO.point(np.full(NTH, z), ang)
        P[0, 0] = P[-1, 0] = 0.0
        for c in range(NTH):
            if not hole_vert(k, c):
                T[k, c] = cg.add(P[c], 'torso')
    for k in range(K - 1):
        for c in range(NTH - 1):
            if not hole_cell(k, c):
                cg.face([T[k, c], T[k, c + 1], T[k + 1, c + 1], T[k + 1, c]])
    # ---- arm (10-vertex rings) ----
    loop = [T[KA, c] for c in range(CA, CB + 1)] + [T[k, CB] for k in range(KA + 1, KB)] \
        + [T[KB, c] for c in range(CB, CA - 1, -1)] + [T[k, CA] for k in range(KB - 1, KA, -1)]
    S, d = SHOULDER, ARM_DIR
    e1 = nrm(np.array([0.0, -1.0, 0.0]) - (np.array([0.0, -1.0, 0.0]) @ d) * d)   # thumb side
    e2 = np.cross(d, e1)                                                               # palm side
    a_nat = unwrap_angles(np.array([cg.V[i] for i in loop]), S, e1, e2)
    sgn = 1.0 if a_nat[-1] > a_nat[0] else -1.0
    # target: 5 vertices on the palm side, 5 on the back of the hand, sides between
    best = None
    for shift in range(NA):
        uni = sgn * (math.radians(18) + 2 * np.pi * ((np.arange(NA) + shift) % NA) / NA)
        diff = np.angle(np.exp(1j * (a_nat - uni)))
        cost = np.sum(diff ** 2)
        if best is None or cost < best[0]:
            best = (cost, uni + np.round((a_nat - uni - diff) / (2 * np.pi)) * 2 * np.pi)
    uni = best[1]
    for m, vi in enumerate(loop):
        am = 0.5 * (a_nat[m] + uni[m])
        cg.V[vi] = S - 0.004 * d + 0.052 * (math.cos(am) * e1 + math.sin(am) * e2)
        cg.part[vi] = "hole"
    rings = [loop]
    ts = np.linspace(0.04, WRIST_T, 11)
    rad = lambda t: np.interp(t, [0.0, 0.06, 0.15, 0.255, 0.33, 0.485], [0.045, 0.036, 0.032, 0.028, 0.031, 0.022])
    for j, t in enumerate(ts, 1):
        w = smoothstep(0, 3, j)
        a = (1 - w) * a_nat + w * uni
        # flatten toward the wrist (wider across the thumb-little finger axis)
        fl = smoothstep(0.30, WRIST_T, t)
        rr = rad(t)
        pts = S + t * d + (rr * (1 + 0.18 * fl) * np.cos(a))[:, None] * e1 + (rr * (1 - 0.25 * fl) * np.sin(a))[:, None] * e2
        rings.append([cg.add(p, 'arm') for p in pts])
    for j in range(len(rings) - 1):
        cg.bridge(rings[j], rings[j + 1])
    hand_rings = build_hand(cg, rings[-1], uni, S, d, e1, e2)
    # ---- spread the torso rings above the arm hole over the shoulder top ----
    top_k = KB + 3
    tgt = TORSO.point(np.full(NTH, TORSO_Z[top_k]), ang)
    for s_ in range(1, 3):
        t = s_ / 3.0
        for c in range(NTH):
            cg.V[T[KB + s_, c]] = cg.V[T[KB, c]] + (tgt[c] - cg.V[T[KB, c]]) * t
            if c in (0, NTH - 1):
                cg.V[T[KB + s_, c]][0] = 0.0
    # ---- crotch + leg ----
    cF = cg.add((0.0, -0.035, 0.800), 'crotch')
    cM = cg.add((0.0, 0.005, 0.785), 'crotch')
    cB = cg.add((0.0, 0.045, 0.800), 'crotch')
    ring0 = [T[0, c] for c in range(NTH)] + [cB, cM, cF]
    c0 = LEG.params(0.83)[0]
    P0 = np.array([cg.V[i] for i in ring0])
    a_nat = np.unwrap(np.arctan2(P0[:, 1] - c0[1], P0[:, 0] - c0[0]))
    sgnl = 1.0 if a_nat[-1] > a_nat[0] else -1.0
    base = a_nat[0] + sgnl * 2 * np.pi * np.arange(NL) / NL
    uni_l = base + np.mean(a_nat - base)
    rings = [ring0]
    zs = [0.815, 0.775, 0.73, 0.68, 0.63, 0.58, 0.535, 0.495, 0.457, 0.42, 0.38, 0.34, 0.30, 0.26, 0.22, 0.18, 0.15]
    for j, zc in enumerate(zs, 1):
        w = smoothstep(0, 4, j)
        tilt = 0.035 * (1 - smoothstep(0, 5, j))
        al = (1 - w) * a_nat + w * uni_l
        z = zc - tilt * np.cos(al - math.pi)
        cx, cy, rx, ryf, ryb, n = LEG.params(z).T
        s_, c_ = np.cos(al), np.sin(al)
        ry = np.where(c_ < 0, ryf, ryb)
        r = 1.0 / ((np.abs(s_) / rx) ** n + (np.abs(c_) / ry) ** n) ** (1.0 / n)
        pts = np.stack([cx + r * s_, cy + r * c_, z], 1)
        rings.append([cg.add(p, 'leg') for p in pts])
    # foot: rings follow the centre line of the foot in the heel pose
    path = np.array([(0.062, 0.008, 0.118), (0.062, 0.006, 0.092), (0.062, -0.010, 0.072),
                     (0.0615, -0.040, 0.056), (0.0605, -0.075, 0.042), (0.059, -0.108, 0.031),
                     (0.058, -0.138, 0.024), (0.057, -0.158, 0.021)])
    sizes = np.array([(0.031, 0.029, 0.034), (0.031, 0.030, 0.048), (0.032, 0.030, 0.046),
                      (0.034, 0.026, 0.030), (0.036, 0.022, 0.022), (0.038, 0.019, 0.018),
                      (0.035, 0.016, 0.015), (0.030, 0.013, 0.012)])
    Tn = tangents(path)
    cs, sn = np.cos(uni_l), np.sin(uni_l)
    for j in range(len(path)):
        ex = np.array([1.0, 0.0, 0.0])
        ex = nrm(ex - (ex @ Tn[j]) * Tn[j])
        ey = np.cross(ex, Tn[j])
        rx, ryt, rys = sizes[j]
        ry = np.where(sn < 0, ryt, rys)
        pts = foot_world(path[j] + (rx * cs)[:, None] * ex + (ry * sn)[:, None] * ey)
        rings.append([cg.add(p, 'foot') for p in pts])
    for j in range(len(rings) - 1):
        cg.bridge(rings[j], rings[j + 1])
    extra = []
    Vtmp = cg.V
    for f in cap_ring(rings[-1], Vtmp, extra):
        cg.face(f)
    for p in extra:
        cg.add(p, 'foot')
    cg.T, cg.leg_rings, cg.hand_rings = T, rings, hand_rings
    return cg


# finger layout in the hand frame (x: toward the finger tips, y: thumb side, z: back of hand)
FINGER_LEN = [(0.036, 0.022, 0.019), (0.040, 0.025, 0.020), (0.037, 0.023, 0.019), (0.029, 0.018, 0.016)]
FINGER_YAW = [-4.0, 0.0, 4.0, 9.0]          # degrees, + toward the little finger


def build_hand(cg, wrist_ring, angles, S, d, e1, e2):
    """Box-modelled glove hand: palm rings, a 4x1 knuckle cap, extruded fingers and thumb."""
    W = S + WRIST_T * d
    def sec(t, half_w, half_t, n_exp=4.0):
        a = angles
        c, s = np.cos(a), np.sin(a)
        x = half_w * np.sign(c) * np.abs(c) ** (2.0 / n_exp)
        y = half_t * np.sign(s) * np.abs(s) ** (2.0 / n_exp)
        return W + t * d + x[:, None] * e1 + y[:, None] * e2
    rings = [wrist_ring]
    for t, hw, ht in ((0.022, 0.030, 0.016), (0.052, 0.037, 0.0135), (0.084, 0.039, 0.0120)):
        rings.append([cg.add(p, 'hand') for p in sec(t, hw, ht)])
    for j in range(len(rings) - 1):
        cg.bridge(rings[j], rings[j + 1])
    knuck = rings[-1]
    # ring order: indices 0..4 lie on the palm side (sin>0), 5..9 on the back; pair them
    order = np.argsort(np.mod(angles, 2 * np.pi))
    palm = [knuck[i] for i in order[:5]]          # thumb side -> little finger side
    back = [knuck[i] for i in order[5:]][::-1]     # thumb side -> little finger side
    for f in range(4):
        root = [palm[f], palm[f + 1], back[f + 1], back[f]]
        ctr = np.mean([cg.V[i] for i in root], axis=0)
        yaw = math.radians(FINGER_YAW[f])
        fdir = nrm(math.cos(yaw) * d - math.sin(yaw) * e1)
        prev = root
        pos = ctr
        for k, L in enumerate(FINGER_LEN[f]):
            fdir = nrm(rot_axis(np.cross(fdir, e2), math.radians(9 + 3 * k)) @ fdir)
            pos = pos + fdir * L
            sc = (0.86 - 0.06 * k) * (0.92 if f == 3 else 1.0)
            ring = [cg.add(pos + (cg.V[i] - ctr) * sc, 'hand') for i in root]
            cg.bridge(prev, ring)
            prev = ring
        cg.face(prev)
    # thumb from the side face of the first palm segment (thumb side = +e1)
    r1, r2 = rings[1], rings[2]
    side = [i for i in range(NA) if np.cos(angles[i]) > 0.75]
    if len(side) >= 2:
        i0, i1 = sorted(side, key=lambda i: np.sin(angles[i]))[:2]
        root = [r1[i0], r1[i1], r2[i1], r2[i0]]
        # remove the palm face covering the root
        cg.F = [f for f in cg.F if sorted(f) != sorted(root)]
        ctr = np.mean([cg.V[i] for i in root], axis=0)
        tdir = nrm(0.55 * d + 0.70 * e1 + 0.45 * e2)
        prev = root
        pos = ctr
        for k, (L, sc) in enumerate(((0.030, 0.95), (0.026, 0.82), (0.022, 0.70))):
            tdir = nrm(tdir + 0.25 * d)
            pos = pos + tdir * L
            ring = [cg.add(pos + (cg.V[i] - ctr) * sc, 'hand') for i in root]
            cg.bridge(prev, ring)
            prev = ring
        cg.face(prev)
    return rings


def ray_exit(sdf, O, D, tmax=0.4, iters=40):
    """First exit point from inside the shape along D (vectorised bisection)."""
    D = nrm(D)
    lo = np.zeros(len(O))
    hi = np.full(len(O), tmax)
    for _ in range(iters):
        m = 0.5 * (lo + hi)
        inside = sdf(O + D * m[:, None]) < 0
        lo = np.where(inside, m, lo)
        hi = np.where(inside, hi, m)
    return O + D * hi[:, None]


def trunk_sdf(P):
    return body_sdf(P, arms=False)


def make_body(coll):
    cg = build_body_cage()
    V = np.array(cg.V)
    part = np.array(cg.part)
    center = np.abs(V[:, 0]) < 1e-6
    # torso grid: horizontal rays from the trunk axis keep rings and columns regular
    tor = part == 'torso'
    z = V[tor, 2]
    cy = TORSO.params(z)[:, 1]
    phi = np.arctan2(V[tor, 0], -(V[tor, 1] - cy))
    O = np.stack([np.zeros_like(z), cy, z], 1)
    D = np.stack([np.sin(phi), -np.cos(phi), np.zeros_like(z)], 1)
    V[tor] = ray_exit(trunk_sdf, O, D)
    # everything else (arm root loop, arms, legs, feet, crotch): closest point
    other = (part != 'torso') & (part != 'hand')
    V[other] = project_to_sdf(body_sdf, V[other], iters=8)
    V[center, 0] = 0.0
    # gentle evening-out of torso / legs; the arm root and arms keep their rings
    relax_mask = np.isin(part, ['torso', 'leg', 'foot', 'crotch'])
    V = relax_on_sdf(V, cg.F, body_sdf, iters=8, lam=0.3, movable=relax_mask)
    ob = mesh_object("Body", V, cg.F, coll, mirror=True, subsurf=SUBSURF_LEVELS)
    recalc_normals(ob)
    fit_cage(ob, body_sdf, iters=FIT_ITERS, movable=part != 'hand')
    return ob, cg

# =============================================================================
# scene: camera, lights, render settings
# =============================================================================


def set_engine(scn, prefer='EEVEE'):
    ids = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    if prefer == 'CYCLES':
        try:
            scn.render.engine = 'CYCLES'
            return
        except TypeError:
            pass
    for name in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
        try:
            scn.render.engine = name
            return
        except TypeError:
            continue


def aim_camera(cam, az_deg, el_deg, target, dist):
    a, e = math.radians(az_deg), math.radians(el_deg)
    tx, ty, tz = target
    pos = Vector((tx + dist * math.sin(a) * math.cos(e), ty - dist * math.cos(a) * math.cos(e), tz + dist * math.sin(e)))
    cam.location = pos
    d = Vector(target) - pos
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def add_area_light(coll, name, loc, target, energy, size, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.energy = energy
    ld.shape = "DISK"
    ld.size = size
    ld.color = color
    ob = bpy.data.objects.new(name, ld)
    coll.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    return ob


def setup_scene(coll, preview=False):
    scn = bpy.context.scene
    cam_d = bpy.data.cameras.new("GanyuCam")
    cam_d.lens = 85
    cam = bpy.data.objects.new("GanyuCam", cam_d)
    coll.objects.link(cam)
    scn.camera = cam
    aim_camera(cam, -18, 6, (0, 0, 0.84), 4.6)
    add_area_light(coll, "Key", (-1.6, -2.4, 2.4), (0, 0, 1.1), 260, 1.6, (1.0, 0.97, 0.93))
    add_area_light(coll, "Fill", (2.2, -1.8, 1.4), (0, 0, 1.0), 90, 2.5, (0.88, 0.92, 1.0))
    add_area_light(coll, "Rim", (0.6, 2.6, 2.2), (0, 0, 1.2), 200, 1.2, (0.95, 0.95, 1.0))
    world = bpy.data.worlds.get("GanyuWorld") or bpy.data.worlds.new("GanyuWorld")
    scn.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Color"].default_value = (0.62, 0.64, 0.68, 1.0)
        bg.inputs["Strength"].default_value = 0.35
    set_engine(scn, 'EEVEE')
    try:
        scn.view_settings.view_transform = 'AgX'
        scn.view_settings.look = 'AgX - Medium High Contrast'
    except TypeError:
        try:
            scn.view_settings.view_transform = 'Filmic'
        except TypeError:
            pass
    scn.render.resolution_x = 600 if preview else 1080
    scn.render.resolution_y = 1000 if preview else 1800
    if hasattr(scn, "eevee"):
        ee = scn.eevee
        if hasattr(ee, "taa_render_samples"):
            ee.taa_render_samples = 32 if preview else 96
        if hasattr(ee, "use_raytracing"):
            ee.use_raytracing = True
        if hasattr(ee, "use_shadows"):
            ee.use_shadows = True
    if hasattr(scn, "cycles"):
        scn.cycles.samples = 64 if preview else 256
        scn.cycles.use_denoising = True
    # ground
    me = bpy.data.meshes.new("Ground")
    s = 6.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ground = bpy.data.objects.new("Ground", me)
    coll.objects.link(ground)
    gm = bpy.data.materials.new("Ground")
    p = gm.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (0.55, 0.56, 0.60, 1.0)
    p.inputs["Roughness"].default_value = 0.8
    me.materials.append(gm)
    return cam


def placeholder_material(name, color, roughness=0.5):
    m = bpy.data.materials.new(name)
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1.0)
    p.inputs["Roughness"].default_value = roughness
    return m



# =============================================================================
# head: cube-sphere quad cage (left half), sculpted face, eye openings, eyeballs
# =============================================================================
# The head group is designed at a neutral scale and enlarged by HEAD_SCALE about the chin
# (anime proportions, ~6.7 heads tall).
HEAD_SCALE = 1.08
HEAD_PIVOT = np.array([0.0, 0.012, 1.385])

FACE_ROWS = [  # lower face sections (proportions measured from the reference): short lower face, V chin
    (1.419, 0, -0.034, 0.011, 0.014, 0.028, 2.0), (1.426, 0, -0.028, 0.030, 0.030, 0.044, 2.0),
    (1.434, 0, -0.021, 0.042, 0.044, 0.055, 2.1), (1.442, 0, -0.015, 0.051, 0.054, 0.065, 2.2),
    (1.452, 0, -0.010, 0.058, 0.064, 0.073, 2.3), (1.465, 0, -0.007, 0.064, 0.071, 0.080, 2.4),
    (1.482, 0, -0.004, 0.069, 0.076, 0.085, 2.5), (1.500, 0, -0.002, 0.071, 0.077, 0.088, 2.5),
    (1.515, 0, 0.000, 0.069, 0.075, 0.088, 2.4),
]
FACE = Loft(FACE_ROWS)
EYE_C = np.array([0.0380, -0.0655, 1.4878])     # eyeball centre (left eye)
EYE_R = np.array([0.0225, 0.0120, 0.0215])      # eyeball radii: big anime eye, flattened front-to-back
MOUTH_Z = 1.4396


def head_xform(P):
    return (np.asarray(P) - HEAD_PIVOT) * HEAD_SCALE + HEAD_PIVOT


def head_sdf(P):
    Q = P.copy()
    Q[:, 0] = np.abs(Q[:, 0])
    d = smin(sd_ellipsoid(Q, (0, 0.012, 1.505), (0.0745, 0.088, 0.091)), FACE.sdf(Q), 0.03)
    # cheekbones: soft ridge below / outside the eyes
    d = smin(d, sd_ellipsoid(Q, (0.047, -0.054, 1.462), (0.020, 0.015, 0.011)), 0.014)
    # brow ridge
    d = smin(d, sd_ellipsoid(Q, (0.035, -0.072, 1.514), (0.024, 0.007, 0.005)), 0.010)
    # eye sockets carved in around the eyeballs
    d = smax(d, -sd_ellipsoid(Q, (EYE_C[0], -0.0890, EYE_C[2] + 0.001), (0.0215, 0.0115, 0.0175)), 0.010)
    # nose bridge and tip
    nose = sd_round_cone(Q, (0, -0.0800, 1.480), (0, -0.0850, 1.462), 0.0024, 0.0036)
    nose = smin(nose, sd_ellipsoid(Q, (0, -0.0842, 1.4605), (0.0040, 0.0038, 0.0034)), 0.003)
    d = smin(d, nose, 0.006)
    # small round chin, jaw corner
    d = smin(d, sd_ellipsoid(Q, (0, -0.0465, 1.4235), (0.010, 0.007, 0.006)), 0.006)
    neck = sd_round_cone(Q, (0, 0.016, 1.33), (0, 0.012, 1.45), 0.0405, 0.039)
    return smin(d, neck, 0.02)


def eye_outline(theta, s=1.0):
    """Eye opening in the front plane (x, z) relative to the eye centre."""
    c, sn = np.cos(theta), np.sin(theta)
    a = np.where(c > 0, 0.0205, 0.0190) * s        # outer / inner half width
    b = np.where(sn > 0, 0.0172, 0.0128) * s       # upper lid higher than the lower lid
    x = a * c
    z = b * np.sign(sn) * np.abs(sn) ** 0.85 - 0.0012 * c * s   # slight droop at the outer corner
    return x, z


def eyeball_front(x, z, offset=0.0):
    """Point on the front of the (left) eyeball at world x, z, pushed out by `offset`."""
    u = np.clip((x - EYE_C[0]) / EYE_R[0], -0.999, 0.999)
    w = np.clip((z - EYE_C[2]) / EYE_R[2], -0.999, 0.999)
    t = np.sqrt(np.maximum(1 - u * u - w * w, 0.02))
    p = np.stack([x, EYE_C[1] - EYE_R[1] * t, z], -1)
    n = nrm((p - EYE_C) / EYE_R ** 2)
    return p + n * offset


def face_hit(x, z):
    """Front surface of the head at (x, z) (ray along +Y)."""
    O = np.stack([x, np.full_like(x, -0.25), z], -1)
    D = np.tile([0.0, 1.0, 0.0], (len(x), 1))
    lo, hi = np.zeros(len(x)), np.full(len(x), 0.25)
    for _ in range(40):
        m = 0.5 * (lo + hi)
        out = head_sdf(O + D * m[:, None]) > 0
        lo, hi = np.where(out, m, lo), np.where(out, hi, m)
    return O + D * hi[:, None]


def cube_sphere_half(N=12):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=2.0)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=N - 1, use_grid_fill=True)
    for v in bm.verts:
        x, y, z = v.co
        v.co = Vector((x * math.sqrt(max(1 - y * y / 2 - z * z / 2 + y * y * z * z / 3, 0)),
                       y * math.sqrt(max(1 - z * z / 2 - x * x / 2 + z * z * x * x / 3, 0)),
                       z * math.sqrt(max(1 - x * x / 2 - y * y / 2 + x * x * y * y / 3, 0))))
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.x < -1e-5], context='VERTS')
    for v in bm.verts:
        if abs(v.co.x) < 1e-5:
            v.co.x = 0.0
    return bm


def region_loop(faces):
    """Ordered boundary vertex loop of a face region."""
    fs = set(faces)
    bedges = [e for f in faces for e in f.edges if sum(1 for lf in e.link_faces if lf in fs) == 1]
    nxt = {}
    for e in bedges:
        a, b = e.verts
        nxt.setdefault(a, []).append(b)
        nxt.setdefault(b, []).append(a)
    start = bedges[0].verts[0]
    loop, prev, cur = [start], None, start
    while True:
        cand = [v for v in nxt[cur] if v is not prev]
        if not cand or cand[0] is start:
            break
        prev, cur = cur, cand[0]
        loop.append(cur)
    return loop


def sort_by_angle(loop, cx, cz):
    return sorted(loop, key=lambda v: math.atan2(v.co.z - cz, v.co.x - cx))


def build_head_cage():
    bm = cube_sphere_half(12)
    C = np.array([0.0, 0.008, 1.47])
    R = np.array([0.080, 0.096, 0.118])
    for v in bm.verts:
        v.co = Vector(C + np.array(v.co) * R)
    bm.verts.ensure_lookup_table()
    # shape: closest point on the sculpted head, then even the quads out on the surface
    F = [[v.index for v in f.verts] for f in bm.faces]
    P = np.array([v.co[:] for v in bm.verts])
    P = project_to_sdf(head_sdf, P, iters=10)
    P = relax_on_sdf(P, F, head_sdf, iters=30, lam=0.5)
    for v, p in zip(bm.verts, P):
        v.co = Vector(p)
    tag = bm.verts.layers.int.new("tag")          # 1..4 eye loops, 5 mouth
    # ---- eye opening: inset three loops, open the centre, tuck the rim behind the lid ----
    cx, cz = EYE_C[0], EYE_C[2]
    region = [f for f in bm.faces if f.calc_center_median().y < -0.03 and
              ((f.calc_center_median().x - cx) / 0.0235) ** 2 + ((f.calc_center_median().z - cz - 0.0025) / 0.0180) ** 2 < 1.0]
    # inset_region moves the original vertices inward, so the loops are collected afterwards,
    # growing outward from the final region one ring of faces at a time
    rings = []
    for th in (0.004, 0.003, 0.002):
        r = bmesh.ops.inset_region(bm, faces=region, thickness=th, depth=0.0, use_even_offset=True, use_boundary=False)
        rings.append(r["faces"])
    loops = [region_loop(region + rings[2] + rings[1] + rings[0]), region_loop(region + rings[2] + rings[1]),
             region_loop(region + rings[2]), region_loop(region)]
    inner_verts = {v for f in region for v in f.verts} - set(loops[-1])
    bmesh.ops.delete(bm, geom=region, context='FACES_ONLY')
    bmesh.ops.delete(bm, geom=list(inner_verts), context='VERTS')
    rim = loops[3]
    rim_edges = [e for e in bm.edges if e.verts[0] in rim and e.verts[1] in rim and len(e.link_faces) == 1]
    ext = bmesh.ops.extrude_edge_only(bm, edges=rim_edges)
    tuck = [g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)]
    # place the loops: L3 rim hugs the eyeball, L2 gives the lid its thickness,
    # L1 is the lid fold on the socket, L0 stays on the face
    for li, (s, off) in enumerate(((None, None), (0.0, None), (1.07, 0.0022), (0.97, 0.0005))):
        if s is None:
            continue
        lp = loops[li]
        a0 = np.unwrap(np.array([math.atan2(v.co.z - cz, v.co.x - cx) for v in lp]))
        sgn = 1.0 if a0[-1] > a0[0] else -1.0
        uni = a0[0] + sgn * 2 * np.pi * np.arange(len(lp)) / len(lp)
        th = uni + np.mean(a0 - uni)   # evenly spaced, same order and phase as the loop
        x, z = eye_outline(th, s)
        if off is None:
            # lid fold: halfway between the outer boundary L0 and the lid edge, on the face
            L0 = np.array([v.co[:] for v in loops[0]])
            a_l0 = np.arctan2(L0[:, 2] - cz, L0[:, 0] - cx)
            idx = [int(np.argmin(np.abs(np.angle(np.exp(1j * (a_l0 - t)))))) for t in th]
            x2, z2 = eye_outline(th, 1.10)
            x = 0.5 * (L0[idx, 0] - cx) + 0.5 * x2
            z = 0.5 * (L0[idx, 2] - cz) + 0.5 * z2
            pts = face_hit(cx + x, cz + z)
        else:
            pts = eyeball_front(cx + x, cz + z, off)
        for v, p in zip(lp, pts):
            v.co = Vector(p)
            v[tag] = li
    for v in tuck:
        p = np.array(v.co)
        n = nrm((p - EYE_C) / EYE_R ** 2)
        v.co = Vector(p - n * 0.0022)
        v[tag] = 4
    # ---- mouth: two insets on the faces at the mirror line, pressed in to a lip line ----
    mouth = [f for f in bm.faces if abs(f.calc_center_median().z - MOUTH_Z) < 0.006 and f.calc_center_median().y < -0.05
             and f.calc_center_median().x < 0.010]
    for th in (0.0025, 0.0018):
        bmesh.ops.inset_region(bm, faces=mouth, thickness=th, depth=0.0, use_even_offset=True, use_boundary=False)
    mloop = region_loop(mouth)
    for f in mouth:
        f.material_index = 1
        for v in f.verts:
            v.co.z = MOUTH_Z + (v.co.z - MOUTH_Z) * 0.15
            v.co.y += 0.0018
            v[tag] = 5
    for v in mloop:
        v.co.z = MOUTH_Z + (v.co.z - MOUTH_Z) * 0.5
        v[tag] = 5
    for v in bm.verts:
        if abs(v.co.x) < 2e-5:
            v.co.x = 0.0
    # crease along the nose bridge (centre line)
    cl = bm.edges.layers.float.get('crease_edge') or bm.edges.layers.float.new('crease_edge')
    for e in bm.edges:
        a, b = e.verts
        if a.co.x == 0.0 and b.co.x == 0.0 and min(a.co.y, b.co.y) < -0.075 and 1.462 < (a.co.z + b.co.z) / 2 < 1.482:
            e[cl] = 0.25
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm, loops


def make_head(coll, m_skin, m_lips):
    bm, loops = build_head_cage()
    me = bpy.data.meshes.new("Head")
    bm.to_mesh(me)
    bm.free()
    me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
    ob = bpy.data.objects.new("Head", me)
    coll.objects.link(ob)
    me.materials.append(m_skin)
    me.materials.append(m_lips)
    m = ob.modifiers.new("Mirror", 'MIRROR')
    m.use_clip, m.use_mirror_merge, m.merge_threshold = True, True, 1e-4
    s = ob.modifiers.new("Subdivision", 'SUBSURF')
    s.levels = s.render_levels = SUBSURF_LEVELS
    # fit the subdivided face to the sculpt (eye and mouth loops keep their placement)
    tags = np.zeros(len(me.vertices), dtype=np.int64)
    me.attributes["tag"].data.foreach_get("value", tags)
    fit_cage(ob, head_sdf, iters=4, movable=tags == 0, max_step=0.003)
    set_co(ob, head_xform(get_co(ob)))
    return ob


def make_eyes(coll, m_eye):
    """Separate UV-sphere eyeballs (flattened); object coordinates drive the iris shader."""
    eyes = []
    for side in (1, -1):
        me = bpy.data.meshes.new("Eyeball")
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.0)
        # pole toward the front so the iris is drawn around it
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, 'X'))
        bm.to_mesh(me)
        bm.free()
        me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
        ob = bpy.data.objects.new("Eye_L" if side > 0 else "Eye_R", me)
        coll.objects.link(ob)
        c = EYE_C * np.array([side, 1, 1])
        ob.location = Vector(head_xform(c))
        ob.scale = Vector(EYE_R * HEAD_SCALE)
        ob.rotation_euler = (0, 0, math.radians(-14 * side))
        me.materials.append(m_eye)
        eyes.append(ob)
    return eyes


def make_lashes_brows(coll, head, m_lash):
    """Upper lash (with a few flicks at the outer corner), lower lash hint and brows."""
    V, F = [], []

    def strip(rows):
        base = len(V)
        nr, nc = len(rows), len(rows[0])
        for r in rows:
            V.extend(r)
        for i in range(nr - 1):
            for j in range(nc - 1):
                F.append([base + i * nc + j, base + i * nc + j + 1, base + (i + 1) * nc + j + 1, base + (i + 1) * nc + j])
    cx, cz = EYE_C[0], EYE_C[2]
    # lash roots sit on the *subdivided* lid edge (loop 2), so they never sink into the lid
    tags = np.zeros(len(head.data.vertices), dtype=np.int64)
    head.data.attributes["tag"].data.foreach_get("value", tags)
    Lp = (limit_positions(head) - HEAD_PIVOT) / HEAD_SCALE + HEAD_PIVOT
    lid = Lp[(tags == 2) & (Lp[:, 0] > 0)]
    ang = np.arctan2(lid[:, 2] - cz, lid[:, 0] - cx)
    order = np.argsort(-ang)
    lid, ang = lid[order], ang[order]
    upper = lid[(ang > -0.25) & (ang < math.pi + 0.25)]
    upper = upper[np.argsort(upper[:, 0])]
    root, _ = resample(catmull_rom(upper, 8), 16)
    root = root + nrm(root - EYE_C) * 0.0010
    t = np.linspace(0, 1, len(root))[:, None]
    up = nrm(np.array([0.0, -0.45, 1.0]))
    L = 0.0026 + 0.0022 * t
    tip = root + up * L + np.array([1.0, 0.0, 0.0]) * 0.0030 * t ** 3 + np.array([0.0, 0.0, -1.0]) * 0.0008 * t ** 2
    strip([list(root), list(root + (tip - root) * 0.5), list(tip)])
    # lower lash: short line on the outer half
    lower = lid[(ang < -0.3) & (ang > -1.6)]
    lower = lower[np.argsort(lower[:, 0])]
    if len(lower) >= 2:
        r2, _ = resample(catmull_rom(lower, 6), 8)
        r2 = r2 + nrm(r2 - EYE_C) * 0.0005
        strip([list(r2), list(r2 + np.array([0.0004, -0.0006, -0.0010]))])
    # brows floating just above the skin
    bx = np.linspace(cx - 0.016, cx + 0.019, 10)
    bz = cz + 0.0340 + 0.0045 * np.sin(np.linspace(0.2, 2.9, 10)) - 0.004 * ((bx - cx) / 0.019) ** 2
    lo = face_hit(bx, bz - 0.0011) + np.array([0, -0.0007, 0])
    hi_ = face_hit(bx, bz + 0.0011 * np.linspace(1.0, 0.3, 10)) + np.array([0, -0.0007, 0])
    strip([list(lo), list(hi_)])
    V = [head_xform(p) for p in V]
    ob = mesh_object("Lashes", V, F, coll, mirror=True, subsurf=1)
    ob.modifiers["Mirror"].use_clip = False
    ob.modifiers["Mirror"].use_mirror_merge = False
    sol = ob.modifiers.new("Solidify", 'SOLIDIFY')
    sol.thickness = 0.0011
    bpy.context.view_layer.update()
    ob.modifiers.move(ob.modifiers.find("Solidify"), 1)
    ob.data.materials.append(m_lash)
    return ob



# =============================================================================
# materials (Principled BSDF)
# =============================================================================


class NT:
    """Tiny node-tree builder."""

    def __init__(self, mat):
        self.nt = mat.node_tree
        self.x = -1400

    def n(self, kind, **inputs):
        node = self.nt.nodes.new(kind)
        node.location = (self.x, len(self.nt.nodes) * -40)
        self.x += 60
        for k, v in inputs.items():
            if k.startswith('_'):
                setattr(node, k[1:], v)
            else:
                self.set(node.inputs[k], v)
        return node

    def set(self, sock, v):
        if isinstance(v, bpy.types.NodeSocket):
            self.nt.links.new(v, sock)
        elif isinstance(v, (tuple, list)) and len(v) == 3 and sock.type == 'RGBA':
            sock.default_value = (*v, 1.0)
        else:
            sock.default_value = v

    def math(self, op, a, b=None, clamp=False):
        m = self.n('ShaderNodeMath', _operation=op, _use_clamp=clamp)
        self.set(m.inputs[0], a)
        if b is not None:
            self.set(m.inputs[1], b)
        return m.outputs[0]

    def mix(self, fac, a, b):
        m = self.n('ShaderNodeMix', _data_type='RGBA')
        self.set(m.inputs['Factor'], fac)
        self.set(m.inputs[6], a)
        self.set(m.inputs[7], b)
        return m.outputs[2]

    def ramp(self, fac, stops):
        r = self.n('ShaderNodeValToRGB')
        self.set(r.inputs[0], fac)
        el = r.color_ramp.elements
        while len(el) < len(stops):
            el.new(0.5)
        for e, (pos, col) in zip(el, stops):
            e.position = pos
            e.color = (*col, 1.0)
        return r.outputs[0]

    def band(self, x, lo, hi, soft=0.01):
        """1 inside [lo, hi] with soft edges."""
        a = self.n('ShaderNodeMapRange', _interpolation_type='SMOOTHSTEP', **{'From Min': lo - soft, 'From Max': lo + soft})
        self.set(a.inputs['Value'], x)
        b = self.n('ShaderNodeMapRange', _interpolation_type='SMOOTHSTEP', **{'From Min': hi + soft, 'From Max': hi - soft})
        self.set(b.inputs['Value'], x)
        return self.math('MULTIPLY', a.outputs[0], b.outputs[0])

    def smooth(self, x, lo, hi):
        a = self.n('ShaderNodeMapRange', _interpolation_type='SMOOTHSTEP', **{'From Min': lo, 'From Max': hi})
        self.set(a.inputs['Value'], x)
        return a.outputs[0]


def new_principled(name, color, rough=0.5, metallic=0.0):
    m = bpy.data.materials.new(name)
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1.0)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metallic
    return m, p


def skin_material(name="M_Skin", color=(0.93, 0.74, 0.66)):
    m, p = new_principled(name, color, 0.5)
    p.inputs["Subsurface Weight"].default_value = 0.18
    p.inputs["Subsurface Radius"].default_value = (1.0, 0.4, 0.2)
    p.inputs["Subsurface Scale"].default_value = 0.012
    p.inputs["Specular IOR Level"].default_value = 0.45
    # blush on the cheeks (world position, both sides)
    b = NT(m)
    geo = b.n('ShaderNodeNewGeometry')
    sep = b.n('ShaderNodeSeparateXYZ', Vector=geo.outputs['Position'])
    ax = b.math('ABSOLUTE', sep.outputs[0])
    comb = b.n('ShaderNodeCombineXYZ', X=ax, Y=sep.outputs[1], Z=sep.outputs[2])
    cheek = head_xform(np.array([0.041, -0.070, 1.446]))
    dist = b.n('ShaderNodeVectorMath', _operation='DISTANCE', Vector=comb.outputs[0])
    dist.inputs[1].default_value = tuple(cheek)
    blush = b.n('ShaderNodeMapRange', _interpolation_type='SMOOTHSTEP',
                **{'From Min': 0.020, 'From Max': 0.004, 'To Max': 0.45})
    b.set(blush.inputs['Value'], dist.outputs['Value'])
    col = b.mix(blush.outputs[0], color, (0.95, 0.48, 0.50))
    b.set(p.inputs["Base Color"], col)
    return m


def eye_material():
    """Anime iris on a glossy eyeball: violet top -> pink -> amber bottom, pupil, highlights.
    Object coordinates of the unit eyeball: front pole at -Y."""
    m, p = new_principled("M_Eye", (0.9, 0.9, 0.95), 0.05)
    p.inputs["Specular IOR Level"].default_value = 0.7
    p.inputs["Coat Weight"].default_value = 0.6
    p.inputs["Coat Roughness"].default_value = 0.03
    b = NT(m)
    tc = b.n('ShaderNodeTexCoord')
    sep = b.n('ShaderNodeSeparateXYZ', Vector=tc.outputs['Object'])
    x, y, z = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    r = b.n('ShaderNodeVectorMath', _operation='LENGTH')
    zs = b.math('MULTIPLY', z, 0.82)                     # iris is a tall ellipse
    comb = b.n('ShaderNodeCombineXYZ', X=x, Z=zs)
    b.set(r.inputs[0], comb.outputs[0])
    rr = r.outputs['Value']
    front = b.smooth(y, 0.1, -0.2)                       # only the front hemisphere
    iris = b.math('MULTIPLY', b.smooth(rr, 0.58, 0.54), front)
    tz = b.math('ADD', b.math('MULTIPLY', z, 0.75), 0.5)   # 0 bottom .. 1 top of the iris
    icol = b.ramp(tz, [(0.0, (1.0, 0.50, 0.22)), (0.28, (0.92, 0.36, 0.48)),
                       (0.55, (0.52, 0.16, 0.62)), (0.82, (0.20, 0.05, 0.32)), (1.0, (0.08, 0.02, 0.14))])
    limbus = b.math('MULTIPLY', b.band(rr, 0.49, 0.58, 0.02), 0.6)
    icol = b.mix(limbus, icol, (0.06, 0.01, 0.10))
    pupil = b.math('MULTIPLY', b.smooth(rr, 0.22, 0.18), front)
    icol = b.mix(pupil, icol, (0.04, 0.0, 0.06))
    col = b.mix(iris, (0.92, 0.92, 0.97), icol)
    # sclera shadow under the upper lid
    shade = b.math('MULTIPLY', b.smooth(z, 0.25, 0.75), b.math('SUBTRACT', 1.0, iris))
    col = b.mix(b.math('MULTIPLY', shade, 0.5), col, (0.62, 0.62, 0.78))
    b.set(p.inputs["Base Color"], col)
    # painted catch-lights (anime style) in addition to the real reflections
    def spot(cx, cz, rad):
        d = b.n('ShaderNodeVectorMath', _operation='DISTANCE', Vector=comb.outputs[0])
        d.inputs[1].default_value = (cx, 0.0, cz)
        return b.math('MULTIPLY', b.smooth(d.outputs['Value'], rad, rad * 0.7), front)
    hl = b.math("MAXIMUM", spot(-0.33, 0.30, 0.12), spot(0.30, -0.34, 0.055))
    glow = b.math('ADD', b.math('MULTIPLY', hl, 4.0), b.math('MULTIPLY', iris, 0.25))
    b.set(p.inputs["Emission Strength"], glow)
    b.set(p.inputs["Emission Color"], b.mix(hl, icol, (1.0, 1.0, 1.0)))
    return m


def simple(name, color, rough=0.5, metallic=0.0, sheen=0.0, coat=0.0):
    m, p = new_principled(name, color, rough, metallic)
    p.inputs["Sheen Weight"].default_value = sheen
    p.inputs["Coat Weight"].default_value = coat
    return m


def make_materials():
    mats = {
        'skin': skin_material(),
        'lips': skin_material("M_Lips", (0.90, 0.52, 0.52)),
        'eye': eye_material(),
        'lash': simple("M_Lash", (0.035, 0.018, 0.03), 0.45),
        'glove': simple("M_Glove", (0.012, 0.010, 0.014), 0.32, coat=0.35),
        'hair': hair_material(),
        'horn': horn_material(),
    }
    return mats



# =============================================================================
# hair: extruded strand clumps lifted off the scalp in layers (all quads)
# =============================================================================
HC = np.array([0.0, 0.010, 1.50])        # head centre (design space) for directions
HAIR_LEN = 1.25                           # metres of strand mapped to U = 0..1 (colour gradient)
TIE = np.array([0.0, 0.094, 1.392])       # ponytail tie at the nape
RNG = np.random.default_rng(7)


def head_dir(phi, lam):
    p, l = math.radians(phi), math.radians(lam)
    return np.array([math.sin(p) * math.cos(l), -math.cos(p) * math.cos(l), math.sin(l)])


def head_point(phi, lam, offset=0.0):
    """Point `offset` above the scalp along the ray (azimuth phi from the front, elevation lam)."""
    d = head_dir(phi, lam)
    lo, hi = 0.0, 0.25
    for _ in range(40):
        m = 0.5 * (lo + hi)
        if head_sdf((HC + d * m)[None])[0] - offset < 0:
            lo = m
        else:
            hi = m
    return HC + d * 0.5 * (lo + hi)


def body_clear(P, clearance):
    P = np.atleast_2d(np.array(P, dtype=np.float64))
    for _ in range(12):
        d = body_sdf(P)
        bad = d < clearance
        if not bad.any():
            break
        P[bad] += nrm(sdf_grad(body_sdf, P[bad])) * (clearance - d[bad])[:, None]
    return P


def outward(C):
    return nrm(C - HC)


class Strands:
    """Accumulates strand clumps: rings of n_cross vertices, quad caps at both ends."""

    def __init__(self):
        self.V, self.F, self.UV = [], [], []

    def _face(self, idx, uv):
        self.F.append(idx)
        self.UV.append(uv)

    def add(self, ctrl, w0, thick=0.22, hint=outward, n_len=20, n_cross=8, bend=0.20, tip=0.7,
            w_profile=None, twist=0.0, root_u=0.0, round_=False):
        C, L = resample(catmull_rom(np.asarray(ctrl, dtype=np.float64), 12), n_len + 1)
        s = np.linspace(0, 1, n_len + 1)
        T = tangents(C)
        S = nrm(np.cross(T, hint(C)))
        for i in range(1, len(S)):
            if S[i] @ S[i - 1] < 0:
                S[i] = -S[i]
        S = nrm(S - np.einsum('ij,ij->i', S, T)[:, None] * T)
        N = np.cross(S, T)
        if twist:
            for i in range(len(C)):
                R = rot_axis(T[i], twist * s[i])
                S[i], N[i] = R @ S[i], R @ N[i]
        w = w0 * ((1 - s) ** tip * (0.80 + 0.80 * s) if w_profile is None else w_profile(s))
        w = np.maximum(w, 0.0012)                 # keep a tiny ring at the tip (quad cap)
        t = np.maximum(w * thick, 0.0008)
        ang = 2 * np.pi * np.arange(n_cross) / n_cross
        ca, sa = np.cos(ang), np.sin(ang)
        arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))])
        uu = root_u + arc / HAIR_LEN
        rings = []
        for i in range(n_len + 1):
            x = 0.5 * w[i] * ca
            y = 0.5 * t[i] * sa - (0.0 if round_ else bend * w[i] * ca ** 2)
            base = len(self.V)
            self.V.extend(list(C[i] + np.outer(x, S[i]) + np.outer(y, N[i])))
            rings.append(list(range(base, base + n_cross)))
        for i in range(n_len):
            a, b = rings[i], rings[i + 1]
            for k in range(n_cross):
                k1 = (k + 1) % n_cross
                v0, v1 = k / n_cross, (k + 1) / n_cross
                self._face([a[k], a[k1], b[k1], b[k]], [(uu[i], v0), (uu[i], v1), (uu[i + 1], v1), (uu[i + 1], v0)])
        Vnp = np.array(self.V)
        for ring, u, flip in ((rings[0], uu[0], True), (rings[-1], uu[-1], False)):
            extra = []
            caps = cap_ring(ring, Vnp, extra)
            base = len(self.V)
            self.V.extend(extra)
            assert base == len(Vnp)
            for f in caps:
                self._face(f[::-1] if flip else f, [(u, 0.5)] * 4)
            Vnp = np.array(self.V)
        return C

    def build(self, name, coll, material, xform=None):
        V = np.array(self.V)
        if xform is not None:
            V = xform(V)
        me = bpy.data.meshes.new(name)
        me.from_pydata(V.tolist(), [], self.F)
        me.update()
        uvl = me.uv_layers.new(name="UVMap")
        uvl.data.foreach_set("uv", np.array([c for f in self.UV for c in f], np.float32).ravel())
        me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
        ob = bpy.data.objects.new(name, me)
        coll.objects.link(ob)
        me.materials.append(material)
        recalc_normals(ob)
        s = ob.modifiers.new("Subdivision", 'SUBSURF')
        s.levels, s.render_levels = 1, 2
        return ob


def hair_bangs(hs):
    # (azimuth, tip elevation, width, sideways sway) -- one thin strand falls between the eyes
    spec = [(-80, -54, 0.038, -4), (-67, -38, 0.044, -3), (-53, -26, 0.048, -3), (-39, -9, 0.048, 2),
            (-25, -6, 0.046, 0), (-12, -5, 0.044, 3), (0, -22, 0.026, -2), (12, -4, 0.044, 2),
            (25, -7, 0.046, -2), (39, -8, 0.048, 2), (53, -25, 0.048, 3), (67, -37, 0.044, 3),
            (80, -52, 0.038, 4), (88, -62, 0.034, 4)]
    for phi in (-62, -36, -12, 12, 36, 62):           # under layer closing the gaps
        hs.add([head_point(phi * 0.30, 62, 0.004), head_point(phi * 0.62, 44, 0.015),
                head_point(phi * 0.90, 22, 0.016), head_point(phi * 0.98, 8, 0.013), head_point(phi, -2, 0.010)],
               0.040, thick=0.22, n_len=16)
    for phi, lam_tip, w, sway in spec:                # top layer, lifted further off the forehead
        hs.add([head_point(phi * 0.30, 64, 0.006), head_point(phi * 0.62, 46, 0.022),
                head_point(phi * 0.90, 24, 0.023), head_point(phi * 0.98, 6, 0.019),
                head_point(phi + sway, (6 + lam_tip) * 0.5, 0.014),
                head_point(phi + sway * 1.6, lam_tip, 0.011 + 0.004 * abs(phi) / 90.0)],
               w, thick=0.24, n_len=20, tip=0.75)


def hair_side_locks(hs):
    """Wavy locks in front of the ears, curling outward at the chest."""
    for side in (1, -1):
        for dphi, dz, w, ex in ((0, 0.0, 0.036, 0.0), (8, 0.014, 0.029, 0.012), (-6, -0.008, 0.026, -0.010)):
            pts = [head_point(side * (84 + dphi), 34, 0.006), head_point(side * (88 + dphi), 4, 0.020),
                   np.array([side * (0.078 + ex * 0.3), -0.030, 1.425 + dz]),
                   np.array([side * (0.100 + ex * 0.4), -0.044, 1.365 + dz]),
                   np.array([side * (0.086 + ex * 0.6), -0.070, 1.300 + dz]),
                   np.array([side * (0.106 + ex), -0.096, 1.246 + dz]),
                   np.array([side * (0.108 + ex), -0.110, 1.208 + dz]),
                   np.array([side * (0.120 + ex), -0.106, 1.196 + dz])]
            pts[2:] = list(body_clear(np.array(pts[2:]), 0.016))

            def hint(C, side=side):
                f = smoothstep(1.45, 1.38, C[:, 2])[:, None]
                return nrm(nrm(C - HC) * (1 - f) + np.array([side * 0.6, -0.8, 0.0]) * f)
            hs.add(pts, w, thick=0.24, bend=0.16, tip=0.55, n_len=28, hint=hint, twist=0.4 * side, root_u=0.22 / HAIR_LEN)


def hair_sides(hs):
    """Volume over the ears (outer layer floats ~4 cm off the scalp)."""
    for side in (1, -1):
        for phi, lam0, lam1, w, drop in ((98, 50, -42, 0.036, 0.00), (108, 48, -46, 0.040, 0.012),
                                         (120, 46, -40, 0.040, 0.0), (134, 44, -34, 0.038, 0.0),
                                         (92, 42, -56, 0.030, 0.02), (146, 40, -28, 0.036, 0.0)):
            hs.add([head_point(side * phi * 0.8, lam0 + 15, 0.010), head_point(side * phi, lam0, 0.032),
                    head_point(side * phi, 10, 0.045), head_point(side * (phi + 3), (10 + lam1) / 2, 0.047),
                    head_point(side * (phi + 6), lam1, 0.040) + np.array([0, 0, -drop])], w, thick=0.24, n_len=20)


def hair_back(hs):
    """Inner layer on the back of the head, gathered into the tie."""
    for phi in (-160, -138, -116, -96, 96, 116, 138, 160, 180):
        for lam_root, wmul in ((68, 1.0), (40, 1.0), (14, 0.9)):
            if lam_root == 14 and abs(phi) < 110:
                continue
            side = 0.0 if phi == 180 else (1.0 if phi > 0 else -1.0)
            root = head_point(phi * (0.85 if lam_root > 60 else 1.0), lam_root, 0.007)
            mid = head_point(phi, max(lam_root - 25, -10), 0.032)
            end = TIE + np.array([side * 0.012, 0.004, 0.004])
            near = 0.5 * (mid + end) + nrm(0.5 * (mid + end) - HC) * 0.012
            hs.add([root, mid, near, end], 0.040 * wmul, thick=0.20, bend=0.22, n_len=16,
                   w_profile=lambda s: 1.0 - 0.55 * s)
    # outer crown layer: big clumps arching over the top, standing off the scalp
    for phi in (-150, -120, -90, -60, -30, 30, 60, 90, 120, 150):
        hs.add([head_point(phi * 0.3, 80, 0.008), head_point(phi * 0.7, 62, 0.034),
                head_point(phi, 40, 0.040), head_point(phi * 1.03, 18, 0.036)],
               0.050, thick=0.22, n_len=16, tip=0.9)


def ponytail_frame(z):
    zs = [1.392, 1.33, 1.24, 1.14, 1.04, 0.95, 0.86, 0.79]
    ys = [0.094, 0.110, 0.122, 0.128, 0.140, 0.180, 0.240, 0.300]
    wx = [0.016, 0.050, 0.066, 0.072, 0.074, 0.078, 0.088, 0.104]
    wy = [0.012, 0.024, 0.029, 0.031, 0.032, 0.034, 0.038, 0.046]
    zz = np.asarray(z, dtype=np.float64)
    f = lambda arr: np.interp(-zz, -np.asarray(zs), arr)
    return f(ys), f(wx), f(wy)


def hair_ponytail(hs, n=30):
    zs = np.linspace(1.392, 0.79, 14)
    for i in range(n):
        a = 2 * np.pi * (i + 0.5 * (i % 2)) / n + RNG.uniform(-0.15, 0.15)
        rad = 0.55 + 0.45 * ((i * 7) % n) / n
        ox, oy = math.cos(a) * rad, math.sin(a) * rad
        zend = 1.392 - (1.392 - 0.79) * (0.70 + 0.30 * RNG.random())
        zz = zs[zs >= zend - 1e-6]
        if zz[-1] > zend + 0.01:
            zz = np.append(zz, zend)
        cy, wx, wy = ponytail_frame(zz)
        t = (1.392 - zz) / (1.392 - 0.79)
        wave = 0.010 * np.sin(RNG.uniform(0, 2 * np.pi) + t * 7.0) * smoothstep(0.1, 0.5, t)
        flare = 1.0 + 0.6 * smoothstep(0.75, 1.0, t)
        ctrl = np.stack([ox * wx * flare + wave, cy + oy * wy * flare, zz], 1)
        ctrl[0] = TIE + np.array([ox * 0.012, oy * 0.008, 0.0])
        ctrl = body_clear(ctrl, 0.012)

        def hint(C):
            cy_, _, _ = ponytail_frame(C[:, 2])
            return nrm(np.stack([C[:, 0], C[:, 1] - cy_ + 0.03, np.zeros(len(C))], 1))
        hs.add(ctrl, 0.036 + 0.010 * RNG.random(), thick=0.20, bend=0.18, n_len=26, hint=hint, root_u=0.20 / HAIR_LEN,
               w_profile=lambda s: (0.45 + 0.75 * smoothstep(0.0, 0.25, s)) * (1 - s) ** 0.8,
               twist=RNG.uniform(-0.6, 0.6))


def hair_ahoge(hs):
    root = head_point(0, 86, 0.0)
    xh = lambda C: np.tile(np.array([1.0, 0.0, 0.0]), (len(C), 1))
    hs.add([root, root + [0.000, -0.004, 0.022], root + [0.004, 0.010, 0.042], root + [0.008, 0.032, 0.048],
            root + [0.010, 0.050, 0.040]], 0.012, thick=0.35, bend=0.10, tip=0.8, n_len=16, hint=xh)


def make_scalp(coll, material, offset=0.006):
    """Base shell under the strands: the head shape lifted off the skin, cut at the hairline."""
    bm = cube_sphere_half(10)
    C, R = np.array([0.0, 0.008, 1.50]), np.array([0.09, 0.10, 0.10])
    for v in bm.verts:
        v.co = Vector(C + np.array(v.co) * R)
    f = lambda Q: head_sdf(Q) - offset
    F = [[v.index for v in fc.verts] for fc in bm.faces]
    P = relax_on_sdf(project_to_sdf(f, np.array([v.co[:] for v in bm.verts]), iters=10), F, f, iters=15)
    for v, p in zip(bm.verts, P):
        v.co = Vector(head_xform(p))
    def hairline(c):
        rel = np.array(c) - head_xform(HC)
        a = abs(math.degrees(math.atan2(rel[0], -rel[1])))
        return c.z - head_xform(np.array([0, 0, np.interp(a, [0, 30, 60, 90, 120, 150, 180],
                                                         [1.565, 1.556, 1.530, 1.470, 1.420, 1.395, 1.385])]))[2]
    bmesh.ops.delete(bm, geom=[fc for fc in bm.faces if hairline(fc.calc_center_median()) < 0], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    me = bpy.data.meshes.new("HairScalp")
    bm.to_mesh(me)
    bm.free()
    me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
    ob = bpy.data.objects.new("HairScalp", me)
    coll.objects.link(ob)
    me.materials.append(material)
    m = ob.modifiers.new("Mirror", 'MIRROR')
    m.use_clip, m.use_mirror_merge = True, True
    s = ob.modifiers.new("Subdivision", 'SUBSURF')
    s.levels = s.render_levels = SUBSURF_LEVELS
    return ob


def make_hair(coll, m_hair):
    hs = Strands()
    hair_bangs(hs)
    hair_side_locks(hs)
    hair_sides(hs)
    hair_back(hs)
    hair_ponytail(hs)
    hair_ahoge(hs)
    hair = hs.build("Hair", coll, m_hair, xform=head_xform)
    scalp = make_scalp(coll, m_hair)
    return hair, scalp


def horn_path(s):
    return np.array([[s * 0.040, -0.024, 1.570], [s * 0.056, 0.004, 1.595], [s * 0.074, 0.044, 1.606],
                     [s * 0.095, 0.086, 1.597], [s * 0.112, 0.116, 1.574], [s * 0.124, 0.130, 1.543],
                     [s * 0.128, 0.128, 1.512], [s * 0.126, 0.118, 1.492]])


def make_horns(coll, m_horn):
    hs = Strands()
    up = lambda C: np.tile(np.array([0.0, 0.0, 1.0]), (len(C), 1))
    for s in (1, -1):
        hs.add(horn_path(s), 0.036, thick=1.05, n_len=24, n_cross=12, hint=up, round_=True,
               w_profile=lambda t: (1.0 - t) ** 0.85 * (1.0 + 0.15 * np.sin(np.pi * t)), twist=-0.5 * s)
    ob = hs.build("Horns", coll, m_horn, xform=head_xform)
    # horn UVs: U along the horn (0..1 per horn)
    return ob


def hair_material():
    """Light lavender-blue at the roots to deep blue at the long tips; anisotropic sheen along the clumps."""
    m, p = new_principled("M_Hair", (0.55, 0.65, 0.92), 0.38)
    p.inputs["Anisotropic"].default_value = 0.55
    p.inputs["Sheen Weight"].default_value = 0.25
    p.inputs["Sheen Tint"].default_value = (0.85, 0.9, 1.0, 1.0)
    p.inputs["Specular IOR Level"].default_value = 0.55
    b = NT(m)
    uv = b.n('ShaderNodeUVMap', _uv_map="UVMap")
    sep = b.n('ShaderNodeSeparateXYZ', Vector=uv.outputs[0])
    col = b.ramp(sep.outputs[0], [(0.0, (0.46, 0.58, 0.88)), (0.36, (0.44, 0.56, 0.88)), (0.50, (0.28, 0.40, 0.84)),
                                  (0.64, (0.10, 0.18, 0.70)), (0.80, (0.05, 0.09, 0.52))])
    # darker inner face of each clump (v in 0.5..1 is the side facing the head)
    inner = b.band(sep.outputs[1], 0.55, 0.95, 0.08)
    col = b.mix(b.math('MULTIPLY', inner, 0.35), col, (0.10, 0.14, 0.35))
    b.set(p.inputs["Base Color"], col)
    tg = b.n('ShaderNodeTangent', _direction_type='UV_MAP', _uv_map="UVMap")
    b.set(p.inputs["Tangent"], tg.outputs[0])
    return m


def horn_material():
    m, p = new_principled("M_Horn", (0.02, 0.012, 0.015), 0.28)
    p.inputs["Coat Weight"].default_value = 0.4
    b = NT(m)
    uv = b.n('ShaderNodeUVMap', _uv_map="UVMap")
    sep = b.n('ShaderNodeSeparateXYZ', Vector=uv.outputs[0])
    red = b.math('MAXIMUM', b.band(sep.outputs[1], 0.02, 0.16, 0.02), b.band(sep.outputs[1], 0.52, 0.66, 0.02))
    b.set(p.inputs["Base Color"], b.mix(red, (0.02, 0.012, 0.015), (0.42, 0.02, 0.04)))
    return m


def build_stage(coll, stage):
    out = {}
    mats = make_materials() if 'make_materials' in globals() else None
    if mats is None:
        mats = {k: placeholder_material("tmp_" + k, c) for k, c in
                (('skin', (0.9, 0.72, 0.66)), ('lips', (0.85, 0.45, 0.48)), ('eye', (0.6, 0.5, 0.8)),
                 ('lash', (0.05, 0.03, 0.04)), ('glove', (0.02, 0.02, 0.02)))}
    if stage in ('body', 'all'):
        out['body'], _ = make_body(coll)
        out['body'].data.materials.append(mats['skin'])
    if stage in ('head', 'all'):
        out['head'] = make_head(coll, mats['skin'], mats['lips'])
        out['eyes'] = make_eyes(coll, mats['eye'])
        out['lashes'] = make_lashes_brows(coll, out['head'], mats['lash'])
        out['hair'], out['scalp'] = make_hair(coll, mats['hair'])
        out['horns'] = make_horns(coll, mats['horn'])
    return out


def main():
    coll = get_collection()
    build_stage(coll, 'all')
    setup_scene(coll)


if __name__ == "__main__":
    main()
