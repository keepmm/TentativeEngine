"""2D painters for the cloth / accessory textures (sRGB). Coordinates are in pixels,
row 0 = bottom (v = 0)."""
import math
import os

import numpy as np

import texbake as tb
from common import smoothstep

GOLD = np.array([0.90, 0.74, 0.43])
GOLD_HI = np.array([1.00, 0.90, 0.62])
GOLD_DK = np.array([0.55, 0.38, 0.20])
WHITE = np.array([0.975, 0.965, 0.985])


class Canvas:
    def __init__(self, W, H, base=(1, 1, 1)):
        self.W, self.H = W, H
        self.img = np.empty((H, W, 3), np.float32)
        self.img[:] = base

    def region(self, x0, x1, y0, y1):
        j0, j1 = max(int(math.floor(x0)), 0), min(int(math.ceil(x1)) + 1, self.W)
        i0, i1 = max(int(math.floor(y0)), 0), min(int(math.ceil(y1)) + 1, self.H)
        X, Y = np.meshgrid(np.arange(j0, j1) + 0.5, np.arange(i0, i1) + 0.5)
        return (slice(i0, i1), slice(j0, j1)), X, Y

    def paint(self, sl, cov, color):
        c = np.asarray(color, np.float32)
        if c.ndim == 1:
            c = c[None, None, :]
        self.img[sl] = self.img[sl] * (1 - cov[..., None]) + c * cov[..., None]

    def stroke(self, pts, widths, color, alpha=1.0, wrap_x=False):
        pts = np.asarray(pts, np.float64)
        widths = np.broadcast_to(np.asarray(widths, np.float64), (len(pts),))
        pad = widths.max() + 2
        offsets = [0.0]
        if wrap_x:
            offsets = [-self.W, 0.0, self.W]
        for ox in offsets:
            q = pts + np.array([ox, 0.0])
            x0, x1 = q[:, 0].min() - pad, q[:, 0].max() + pad
            if x1 < 0 or x0 > self.W:
                continue
            sl, X, Y = self.region(x0, x1, q[:, 1].min() - pad, q[:, 1].max() + pad)
            if X.size == 0:
                continue
            best = np.full(X.shape, 1e9)
            for k in range(len(q) - 1):
                a, b = q[k], q[k + 1]
                ab = b - a
                L2 = max(float(ab @ ab), 1e-9)
                t = np.clip(((X - a[0]) * ab[0] + (Y - a[1]) * ab[1]) / L2, 0, 1)
                dx = X - (a[0] + t * ab[0])
                dy = Y - (a[1] + t * ab[1])
                w = widths[k] + t * (widths[k + 1] - widths[k])
                best = np.minimum(best, np.sqrt(dx * dx + dy * dy) - w)
            self.paint(sl, np.clip(0.5 - best, 0, 1) * alpha, color)

    def gold_stroke(self, pts, width):
        """Gold line with dark outline and a light core (cel look)."""
        self.stroke(pts, width + 1.6, GOLD_DK)
        self.stroke(pts, width, GOLD)
        self.stroke(pts, np.maximum(width * 0.35, 0.6), GOLD_HI, 0.8)

    def save(self, path):
        tb.save_png(self.img, path)
        return path


def spiral(cx, cy, r0, turns, start=0.0, ccw=True, n=80, squash=1.0):
    th = np.linspace(0, turns * 2 * np.pi, n)
    r = r0 * (1.0 - th / (turns * 2 * np.pi) * 0.85)
    s = 1.0 if ccw else -1.0
    a = start + s * th
    return np.stack([cx + r * np.cos(a), cy + r * np.sin(a) * squash], 1)


def cloud_scroll(cv, x, y, size, flip=False, up=True, width=None):
    """Chinese cloud-like scroll: a stem curling into a spiral, plus a smaller second curl."""
    s = -1.0 if flip else 1.0
    v = 1.0 if up else -1.0
    w = width or max(size * 0.07, 2.0)
    stem = np.array([[x, y], [x + s * size * 0.25, y + v * size * 0.35], [x + s * size * 0.55, y + v * size * 0.55]])
    sp = spiral(x + s * size * 0.75, y + v * size * 0.55, size * 0.25, 1.4, start=math.pi if s > 0 else 0.0,
                ccw=(s * v) < 0)
    cv.gold_stroke(np.vstack([stem, sp]), w)
    sp2 = spiral(x + s * size * 0.20, y + v * size * 0.05, size * 0.16, 1.2, start=0.0 if s > 0 else math.pi,
                 ccw=(s * v) > 0)
    cv.gold_stroke(sp2, w * 0.8)


