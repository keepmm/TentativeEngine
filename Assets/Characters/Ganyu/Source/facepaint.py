"""Face texture painter (eyes, brows, nose, mouth, blush) in front-projected world space.

The face texture's left half (u in [0, .5]) is an orthographic front view of the head
(see head.face_uv), so features are placed directly in world (x, z) metres.
Colours are sRGB.
"""
import math
import os

import numpy as np

import texbake as tb
from textures import SKIN_SRGB


class FaceCanvas:
    def __init__(self, img, span, z0):
        self.img = img
        self.H, self.W = img.shape[:2]
        self.span = span
        self.z0 = z0
        self.px = span / self.H

    def col(self, x):
        return (x / self.span + 0.25) * self.W - 0.5

    def row(self, z):
        return (z - self.z0) / self.span * self.H - 0.5

    def region(self, x0, x1, z0, z1):
        j0 = max(int(np.floor(self.col(x0))) - 1, 0)
        j1 = min(int(np.ceil(self.col(x1))) + 2, self.W // 2)
        i0 = max(int(np.floor(self.row(z0))) - 1, 0)
        i1 = min(int(np.ceil(self.row(z1))) + 2, self.H)
        js = np.arange(j0, j1)
        is_ = np.arange(i0, i1)
        X = ((js + 0.5) / self.W - 0.25) * self.span
        Z = self.z0 + (is_ + 0.5) / self.H * self.span
        XX, ZZ = np.meshgrid(X, Z)
        return (slice(i0, i1), slice(j0, j1)), XX, ZZ

    def paint(self, sl, cov, color):
        sub = self.img[sl]
        c = np.asarray(color, np.float32)
        if c.ndim == 1:
            c = c[None, None, :]
        self.img[sl] = sub * (1 - cov[..., None]) + c * cov[..., None]

    def cover(self, d):
        return np.clip(0.5 - d / self.px, 0.0, 1.0)


def bezier(p0, p1, p2, n=48):
    t = np.linspace(0.0, 1.0, n)[:, None]
    p0, p1, p2 = (np.asarray(p, np.float64) for p in (p0, p1, p2))
    return (1 - t) ** 2 * p0 + 2 * (1 - t) * t * p1 + t ** 2 * p2


def stroke_dist(XX, ZZ, pts, widths):
    best = np.full(XX.shape, 1e9)
    for k in range(len(pts) - 1):
        a, b = pts[k], pts[k + 1]
        ab = b - a
        L2 = max(float(ab @ ab), 1e-14)
        t = np.clip(((XX - a[0]) * ab[0] + (ZZ - a[1]) * ab[1]) / L2, 0.0, 1.0)
        dx = XX - (a[0] + t * ab[0])
        dz = ZZ - (a[1] + t * ab[1])
        w = widths[k] + t * (widths[k + 1] - widths[k])
        best = np.minimum(best, np.sqrt(dx * dx + dz * dz) - w)
    return best


def draw_stroke(cv, pts, widths, color, alpha=1.0):
    pts = np.asarray(pts)
    widths = np.asarray(widths, np.float64)
    if widths.ndim == 0:
        widths = np.full(len(pts), float(widths))
    pad = widths.max() + 3 * cv.px
    sl, XX, ZZ = cv.region(pts[:, 0].min() - pad, pts[:, 0].max() + pad,
                           pts[:, 1].min() - pad, pts[:, 1].max() + pad)
    if XX.size == 0:
        return
    d = stroke_dist(XX, ZZ, pts, widths)
    cv.paint(sl, cv.cover(d) * alpha, color)


def taper(n, w0, wm, w1, peak=0.6):
    t = np.linspace(0, 1, n)
    return np.where(t < peak, w0 + (wm - w0) * np.sin(0.5 * np.pi * t / peak),
                    wm + (w1 - wm) * np.clip((t - peak) / (1 - peak), 0, 1) ** 1.5)


def ellipse_dist(XX, ZZ, c, r, ang=0.0):
    ca, sa = math.cos(ang), math.sin(ang)
    dx, dz = XX - c[0], ZZ - c[1]
    u = (dx * ca + dz * sa) / r[0]
    v = (-dx * sa + dz * ca) / r[1]
    k = np.sqrt(u * u + v * v)
    return (k - 1.0) * min(r)


def bezier3(p0, p1, p2, p3, n=48):
    t = np.linspace(0.0, 1.0, n)[:, None]
    p0, p1, p2, p3 = (np.asarray(p, np.float64) for p in (p0, p1, p2, p3))
    return ((1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3)


# Eye placement (world, front view). Big round eyes, set low on the face (~37 % of the
# head height above the chin) like the reference art.
EYE_C = (0.0330, 1.4685)
EYE_SCALE = 1.0
LASH = (0.22, 0.12, 0.18)


def paint_eye(cv, side):
    """Eye drawn in local coordinates (u toward the outer corner, v up), metres."""
    s = 1.0 if side > 0 else -1.0
    k_ = EYE_SCALE

    def P(u, v):
        return (s * (EYE_C[0] + u * k_), EYE_C[1] + v * k_)

    def pts(arr):
        return np.array([P(u, v) for u, v in arr])

    up_l = bezier3((-0.0195, -0.0010), (-0.0180, 0.0210), (0.0150, 0.0230), (0.0205, -0.0035))
    lo_l = bezier3((-0.0180, -0.0065), (-0.0130, -0.0175), (0.0120, -0.0180), (0.0190, -0.0075))
    up, lo = pts(up_l), pts(lo_l)
    x0, x1 = sorted([P(-0.026, 0)[0], P(0.032, 0)[0]])
    sl, XX, ZZ = cv.region(x0, x1, P(0, -0.024)[1], P(0, 0.045)[1])
    # local coordinates of every pixel in the region
    U = (XX * s - EYE_C[0]) / k_
    V = (ZZ - EYE_C[1]) / k_
    zu = np.interp(U, up_l[:, 0], up_l[:, 1], left=np.nan, right=np.nan)
    zl = np.interp(U, lo_l[:, 0], lo_l[:, 1], left=np.nan, right=np.nan)
    inside_u = np.minimum(U + 0.0185, 0.0198 - U)
    d_open = np.maximum.reduce([V - np.nan_to_num(zu, nan=-1.0), np.nan_to_num(zl, nan=1.0) - V,
                                -inside_u * 0.7]) * k_
    open_cov = cv.cover(d_open)
    # sclera with a soft lavender shadow under the upper lid
    shade = np.clip((np.nan_to_num(zu, nan=0.0) - V) / 0.010, 0, 1)[..., None]
    white = np.array([0.78, 0.76, 0.90]) * (1 - shade) + np.array([1.0, 0.99, 1.0]) * shade
    cv.paint(sl, open_cov, white)
    # iris: deep violet at the top -> pink-violet -> warm amber at the bottom (Ganyu's gradient)
    ic_l = (0.0005, -0.0012)
    ir_l = (0.0116, 0.0142)
    du = (U - ic_l[0]) / ir_l[0]
    dv = (V - ic_l[1]) / ir_l[1]
    d_iris = (np.sqrt(du * du + dv * dv) - 1.0) * min(ir_l) * k_
    iris_cov = cv.cover(d_iris) * open_cov
    t = np.clip((V - (ic_l[1] - ir_l[1])) / (2 * ir_l[1]), 0, 1)
    stops_t = [0.0, 0.14, 0.30, 0.50, 0.74, 1.0]
    stops_c = np.array([(1.00, 0.76, 0.46), (0.98, 0.66, 0.54), (0.92, 0.58, 0.76), (0.72, 0.42, 0.80),
                        (0.44, 0.21, 0.60), (0.25, 0.10, 0.37)])
    col = np.stack([np.interp(t, stops_t, stops_c[:, c]) for c in range(3)], -1)
    rim = (np.clip(1.0 - np.abs(d_iris) / (0.0013 * k_), 0, 1) * (d_iris < 0))[..., None]
    col = col * (1 - 0.65 * rim) + np.array([0.18, 0.06, 0.24]) * 0.65 * rim
    cv.paint(sl, iris_cov, col)
    # radial streaks (subtle)
    ang = np.arctan2(dv, du)
    streak = (0.5 + 0.5 * np.cos(ang * 14.0)) * np.clip(np.sqrt(du * du + dv * dv), 0, 1)
    cv.paint(sl, iris_cov * streak * 0.10, (0.25, 0.10, 0.35))
    # pupil
    dp = np.sqrt(((U - 0.0005) / 0.0040) ** 2 + ((V - 0.0002) / 0.0066) ** 2)
    cv.paint(sl, cv.cover((dp - 1.0) * 0.0040 * k_) * open_cov * 0.95, (0.16, 0.05, 0.22))
    # shadow of the upper lid / lashes across the top of the iris
    lid_sh = np.clip(1 - (np.nan_to_num(zu, nan=0.0) - V) / 0.0060, 0, 1) * iris_cov
    cv.paint(sl, lid_sh * 0.45, (0.16, 0.06, 0.25))
    # warm glow in the lower iris
    dg = np.sqrt(((U - 0.0005) / 0.0080) ** 2 + ((V + 0.0090) / 0.0042) ** 2)
    cv.paint(sl, np.clip(1.0 - dg, 0, 1) ** 1.5 * iris_cov * 0.45, (1.0, 0.86, 0.74))
    # highlights: big soft one top-inner, small ones bottom-outer
    for (hu, hv, ru, rv, a, alpha) in ((-0.0048, 0.0058, 0.0036, 0.0044, 0.35, 1.0),
                                       (0.0052, -0.0072, 0.0019, 0.0014, 0.0, 0.95),
                                       (0.0058, 0.0046, 0.0010, 0.0010, 0.0, 0.85)):
        ca, sa = math.cos(a * s), math.sin(a * s)
        qu, qv = (U - hu) * ca + (V - hv) * sa, -(U - hu) * sa + (V - hv) * ca
        dh = (np.sqrt((qu / ru) ** 2 + (qv / rv) ** 2) - 1.0) * min(ru, rv) * k_
        cv.paint(sl, cv.cover(dh) * open_cov * alpha, (1.0, 1.0, 1.0))
    # upper lash line (thick toward the outer corner) with an up-swept flick
    draw_stroke(cv, up, taper(len(up), 0.0005 * k_, 0.0021 * k_, 0.0016 * k_, 0.70), LASH)
    flick = pts(bezier3((0.0165, 0.0035), (0.0215, 0.0045), (0.0245, 0.0060), (0.0272, 0.0095), 16))
    draw_stroke(cv, flick, np.linspace(0.0017, 0.0002, 16) * k_, LASH)
    for (a0, a1, a2) in (((0.0125, 0.0120), (0.0175, 0.0160), (0.0215, 0.0200)),
                         ((0.0055, 0.0150), (0.0090, 0.0195), (0.0115, 0.0240))):
        draw_stroke(cv, pts(bezier(a0, a1, a2, 12)), np.linspace(0.0010, 0.0001, 12) * k_, LASH)
    # lower lid: soft line on the outer half plus one small lash
    n = len(lo_l)
    draw_stroke(cv, lo[n // 3:], taper(n - n // 3, 0.00015, 0.00045, 0.0003, 0.7) * k_, (0.55, 0.30, 0.36), 0.85)
    draw_stroke(cv, pts(bezier((0.0160, -0.0095), (0.0185, -0.0112), (0.0205, -0.0138), 10)),
                np.linspace(0.0005, 0.0001, 10) * k_, (0.45, 0.24, 0.30))
    # double-eyelid crease and eyebrow (brows mostly sit under the bangs)
    crease = pts(bezier3((-0.0150, 0.0118), (-0.0125, 0.0255), (0.0120, 0.0272), (0.0190, 0.0085), 28))
    draw_stroke(cv, crease, taper(28, 0.0001, 0.00032, 0.0001, 0.5) * k_, (0.80, 0.56, 0.58), 0.65)
    brow = pts(bezier((-0.0175, 0.0330), (0.0010, 0.0392), (0.0205, 0.0335), 32))
    draw_stroke(cv, brow, taper(32, 0.0009, 0.0010, 0.0002, 0.3) * k_, (0.48, 0.54, 0.72))


def face_texture(tex_dir, span, z0, size=2048, hair_srgb=(0.62, 0.74, 0.92)):
    img = np.empty((size, size, 3), np.float32)
    img[:] = SKIN_SRGB
    cv = FaceCanvas(img, span, z0)
    # scalp in hair colour above the hairline (insurance under the hair cap)
    U = (np.arange(size) + 0.5) / size
    UU, VV = np.meshgrid(U, U)
    front = UU < 0.5
    X = np.where(front, (UU - 0.25) * span, -(UU - 0.75) * span)
    Z = z0 + VV * span
    hairline = np.where(front, 1.548 - 0.06 * np.clip(np.abs(X) / 0.07, 0, 1) ** 2, 1.40)
    scalp = np.clip((Z - hairline) / 0.006, 0, 1)[..., None]
    img[:] = img * (1 - scalp) + np.asarray(hair_srgb) * scalp
    # soft blush under the eyes
    for s in (1, -1):
        sl, XX, ZZ = cv.region(s * 0.041 - 0.024, s * 0.041 + 0.024, 1.432, 1.462)
        d = ellipse_dist(XX, ZZ, (s * 0.0405, 1.4465), (0.0130, 0.0058))
        cov = np.clip(1.0 - (d + 0.004) / 0.011, 0, 1) ** 2 * 0.42
        cv.paint(sl, cov, (1.0, 0.68, 0.72))
    for s in (1, -1):
        paint_eye(cv, s)
    # tiny nose and a small soft smile
    nose = bezier((0.0019, 1.4458), (0.0021, 1.4442), (0.0006, 1.4430), 12)
    draw_stroke(cv, nose, np.linspace(0.00018, 0.00030, 12), (0.88, 0.64, 0.62), 0.75)
    mouth = bezier((-0.0044, 1.4214), (0.0, 1.4196), (0.0044, 1.4214), 24)
    draw_stroke(cv, mouth, taper(24, 0.0001, 0.00038, 0.0001, 0.5), (0.80, 0.42, 0.50))
    lip = bezier((-0.0022, 1.4183), (0.0, 1.4177), (0.0022, 1.4183), 12)
    draw_stroke(cv, lip, taper(12, 0.0001, 0.00026, 0.0001, 0.5), (0.96, 0.72, 0.74), 0.55)
    p = os.path.join(tex_dir, "T_Face_BaseColor.png")
    tb.save_png(img, p)
    return p
