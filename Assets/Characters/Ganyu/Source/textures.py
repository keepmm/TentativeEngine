"""Procedural texture painting for the character (all colours in sRGB unless noted)."""
import math
import os

import numpy as np

import texbake as tb
from common import smoothstep

SKIN_SRGB = np.array([1.00, 0.925, 0.885])


def srgb_to_lin(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(np.asarray(c, np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def surface_scale(ob):
    """UV units per meter of surface (sqrt of area ratio)."""
    me = ob.data
    uvl = me.uv_layers.active
    uv = np.zeros(len(me.loops) * 2)
    uvl.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    a3 = 0.0
    a2 = 0.0
    for p in me.polygons:
        a3 += p.area
        idx = list(p.loop_indices)
        q = uv[idx]
        a2 += 0.5 * abs(np.dot(q[:, 0], np.roll(q[:, 1], -1)) - np.dot(q[:, 1], np.roll(q[:, 0], -1)))
    return math.sqrt(a2 / max(a3, 1e-9))


def tights_clearness(pos, body_mod):
    """Stretch-dependent clearness (0..1) of the tights fabric at world positions (...,3)."""
    shp = pos.shape[:-1]
    P = pos.reshape(-1, 3)
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    zl = np.clip(z, 0.09, 0.95)
    pl = body_mod.LEGS[1].params(zl)
    r_leg = 0.5 * pl[:, 2] + 0.25 * (pl[:, 3] + pl[:, 4])
    s_leg = smoothstep(0.029, 0.072, r_leg)
    # stretched over the knee cap a little more
    knee = np.exp(-((np.abs(x) - 0.068) ** 2 + (y + 0.04) ** 2 * 0.5 + (z - 0.455) ** 2) / (2 * 0.03 ** 2))
    s_leg = np.clip(s_leg + 0.25 * knee, 0, 1)
    bc = np.array([0.060, -0.10, 1.176])
    d_b = np.minimum(np.sum((P - bc) ** 2 * [1, 1, 1], 1), np.sum((P - bc * [-1, 1, 1]) ** 2, 1))
    bust = np.exp(-d_b / (2 * 0.045 ** 2))
    gc = np.array([0.062, 0.095, 0.855])
    d_g = np.minimum(np.sum((P - gc) ** 2, 1), np.sum((P - gc * [-1, 1, 1]) ** 2, 1))
    glute = np.exp(-d_g / (2 * 0.05 ** 2))
    s_torso = np.clip(0.42 + 0.50 * bust + 0.40 * glute, 0, 1)
    w_leg = 1.0 - smoothstep(0.80, 0.90, z)
    s = w_leg * s_leg + (1 - w_leg) * s_torso
    foot = 1.0 - smoothstep(0.095, 0.135, z)
    s = s * (1.0 - 0.75 * foot)
    return (0.28 + 0.50 * s).reshape(shp)


def body_textures(ob, tex_dir, body_mod, size=2048):
    """Returns paths: base color, tights mask (R clearness, G tights coverage)."""
    maps = tb.bake_maps(ob, size, pad=8)
    pos, mask, mat = maps['pos'], maps['mask'], maps['mat']
    clear = tights_clearness(pos, body_mod)
    tights = (mat == 1)
    # view independent approximation of the tights look for plain PBR shaders (N.V ~ 0.75)
    t_lin = np.array([0.018, 0.011, 0.020])
    s_lin = np.array([0.80, 0.50, 0.50])
    sheer = 0.20 * np.power(clear, 1.0 / 0.75)
    approx = lin_to_srgb(t_lin + (s_lin - t_lin) * sheer[..., None])
    skin = np.broadcast_to(SKIN_SRGB, pos.shape)
    base = np.where(tights[..., None], approx, skin)
    m = np.zeros(pos.shape, np.float32)
    m[..., 0] = clear
    m[..., 1] = tights
    p_base = os.path.join(tex_dir, "T_Body_BaseColor.png")
    p_mask = os.path.join(tex_dir, "T_Body_TightsMask.png")
    tb.save_png(base, p_base)
    tb.save_png(m, p_mask)
    return p_base, p_mask


def knit_normal(tex_dir, size=256):
    h = tb.knit_height(size, wales=8, courses=10)
    n = tb.height_to_normal(h, strength=6.0)
    p = os.path.join(tex_dir, "T_Tights_Knit_N.png")
    tb.save_png(n, p)
    return p


def _periodic_dist(u, c):
    return np.abs(((u - c + 0.5) % 1.0) - 0.5)


def hair_texture(tex_dir, v_len, size=1024):
    """u: around the strand (0..0.5 outer face), v: strand length / v_len.
    Light sky blue near the roots, deep blue towards the long ends, thin strand lines."""
    v = (np.arange(size) + 0.5) / size
    L = v * v_len
    keys = [0.0, 0.46, 0.60, 0.73, 0.86, 2.0]
    cols = np.array([(0.66, 0.75, 0.94), (0.64, 0.73, 0.94), (0.52, 0.63, 0.93), (0.33, 0.46, 0.90),
                     (0.22, 0.33, 0.83), (0.20, 0.30, 0.80)])
    grad = np.stack([np.interp(L, keys, cols[:, c]) for c in range(3)], 1)
    img = np.repeat(grad[:, None, :], size, axis=1).astype(np.float32)
    UU, VV = np.meshgrid((np.arange(size) + 0.5) / size, v)
    px = 1.0 / size
    rng = np.random.default_rng(3)
    for k in range(5):
        uc = rng.uniform(0.06, 0.44)
        w = rng.uniform(0.0015, 0.0035)
        amp = rng.uniform(0.002, 0.008)
        ph = rng.uniform(0, 6.28)
        dark = rng.uniform(0.06, 0.13)
        d = _periodic_dist(UU, uc + amp * np.sin(VV * 23.0 + ph)) - w
        cov = np.clip(0.5 - d / px, 0, 1) * smoothstep(0.0, 0.02, VV)
        img *= (1.0 - dark * cov)[..., None]
    # soften both side edges of the ribbon
    e = np.minimum.reduce([_periodic_dist(UU, 0.0), _periodic_dist(UU, 0.5)])
    img *= (1.0 - 0.12 * np.exp(-(e / 0.035) ** 2))[..., None]
    # inner face a little darker (seen through gaps)
    inner = (UU > 0.5).astype(np.float32)
    img *= (1.0 - 0.10 * inner)[..., None]
    p = os.path.join(tex_dir, "T_Hair_BaseColor.png")
    tb.save_png(img, p)
    return p


def horn_texture(tex_dir, size=512):
    """u: around the horn, v: base (0) -> tip (1). Dark plum-black with a red band."""
    UU, VV = np.meshgrid((np.arange(size) + 0.5) / size, (np.arange(size) + 0.5) / size)
    img = np.empty((size, size, 3), np.float32)
    img[:] = (0.13, 0.08, 0.11)
    px = 1.0 / size
    for c in (0.0, 0.5):
        half = 0.075 * (1.0 - smoothstep(0.55, 0.95, VV))
        d = _periodic_dist(UU, c) - half
        cov = np.clip(0.5 - d / px, 0, 1)
        red = np.stack([0.74 - 0.20 * VV, 0.12 + 0.0 * VV, 0.17 + 0.0 * VV], -1)
        img = img * (1 - cov[..., None]) + red * cov[..., None]
        edge = np.clip(1 - np.abs(d) / 0.012, 0, 1) * (half > 0.01)
        img *= (1 - 0.35 * edge)[..., None]
    # subtle growth rings
    img *= (1.0 - 0.06 * (0.5 + 0.5 * np.sin(VV * 90.0)))[..., None]
    p = os.path.join(tex_dir, "T_Horn_BaseColor.png")
    tb.save_png(img, p)
    return p
