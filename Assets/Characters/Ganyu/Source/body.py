"""Body mesh: an implicit (SDF) body shape + a designed quad topology projected onto it.

Rest pose is an A-pose (arms 40 deg below horizontal). Hands are separate glove meshes,
the head is a separate mesh; the neck top and wrists are capped inside them.
"""
import math

import numpy as np
import bmesh
import bpy

from common import (nrm, smoothstep, rot_x, rot_z, smin, sd_ellipsoid, sd_round_cone,
                    Loft, project_to_sdf, sdf_grad, vertex_neighbors, laplacian_relax,
                    bm_levelset_cut, MeshBuilder, catmull_rom, resample_polyline,
                    polyline_tangents)

# ---------------------------------------------------------------------------
# proportions (meters)
# ---------------------------------------------------------------------------
NT = 48                      # torso ring resolution
NL = 28                      # leg ring resolution (25 torso-half verts + 3 crotch verts)
NA = 24                      # arm ring resolution (hole of 6x6 cells)
TORSO_Z = np.linspace(0.86, 1.45, 38)
KA, KB = 22, 28              # armhole ring range in the torso grid
CA, CB = 9, 15               # armhole column range for the left side (column 12 = +X)

ARM_ANGLE = math.radians(40.0)
SHOULDER_L = np.array([0.128, 0.005, 1.282])
ARM_DIR_L = np.array([math.cos(ARM_ANGLE), 0.0, -math.sin(ARM_ANGLE)])
UPPER_ARM_LEN = 0.255
FOREARM_LEN = 0.230
WRIST_T = UPPER_ARM_LEN + FOREARM_LEN

HIP_JOINT_L = np.array([0.085, 0.006, 0.865])
KNEE_L = np.array([0.068, -0.006, 0.452])
ANKLE_L = np.array([0.062, 0.010, 0.112])
BALL_L = np.array([0.058, -0.110, 0.030])
TOE_L = np.array([0.056, -0.162, 0.022])

PART_TORSO, PART_LEG_L, PART_LEG_R, PART_ARM_L, PART_ARM_R = 0, 1, 2, 3, 4


def side_vec(v, side):
    v = np.array(v, dtype=np.float64)
    v[0] *= side
    return v


def shoulder(side):
    return side_vec(SHOULDER_L, side)


def arm_dir(side):
    return side_vec(ARM_DIR_L, side)


def elbow(side):
    return shoulder(side) + UPPER_ARM_LEN * arm_dir(side)


def wrist(side):
    return shoulder(side) + WRIST_T * arm_dir(side)

# ---------------------------------------------------------------------------
# implicit shape
# ---------------------------------------------------------------------------
TORSO_ROWS = [  # z, cx, cy, rx, ry_front, ry_back, n
    (0.775, 0, 0.012, 0.030, 0.025, 0.030, 2.0),
    (0.80, 0, 0.012, 0.085, 0.052, 0.062, 2.2),
    (0.83, 0, 0.012, 0.130, 0.068, 0.078, 2.4),
    (0.87, 0, 0.012, 0.150, 0.076, 0.084, 2.4),
    (0.91, 0, 0.010, 0.147, 0.079, 0.082, 2.4),
    (0.95, 0, 0.008, 0.135, 0.078, 0.075, 2.3),
    (0.99, 0, 0.007, 0.117, 0.075, 0.067, 2.2),
    (1.03, 0, 0.006, 0.101, 0.073, 0.066, 2.2),
    (1.07, 0, 0.004, 0.103, 0.072, 0.068, 2.2),
    (1.11, 0, 0.001, 0.110, 0.074, 0.074, 2.2),
    (1.15, 0, -0.002, 0.116, 0.076, 0.080, 2.3),
    (1.19, 0, -0.003, 0.119, 0.075, 0.084, 2.3),
    (1.23, 0, -0.002, 0.118, 0.071, 0.085, 2.3),
    (1.27, 0, 0.001, 0.110, 0.063, 0.079, 2.3),
    (1.30, 0, 0.005, 0.090, 0.053, 0.068, 2.2),
    (1.325, 0, 0.010, 0.060, 0.044, 0.054, 2.0),
    (1.35, 0, 0.014, 0.043, 0.040, 0.042, 2.0),
    (1.38, 0, 0.014, 0.038, 0.036, 0.037, 2.0),
    (1.40, 0, 0.014, 0.036, 0.034, 0.035, 2.0),
    (1.46, 0, 0.012, 0.036, 0.034, 0.035, 2.0),
]

