"""Shared helpers for the procedural character build.

Coordinates: Blender world, Z up, the character faces -Y, character's left = +X.
Units are meters.
"""
import math

import numpy as np
import bpy
import bmesh

# ---------------------------------------------------------------------------
# small math helpers
# ---------------------------------------------------------------------------

def nrm(v, eps=1e-12):
    v = np.asarray(v, dtype=np.float64)
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, eps)


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)


def rot_axis(axis, a):
    axis = nrm(axis)
    x, y, z = axis
    c, s = math.cos(a), math.sin(a)
    C = 1 - c
    return np.array([
        [c + x * x * C, x * y * C - z * s, x * z * C + y * s],
        [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
        [z * x * C - y * s, z * y * C + x * s, c + z * z * C]], dtype=np.float64)


def mirror_x(P):
    P = np.array(P, dtype=np.float64)
    P[..., 0] *= -1.0
    return P

# ---------------------------------------------------------------------------
# SDF primitives (vectorized: P is (N,3))
# ---------------------------------------------------------------------------

def smin(a, b, k):
    if k <= 0.0:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


def sd_sphere(P, c, r):
    return np.linalg.norm(P - np.asarray(c), axis=1) - r


def sd_ellipsoid(P, c, r, R=None):
    """Approximate ellipsoid SDF. R maps local->world (columns are local axes)."""
    q = P - np.asarray(c, dtype=np.float64)
    if R is not None:
        q = q @ np.asarray(R)
    r = np.asarray(r, dtype=np.float64)
    k0 = np.linalg.norm(q / r, axis=1)
    k1 = np.linalg.norm(q / (r * r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-12)


def sd_round_cone(P, a, b, r1, r2):
    """Exact round-cone (capsule with different end radii) SDF (after I. Quilez)."""
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
    end_b = np.sqrt(x2 + z2) * il2 - r2
    end_a = np.sqrt(x2 + y2) * il2 - r1
    return np.where(np.sign(z) * a2 * z2 > k, end_b,
                    np.where(np.sign(y) * a2 * y2 < k, end_a, body))


def sd_capsule(P, a, b, r):
    return sd_round_cone(P, a, b, r, r)


def sd_plane(P, p0, n):
    n = nrm(n)
    return (P - np.asarray(p0)) @ n


def sd_round_box(P, c, half, r, R=None):
    q = P - np.asarray(c, dtype=np.float64)
    if R is not None:
        q = q @ np.asarray(R)
    q = np.abs(q) - (np.asarray(half) - r)
    return (np.linalg.norm(np.maximum(q, 0.0), axis=1)
            + np.minimum(np.max(q, axis=1), 0.0) - r)


def sd_chain(P, pts, radii):
    """Union of round cones through a polyline."""
    d = None
    for i in range(len(pts) - 1):
        di = sd_round_cone(P, pts[i], pts[i + 1], radii[i], radii[i + 1])
        d = di if d is None else np.minimum(d, di)
    return d


def sdf_grad(f, P, h=2e-4):
    g = np.empty_like(P)
    for i in range(3):
        e = np.zeros(3)
        e[i] = h
        g[:, i] = (f(P + e) - f(P - e)) / (2 * h)
    return g


def project_to_sdf(f, P, iters=6, max_step=0.05):
    P = np.array(P, dtype=np.float64)
    for _ in range(iters):
        d = f(P)
        g = sdf_grad(f, P)
        g2 = np.maximum(np.einsum('ij,ij->i', g, g), 1e-10)
        step = (d / g2)[:, None] * g
        ln = np.linalg.norm(step, axis=1, keepdims=True)
        step = step * np.minimum(1.0, max_step / np.maximum(ln, 1e-12))
        P = P - step
    return P


def ray_hit_sdf(f, origins, dirs, t0=0.0, t1=1.0, steps=200):
    """First zero crossing of f along rays (bisection after coarse march). Returns t (nan if none)."""
    origins = np.atleast_2d(origins).astype(np.float64)
    dirs = nrm(np.atleast_2d(dirs))
    n = len(origins)
    ts = np.linspace(t0, t1, steps)
    prev_t = np.full(n, t0)
    prev_v = f(origins + dirs * t0)
    hit = np.full(n, np.nan)
    lo = np.full(n, np.nan)
    hi = np.full(n, np.nan)
    for t in ts[1:]:
        v = f(origins + dirs * t)
        cross = np.isnan(hit) & np.isnan(lo) & (prev_v > 0) & (v <= 0)
        lo[cross] = prev_t[cross]
        hi[cross] = t
        prev_t[:] = t
        prev_v = v
    ok = ~np.isnan(lo)
    if ok.any():
        a, b = lo[ok].copy(), hi[ok].copy()
        o, d = origins[ok], dirs[ok]
        for _ in range(30):
            m = 0.5 * (a + b)
            v = f(o + d * m[:, None])
            inside = v <= 0
            b = np.where(inside, m, b)
            a = np.where(inside, a, m)
        hit[ok] = 0.5 * (a + b)
    return hit

# ---------------------------------------------------------------------------
# 1D monotone interpolation (PCHIP) used for measured profiles
# ---------------------------------------------------------------------------

class Profile:
    """Columns of values sampled at increasing keys, PCHIP interpolated."""

    def __init__(self, keys, columns):
        self.x = np.asarray(keys, dtype=np.float64)
        self.Y = np.asarray(columns, dtype=np.float64)  # (n, c)
        if self.Y.ndim == 1:
            self.Y = self.Y[:, None]
        self.M = np.stack([self._slopes(self.x, self.Y[:, j]) for j in range(self.Y.shape[1])], 1)

    @staticmethod
    def _slopes(x, y):
        h = np.diff(x)
        d = np.diff(y) / h
        m = np.zeros_like(y)
        for k in range(1, len(x) - 1):
            if d[k - 1] * d[k] <= 0:
                m[k] = 0.0
            else:
                w1 = 2 * h[k] + h[k - 1]
                w2 = h[k] + 2 * h[k - 1]
                m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k])
        m[0] = d[0]
        m[-1] = d[-1]
        return m

    def __call__(self, xq):
        xq = np.clip(np.asarray(xq, dtype=np.float64), self.x[0], self.x[-1])
        i = np.clip(np.searchsorted(self.x, xq) - 1, 0, len(self.x) - 2)
        h = self.x[i + 1] - self.x[i]
        t = (xq - self.x[i]) / h
        t2, t3 = t * t, t * t * t
        h00 = 2 * t3 - 3 * t2 + 1
        h10 = t3 - 2 * t2 + t
        h01 = -2 * t3 + 3 * t2
        h11 = t3 - t2
        Y, M = self.Y, self.M
        return (h00[:, None] * Y[i] + (h10 * h)[:, None] * M[i]
                + h01[:, None] * Y[i + 1] + (h11 * h)[:, None] * M[i + 1])


