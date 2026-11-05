"""Texture helpers: UV-space rasterization of mesh attributes and PNG output.

Image arrays are (H, W, C) float in [0,1], row 0 is the BOTTOM row (v = 0), matching
Blender's pixel order and UV convention.
"""
import os

import numpy as np
import bpy
import bmesh


def uv_unwrap(ob, method='ANGLE_BASED', margin=0.004, pack=True, rotate=True):
    ctx = bpy.context
    for o in ctx.view_layer.objects:
        if o is not None:
            o.select_set(False)
    ob.select_set(True)
    ctx.view_layer.objects.active = ob
    if not ob.data.uv_layers:
        ob.data.uv_layers.new(name="UVMap")
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.unwrap(method=method, margin=margin)
    if pack:
        bpy.ops.uv.pack_islands(margin=margin, rotate=rotate)
    bpy.ops.object.mode_set(mode='OBJECT')


def mesh_triangles(ob, uv_name=None):
    """Returns dict with per-triangle uv (T,3,2), position (T,3,3), normal (T,3,3), poly index (T,),
    material index (T,) and vertex index (T,3)."""
    me = ob.data
    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    tl = np.zeros(nt * 3, np.int32)
    me.loop_triangles.foreach_get("loops", tl)
    tv = np.zeros(nt * 3, np.int32)
    me.loop_triangles.foreach_get("vertices", tv)
    tp = np.zeros(nt, np.int32)
    me.loop_triangles.foreach_get("polygon_index", tp)
    uvl = me.uv_layers[uv_name] if uv_name else me.uv_layers.active
    uv = np.zeros(len(me.loops) * 2, np.float32)
    uvl.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    co = np.zeros(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    cn = np.zeros(len(me.loops) * 3, np.float32)
    me.corner_normals.foreach_get("vector", cn)
    cn = cn.reshape(-1, 3)
    mi = np.zeros(len(me.polygons), np.int32)
    me.polygons.foreach_get("material_index", mi)
    tl = tl.reshape(-1, 3)
    tv = tv.reshape(-1, 3)
    M = np.asarray(ob.matrix_world)
    pos = co[tv] @ M[:3, :3].T + M[:3, 3]
    nrmw = cn[tl] @ np.linalg.inv(M[:3, :3])
    return dict(uv=uv[tl], pos=pos, nrm=nrmw, poly=tp, mat=mi[tp], vidx=tv, loops=tl)


def rasterize(tri_uv, tri_attr, W, H, tri_ids=None, pad=6):
    """Rasterize per-corner attributes (T,3,C) into a (H,W,C) map. Returns (img, mask, tri_index)."""
    T = len(tri_uv)
    C = tri_attr.shape[2]
    out = np.zeros((H, W, C), np.float32)
    mask = np.zeros((H, W), bool)
    tid = -np.ones((H, W), np.int32)
    px = tri_uv[..., 0] * W - 0.5
    py = tri_uv[..., 1] * H - 0.5
    for t in range(T):
        xa, xb = px[t].min(), px[t].max()
        ya, yb = py[t].min(), py[t].max()
        x0, x1 = max(int(np.floor(xa)), 0), min(int(np.ceil(xb)), W - 1)
        y0, y1 = max(int(np.floor(ya)), 0), min(int(np.ceil(yb)), H - 1)
        if x1 < x0 or y1 < y0:
            continue
        ax, ay = px[t, 0], py[t, 0]
        bx, by = px[t, 1], py[t, 1]
        cx, cy = px[t, 2], py[t, 2]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-12:
            continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        l0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / den
        l1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / den
        l2 = 1.0 - l0 - l1
        e = -1e-3
        ins = (l0 >= e) & (l1 >= e) & (l2 >= e)
        if not ins.any():
            continue
        A = tri_attr[t]
        vals = l0[ins, None] * A[0] + l1[ins, None] * A[1] + l2[ins, None] * A[2]
        yy, xx = Y[ins], X[ins]
        out[yy, xx] = vals
        mask[yy, xx] = True
        tid[yy, xx] = t if tri_ids is None else tri_ids[t]
    if pad > 0:
        out, tid = dilate(out, mask, pad, tid)
    return out, mask, tid


def dilate(img, mask, iters, ids=None):
    img = img.copy()
    m = mask.copy()
    ids = None if ids is None else ids.copy()
    H, W = m.shape
    for _ in range(iters):
        acc = np.zeros_like(img)
        cnt = np.zeros((H, W), np.float32)
        best = None if ids is None else ids.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sm = np.zeros_like(m)
            si = np.zeros_like(img)
            ys = slice(max(dy, 0), H + min(dy, 0))
            yd = slice(max(-dy, 0), H + min(-dy, 0))
            xs = slice(max(dx, 0), W + min(dx, 0))
            xd = slice(max(-dx, 0), W + min(-dx, 0))
            sm[yd, xd] = m[ys, xs]
            si[yd, xd] = img[ys, xs]
            acc += si * sm[..., None]
            cnt += sm
            if ids is not None:
                sid = -np.ones_like(ids)
                sid[yd, xd] = ids[ys, xs]
                take = (best < 0) & sm & ~m
                best[take] = sid[take]
        grow = (~m) & (cnt > 0)
        if not grow.any():
            break
        img[grow] = acc[grow] / cnt[grow][:, None]
        if ids is not None:
            ids = best
        m = m | grow
    return img, ids


def bake_maps(ob, size, uv_name=None, pad=6):
    """Position / normal / material-index maps of ob in its UV space."""
    tr = mesh_triangles(ob, uv_name)
    attr = np.concatenate([tr['pos'], tr['nrm']], axis=2)
    img, mask, tid = rasterize(tr['uv'], attr, size, size, pad=pad)
    pos = img[..., :3]
    nrm = img[..., 3:6]
    nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-8)
    mat = np.where(tid >= 0, tr['mat'][np.maximum(tid, 0)], -1)
    return dict(pos=pos, nrm=nrm, mask=mask, tri=tid, mat=mat, tris=tr)