LEG_ROWS = [  # left leg: z, cx, cy, rx, ry_front, ry_back, n
    (0.95, 0.075, 0.010, 0.040, 0.040, 0.040, 2.0),
    (0.90, 0.082, 0.010, 0.066, 0.065, 0.075, 2.0),
    (0.84, 0.086, 0.008, 0.073, 0.071, 0.079, 2.0),
    (0.78, 0.086, 0.004, 0.071, 0.071, 0.071, 2.0),
    (0.70, 0.083, 0.000, 0.066, 0.069, 0.064, 2.0),
    (0.62, 0.079, -0.004, 0.060, 0.064, 0.058, 2.0),
    (0.55, 0.074, -0.006, 0.053, 0.057, 0.050, 2.0),
    (0.50, 0.071, -0.007, 0.048, 0.050, 0.045, 2.0),
    (0.455, 0.068, -0.008, 0.046, 0.048, 0.043, 2.0),
    (0.41, 0.067, -0.006, 0.043, 0.043, 0.048, 2.0),
    (0.37, 0.066, -0.004, 0.045, 0.040, 0.058, 2.0),
    (0.33, 0.065, -0.002, 0.045, 0.038, 0.061, 2.0),
    (0.28, 0.064, 0.000, 0.042, 0.036, 0.055, 2.0),
    (0.22, 0.063, 0.003, 0.035, 0.032, 0.041, 2.0),
    (0.17, 0.062, 0.005, 0.030, 0.029, 0.031, 2.0),
    (0.13, 0.062, 0.007, 0.030, 0.029, 0.029, 2.0),
    (0.09, 0.062, 0.008, 0.028, 0.026, 0.028, 2.0),
]

TORSO = Loft(TORSO_ROWS)
LEGS = {1: Loft(LEG_ROWS), -1: Loft(LEG_ROWS, mirror=True)}


def bust_sdf(P):
    c = np.array([0.060, -0.070, 1.176])
    r = (0.064, 0.056, 0.060)
    dl = sd_ellipsoid(P, c, r, rot_z(math.radians(10)) @ rot_x(math.radians(8)))
    dr = sd_ellipsoid(P, side_vec(c, -1), r, rot_z(math.radians(-10)) @ rot_x(math.radians(8)))
    return smin(dl, dr, 0.03)


def glute_sdf(P):
    c = np.array([0.062, 0.050, 0.855])
    r = (0.070, 0.062, 0.075)
    return smin(sd_ellipsoid(P, c, r), sd_ellipsoid(P, side_vec(c, -1), r), 0.04)


def shoulder_sdf(P, side):
    a = side_vec((0.035, 0.016, 1.322), side)
    b = side_vec((0.120, 0.008, 1.292), side)
    return sd_round_cone(P, a, b, 0.027, 0.030)


def arm_sdf(P, side):
    S, d = shoulder(side), arm_dir(side)
    E = elbow(side)
    W = wrist(side)
    delt = sd_round_cone(P, S - 0.010 * d + np.array([0, 0, -0.006]), S + 0.10 * d, 0.039, 0.031)
    upper = sd_round_cone(P, S, E, 0.034, 0.028)
    fore1 = sd_round_cone(P, E, E + 0.07 * d, 0.029, 0.031)
    fore2 = sd_round_cone(P, E + 0.07 * d, W, 0.031, 0.021)
    out = smin(delt, upper, 0.02)
    out = smin(out, fore1, 0.015)
    return smin(out, fore2, 0.015)


FOOT_YAW = math.radians(6.0)   # toe-out
# foot centre line in the (un-yawed, left) foot frame and ring sizes (rx, ry_top, ry_sole)
FOOT_PATH = [
    (0.062, 0.006, 0.150), (0.062, 0.008, 0.118), (0.062, 0.008, 0.094), (0.062, -0.004, 0.074),
    (0.0615, -0.030, 0.058), (0.061, -0.062, 0.044), (0.0595, -0.095, 0.032), (0.058, -0.125, 0.025),
    (0.057, -0.150, 0.021)]
