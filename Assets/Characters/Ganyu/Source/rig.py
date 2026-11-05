"""Armature and skin weights.

Humanoid bone names follow the Unity / VRM humanoid convention (Hips, Spine, Chest, ...)
with .L/.R suffixes, plus secondary chains for physics: Hair_Back_*, Hair_Side_*,
Tail_*, Tassel_*.
"""
import math

import numpy as np
import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree

from common import nrm, smoothstep, mesh_arrays, catmull_rom, resample_polyline
import body as B
import head as Hd
import hair as HR
import hands as HN
import outfit as OF

SECONDARY_PREFIX = ("Hair_", "Tail_", "Tassel_")


def V(*a):
    return Vector([float(x) for x in a])


def bone_layout():
    """name -> (head, tail, parent, roll_axis) in world space."""
    L = {}
    hip_c = np.array([0.0, 0.010, 0.900])
    L['Root'] = (np.array([0, 0, 0.0]), np.array([0, 0.12, 0.0]), None, (0, 0, 1))
    L['Hips'] = (hip_c, np.array([0.0, 0.008, 0.985]), 'Root', (0, -1, 0))
    L['Spine'] = (np.array([0.0, 0.008, 0.985]), np.array([0.0, 0.004, 1.085]), 'Hips', (0, -1, 0))
    L['Chest'] = (np.array([0.0, 0.004, 1.085]), np.array([0.0, 0.000, 1.200]), 'Spine', (0, -1, 0))
    L['UpperChest'] = (np.array([0.0, 0.000, 1.200]), np.array([0.0, 0.010, 1.318]), 'Chest', (0, -1, 0))
    neck0 = np.array([0.0, 0.014, 1.330])
    head0 = Hd.head_xform(np.array([0.0, 0.012, 1.425]))
    L['Neck'] = (neck0, head0, 'UpperChest', (0, -1, 0))
    L['Head'] = (head0, Hd.head_xform(np.array([0.0, 0.012, 1.60])), 'Neck', (0, -1, 0))
    for side, s in ((1, 'L'), (-1, 'R')):
        S, d = B.shoulder(side), B.arm_dir(side)
        E, W = B.elbow(side), B.wrist(side)
        L[f'Shoulder.{s}'] = (np.array([0.020 * side, 0.004, 1.305]), S + np.array([0, 0, 0.004]), 'UpperChest', (0, -1, 0))
        L[f'UpperArm.{s}'] = (S, E, f'Shoulder.{s}', (0, -1, 0))
        L[f'LowerArm.{s}'] = (E, W, f'UpperArm.{s}', (0, -1, 0))
        R, O = HN.hand_frame(1)
        hand_tip = W + d * 0.075
        L[f'Hand.{s}'] = (W, hand_tip, f'LowerArm.{s}', (0, -1, 0))
        J = HN.joint_positions(side)
        for fname in ('Thumb', 'Index', 'Middle', 'Ring', 'Little'):
            pts = J[fname]
            for k in range(3):
                parent = f'Hand.{s}' if k == 0 else f'{fname}{k}.{s}'
                L[f'{fname}{k + 1}.{s}'] = (pts[k], pts[k + 1], parent, (0, 0, 1))
        hipj = np.array([0.085 * side, 0.006, 0.865])
        knee = np.array([0.068 * side, -0.006, 0.452])
        ankle = np.array([0.062 * side, 0.010, 0.112])
        ball = B.foot_to_world(np.array([[0.058, -0.105, 0.030]]), side)[0]
        toe = B.foot_to_world(np.array([[0.056, -0.162, 0.022]]), side)[0]
        L[f'UpperLeg.{s}'] = (hipj, knee, 'Hips', (0, -1, 0))
        L[f'LowerLeg.{s}'] = (knee, ankle, f'UpperLeg.{s}', (0, -1, 0))
        L[f'Foot.{s}'] = (ankle, ball, f'LowerLeg.{s}', (0, 0, 1))
        L[f'Toes.{s}'] = (ball, toe, f'Foot.{s}', (0, 0, 1))
    # ---- secondary chains ----
    tie = Hd.head_xform(HR.TIE)
    zs = np.linspace(1.392, 0.80, 7)
    cy, _, _ = HR.ponytail_frame(zs)
    pts = Hd.head_xform(np.stack([np.zeros_like(zs), cy, zs], 1))
    pts[0] = tie
    for k in range(6):
        L[f'Hair_Back_{k + 1}'] = (pts[k], pts[k + 1], 'Head' if k == 0 else f'Hair_Back_{k}', (1, 0, 0))
    for side, s in ((1, 'L'), (-1, 'R')):
        lock = Hd.head_xform(np.array([[side * 0.074, -0.030, 1.425], [side * 0.086, -0.042, 1.365],
                                       [side * 0.088, -0.070, 1.295], [side * 0.098, -0.108, 1.215]]))
        for k in range(3):
            L[f'Hair_Side_{k + 1}.{s}'] = (lock[k], lock[k + 1], 'Head' if k == 0 else f'Hair_Side_{k}.{s}', (1, 0, 0))
    x0, z0, tip, hw, bulge = OF.TAIL_FLAPS[0]
    tail_pts = np.array([[0.0, 0.135, 0.815], [0.024, 0.190, 0.680], [0.048, 0.245, 0.540], [0.072, 0.300, 0.405]])
    for k in range(3):
        L[f'Tail_{k + 1}'] = (tail_pts[k], tail_pts[k + 1], 'Hips' if k == 0 else f'Tail_{k}', (1, 0, 0))
    c = OF.gem_center()
    tas = np.array([c + [0.0, -0.006, -0.012], c + [0.004, -0.010, -0.14], c + [0.010, -0.012, -0.29]])
    for k in range(2):
        L[f'Tassel_{k + 1}'] = (tas[k], tas[k + 1], 'Hips' if k == 0 else f'Tassel_{k}', (1, 0, 0))
    return L