class Loft:
    """Generalized cylinder with horizontal cross-sections.

    rows: (z, cx, cy, rx, ry_front, ry_back, n) where the section is a superellipse
    with exponent n; front is -Y.
    """

    def __init__(self, rows, mirror=False):
        rows = np.asarray(rows, dtype=np.float64)
        rows = rows[np.argsort(rows[:, 0])]
        if mirror:
            rows[:, 1] *= -1.0
        self.zmin, self.zmax = rows[0, 0], rows[-1, 0]
        self.prof = Profile(rows[:, 0], rows[:, 1:])

    def params(self, z):
        return self.prof(z)

    def section_point(self, z, ang, scale=1.0):
        """Point on the section at height z and polar angle ang (0=front, +90deg = +X)."""
        z = np.atleast_1d(z)
        ang = np.atleast_1d(ang)
        p = self.params(z)
        cx, cy, rx, ryf, ryb, n = p.T
        s, c = np.sin(ang), -np.cos(ang)
        ry = np.where(c < 0, ryf, ryb)
        # superellipse radius along direction (s, c)
        r = 1.0 / ((np.abs(s) / rx) ** n + (np.abs(c) / ry) ** n) ** (1.0 / n)
        r = r * scale
        return np.stack([cx + r * s, cy + r * c, z], 1)

    def sdf(self, P):
        z = P[:, 2]
        p = self.params(z)
        cx, cy, rx, ryf, ryb, n = p.T
        dx = P[:, 0] - cx
        dy = P[:, 1] - cy
        ry = np.where(dy < 0, ryf, ryb)
        u = np.abs(dx / rx) + 1e-12
        v = np.abs(dy / ry) + 1e-12
        f = (u ** n + v ** n) ** (1.0 / n)
        gu = f ** (1 - n) * u ** (n - 1) / rx
        gv = f ** (1 - n) * v ** (n - 1) / ry
        g = np.sqrt(gu * gu + gv * gv)
        d = (f - 1.0) / np.maximum(g, 1e-9)
        return np.maximum(d, np.maximum(self.zmin - z, z - self.zmax))

