"""Outfit pieces. Most pieces conform to the body SDF via ray casts.

Pieces: white front panel, back panel, tail flaps, detached sleeves, high collar + bell,
under-bust cord + ornament, thigh straps + pendants, shoes, waist tassels + vision gem,
hair-tie ornament.
"""
import math

import numpy as np

from common import (nrm, smoothstep, rot_axis, rot_x, rot_z, smin, smax, sd_ellipsoid,
                    sd_round_cone, sd_round_box, sd_capsule, sdf_grad, ray_hit_sdf,
                    catmull_rom, resample_polyline, polyline_tangents, MeshBuilder,
                    surface_nets, recalc_normals, smart_uv, solidify_apply, project_to_sdf)
import body as B
import head as Hd

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def head_sdf_scaled(P):
    Q = (P - Hd.HEAD_PIVOT) / Hd.HEAD_SCALE + Hd.HEAD_PIVOT
    return Hd.head_sdf(Q) * Hd.HEAD_SCALE


def neck_sdf(P):
    return np.minimum(B.trunk_sdf(P), head_sdf_scaled(P))


def ray_exit(sdf, O, D, tmax=0.30, iters=36):
    """From a point inside (sdf<0), the first exit point along D (vectorized)."""
    O = np.atleast_2d(O).astype(np.float64)
    D = nrm(np.atleast_2d(D))
    lo = np.zeros(len(O))
    hi = np.full(len(O), tmax)
    for _ in range(iters):
        m = 0.5 * (lo + hi)
        inside = sdf(O + D * m[:, None]) < 0
        lo = np.where(inside, m, lo)
        hi = np.where(inside, hi, m)
    return O + D * hi[:, None]


def offset_along_normal(sdf, P, off):
    n = nrm(sdf_grad(sdf, P))
    return P + n * off, n


def upper_envelope(xs, ys):
    """Concave (upper) hull of points sorted by x, evaluated at xs."""
    ok = np.isfinite(ys)
    px, py = xs[ok], ys[ok]
    hull = []
    for p in zip(px, py):
        while len(hull) >= 2:
            (x1, y1), (x2, y2) = hull[-2], hull[-1]
            if (x2 - x1) * (p[1] - y1) - (y2 - y1) * (p[0] - x1) >= 0:
                hull.pop()
            else:
                break
        hull.append(p)
    hx, hy = np.array(hull).T
    return np.interp(xs, hx, hy)


class ProfileTable:
    """Height field y(x, z) of the body seen from the front (side=-1) or back (side=+1),
    bridged across concavities, optionally 'hanging' straight down below z_hang."""

    def __init__(self, sdf, xr, zr, side=-1, dx=0.004, dz=0.004, z_hang=None):
        self.xs = np.arange(xr[0], xr[1] + dx * 0.5, dx)
        self.zs = np.arange(zr[0], zr[1] + dz * 0.5, dz)
        Y = np.zeros((len(self.zs), len(self.xs)))
        for i, z in enumerate(self.zs):
            O = np.stack([self.xs, np.full_like(self.xs, 0.32 * side), np.full_like(self.xs, z)], 1)
            D = np.tile([0.0, -float(side), 0.0], (len(self.xs), 1))
            t = ray_hit_sdf(sdf, O, D, 0.0, 0.64, steps=150)
            y = 0.32 * side - side * t
            # outer envelope (front: smallest y, back: largest y) bridging concavities
            Y[i] = side * upper_envelope(self.xs, side * y)
        if z_hang is not None:
            order = np.argsort(-self.zs)
            prev = None
            for i in order:
                if self.zs[i] > z_hang:
                    continue
                if prev is not None:
                    Y[i] = np.minimum(Y[i], Y[prev]) if side < 0 else np.maximum(Y[i], Y[prev])
                prev = i
        self.Y = Y
        self.side = side

    def y(self, x, z):
        x = np.asarray(x, np.float64)
        z = np.asarray(z, np.float64)
        fx = np.clip((x - self.xs[0]) / (self.xs[1] - self.xs[0]), 0, len(self.xs) - 1.001)
        fz = np.clip((z - self.zs[0]) / (self.zs[1] - self.zs[0]), 0, len(self.zs) - 1.001)
        ix, iz = fx.astype(int), fz.astype(int)
        tx, tz = fx - ix, fz - iz
        Y = self.Y
        a = Y[iz, ix] * (1 - tx) + Y[iz, ix + 1] * tx
        b = Y[iz + 1, ix] * (1 - tx) + Y[iz + 1, ix + 1] * tx
        return a * (1 - tz) + b * tz

    def slope(self, x, z, h=0.003):
        return (self.y(x + h, z) - self.y(x - h, z)) / (2 * h)


