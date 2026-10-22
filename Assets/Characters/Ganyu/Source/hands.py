"""Gloved hands: palm / fingers / thumb / cuff SDF polygonized with surface nets.

Local frame of the LEFT hand: +X toward the finger tips (along the arm), +Z = back of the
hand, thumb on -Y. The right hand is the mirror image.
"""
import math

import numpy as np

from common import (nrm, rot_axis, smin, sd_round_cone, sd_round_box, sd_ellipsoid, surface_nets,
                    MeshBuilder, recalc_normals, smart_uv)
import body as B

# name, knuckle base, yaw (deg, + toward the pinky side), phalanx lengths, joint radii, curl per joint
FINGERS = [
    ("Index", (0.087, -0.024, 0.000), -4.0, (0.037, 0.023, 0.019), (0.0085, 0.0080, 0.0074, 0.0066), (8, 12, 10)),
    ("Middle", (0.090, -0.007, 0.000), 0.0, (0.041, 0.026, 0.020), (0.0088, 0.0083, 0.0077, 0.0068), (9, 13, 10)),
    ("Ring", (0.087, 0.010, 0.000), 4.0, (0.038, 0.024, 0.019), (0.0083, 0.0078, 0.0072, 0.0065), (10, 14, 11)),
    ("Little", (0.080, 0.026, -0.001), 9.0, (0.030, 0.019, 0.017), (0.0074, 0.0070, 0.0065, 0.0059), (12, 15, 12)),
]
THUMB = [(0.012, -0.022, -0.006), (0.032, -0.042, -0.021), (0.054, -0.055, -0.032), (0.074, -0.060, -0.040)]
THUMB_R = (0.0115, 0.0098, 0.0085, 0.0070)


def finger_points(base, yaw, lens, curls):
    a = math.radians(yaw)
    d = np.array([math.cos(a), math.sin(a), 0.0])
    side = np.array([-math.sin(a), math.cos(a), 0.0])
    pts = [np.asarray(base, dtype=np.float64)]
    for L, c in zip(lens, curls):
        d = rot_axis(side, math.radians(c)) @ d
        pts.append(pts[-1] + L * d)
    return pts


def hand_local_sdf(Q):
    palm = sd_round_box(Q, (0.046, 0.0, 0.0), (0.043, 0.036, 0.0105), 0.0095)
    Qw = Q * np.array([1.0, 1.0, 1.22])
    wrist = sd_round_cone(Qw, (0.0, 0.0, 0.0), (0.020, 0.0, 0.0), 0.0235, 0.0245)
    d = smin(palm, wrist, 0.012)
    cuff = sd_round_cone(Q, (-0.046, 0.0, 0.0), (0.004, 0.0, 0.0), 0.0280, 0.0255)
    d = smin(d, cuff, 0.006)
    thenar = sd_ellipsoid(Q, (0.024, -0.020, -0.006), (0.030, 0.017, 0.012))
    d = smin(d, thenar, 0.008)
    for _, base, yaw, lens, radii, curls in FINGERS:
        pts = finger_points(base, yaw, lens, curls)
        df = None
        for i in range(3):
            seg = sd_round_cone(Q, pts[i], pts[i + 1], radii[i], radii[i + 1])
            df = seg if df is None else np.minimum(df, seg)
        d = smin(d, df, 0.0045)
    dt = None
    for i in range(3):
        seg = sd_round_cone(Q, THUMB[i], THUMB[i + 1], THUMB_R[i], THUMB_R[i + 1])
        dt = seg if dt is None else np.minimum(dt, seg)
    return smin(d, dt, 0.008)


def hand_frame(side):
    """Rotation (columns = local axes in world) and origin of the hand frame."""
    d = B.arm_dir(1)
    up = np.array([0.0, 0.0, 1.0])
    z = nrm(up - (up @ d) * d)
    y = np.cross(z, d)
    R = np.stack([d, y, z], 1)
    O = B.wrist(1)
    return R, O


def joint_positions(side):
    """World positions of finger joints (for the armature)."""
    R, O = hand_frame(1)
    out = {}
    for name, base, yaw, lens, radii, curls in FINGERS:
        pts = finger_points(base, yaw, lens, curls)
        out[name] = [O + R @ p for p in pts]
    out["Thumb"] = [O + R @ np.asarray(p) for p in THUMB]
    if side < 0:
        out = {k: [v * np.array([-1, 1, 1]) for v in vs] for k, vs in out.items()}
    return out


def build_gloves(material, collection, h=0.0022):
    R, O = hand_frame(1)

    def world_sdf(P):
        return hand_local_sdf((P - O) @ R)
    # bounding box of the hand in world space
    loc = np.array([[x, y, z] for x in (-0.065, 0.19) for y in (-0.09, 0.06) for z in (-0.07, 0.035)])
    W = loc @ R.T + O
    V, F = surface_nets(world_sdf, W.min(0), W.max(0), h, project_iters=4, relax_iters=3)
    objs = []
    for side in (1, -1):
        mb = MeshBuilder()
        P = V.copy()
        if side < 0:
            P[:, 0] *= -1
        mb.add_verts(P)
        for f in F:
            mb.add_face(f if side > 0 else f[::-1])
        ob = mb.build("Glove_L" if side > 0 else "Glove_R", materials=[material], collection=collection)
        recalc_normals(ob)
        smart_uv(ob)
        objs.append(ob)
    return objs