# ---------------------------------------------------------------------------
# splines / strands
# ---------------------------------------------------------------------------

def catmull_rom(points, samples_per_seg=16):
    P = np.asarray(points, dtype=np.float64)
    if len(P) < 2:
        return P
    ext = np.vstack([2 * P[0] - P[1], P, 2 * P[-1] - P[-2]])
    out = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        # centripetal parameterization
        def tj(ti, a, b):
            return ti + max(np.linalg.norm(b - a), 1e-6) ** 0.5
        t0 = 0.0
        t1 = tj(t0, p0, p1)
        t2 = tj(t1, p1, p2)
        t3 = tj(t2, p2, p3)
        ts = np.linspace(t1, t2, samples_per_seg, endpoint=False)[:, None]
        A1 = (t1 - ts) / (t1 - t0) * p0 + (ts - t0) / (t1 - t0) * p1
        A2 = (t2 - ts) / (t2 - t1) * p1 + (ts - t1) / (t2 - t1) * p2
        A3 = (t3 - ts) / (t3 - t2) * p2 + (ts - t2) / (t3 - t2) * p3
        B1 = (t2 - ts) / (t2 - t0) * A1 + (ts - t0) / (t2 - t0) * A2
        B2 = (t3 - ts) / (t3 - t1) * A2 + (ts - t1) / (t3 - t1) * A3
        C = (t2 - ts) / (t2 - t1) * B1 + (ts - t1) / (t2 - t1) * B2
        out.append(C)
    out.append(P[-1][None])
    return np.vstack(out)