def grid_object(name, P, uv, material, collection, closed_u=False, flip=False):
    """P: (rows, cols, 3) grid, uv: (rows, cols, 2)."""
    R, C = P.shape[:2]
    mb = MeshBuilder()
    mb.add_verts(P.reshape(-1, 3))
    cmax = C if closed_u else C - 1
    for r in range(R - 1):
        for c in range(cmax):
            c1 = (c + 1) % C
            f = [r * C + c, r * C + c1, (r + 1) * C + c1, (r + 1) * C + c]
            uvf = [uv[r, c], uv[r, c1], uv[r + 1, c1], uv[r + 1, c]]
            if closed_u and c1 == 0:
                uvf = [uv[r, c], (uv[r, c1][0] + 1.0, uv[r, c1][1]), (uv[r + 1, c1][0] + 1.0, uv[r + 1, c1][1]),
                       uv[r + 1, c]]
            if flip:
                f = f[::-1]
                uvf = uvf[::-1]
            mb.add_face(f, uvf)
    return mb.build(name, materials=[material], collection=collection)

# ---------------------------------------------------------------------------
# white front panel (tabard) and back panel
# ---------------------------------------------------------------------------

def front_panel_halfwidth(z):
    return np.interp(z, [0.735, 0.80, 0.86, 0.92, 0.98, 1.03, 1.08, 1.112],
                     [0.090, 0.100, 0.102, 0.092, 0.076, 0.061, 0.045, 0.032])


def build_front_panel(material, collection, ns=16, nt=40):
    tab = ProfileTable(B.trunk_sdf, (-0.14, 0.14), (0.70, 1.13), side=-1, z_hang=0.86)
    zt = 1.112
    S, T = np.meshgrid(np.linspace(-1, 1, ns + 1), np.linspace(0, 1, nt + 1))
    zbot = 0.738 + 0.030 * S ** 2
    Z = zt + (zbot - zt) * T
    X = S * front_panel_halfwidth(Z)
    Y = tab.y(X, Z)
    sl = tab.slope(X, Z)
    off = 0.0055 * np.sqrt(1 + sl ** 2)
    # lower free part flares forward a little
    Y = Y - off - 0.010 * smoothstep(0.84, 0.74, Z)
    P = np.stack([X, Y, Z], -1)
    uv = np.stack([(S + 1) * 0.5, 1.0 - T], -1)
    ob = grid_object("FrontPanel", P, uv, material, collection)
    recalc_normals(ob)
    solidify_apply(ob, 0.0022, offset=1.0)
    return ob


def back_panel_halfwidth(z):
    return np.interp(z, [0.785, 0.84, 0.90, 0.96, 1.015], [0.122, 0.124, 0.118, 0.100, 0.086])


def build_back_panel(material, collection, ns=18, nt=26):
    tab = ProfileTable(B.trunk_sdf, (-0.16, 0.16), (0.70, 1.03), side=1, z_hang=0.85)
    zt = 1.015
    S, T = np.meshgrid(np.linspace(-1, 1, ns + 1), np.linspace(0, 1, nt + 1))
    zbot = 0.786 + 0.012 * np.cos(S * np.pi * 3) ** 2
    Z = zt + (zbot - zt) * T
    X = S * back_panel_halfwidth(Z)
    Y = tab.y(X, Z)
    sl = tab.slope(X, Z)
    Y = Y + 0.0055 * np.sqrt(1 + sl ** 2) + 0.006 * smoothstep(0.84, 0.78, Z)
    P = np.stack([X, Y, Z], -1)
    uv = np.stack([(S + 1) * 0.5, 1.0 - T], -1)
    ob = grid_object("BackPanel", P, uv, material, collection)
    recalc_normals(ob)
    solidify_apply(ob, 0.0022, offset=1.0)
    return ob