def build_armature(collection, name="Armature"):
    arm_d = bpy.data.armatures.new(name)
    arm = bpy.data.objects.new(name, arm_d)
    collection.objects.link(arm)
    ctx = bpy.context
    ctx.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    L = bone_layout()
    eb = arm_d.edit_bones
    for nm, (h, t, parent, roll_ax) in L.items():
        b = eb.new(nm)
        b.head = V(*h)
        b.tail = V(*t)
    for nm, (h, t, parent, roll_ax) in L.items():
        b = eb[nm]
        if parent:
            b.parent = eb[parent]
            ph = eb[parent]
            if (ph.tail - b.head).length < 1e-4:
                b.use_connect = True
        b.align_roll(V(*roll_ax))
        b.use_deform = nm != 'Root'
    bpy.ops.object.mode_set(mode='OBJECT')
    arm_d.display_type = 'STICK'
    arm.show_in_front = True
    return arm


def set_secondary_deform(arm, flag):
    for b in arm.data.bones:
        if b.name.startswith(SECONDARY_PREFIX):
            b.use_deform = flag


def bind(ob, arm):
    mod = ob.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm
    ob.parent = arm


def heat_weights(ob, arm):
    """Blender's bone heat weighting (secondary bones excluded)."""
    set_secondary_deform(arm, False)
    ctx = bpy.context
    for o in ctx.view_layer.objects:
        if o is not None:
            o.select_set(False)
    ob.select_set(True)
    arm.select_set(True)
    ctx.view_layer.objects.active = arm
    with ctx.temp_override(active_object=arm, object=arm, selected_objects=[ob, arm],
                           selected_editable_objects=[ob, arm]):
        bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    set_secondary_deform(arm, True)
    # parent_set adds its own armature modifier; keep a single one
    mods = [m for m in ob.modifiers if m.type == 'ARMATURE']
    for m in mods[1:]:
        ob.modifiers.remove(m)
    mods[0].object = arm


def weights_of(ob):
    """(n_verts, n_groups) dense weight matrix and group names."""
    names = [g.name for g in ob.vertex_groups]
    W = np.zeros((len(ob.data.vertices), len(names)))
    for v in ob.data.vertices:
        for g in v.groups:
            W[v.index, g.group] = g.weight
    return W, names