def save_png(arr, path, name=None, alpha=None):
    arr = np.clip(np.asarray(arr, np.float32), 0.0, 1.0)
    if arr.ndim == 2:
        arr = arr[..., None]
    H, W, C = arr.shape
    rgba = np.ones((H, W, 4), np.float32)
    if C == 1:
        rgba[..., :3] = arr
    else:
        rgba[..., :min(C, 4)] = arr[..., :min(C, 4)]
    if alpha is not None:
        rgba[..., 3] = alpha
    os.makedirs(os.path.dirname(path), exist_ok=True)
    name = name or os.path.splitext(os.path.basename(path))[0]
    img = bpy.data.images.get(name)
    if img is not None:
        bpy.data.images.remove(img)
    img = bpy.data.images.new(name, W, H, alpha=True)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    img.filepath = path
    return img


def load_image(path, non_color=False):
    img = bpy.data.images.load(path, check_existing=True)
    if non_color:
        img.colorspace_settings.name = 'Non-Color'
    return img

# ---------------------------------------------------------------------------
# 2D drawing primitives on (H,W) coordinate grids
# ---------------------------------------------------------------------------

def aa_fill(d, px):
    """Anti-aliased coverage from a signed distance (negative inside) with px = pixel size."""
    return np.clip(0.5 - d / px, 0.0, 1.0)


def blend(dst, color, cov):
    color = np.asarray(color, np.float32)
    return dst * (1.0 - cov[..., None]) + color * cov[..., None]


def height_to_normal(h, strength=1.0):
    """Tileable height (H,W) -> tangent space normal map (H,W,3) in [0,1]."""
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(h)], axis=2)
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * 0.5 + 0.5


def knit_height(size=256, wales=8, courses=10):
    """Jersey knit: columns of interlocking V-shaped loops. Tileable."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    u = (x * wales) % 1.0
    v = (y * courses + 0.5 * (np.floor(x * wales) % 2) * 0.0) % 1.0
    # two slanted legs per stitch: centre lines x = 0.5 +- 0.28*(v-0.5)
    h = np.zeros_like(u)
    for sgn in (-1.0, 1.0):
        cxl = 0.5 + sgn * (0.22 - 0.30 * (v - 0.5))
        d = np.abs(u - cxl)
        h = np.maximum(h, np.clip(1.0 - (d / 0.20) ** 2, 0.0, 1.0))
    # course crossings slightly lower
    h *= 0.75 + 0.25 * np.cos((v - 0.5) * np.pi) ** 2
    return h