# ---------------------------------------------------------------------------
# tail flaps hanging from the back panel
# ---------------------------------------------------------------------------

TAIL_FLAPS = [
    # top centre x, top z, tip (x, y, z), half width, bulge
    (0.042, 0.800, (0.072, 0.300, 0.405), 0.070, 0.024),
    (-0.044, 0.800, (-0.064, 0.270, 0.500), 0.064, 0.022),
    (0.000, 0.795, (0.004, 0.235, 0.575), 0.052, 0.016),
]


def build_tail(material, collection, ns=10, nt=28):
    tab = ProfileTable(B.trunk_sdf, (-0.16, 0.16), (0.70, 1.03), side=1, z_hang=0.85)
    objs = []
    for k, (x0, z0, tip, hw, bulge) in enumerate(TAIL_FLAPS):
        y0 = float(tab.y(np.array([x0]), np.array([z0]))[0]) + 0.012 + 0.004 * k
        top = np.array([x0, y0, z0])
        tip = np.asarray(tip)
        mid1 = top + (tip - top) * 0.33 + np.array([0, 0.020, 0])
        mid2 = top + (tip - top) * 0.70 + np.array([0, 0.010, 0])
        C, L = resample_polyline(catmull_rom([top, mid1, mid2, tip], 12), nt + 1)
        Tn = polyline_tangents(C)
        side_dir = np.array([1.0, 0.0, 0.0])
        Sd = nrm(side_dir - np.einsum('j,ij->i', side_dir, Tn)[:, None] * Tn)
        Nd = np.cross(Tn, Sd)          # points away from the body (+y-ish)
        if np.mean(Nd[:, 1]) < 0:
            Nd = -Nd
        t = np.linspace(0, 1, nt + 1)
        w = hw * (1 - t ** 1.6) ** 0.85 * (0.85 + 0.3 * np.sin(np.pi * np.clip(t * 1.4, 0, 1)))
        s = np.linspace(-1, 1, ns + 1)
        P = (C[:, None, :] + (w[:, None] * s[None, :])[..., None] * Sd[:, None, :]
             + (bulge * (1 - s[None, :] ** 2) * (1 - t[:, None]) ** 0.5)[..., None] * Nd[:, None, :])
        uv = np.stack(np.meshgrid((s + 1) * 0.5, 1.0 - t), -1)
        ob = grid_object(f"TailFlap{k}", P, uv, material, collection)
        recalc_normals(ob)
        solidify_apply(ob, 0.003, offset=0.0)
        objs.append(ob)
    return objs

# ---------------------------------------------------------------------------
# detached sleeves
# ---------------------------------------------------------------------------

SLEEVE_T0 = 0.088


def sleeve_radius(t):
    return np.interp(t, [SLEEVE_T0, 0.12, 0.20, 0.30, 0.40, 0.52], [0.045, 0.048, 0.057, 0.069, 0.080, 0.088])


def sleeve_length(psi, side):
    """Distance along the arm where the sleeve ends, per angle (jagged crystal tips)."""
    k = 7
    f = (psi / (2 * np.pi) * k + 0.25) % 1.0
    tri = 1.0 - np.abs(2 * f - 1.0)           # 0..1 triangle wave
    down = np.maximum(0.0, np.sin(psi))        # e2 side (gravity) hangs longer
    return 0.430 + 0.050 * tri ** 1.8 + 0.035 * down