FOOT_SIZES = [
    (0.029, 0.028, 0.030), (0.031, 0.029, 0.034), (0.031, 0.030, 0.048), (0.032, 0.030, 0.046),
    (0.034, 0.026, 0.030), (0.036, 0.022, 0.022), (0.038, 0.019, 0.018), (0.036, 0.016, 0.015),
    (0.031, 0.013, 0.012)]


def foot_to_local(P, side):
    """World -> un-yawed left-foot frame."""
    Q = np.array(P, dtype=np.float64)
    if side < 0:
        Q[:, 0] *= -1.0
    return (Q - ANKLE_L) @ rot_z(-FOOT_YAW).T + ANKLE_L


def foot_to_world(Q, side):
    P = (np.asarray(Q, dtype=np.float64) - ANKLE_L) @ rot_z(FOOT_YAW).T + ANKLE_L
    if side < 0:
        P[:, 0] *= -1.0
    return P


def foot_sdf(P, side):
    Q = foot_to_local(P, side)
    heel = sd_ellipsoid(Q, (0.062, 0.030, 0.078), (0.025, 0.027, 0.028))
    mid = sd_ellipsoid(Q, (0.061, -0.045, 0.064), (0.031, 0.050, 0.024), rot_x(math.radians(28)))
    ball = sd_ellipsoid(Q, (0.058, -0.105, 0.030), (0.037, 0.030, 0.018))
    toes = sd_ellipsoid(Q, (0.056, -0.140, 0.022), (0.033, 0.030, 0.014))
    d = smin(heel, mid, 0.02)
    d = smin(d, ball, 0.02)
    return smin(d, toes, 0.015)


def leg_sdf(P, side):
    d = LEGS[side].sdf(P)
    knee = sd_ellipsoid(P, side_vec((0.068, -0.040, 0.455), side), (0.024, 0.012, 0.022))
    d = smin(d, knee, 0.012)
    return smin(d, foot_sdf(P, side), 0.025)


def body_sdf(P, arms=True):
    d = TORSO.sdf(P)
    d = smin(d, bust_sdf(P), 0.035)
    d = smin(d, glute_sdf(P), 0.035)
    d = smin(d, np.minimum(shoulder_sdf(P, 1), shoulder_sdf(P, -1)), 0.035)
    d = smin(d, np.minimum(leg_sdf(P, 1), leg_sdf(P, -1)), 0.03)
    if arms:
        d = smin(d, np.minimum(arm_sdf(P, 1), arm_sdf(P, -1)), 0.025)
    return d


def trunk_sdf(P):
    return body_sdf(P, arms=False)

# ---------------------------------------------------------------------------
# bodysuit coverage (positive = covered by the black suit)
# ---------------------------------------------------------------------------

def bodysuit_fn(P):
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    ax = np.abs(x)
    w = np.interp(z, [1.15, 1.20, 1.25, 1.30, 1.34, 1.40], [0.25, 0.115, 0.090, 0.068, 0.050, 0.050])
    s_front = np.minimum(np.minimum(w - ax, (0.02 - y) * 2.0), 1.392 - z)
    phi = np.degrees(np.arctan2(ax, -y))
    zc = 1.185 - (1.185 - 1.005) * smoothstep(80.0, 170.0, phi)
    s_low = zc - z
    return np.maximum(s_front, s_low)

# ---------------------------------------------------------------------------
# topology template
# ---------------------------------------------------------------------------

class Template:
    def __init__(self):
        self.V = []
        self.part = []
        self.F = []
        self.seams = set()

    def add(self, p, part):
        self.V.append(np.asarray(p, dtype=np.float64))
        self.part.append(part)
        return len(self.V) - 1

    def quad(self, a, b, c, d):
        self.F.append([a, b, c, d])

    def seam(self, a, b):
        self.seams.add((min(a, b), max(a, b)))


def _arm_radius(t):
    return np.interp(t, [0.0, 0.06, 0.15, 0.255, 0.33, 0.485], [0.045, 0.036, 0.032, 0.028, 0.031, 0.022])


def _bridge(tp, ring_a, ring_b):
    n = len(ring_a)
    for m in range(n):
        m1 = (m + 1) % n
        tp.quad(ring_a[m], ring_a[m1], ring_b[m1], ring_b[m])


def _fan(tp, ring, center):
    n = len(ring)
    for m in range(n):
        tp.F.append([ring[m], ring[(m + 1) % n], center])