def assign(ob, W, names, prune=1e-3, max_influence=4):
    """Write a dense weight matrix to vertex groups (top-k, normalized)."""
    for g in list(ob.vertex_groups):
        ob.vertex_groups.remove(g)
    groups = [ob.vertex_groups.new(name=n) for n in names]
    W = np.asarray(W, np.float64)
    if W.shape[1] > max_influence:
        idx = np.argsort(-W, axis=1)[:, max_influence:]
        np.put_along_axis(W, idx, 0.0, axis=1)
    W[W < prune] = 0.0
    s = W.sum(1, keepdims=True)
    W = np.where(s > 0, W / np.maximum(s, 1e-12), 0.0)
    for j, g in enumerate(groups):
        nz = np.nonzero(W[:, j])[0]
        for i in nz:
            g.add([int(i)], float(W[i, j]), 'REPLACE')
    # drop empty groups
    for g in list(ob.vertex_groups):
        if not np.any(W[:, names.index(g.name)] > 0):
            ob.vertex_groups.remove(g)


def transfer_from(src, dst_P, k=6, power=2.0):
    """Inverse-distance weights from the k nearest source vertices."""
    Ws, names = weights_of(src)
    Ps = mesh_arrays(src)
    kd = KDTree(len(Ps))
    for i, p in enumerate(Ps):
        kd.insert(p, i)
    kd.balance()
    out = np.zeros((len(dst_P), len(names)))
    for i, p in enumerate(dst_P):
        hits = kd.find_n(Vector(p), k)
        ws = np.array([1.0 / max(dd, 1e-4) ** power for _, _, dd in hits])
        ids = [j for _, j, _ in hits]
        out[i] = (ws[:, None] * Ws[ids]).sum(0) / ws.sum()
    return out, names


def chain_weights(P, pts, names, blend=0.035, base=None, base_blend=0.03):
    """Weights along a bone chain (pts: joints). Vertices are projected on the polyline."""
    pts = np.asarray(pts)
    seg = pts[1:] - pts[:-1]
    L = np.linalg.norm(seg, axis=1)
    cum = np.concatenate([[0], np.cumsum(L)])
    best_d = np.full(len(P), 1e9)
    best_s = np.zeros(len(P))
    for i in range(len(seg)):
        t = np.clip(((P - pts[i]) @ seg[i]) / (L[i] ** 2), 0, 1)
        q = pts[i] + t[:, None] * seg[i]
        d = np.linalg.norm(P - q, axis=1)
        better = d < best_d
        best_d[better] = d[better]
        best_s[better] = cum[i] + t[better] * L[i]
    nb = len(seg)
    W = np.zeros((len(P), nb + (1 if base else 0)))
    for i in range(nb):
        lo = cum[i] - (blend if i > 0 else 0.0)
        hi = cum[i + 1] + (blend if i < nb - 1 else 0.0)
        up = smoothstep(lo - blend, lo + blend, best_s) if i > 0 else np.ones(len(P))
        dn = 1.0 - smoothstep(hi - blend, hi + blend, best_s) if i < nb - 1 else np.ones(len(P))
        W[:, i] = up * dn
    out_names = list(names)
    if base:
        wb = 1.0 - smoothstep(0.0, base_blend, best_s)
        W[:, :nb] *= (1 - wb)[:, None]
        W[:, nb] = wb
        out_names.append(base)
    return W, out_names


def merge_weight_sets(*sets):
    names = []
    for _, n in sets:
        for x in n:
            if x not in names:
                names.append(x)
    n_rows = len(sets[0][0])
    W = np.zeros((n_rows, len(names)))
    for Ws, nm in sets:
        for j, x in enumerate(nm):
            W[:, names.index(x)] += Ws[:, j]
    return W, names


def smooth_weights(ob, W, iters=2, lam=0.5):
    me = ob.data
    nbrs = [[] for _ in range(len(me.vertices))]
    for e in me.edges:
        a, b = e.vertices
        nbrs[a].append(b)
        nbrs[b].append(a)
    for _ in range(iters):
        W2 = W.copy()
        for i, nb in enumerate(nbrs):
            if nb:
                W2[i] = (1 - lam) * W[i] + lam * W[nb].mean(0)
        W = W2
    return W