def build_sleeves(material, collection, npsi=42, nt=26):
    objs = []
    for side in (1, -1):
        S0, d = B.shoulder(side), B.arm_dir(side)
        e1 = np.array([0.0, -1.0, 0.0])
        e1 = nrm(e1 - (e1 @ d) * d)
        e2 = np.cross(d, e1)
        g = np.array([0.0, 0.0, -1.0])
        if (g - (g @ d) * d) @ e2 < 0:
            e2 = -e2
        psi = 2 * np.pi * np.arange(npsi) / npsi
        Lend = sleeve_length(psi, side)
        tt = np.linspace(0, 1, nt + 1)
        Tm = SLEEVE_T0 + (Lend[None, :] - SLEEVE_T0) * tt[:, None]        # (nt+1, npsi)
        r = sleeve_radius(Tm)
        droop = 0.022 * smoothstep(0.10, 0.50, Tm)
        bell = 1.0 + 0.06 * np.sin(psi)[None, :] * smoothstep(0.2, 0.5, Tm)
        P = (S0[None, None, :] + Tm[..., None] * d + droop[..., None] * e2
             + (r * bell)[..., None] * (np.cos(psi)[None, :, None] * e1 + np.sin(psi)[None, :, None] * e2))
        uv = np.stack([np.broadcast_to(psi[None, :] / (2 * np.pi), Tm.shape), 1.0 - np.broadcast_to(tt[:, None], Tm.shape)], -1)
        ob = grid_object("Sleeve_L" if side > 0 else "Sleeve_R", P, uv, material, collection, closed_u=True)
        recalc_normals(ob)
        solidify_apply(ob, 0.0022, offset=1.0)
        objs.append(ob)
    return objs

# ---------------------------------------------------------------------------
# collar, under-bust cord, thigh straps
# ---------------------------------------------------------------------------

def ring_band(sdf, axis_fn, zs, nseg, offsets, name, material, collection, thickness=0.0022,
              v_range=(0.0, 1.0), zfun=None):
    """Band of rings around an axis at heights zs. axis_fn(z) -> (cx, cy)."""
    phi = 2 * np.pi * np.arange(nseg) / nseg
    rows = []
    for i, z in enumerate(zs):
        zz = np.full(nseg, z) if zfun is None else z + zfun(phi)
        cx, cy = axis_fn(zz)
        O = np.stack([cx, cy, zz], 1)
        D = np.stack([np.sin(phi), -np.cos(phi), np.zeros(nseg)], 1)
        P = ray_exit(sdf, O, D, tmax=0.25)
        P, _ = offset_along_normal(sdf, P, offsets[i])
        rows.append(P)
    P = np.stack(rows, 0)
    v = np.linspace(v_range[0], v_range[1], len(zs))
    uv = np.stack(np.meshgrid(phi / (2 * np.pi), v), -1)
    ob = grid_object(name, P, uv, material, collection, closed_u=True)
    recalc_normals(ob)
    if thickness:
        solidify_apply(ob, thickness, offset=-1.0)
    return ob


def torso_axis(z):
    p = B.TORSO.params(np.atleast_1d(z))
    return p[:, 0], p[:, 1]


def build_collar(material, collection):
    zs = np.linspace(1.350, 1.396, 6)
    offs = np.linspace(0.0035, 0.0060, 6)
    return ring_band(neck_sdf, torso_axis, zs, 40, offs, "Collar", material, collection, thickness=0.0025)


def build_cord(material, collection):
    zs = np.array([1.098, 1.104, 1.110])
    dip = lambda phi: -0.012 * np.exp(-(((phi + np.pi) % (2 * np.pi) - np.pi) / 0.35) ** 2)
    return ring_band(B.trunk_sdf, torso_axis, zs, 72, [0.0062, 0.0068, 0.0062], "Cord", material,
                     collection, thickness=0.003, zfun=dip)


def leg_axis_fn(side):
    loft = B.LEGS[side]

    def f(z):
        p = loft.params(np.atleast_1d(z))
        return p[:, 0], p[:, 1]
    return f


def build_thigh_straps(material, collection):
    objs = []
    for side in (1, -1):
        for j, zc in enumerate((0.742, 0.712)):
            zs = np.array([zc - 0.0055, zc, zc + 0.0055])
            ob = ring_band(lambda P: B.leg_sdf(P, side), leg_axis_fn(side), zs, 40, [0.0026, 0.0030, 0.0026],
                           f"ThighStrap_{'L' if side > 0 else 'R'}{j}", material, collection, thickness=0.002)
            objs.append(ob)
        objs += build_pendants(side, material, collection)
    return objs

# ---------------------------------------------------------------------------
# small solid ornaments via SDF + surface nets
# ---------------------------------------------------------------------------