def resample_polyline(P, n):
    P = np.asarray(P, dtype=np.float64)
    seg = np.linalg.norm(np.diff(P, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    L = s[-1]
    t = np.linspace(0.0, L, n)
    out = np.stack([np.interp(t, s, P[:, i]) for i in range(3)], 1)
    return out, L


def polyline_tangents(P):
    T = np.zeros_like(P)
    T[1:-1] = P[2:] - P[:-2]
    T[0] = P[1] - P[0]
    T[-1] = P[-1] - P[-2]
    return nrm(T)

# ---------------------------------------------------------------------------
# mesh building
# ---------------------------------------------------------------------------

class MeshBuilder:
    """Accumulates vertices / faces / per-corner UVs / material indices / vertex tags."""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.uvs = []        # list per face: list of (u,v)
        self.mats = []
        self.vtags = {}      # name -> list of per-vertex values
        self.nv = 0

    def add_verts(self, P, **tags):
        P = np.atleast_2d(np.asarray(P, dtype=np.float64))
        base = self.nv
        self.verts.append(P)
        self.nv += len(P)
        for k, v in tags.items():
            arr = self.vtags.setdefault(k, [])
            # pad previous
            if len(arr) < base:
                arr.extend([0.0] * (base - len(arr)))
            vv = np.broadcast_to(np.asarray(v, dtype=np.float64), (len(P),))
            arr.extend(vv.tolist())
        return np.arange(base, base + len(P))

    def add_face(self, idx, uv=None, mat=0):
        self.faces.append([int(i) for i in idx])
        self.uvs.append(None if uv is None else [tuple(map(float, u)) for u in uv])
        self.mats.append(int(mat))

    def all_verts(self):
        return np.vstack(self.verts) if self.verts else np.zeros((0, 3))

    def tag_array(self, name, default=0.0):
        arr = list(self.vtags.get(name, []))
        if len(arr) < self.nv:
            arr.extend([default] * (self.nv - len(arr)))
        return np.asarray(arr, dtype=np.float64)

    def build(self, name, materials=(), collection=None, smooth=True):
        V = self.all_verts()
        me = bpy.data.meshes.new(name)
        me.from_pydata(V.tolist(), [], self.faces)
        me.update()
        if any(u is not None for u in self.uvs):
            uvl = me.uv_layers.new(name="UVMap")
            flat = []
            for f, u in zip(self.faces, self.uvs):
                if u is None:
                    flat.extend([(0.0, 0.0)] * len(f))
                else:
                    flat.extend(u)
            uvl.data.foreach_set("uv", np.asarray(flat, dtype=np.float32).ravel())
        for m in materials:
            me.materials.append(m)
        if materials:
            me.polygons.foreach_set("material_index", np.asarray(self.mats, dtype=np.int32))
        if smooth:
            me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
        me.validate(clean_customdata=False)
        me.update()
        ob = bpy.data.objects.new(name, me)
        (collection or bpy.context.scene.collection).objects.link(ob)
        return ob


def grid_faces(rows, cols, closed_u=True):
    """Quad indices for a (rows x cols) vertex grid (row-major)."""
    faces = []
    cmax = cols if closed_u else cols - 1
    for r in range(rows - 1):
        for c in range(cmax):
            c1 = (c + 1) % cols
            faces.append((r * cols + c, r * cols + c1, (r + 1) * cols + c1, (r + 1) * cols + c))
    return faces


def object_from_arrays(name, V, F, uv=None, mat_idx=None, materials=(), collection=None, smooth=True):
    """uv: per-corner array aligned with F (list of lists) or None."""
    mb = MeshBuilder()
    mb.add_verts(V)
    for i, f in enumerate(F):
        mb.add_face(f, None if uv is None else uv[i], 0 if mat_idx is None else mat_idx[i])
    return mb.build(name, materials=materials, collection=collection, smooth=smooth)


def mesh_arrays(ob):
    me = ob.data
    V = np.zeros(len(me.vertices) * 3)
    me.vertices.foreach_get("co", V)
    return V.reshape(-1, 3)


def set_mesh_verts(ob, V):
    ob.data.vertices.foreach_set("co", np.asarray(V, dtype=np.float32).ravel())
    ob.data.update()


def vertex_neighbors(F, nv):
    nb = [set() for _ in range(nv)]
    for f in F:
        n = len(f)
        for i in range(n):
            a, b = f[i], f[(i + 1) % n]
            nb[a].add(b)
            nb[b].add(a)
    return [np.fromiter(s, dtype=np.int64) for s in nb]


def laplacian_relax(P, nbrs, sdf, iters=20, lam=0.5, fixed=None, project_iters=3):
    P = np.array(P, dtype=np.float64)
    counts = np.array([len(n) for n in nbrs])
    flat = np.concatenate(nbrs)
    owner = np.repeat(np.arange(len(nbrs)), counts)
    for _ in range(iters):
        acc = np.zeros_like(P)
        np.add.at(acc, owner, P[flat])
        avg = acc / np.maximum(counts, 1)[:, None]
        delta = avg - P
        if sdf is not None:
            n = nrm(sdf_grad(sdf, P))
            delta -= np.einsum('ij,ij->i', delta, n)[:, None] * n
        if fixed is not None:
            delta[fixed] = 0.0
        P = P + lam * delta
        if sdf is not None:
            Q = project_to_sdf(sdf, P, iters=project_iters)
            if fixed is not None:
                Q[fixed] = P[fixed]
            P = Q
    return P

# ---------------------------------------------------------------------------
# bmesh level-set cut
# ---------------------------------------------------------------------------

def bm_levelset_cut(bm, func, edge_ok=None, refine=12):
    """Insert vertices where func changes sign along edges and split faces there.

    func maps (N,3) -> (N,). Returns the set of new BMVerts (on the zero set).
    """
    bm.verts.ensure_lookup_table()
    verts = list(bm.verts)
    P = np.array([v.co[:] for v in verts])
    f = func(P)
    f = np.where(np.abs(f) < 1e-9, 1e-9, f)
    fv = {v: float(f[i]) for i, v in enumerate(verts)}
    cut_edges = [e for e in bm.edges
                 if fv[e.verts[0]] * fv[e.verts[1]] < 0 and (edge_ok is None or edge_ok(e))]
    new = set()
    for e in cut_edges:
        v0, v1 = e.verts
        a = np.array(v0.co[:])
        b = np.array(v1.co[:])
        fa, fb = fv[v0], fv[v1]
        lo, hi = 0.0, 1.0
        flo = fa
        # bisection on the true function along the edge
        for _ in range(refine):
            m = 0.5 * (lo + hi)
            fm = float(func((a + (b - a) * m)[None])[0])
            if (fm > 0) == (flo > 0):
                lo, flo = m, fm
            else:
                hi = m
        t = 0.5 * (lo + hi)
        _, nv = bmesh.utils.edge_split(e, v0, t)
        nv.co = a + (b - a) * t
        new.add(nv)
        fv[nv] = 0.0
    for face in list(bm.faces):
        vs = [v for v in face.verts if v in new]
        if len(vs) == 2:
            try:
                bmesh.utils.face_split(face, vs[0], vs[1])
            except ValueError:
                pass
        elif len(vs) > 2:
            # pair consecutive cut verts around the face
            order = [v for v in face.verts if v in new]
            for i in range(0, len(order) - 1, 2):
                try:
                    bmesh.utils.face_split(face, order[i], order[i + 1])
                except ValueError:
                    pass
    return new


def face_centers_bm(bm):
    return np.array([f.calc_center_median()[:] for f in bm.faces])

# ---------------------------------------------------------------------------
# scene helpers
# ---------------------------------------------------------------------------

def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.armatures,
                 bpy.data.curves, bpy.data.cameras, bpy.data.lights, bpy.data.node_groups,
                 bpy.data.actions):
        for item in list(coll):
            coll.remove(item)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    bpy.context.view_layer.update()


def get_collection(name):
    c = bpy.data.collections.get(name)
    if c is None:
        c = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(c)
    return c


def solidify_apply(ob, thickness, offset=-1.0):
    mod = ob.modifiers.new("Solidify", 'SOLIDIFY')
    mod.thickness = thickness
    mod.offset = offset
    mod.use_even_offset = True
    mod.use_quality_normals = True
    apply_modifiers(ob)


def apply_modifiers(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ob_eval = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ob_eval, preserve_all_data_layers=True, depsgraph=dg)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    me.name = old.name
    bpy.data.meshes.remove(old)


def recalc_normals(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()


def join_objects(objs, name=None):
    objs = [o for o in objs if o is not None]
    if len(objs) == 1:
        return objs[0]
    ctx = bpy.context
    for o in ctx.view_layer.objects:
        if o is not None:
            o.select_set(False)
    for o in objs:
        o.select_set(True)
    ctx.view_layer.objects.active = objs[0]
    with ctx.temp_override(active_object=objs[0], selected_editable_objects=objs,
                           selected_objects=objs):
        bpy.ops.object.join()
    ob = objs[0]
    if name:
        ob.name = name
        ob.data.name = name
    return ob


def sock(coll, ident):
    """Node socket lookup by identifier (names are not unique on e.g. the Mix node)."""
    for s in coll:
        if s.identifier == ident:
            return s
    for s in coll:
        if s.name == ident:
            return s
    raise KeyError(ident)


def orient_outward(ob, center):
    """Flip all faces if, on average, normals point toward `center` (for open shells)."""
    me = ob.data
    n = np.zeros(len(me.polygons) * 3)
    c = np.zeros(len(me.polygons) * 3)
    me.polygons.foreach_get("normal", n)
    me.polygons.foreach_get("center", c)
    n = n.reshape(-1, 3)
    c = c.reshape(-1, 3)
    if np.mean(np.einsum('ij,ij->i', n, c - np.asarray(center))) < 0:
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
        bm.to_mesh(me)
        bm.free()
        me.update()


def scale_about(ob, s, pivot):
    """Scale mesh data uniformly about a world-space pivot (object at identity)."""
    V = mesh_arrays(ob)
    V = (V - np.asarray(pivot)) * s + np.asarray(pivot)
    set_mesh_verts(ob, V)


def surface_nets(sdf, bmin, bmax, h, project_iters=4, relax_iters=0):
    """Polygonize sdf < 0 with naive surface nets. Returns (V, F) with quads."""
    bmin = np.asarray(bmin, np.float64)
    bmax = np.asarray(bmax, np.float64)
    n = np.ceil((bmax - bmin) / h).astype(int) + 1
    xs = [bmin[i] + h * np.arange(n[i]) for i in range(3)]
    G = np.stack(np.meshgrid(*xs, indexing='ij'), -1)
    D = sdf(G.reshape(-1, 3)).reshape(n[0], n[1], n[2])
    D = np.where(np.abs(D) < 1e-12, 1e-12, D)
    ins = D < 0
    nx, ny, nz = n
    # 8 corner offsets and 12 edges of a cell
    corners = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (0, 0, 1), (1, 0, 1), (0, 1, 1), (1, 1, 1)]
    edges = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]

    def csl(o):
        return (slice(o[0], nx - 1 + o[0]), slice(o[1], ny - 1 + o[1]), slice(o[2], nz - 1 + o[2]))
    cD = [D[csl(o)] for o in corners]
    cP = [G[csl(o)] for o in corners]
    acc = np.zeros((nx - 1, ny - 1, nz - 1, 3))
    cnt = np.zeros((nx - 1, ny - 1, nz - 1))
    for a, b in edges:
        da, db = cD[a], cD[b]
        m = (da < 0) != (db < 0)
        t = np.where(m, da / np.where(m, da - db, 1.0), 0.0)
        p = cP[a] + (cP[b] - cP[a]) * t[..., None]
        acc += p * m[..., None]
        cnt += m
    active = cnt > 0
    vid = -np.ones((nx - 1, ny - 1, nz - 1), np.int64)
    vid[active] = np.arange(int(active.sum()))
    V = acc[active] / cnt[active][:, None]
    F = []
    # x edges: point (i,j,k)-(i+1,j,k), cells (i, j-1..j, k-1..k)
    m = ins[:-1, 1:-1, 1:-1] != ins[1:, 1:-1, 1:-1]
    I, J, K = np.nonzero(m)
    J += 1
    K += 1
    for i, j, k in zip(I, J, K):
        q = [vid[i, j - 1, k - 1], vid[i, j, k - 1], vid[i, j, k], vid[i, j - 1, k]]
        F.append(q if ins[i, j, k] else q[::-1])
    m = ins[1:-1, :-1, 1:-1] != ins[1:-1, 1:, 1:-1]
    I, J, K = np.nonzero(m)
    I += 1
    K += 1
    for i, j, k in zip(I, J, K):
        q = [vid[i - 1, j, k - 1], vid[i - 1, j, k], vid[i, j, k], vid[i, j, k - 1]]
        F.append(q if ins[i, j, k] else q[::-1])
    m = ins[1:-1, 1:-1, :-1] != ins[1:-1, 1:-1, 1:]
    I, J, K = np.nonzero(m)
    I += 1
    J += 1
    for i, j, k in zip(I, J, K):
        q = [vid[i - 1, j - 1, k], vid[i, j - 1, k], vid[i, j, k], vid[i - 1, j, k]]
        F.append(q if ins[i, j, k] else q[::-1])
    F = [f for f in F if min(f) >= 0]
    if project_iters:
        V = project_to_sdf(sdf, V, iters=project_iters, max_step=h)
    if relax_iters:
        V = laplacian_relax(V, vertex_neighbors(F, len(V)), sdf, iters=relax_iters, lam=0.4)
    return V, F


def smart_uv(ob, angle=66.0, margin=0.01):
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
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin)
    bpy.ops.object.mode_set(mode='OBJECT')


def decimate(ob, ratio):
    if ratio >= 1.0:
        return
    mod = ob.modifiers.new("Decimate", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = ratio
    mod.use_collapse_triangulate = False
    apply_modifiers(ob)


def tri_count(ob):
    ob.data.calc_loop_triangles()
    return len(ob.data.loop_triangles)


def decimate_to(ob, target_tris):
    n = tri_count(ob)
    if n > target_tris:
        decimate(ob, target_tris / float(n))