def build_template():
    tp = Template()
    K = len(TORSO_Z)
    ang = 2.0 * np.pi * np.arange(NT) / NT

    def in_hole_vert(k, i):
        if not (KA < k < KB):
            return False
        return (CA < i < CB) or (NT - CB < i < NT - CA)

    def in_hole_cell(k, i):
        if not (KA <= k < KB):
            return False
        return (CA <= i < CB) or (NT - CB <= i < NT - CA)

    # rings above KR keep only every other column (48 -> 24 around the neck)
    KR = KB + 3
    idxT = -np.ones((K, NT), dtype=np.int64)
    for k, z in enumerate(TORSO_Z):
        P = TORSO.section_point(np.full(NT, z), ang)
        for i in range(NT):
            if k > KR and i % 2:
                continue
            if not in_hole_vert(k, i):
                idxT[k, i] = tp.add(P[i], PART_TORSO)
    for k in range(K - 1):
        if k == KR:
            # 2:1 reduction strip (triangle + quad per upper vertex)
            for m in range(NT // 2):
                a0, a1, a2 = idxT[k, 2 * m], idxT[k, 2 * m + 1], idxT[k, (2 * m + 2) % NT]
                b0, b1 = idxT[k + 1, 2 * m], idxT[k + 1, (2 * m + 2) % NT]
                tp.F.append([a0, a1, b0])
                tp.quad(a1, a2, b1, b0)
            continue
        step = 2 if k > KR else 1
        for i in range(0, NT, step):
            if in_hole_cell(k, i):
                continue
            i1 = (i + step) % NT
            tp.quad(idxT[k, i], idxT[k, i1], idxT[k + 1, i1], idxT[k + 1, i])
    # back seam on the torso
    for k in range(K - 1):
        tp.seam(idxT[k, NT // 2], idxT[k + 1, NT // 2])
    neck_c = tp.add((0.0, 0.012, TORSO_Z[-1] + 0.004), PART_TORSO)
    _fan(tp, [idxT[K - 1, i] for i in range(0, NT, 2)], neck_c)
    tp.seam(idxT[K - 1, NT // 2], neck_c)

    # ---- arms ----
    def hole_loop(ca, cb):
        loop = [idxT[KA, i] for i in range(ca, cb + 1)]
        loop += [idxT[k, cb] for k in range(KA + 1, KB)]
        loop += [idxT[KB, i] for i in range(cb, ca - 1, -1)]
        loop += [idxT[k, ca] for k in range(KB - 1, KA, -1)]
        return loop

    arm_rings = {}
    for side, (ca, cb), part in ((1, (CA, CB), PART_ARM_L), (-1, (NT - CB, NT - CA), PART_ARM_R)):
        loop = hole_loop(ca, cb)
        assert len(loop) == NA
        S, d = shoulder(side), arm_dir(side)
        e1 = np.array([0.0, -1.0, 0.0])
        e1 = nrm(e1 - (e1 @ d) * d)
        e2 = np.cross(d, e1)
        P0 = np.array([tp.V[i] for i in loop])
        # angles around the hole's own centroid, measured in the plane normal to the arm
        Q = P0 - S
        Q = Q - np.outer(Q @ d, d)
        rel = Q - Q.mean(0)
        a = np.unwrap(np.arctan2(rel @ e2, rel @ e1))
        print(f"[body] arm {side}: hole angle span {math.degrees(a[-1] - a[0]):.1f} deg")
        sgn = 1.0 if a[-1] > a[0] else -1.0
        base = a[0] + sgn * 2.0 * np.pi * np.arange(NA) / NA
        uni = base + np.mean(a - base)
        # tidy the hole loop itself onto a circle around the arm root
        C0 = S - 0.004 * d
        for m, vi in enumerate(loop):
            am = 0.5 * (a[m] + uni[m])
            tp.V[vi] = C0 + 0.052 * (math.cos(am) * e1 + math.sin(am) * e2)
        rings = [loop]
        ts = np.linspace(0.03, WRIST_T, 27)
        for j, t in enumerate(ts, 1):
            w = smoothstep(0.0, 3.0, j)
            angj = (1 - w) * a + w * uni
            r = _arm_radius(t)
            C = S + t * d
            pts = C + r * (np.cos(angj)[:, None] * e1 + np.sin(angj)[:, None] * e2)
            rings.append([tp.add(p, part) for p in pts])
        for j in range(len(rings) - 1):
            _bridge(tp, rings[j], rings[j + 1])
        cap = tp.add(S + (WRIST_T + 0.006) * d, part)
        _fan(tp, rings[-1], cap)
        # seams: armhole loop + underside line
        for m in range(NA):
            tp.seam(loop[m], loop[(m + 1) % NA])
        P1 = np.array([tp.V[i] for i in rings[1]])
        m_low = int(np.argmin(P1[:, 2]))
        for j in range(len(rings) - 1):
            tp.seam(rings[j][m_low], rings[j + 1][m_low])
        tp.seam(rings[-1][m_low], cap)
        arm_rings[side] = rings

    # spread the torso rings above the armholes evenly over the shoulder tops
    top_k = KR + 1
    tgt = TORSO.section_point(np.full(NT, TORSO_Z[top_k]), ang)
    n_s = top_k - KB
    for s in range(1, n_s):
        t = s / float(n_s)
        for i in range(NT):
            a_ = tp.V[idxT[KB, i]]
            tp.V[idxT[KB + s, i]] = a_ + (tgt[i] - a_) * t

    # ---- crotch + legs ----
    cF = tp.add((0.0, -0.035, 0.800), PART_TORSO)
    cM = tp.add((0.0, 0.005, 0.785), PART_TORSO)
    cB = tp.add((0.0, 0.045, 0.800), PART_TORSO)
    ring0 = {
        1: [idxT[0, i] for i in range(0, NT // 2 + 1)] + [cB, cM, cF],
        -1: [idxT[0, i] for i in range(NT // 2, NT)] + [idxT[0, 0], cF, cM, cB],
    }
    for i in range(NT):
        tp.seam(idxT[0, i], idxT[0, (i + 1) % NT])
    for a_, b_ in ((idxT[0, 0], cF), (cF, cM), (cM, cB), (cB, idxT[0, NT // 2])):
        tp.seam(a_, b_)

    # foot centre line (left, un-yawed frame) resampled by arc length
    fp = catmull_rom(FOOT_PATH, 12)
    fsz_key = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(np.asarray(FOOT_PATH), axis=0), axis=1))])
    fpath, flen = resample_polyline(fp, 27)
    fT = polyline_tangents(fpath)
    fs = np.linspace(0.0, fsz_key[-1], len(fpath))
    fsizes = np.stack([np.interp(fs, fsz_key, np.asarray(FOOT_SIZES)[:, c]) for c in range(3)], 1)
    leg_rings = {}
    for side, part in ((1, PART_LEG_L), (-1, PART_LEG_R)):
        loft = LEGS[side]
        r0 = ring0[side]
        assert len(r0) == NL
        c0 = loft.params(np.array([0.83]))[0]
        P0 = np.array([tp.V[i] for i in r0])
        rel = P0[:, :2] - c0[:2]
        a = np.unwrap(np.arctan2(rel[:, 1], rel[:, 0]))
        sgn = 1.0 if a[-1] > a[0] else -1.0
        base = a[0] + sgn * 2.0 * np.pi * np.arange(NL) / NL
        uni = base + np.mean(a - base)
        a_in = math.pi if side > 0 else 0.0
        rings = [r0]
        zs = np.linspace(0.815, 0.168, 37)
        for j, zc in enumerate(zs, 1):
            w = smoothstep(0.0, 6.0, j)
            tilt = 0.035 * (1.0 - smoothstep(0.0, 7.0, j))
            al = (1 - w) * a + w * uni
            z = zc - tilt * np.cos(al - a_in)
            p = loft.params(z)
            cx, cy, rx, ryf, ryb, n = p.T
            s_, c_ = np.cos(al), np.sin(al)
            ry = np.where(c_ < 0, ryf, ryb)
            rr = 1.0 / ((np.abs(s_) / rx) ** n + (np.abs(c_) / ry) ** n) ** (1.0 / n)
            pts = np.stack([cx + rr * s_, cy + rr * c_, z], 1)
            rings.append([tp.add(q, part) for q in pts])
        # foot: rings follow the foot centre line (frames rotate from vertical to the foot)
        al_loc = uni if side > 0 else (math.pi - uni)
        cs, sn = np.cos(al_loc), np.sin(al_loc)
        for j in range(len(fpath)):
            T = fT[j]
            ex = np.array([1.0, 0.0, 0.0])
            ex = nrm(ex - (ex @ T) * T)
            ey = np.cross(ex, T)
            rx, ryt, rys = fsizes[j]
            ry = np.where(sn < 0, ryt, rys)
            pts = fpath[j] + (rx * cs)[:, None] * ex + (ry * sn)[:, None] * ey
            pts = foot_to_world(pts, side)
            rings.append([tp.add(q, part) for q in pts])
        for j in range(len(rings) - 1):
            _bridge(tp, rings[j], rings[j + 1])
        tip = fpath[-1] + 0.010 * fT[-1]
        toe = tp.add(foot_to_world(tip[None], side)[0], part)
        _fan(tp, rings[-1], toe)
        m_in = NL - 2  # cM index in ring 0
        for j in range(len(rings) - 1):
            tp.seam(rings[j][m_in], rings[j + 1][m_in])
        tp.seam(rings[-1][m_in], toe)
        leg_rings[side] = rings

    tp.V = np.array(tp.V)
    tp.part = np.array(tp.part)
    tp.idxT = idxT
    tp.arm_rings = arm_rings
    tp.leg_rings = leg_rings
    return tp


def face_normals(V, F):
    N = np.zeros((len(F), 3))
    C = np.zeros((len(F), 3))
    for i, f in enumerate(F):
        p = V[f]
        C[i] = p.mean(0)
        n = np.zeros(3)
        for k in range(len(f)):
            n += np.cross(p[k], p[(k + 1) % len(f)])
        N[i] = n
    return nrm(N), C


def shape_template(tp, relax_iters=30):
    V = project_to_sdf(body_sdf, tp.V, iters=10)
    nb = vertex_neighbors(tp.F, len(V))
    V = laplacian_relax(V, nb, body_sdf, iters=relax_iters, lam=0.5)
    V = project_to_sdf(body_sdf, V, iters=6)
    N, C = face_normals(V, tp.F)
    G = nrm(sdf_grad(body_sdf, C))
    dots = np.einsum('ij,ij->i', N, G)
    return V, dots


def build_body(materials, collection, relax_iters=60):
    """materials: dict with 'skin' and 'tights'. Returns (object, template, info)."""
    tp = build_template()
    V, dots = shape_template(tp, relax_iters)
    flipped = int(np.sum(dots < 0))
    weak = int(np.sum(np.abs(dots) < 0.3))
    print(f"[body] verts={len(V)} faces={len(tp.F)} flipped={flipped} weak={weak} "
          f"(of which flipped means normal disagrees with sdf)")

    me = bpy.data.meshes.new("Body")
    me.from_pydata(V.tolist(), [], tp.F)
    me.update()
    attr = me.attributes.new("part", 'INT', 'POINT')
    attr.data.foreach_set("value", tp.part.astype(np.int32))
    ob = bpy.data.objects.new("Body", me)
    collection.objects.link(ob)

    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    seams = tp.seams
    for e in bm.edges:
        a, b = e.verts[0].index, e.verts[1].index
        if (min(a, b), max(a, b)) in seams:
            e.seam = True
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    play = bm.verts.layers.int.get("part")

    # bodysuit boundary: cut torso edges along the zero set of bodysuit_fn
    def torso_edge(e):
        return e.verts[0][play] == PART_TORSO and e.verts[1][play] == PART_TORSO
    newv = bm_levelset_cut(bm, bodysuit_fn, edge_ok=torso_edge)
    if newv:
        P = np.array([v.co[:] for v in newv])
        Q = project_to_sdf(body_sdf, P, iters=4)
        for v, q in zip(newv, Q):
            v.co = q
    # materials
    for f in bm.faces:
        parts = [v[play] for v in f.verts]
        c = np.array(f.calc_center_median()[:])[None]
        if any(p in (PART_ARM_L, PART_ARM_R) for p in parts):
            f.material_index = 0
        elif any(p in (PART_LEG_L, PART_LEG_R) for p in parts):
            f.material_index = 1
        else:
            f.material_index = 1 if bodysuit_fn(c)[0] > 0 else 0
        f.smooth = True
    bm.to_mesh(me)
    bm.free()
    me.materials.append(materials['skin'])
    me.materials.append(materials['tights'])
    me.update()
    return ob, tp