def sdf_object(name, sdf, bmin, bmax, h, material, collection, relax=2, uv=True):
    V, F = surface_nets(sdf, bmin, bmax, h, project_iters=4, relax_iters=relax)
    mb = MeshBuilder()
    mb.add_verts(V)
    for f in F:
        mb.add_face(f)
    ob = mb.build(name, materials=[material], collection=collection)
    recalc_normals(ob)
    if uv:
        smart_uv(ob)
    return ob


def sd_torus(P, c, n, R, r):
    """Torus centred at c with axis n."""
    n = nrm(np.asarray(n, np.float64))
    q = P - np.asarray(c)
    h = q @ n
    rad = np.linalg.norm(q - h[:, None] * n, axis=1)
    return np.sqrt((rad - R) ** 2 + h ** 2) - r


def build_bell(material, collection):
    """Gold ornament hanging at the collar front."""
    c = np.array([0.0, -0.052, 1.334])

    def f(P):
        body = sd_round_cone(P, c + [0, 0, 0.010], c + [0, -0.002, -0.008], 0.0075, 0.0105)
        cut = sd_ellipsoid(P, c + [0, -0.003, -0.016], (0.009, 0.006, 0.006))
        d = smax(body, -cut, 0.002)
        ring = sd_torus(P, c + [0, 0.001, 0.016], (0, 1, 0), 0.0045, 0.0013)
        knob = sd_ellipsoid(P, c + [0, -0.002, -0.013], (0.003, 0.003, 0.003))
        return np.minimum(np.minimum(d, ring), knob)
    return sdf_object("CollarBell", f, c - 0.03, c + 0.03, 0.0009, material, collection)


def build_cord_ornament(gold, red, collection):
    """Gold diamond with a red stone at the front of the under-bust cord."""
    tab_P = ray_exit(B.trunk_sdf, np.array([[0.0, 0.0, 1.092]]), np.array([[0.0, -1.0, 0.0]]))[0]
    c = tab_P + np.array([0.0, -0.007, 0.0])

    def frame(P):
        q = P - c
        dia = (np.abs(q[:, 0]) / 0.010 + np.abs(q[:, 2]) / 0.015) - 1.0
        d2 = dia * 0.009
        return smax(d2, np.abs(q[:, 1]) - 0.0025, 0.001)

    def stone(P):
        q = P - c - np.array([0, -0.0022, 0])
        dia = (np.abs(q[:, 0]) / 0.0058 + np.abs(q[:, 2]) / 0.0090) - 1.0
        return smax(dia * 0.005, np.abs(q[:, 1]) - 0.0022, 0.001)
    a = sdf_object("CordOrnament", frame, c - 0.025, c + 0.025, 0.0008, gold, collection)
    b = sdf_object("CordStone", stone, c - 0.02, c + 0.02, 0.0007, red, collection)
    return [a, b]

# ---------------------------------------------------------------------------
# shoes (built in the un-yawed left-foot frame, then placed / mirrored)
# ---------------------------------------------------------------------------

def _leg_local(Q):
    return B.leg_sdf(B.foot_to_world(Q, 1), 1)


def _foot_bottom_table():
    ys = np.linspace(0.075, -0.185, 140)
    O = np.stack([np.full_like(ys, 0.060), ys, np.full_like(ys, -0.06)], 1)
    t = ray_hit_sdf(_leg_local, O, np.tile([0.0, 0.0, 1.0], (len(ys), 1)), 0.0, 0.35, steps=350)
    zb = -0.06 + t
    ok = np.isfinite(zb)
    zb = np.interp(ys, ys[ok][::-1], zb[ok][::-1])
    return ys, zb