def front_panel_texture(tex_dir, W=512, H=1024):
    cv = Canvas(W, H, WHITE)
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    # light blue gradient at the hem (v = 0 is the bottom edge)
    blue = smoothstep(0.16, 0.0, V)
    cv.img[:] = cv.img * (1 - blue[..., None] * 0.55) + np.array([0.62, 0.70, 0.98]) * blue[..., None] * 0.55
    # soft vertical shading toward the side borders
    cv.img *= (1.0 - 0.05 * smoothstep(0.25, 0.0, np.minimum(U, 1 - U)))[..., None]
    # gold side borders
    for x in (0.075 * W, 0.925 * W):
        cv.gold_stroke(np.array([[x, -5], [x, H + 5]]), 0.030 * W)
    cv.gold_stroke(np.array([[-5, H - 6], [W + 5, H - 6]]), 5)
    # cloud scrolls along both borders
    for v0, size in ((0.80, 0.40), (0.55, 0.46), (0.30, 0.40)):
        cloud_scroll(cv, 0.10 * W, v0 * H, size * W, flip=False, up=True, width=0.022 * W)
        cloud_scroll(cv, 0.90 * W, (v0 - 0.12) * H, size * W, flip=True, up=True, width=0.022 * W)
    # scalloped hem line
    xs = np.linspace(0, W, 60)
    ys = 0.035 * H + 0.012 * H * np.abs(np.sin(xs / W * np.pi * 7))
    cv.stroke(np.stack([xs, ys], 1), 2.0, (0.55, 0.62, 0.95), 0.8)
    return cv.save(os.path.join(tex_dir, "T_FrontPanel_BaseColor.png"))


def back_panel_texture(tex_dir, W=1024, H=512):
    base = np.array([0.98, 0.93, 0.96])
    cv = Canvas(W, H, base)
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    lav = smoothstep(0.35, 0.0, V)
    cv.img[:] = cv.img * (1 - lav[..., None] * 0.35) + np.array([0.78, 0.72, 0.95]) * lav[..., None] * 0.35
    for x in (0.04 * W, 0.96 * W):
        cv.gold_stroke(np.array([[x, -5], [x, H + 5]]), 0.012 * W)
    cv.gold_stroke(np.array([[-5, H - 5], [W + 5, H - 5]]), 4)
    cv.gold_stroke(np.array([[-5, 6], [W + 5, 6]]), 4)
    for (x, y, size, flip) in ((0.22, 0.25, 0.22, False), (0.78, 0.25, 0.22, True), (0.36, 0.55, 0.16, True),
                               (0.64, 0.55, 0.16, False), (0.12, 0.70, 0.14, False), (0.88, 0.70, 0.14, True)):
        cloud_scroll(cv, x * W, y * H, size * W, flip=flip, up=True)
    return cv.save(os.path.join(tex_dir, "T_BackPanel_BaseColor.png"))


def crystal_gradient(V, stops):
    keys = [s[0] for s in stops]
    cols = np.array([s[1] for s in stops])
    return np.stack([np.interp(V, keys, cols[:, c]) for c in range(3)], -1)


def facets(cv, U, V, period_u, v0, v1, strength=0.22, phase=0.0):
    """Lighter / darker diamond facets (crystal look) between v0..v1."""
    fu = (U / period_u + phase) % 1.0
    fv = (V - v0) / (v1 - v0)
    inside = (fv > 0) & (fv < 1)
    a = np.abs(fu - 0.5) * 2.0
    rows = 3.0
    g = (fv * rows) % 1.0
    dia = (a + np.abs(g - 0.5) * 2.0)
    light = (dia < 0.75) & inside
    edge = (np.abs(dia - 0.75) < 0.05) & inside
    cv.img[light] = np.clip(cv.img[light] * (1 + strength), 0, 1)
    cv.img[edge] = cv.img[edge] * (1 - strength * 0.8) + strength * 0.8 * np.array([0.95, 0.95, 1.0])


def tail_texture(tex_dir, W=512, H=1024):
    cv = Canvas(W, H)
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    cv.img[:] = crystal_gradient(V, [(0.0, (0.20, 0.22, 0.70)), (0.30, (0.30, 0.32, 0.82)),
                                     (0.62, (0.55, 0.56, 0.93)), (0.85, (0.86, 0.82, 0.97)),
                                     (1.0, (0.96, 0.90, 0.97))])
    facets(cv, U, V, 0.5, 0.0, 0.70, 0.18)
    # gold edging on the upper part of both long edges
    for x in (0.03 * W, 0.97 * W):
        cv.gold_stroke(np.array([[x, 0.55 * H], [x, H + 5]]), 7)
    cloud_scroll(cv, 0.5 * W, 0.80 * H, 0.35 * W, flip=False, up=False)
    return cv.save(os.path.join(tex_dir, "T_Tail_BaseColor.png"))


