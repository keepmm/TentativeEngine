"""Head: anime head SDF + sphere topology projected onto it, front-projected face UVs."""
import math

import numpy as np
import bpy

from common import (rot_x, smin, smax, sd_ellipsoid, sd_round_cone, project_to_sdf, Loft,
                    vertex_neighbors, laplacian_relax, MeshBuilder)

HEAD_CENTER = np.array([0.0, 0.008, 1.47])
# face texture projection: front half of the texture (u in [0, .5]) is an orthographic
# front view, back half (u in [.5, 1]) an orthographic back view
UV_SPAN = 0.34
UV_Z0 = 1.30
SEG = 64
RINGS = 44
# the whole head group (head, hair, horns, hair ornaments) is scaled up after building
HEAD_SCALE = 1.08
HEAD_PIVOT = np.array([0.0, 0.012, 1.385])


def head_xform(P):
    """Map a point from build space to the scaled head-group space."""
    return (np.asarray(P) - HEAD_PIVOT) * HEAD_SCALE + HEAD_PIVOT


FACE_ROWS = [  # lower face as horizontal sections: z, cx, cy, rx, ry_front, ry_back, n
    # short, round lower face with soft full cheeks (cute proportions: eyes at ~37 % of
    # the head height measured from the chin)
    (1.395, 0, -0.036, 0.011, 0.014, 0.028, 2.0),   # chin tip
    (1.402, 0, -0.030, 0.021, 0.030, 0.044, 2.0),
    (1.411, 0, -0.022, 0.033, 0.044, 0.055, 2.1),
    (1.422, 0, -0.015, 0.045, 0.055, 0.065, 2.2),   # mouth
    (1.436, 0, -0.010, 0.056, 0.065, 0.073, 2.3),
    (1.452, 0, -0.007, 0.064, 0.072, 0.080, 2.4),   # cheeks
    (1.470, 0, -0.004, 0.069, 0.076, 0.085, 2.5),   # eyes
    (1.490, 0, -0.002, 0.070, 0.077, 0.088, 2.5),
    (1.510, 0, 0.000, 0.068, 0.075, 0.088, 2.4),
]
FACE = Loft(FACE_ROWS)


def head_sdf(P):
    cran = sd_ellipsoid(P, (0, 0.012, 1.505), (0.0745, 0.088, 0.091))
    d = smin(cran, FACE.sdf(P), 0.03)
    nose = sd_ellipsoid(P, (0, -0.0790, 1.447), (0.0045, 0.0075, 0.0105), rot_x(math.radians(-25)))
    d = smin(d, nose, 0.008)
    neck = sd_round_cone(P, (0, 0.014, 1.36), (0, 0.012, 1.45), 0.0405, 0.039)
    return smin(d, neck, 0.02)


def face_uv(P, front):
    u = np.where(front, 0.25 + P[:, 0] / UV_SPAN, 0.75 - P[:, 0] / UV_SPAN)
    v = (P[:, 2] - UV_Z0) / UV_SPAN
    return np.stack([u, v], 1)


def build_head(material, collection, relax_iters=40):
    C = HEAD_CENTER
    R = np.array([0.085, 0.100, 0.135])
    th = np.pi * np.arange(1, RINGS + 1) / (RINGS + 1)
    ph = 2 * np.pi * np.arange(SEG) / SEG
    TH, PH = np.meshgrid(th, ph, indexing='ij')
    P = np.stack([R[0] * np.sin(TH) * np.sin(PH), -R[1] * np.sin(TH) * np.cos(PH), R[2] * np.cos(TH)], -1)
    P = P.reshape(-1, 3) + C
    top = C + [0, 0, R[2]]
    bot = C - [0, 0, R[2]]
    V = np.vstack([P, top[None], bot[None]])
    it, ib = len(P), len(P) + 1
    F = []
    col_of_face = []
    for k in range(RINGS - 1):
        for i in range(SEG):
            i1 = (i + 1) % SEG
            F.append([k * SEG + i, (k + 1) * SEG + i, (k + 1) * SEG + i1, k * SEG + i1])
            col_of_face.append(i)
    for i in range(SEG):
        i1 = (i + 1) % SEG
        F.append([it, i, i1])
        col_of_face.append(i)
        F.append([(RINGS - 1) * SEG + i, ib, (RINGS - 1) * SEG + i1])
        col_of_face.append(i)
    V = project_to_sdf(head_sdf, V, iters=12)
    nb = vertex_neighbors(F, len(V))
    V = laplacian_relax(V, nb, head_sdf, iters=relax_iters, lam=0.5)
    V = project_to_sdf(head_sdf, V, iters=6)

    mb = MeshBuilder()
    mb.add_verts(V)
    q = SEG // 4
    for f, c in zip(F, col_of_face):
        front = (c >= SEG - q) or (c < q)
        uv = face_uv(V[f], np.full(len(f), front))
        mb.add_face(f, uv, 0)
    ob = mb.build("Head", materials=[material], collection=collection)
    from common import recalc_normals
    recalc_normals(ob)
    return ob