def build_shoes(m_upper, m_sole, m_red, collection, h=0.0019):
    ys, zb = _foot_bottom_table()

    def zbot(y):
        return np.interp(-y, -ys, zb)

    def sole(Q):
        x, y, z = Q[:, 0], Q[:, 1], Q[:, 2]
        th = 0.0042
        d_slab = np.abs(z - (zbot(y) - th - 0.0004)) - th
        w = np.interp(-y, [-0.062, -0.02, 0.04, 0.105, 0.150, 0.176],
                      [0.024, 0.026, 0.030, 0.041, 0.035, 0.010])
        d_out = np.abs(x - 0.060) - w
        d_len = np.maximum(y - 0.062, -0.180 - y)
        return smax(smax(d_slab, d_out, 0.003), d_len, 0.004)

    z_heel_top = float(zbot(np.array([0.035]))[0]) - 0.006

    def heel(Q):
        x, y, z = Q[:, 0], Q[:, 1], Q[:, 2]
        f = np.clip(z / z_heel_top, 0, 1)
        hx = 0.0135 + 0.0075 * f
        hy = 0.0140 + 0.0095 * f
        cy = 0.042 - 0.006 * f
        r = 0.004
        qx = np.abs(x - 0.060) - (hx - r)
        qy = np.abs(y - cy) - (hy - r)
        d2 = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2) + np.minimum(np.maximum(qx, qy), 0) - r
        return np.maximum(np.maximum(d2, z - z_heel_top - 0.006), -z)

    def toecap(Q):
        x, y, z = Q[:, 0], Q[:, 1], Q[:, 2]
        shell = _leg_local(Q) - 0.0036
        y_open = -0.094 + 0.85 * np.maximum(0.052 - z, 0)
        d = smax(shell, y - y_open, 0.0015)
        return smax(d, (zbot(y) - 0.0008) - z, 0.001)

    def counter(Q):
        x, y, z = Q[:, 0], Q[:, 1], Q[:, 2]
        shell = _leg_local(Q) - 0.0036
        z_top = 0.110 - 0.60 * np.maximum(0.046 - y, 0)
        d = smax(shell, z - z_top, 0.0015)
        d = smax(d, -0.014 - y, 0.0015)
        return smax(d, (zbot(y) - 0.0008) - z, 0.001)

    lo = np.array([0.015, -0.20, -0.005])
    hi = np.array([0.105, 0.085, 0.135])
    parts = []
    for name, f, mat, hh in (("Sole", sole, m_sole, h), ("Heel", heel, m_sole, h),
                             ("ToeCap", toecap, m_upper, h), ("HeelCounter", counter, m_upper, h)):
        V, F = surface_nets(f, lo, hi, hh, project_iters=4, relax_iters=2)
        parts.append((name, V, F, mat))
    objs = []
    for side in (1, -1):
        sfx = "L" if side > 0 else "R"
        for name, V, F, mat in parts:
            P = B.foot_to_world(V, side)
            mb = MeshBuilder()
            mb.add_verts(P)
            for fc in F:
                mb.add_face(fc if side > 0 else fc[::-1])
            ob = mb.build(f"Shoe{name}_{sfx}", materials=[mat], collection=collection)
            recalc_normals(ob)
            smart_uv(ob)
            objs.append(ob)
        # ankle cord + knot
        zs = np.array([0.1195, 0.1240, 0.1285])
        objs.append(ring_band(lambda P: B.leg_sdf(P, side), leg_axis_fn(side), zs, 40,
                              [0.0024, 0.0030, 0.0024], f"AnkleCord_{sfx}", m_red, collection, thickness=0.002))
        cx, cy = leg_axis_fn(side)(np.array([0.124]))
        front = ray_exit(lambda P: B.leg_sdf(P, side), np.array([[cx[0], cy[0], 0.124]]),
                         np.array([[0.0, -1.0, 0.0]]))[0] + np.array([0.0, -0.006, 0.0])
        objs.append(build_knot(f"AnkleKnot_{sfx}", front, m_red, collection, scale=1.0))
    return objs


def build_knot(name, c, material, collection, scale=1.0, normal=(0.0, -1.0, 0.0)):
    """Four-loop 'lucky' knot facing `normal`, plus a centre bead."""
    n = nrm(np.asarray(normal, np.float64))
    up = np.array([0.0, 0.0, 1.0])
    ex = nrm(np.cross(up, n))
    ez = np.cross(n, ex)
    R0 = 0.0055 * scale
    r0 = 0.0016 * scale

    def f(P):
        d = sd_ellipsoid(P, c, (0.0030 * scale,) * 3)
        for a in range(4):
            ang = a * np.pi / 2 + np.pi / 4
            o = c + (math.cos(ang) * ex + math.sin(ang) * ez) * 0.0062 * scale
            d = np.minimum(d, sd_torus(P, o, n, R0 * 0.75, r0))
        return d
    ext = 0.02 * scale
    return sdf_object(name, f, c - ext, c + ext, 0.0006 * scale, material, collection)