def sleeve_texture(tex_dir, W=1024, H=1024):
    cv = Canvas(W, H, WHITE)
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    grad = crystal_gradient(V, [(0.0, (0.24, 0.24, 0.72)), (0.14, (0.34, 0.34, 0.84)),
                                (0.30, (0.60, 0.64, 0.97)), (0.46, (0.90, 0.92, 1.0)),
                                (0.56, (0.985, 0.97, 0.99)), (1.0, (0.985, 0.97, 0.99))])
    cv.img[:] = grad
    pink = np.exp(-((V - 0.55) / 0.10) ** 2) * 0.10
    cv.img[:] = cv.img * (1 - pink[..., None]) + np.array([1.0, 0.80, 0.90]) * pink[..., None]
    facets(cv, U, V, 1.0 / 7.0, 0.0, 0.32, 0.20, phase=0.25)
    # crystal crown at the top band (triangles hanging from the top edge)
    n = 12
    fu = (U * n) % 1.0
    tri_h = 0.10 * (1 - np.abs(fu - 0.5) * 2) + 0.025
    crown = (V > 1.0 - tri_h) & (V < 0.995)
    shade = np.clip((1.0 - V) / 0.125, 0, 1)
    col = np.array([0.42, 0.40, 0.88]) * (1 - shade[..., None]) + np.array([0.70, 0.72, 0.98]) * shade[..., None]
    cv.img[crown] = col[crown]
    cv.gold_stroke(np.array([[-5, 0.865 * H], [W + 5, 0.865 * H]]), 3)
    cv.gold_stroke(np.array([[-5, H - 4], [W + 5, H - 4]]), 3)
    # gold curls on the outer side
    for u0 in (0.75, 0.25):
        cloud_scroll(cv, u0 * W, 0.62 * H, 0.10 * W, flip=False, up=True)
        cloud_scroll(cv, u0 * W, 0.62 * H, 0.10 * W, flip=True, up=True)
    return cv.save(os.path.join(tex_dir, "T_Sleeve_BaseColor.png"))


def collar_texture(tex_dir, W=512, H=128):
    cv = Canvas(W, H, (0.11, 0.09, 0.12))
    for y in (0.10 * H, 0.90 * H):
        cv.gold_stroke(np.array([[-5, y], [W + 5, y]]), 3.5)
    return cv.save(os.path.join(tex_dir, "T_Collar_BaseColor.png"))


def hair_tie_texture(tex_dir, S=512):
    cv = Canvas(S, S, (0.10, 0.07, 0.09))
    c = S * 0.5
    red = np.array([0.72, 0.12, 0.16])
    for k in range(5):
        a = k * 2 * np.pi / 5 + np.pi / 2
        p = np.array([c + 0.17 * S * math.cos(a), c + 0.17 * S * math.sin(a)])
        sl, X, Y = cv.region(p[0] - 0.12 * S, p[0] + 0.12 * S, p[1] - 0.12 * S, p[1] + 0.12 * S)
        d = np.sqrt((X - p[0]) ** 2 + (Y - p[1]) ** 2) - 0.11 * S
        cv.paint(sl, np.clip(0.5 - d, 0, 1), red)
    sl, X, Y = cv.region(c - 0.1 * S, c + 0.1 * S, c - 0.1 * S, c + 0.1 * S)
    d = np.sqrt((X - c) ** 2 + (Y - c) ** 2) - 0.07 * S
    cv.paint(sl, np.clip(0.5 - d, 0, 1), GOLD)
    for t in np.linspace(0, 1, 28, endpoint=False):
        a = t * 2 * np.pi
        p = np.array([c + 0.42 * S * math.cos(a), c + 0.42 * S * math.sin(a)])
        sl, X, Y = cv.region(p[0] - 10, p[0] + 10, p[1] - 10, p[1] + 10)
        d = np.sqrt((X - p[0]) ** 2 + (Y - p[1]) ** 2) - 0.018 * S
        cv.paint(sl, np.clip(0.5 - d, 0, 1), red)
    return cv.save(os.path.join(tex_dir, "T_HairTie_BaseColor.png"))