# ---------------------------------------------------------------------------
# vision gem, red knot and tassels at the front-left of the waist
# ---------------------------------------------------------------------------

_FRONT_TAB = None


def front_table():
    global _FRONT_TAB
    if _FRONT_TAB is None:
        _FRONT_TAB = ProfileTable(B.trunk_sdf, (-0.15, 0.15), (0.55, 1.13), side=-1, z_hang=0.86)
    return _FRONT_TAB


GEM_POS = (0.086, 1.002)       # x, z


def gem_center():
    tab = front_table()
    x, z = GEM_POS
    y = float(tab.y(np.array([x]), np.array([z]))[0]) - 0.016
    return np.array([x, y, z])


def build_gem(m_gold, m_gem, m_red, collection):
    c = gem_center()
    n = np.array([0.0, -1.0, 0.0])

    def frame(P):
        d = sd_torus(P, c, n, 0.0115, 0.0022)
        for k in range(4):
            a = k * np.pi / 2
            tip = c + np.array([math.cos(a), 0.0, math.sin(a)]) * 0.019
            d = np.minimum(d, sd_round_cone(P, c + np.array([math.cos(a), 0, math.sin(a)]) * 0.011, tip, 0.0030, 0.0008))
        return d

    def gem(P):
        return sd_ellipsoid(P, c + np.array([0, -0.001, 0]), (0.0100, 0.0065, 0.0100))
    objs = [sdf_object("VisionFrame", frame, c - 0.03, c + 0.03, 0.0007, m_gold, collection),
            sdf_object("VisionGem", gem, c - 0.02, c + 0.02, 0.0007, m_gem, collection)]
    objs.append(build_knot("VisionKnot", c + np.array([0, 0.004, 0]), m_red, collection, scale=2.6))
    return objs


def tube_along(name, pts, radius, material, collection, nseg=8, n_len=None, r_end=None, uv_v=1.0, close=True):
    C, L = resample_polyline(catmull_rom(np.asarray(pts, np.float64), 12), n_len or max(8, int(len(pts) * 8)))
    T = polyline_tangents(C)
    ref = np.array([1.0, 0.0, 0.0])
    S = nrm(np.cross(T, ref if abs(T[0] @ ref) < 0.9 else np.array([0, 1.0, 0])))
    for i in range(1, len(S)):
        S[i] = nrm(S[i] - (S[i] @ T[i]) * T[i])
        if S[i] @ S[i - 1] < 0:
            S[i] = -S[i]
    N = np.cross(T, S)
    rr = np.full(len(C), radius) if r_end is None else np.linspace(radius, r_end, len(C))
    ang = 2 * np.pi * np.arange(nseg) / nseg
    G = C[:, None, :] + rr[:, None, None] * (np.cos(ang)[None, :, None] * S[:, None, :]
                                             + np.sin(ang)[None, :, None] * N[:, None, :])
    uv = np.stack(np.meshgrid(ang / (2 * np.pi), np.linspace(0, uv_v, len(C))), -1)
    ob = grid_object(name, G, uv, material, collection, closed_u=True)
    recalc_normals(ob)
    return ob


def build_tassels(m_red, m_gold, collection):
    tab = front_table()
    c = gem_center()
    objs = []
    for k, (dx, z_end, swing, t_len) in enumerate(((-0.006, 0.785, 0.010, 0.075), (0.008, 0.705, 0.018, 0.090))):
        zs = np.linspace(c[2] - 0.012, z_end, 7)
        xs = c[0] + dx + swing * np.sin(np.linspace(0, 1, 7) * np.pi * 0.8)
        ys = tab.y(xs, zs) - 0.014 - 0.004 * k
        pts = np.stack([xs, ys, zs], 1)
        pts[0] = c + np.array([dx * 0.5, 0.002, -0.012])
        objs.append(tube_along(f"TasselCord{k}", pts, 0.0021, m_red, collection, nseg=8))
        end = pts[-1]
        cap_top = end + np.array([0, 0, 0.001])
        cap = tube_along(f"TasselCap{k}", [cap_top, cap_top - [0, 0, 0.005], cap_top - [0, 0, 0.011]], 0.0042,
                         m_gold, collection, nseg=12, n_len=6, r_end=0.0050)
        objs.append(cap)
        top = cap_top - np.array([0, 0, 0.010])
        tas = tube_along(f"Tassel{k}", [top, top - [0, 0.002, t_len * 0.5], top - [0, 0.004, t_len]], 0.0048,
                         m_red, collection, nseg=16, n_len=14, r_end=0.0105)
        solidify_apply(tas, 0.0015, offset=-1.0)
        objs.append(tas)
    return objs

# ---------------------------------------------------------------------------
# hair tie ornament (black wings with a red flower) at the ponytail base
# ---------------------------------------------------------------------------

def build_hair_tie(m_tie, m_black, collection, tie_pos):
    objs = []
    for side in (1, -1):
        c = tie_pos + np.array([side * 0.024, 0.013, 0.004])
        ang = math.radians(28) * side
        r = np.array([math.cos(ang), math.sin(ang), 0.0])
        u = np.array([0.0, 0.0, 1.0])
        nvec = np.cross(r, u)
        if nvec[1] < 0:
            nvec = -nvec
        S, T = np.meshgrid(np.linspace(-1, 1, 9), np.linspace(-1, 1, 9))
        hw, hh = 0.021, 0.019
        bend = 0.006 * (1 - S ** 2)
        P = (c[None, None, :] + (S * hw)[..., None] * r + (T * hh)[..., None] * u + bend[..., None] * nvec)
        uv = np.stack([(S + 1) * 0.5 if side > 0 else (1 - S) * 0.5, (T + 1) * 0.5], -1)
        ob = grid_object(f"HairTieWing_{'L' if side > 0 else 'R'}", P, uv, m_tie, collection)
        recalc_normals(ob)
        solidify_apply(ob, 0.0025, offset=0.0)
        objs.append(ob)

    def band(P):
        return sd_torus(P, tie_pos + np.array([0, 0.004, 0.0]), (0, 0.35, 1.0), 0.0135, 0.0042)
    objs.append(sdf_object("HairTieBand", band, tie_pos - 0.03, tie_pos + 0.03, 0.0009, m_black, collection))
    return objs


def build_pendants(side, material, collection, z_top=0.7060, length=0.050, half_w=0.0042):
    """Small pointed gold tabs hanging from the lower thigh strap."""
    sdf = lambda P: B.leg_sdf(P, side)
    axis = leg_axis_fn(side)
    objs = []
    # angles around the leg (0 = front, + toward the outside of this leg)
    for k, a_deg in enumerate((-8.0, 38.0)):
        a0 = math.radians(a_deg) * side
        T, S = np.meshgrid(np.linspace(0, 1, 9), np.linspace(-1, 1, 5), indexing='ij')
        z = z_top - length * T
        w = half_w * (1 - T ** 2.2) + 0.0004
        cx, cy = axis(z.ravel())
        cx, cy = cx.reshape(z.shape), cy.reshape(z.shape)
        R = 0.05
        ang = a0 + S * w / R
        O = np.stack([cx, cy, z], -1).reshape(-1, 3)
        D = np.stack([np.sin(ang), -np.cos(ang), np.zeros_like(ang)], -1).reshape(-1, 3)
        P = ray_exit(sdf, O, D, tmax=0.12)
        P, _ = offset_along_normal(sdf, P, 0.0042)
        P = P.reshape(z.shape + (3,))
        uv = np.stack([(S + 1) * 0.5, 1 - T], -1)
        ob = grid_object(f"StrapPendant_{'L' if side > 0 else 'R'}{k}", P, uv, material, collection)
        recalc_normals(ob)
        solidify_apply(ob, 0.0016, offset=0.0)
        objs.append(ob)
    return objs
