"""Skin weights for every piece (computed per part, before the meshes are merged)."""
import numpy as np

from common import mesh_arrays, smoothstep
import body as B
import head as Hd
import rig as RG


def _set(ob, W, names, smooth=0):
    if smooth:
        W = RG.smooth_weights(ob, W, iters=smooth)
    RG.assign(ob, W, names)


def rigid(ob, names_weights):
    P = mesh_arrays(ob)
    names = list(names_weights.keys())
    W = np.tile(np.array([names_weights[n] for n in names], np.float64), (len(P), 1))
    RG.assign(ob, W, names)


def rigid_from(ob, src, point=None):
    """Whole object follows the body skin at one point (keeps small ornaments rigid)."""
    P = mesh_arrays(ob)
    c = P.mean(0) if point is None else np.asarray(point)
    Wc, names = RG.transfer_from(src, c[None], k=8)
    RG.assign(ob, np.tile(Wc, (len(P), 1)), names)


def head_weights(ob):
    P = mesh_arrays(ob)
    # the face front (chin included) follows the head; only the neck blends into Neck
    w = np.maximum(smoothstep(1.372, 1.418, P[:, 2]), smoothstep(-0.030, -0.046, P[:, 1]))
    _set(ob, np.stack([w, 1 - w], 1), ['Head', 'Neck'], smooth=1)


def hair_weights(ob, hb, layout):
    n = len(ob.data.vertices)
    P = mesh_arrays(ob)
    names = ['Head']
    W = np.zeros((n, 1))
    W[:, 0] = 1.0
    sets = [(W, names)]
    pony_idx = np.concatenate([r[0] for r in hb.strands if r[3] == 'pony'])
    back = [layout[f'Hair_Back_{k}'] for k in range(1, 7)]
    pts = [back[0][0]] + [b[1] for b in back]
    Wp, nm = RG.chain_weights(P[pony_idx], pts, [f'Hair_Back_{k}' for k in range(1, 7)], blend=0.04,
                              base='Head', base_blend=0.04)
    Wfull = np.zeros((n, len(nm)))
    Wfull[pony_idx] = Wp
    head_col = nm.index('Head')
    sets[0][0][pony_idx, 0] = 0.0
    sets.append((Wfull, nm))
    for s in ('L', 'R'):
        lock_idx = np.concatenate([r[0] for r in hb.strands
                                   if r[3] == 'lock' and (r[2][-1][0] > 0) == (s == 'L')])
        ch = [layout[f'Hair_Side_{k}.{s}'] for k in range(1, 4)]
        pts = [ch[0][0]] + [b[1] for b in ch]
        Wl, nm = RG.chain_weights(P[lock_idx], pts, [f'Hair_Side_{k}.{s}' for k in range(1, 4)], blend=0.03,
                                  base='Head', base_blend=0.05)
        # above the chain start the lock stays on the head
        top = P[lock_idx, 2] > pts[0][2] + 0.01
        Wl[top] = 0.0
        Wl[top, nm.index('Head')] = 1.0
        Wfull = np.zeros((n, len(nm)))
        Wfull[lock_idx] = Wl
        sets[0][0][lock_idx, 0] = 0.0
        sets.append((Wfull, nm))
    W, names = RG.merge_weight_sets(*sets)
    RG.assign(ob, W, names)


def chain_part(ob, layout, bones, base=None, blend=0.04, base_blend=0.03, extend=0.0):
    P = mesh_arrays(ob)
    ch = [layout[b] for b in bones]
    pts = [ch[0][0]] + [b[1] for b in ch]
    if extend:
        d = pts[-1] - pts[-2]
        pts[-1] = pts[-1] + d / np.linalg.norm(d) * extend
    W, nm = RG.chain_weights(P, pts, list(bones), blend=blend, base=base, base_blend=base_blend)
    _set(ob, W, nm, smooth=1)


def sleeve_weights(ob, side):
    s = 'L' if side > 0 else 'R'
    P = mesh_arrays(ob)
    S, d = B.shoulder(side), B.arm_dir(side)
    t = (P - S) @ d
    w_low = smoothstep(B.UPPER_ARM_LEN - 0.05, B.UPPER_ARM_LEN + 0.05, t)
    _set(ob, np.stack([1 - w_low, w_low], 1), [f'UpperArm.{s}', f'LowerArm.{s}'], smooth=1)


def transfer(ob, src, smooth=1):
    P = mesh_arrays(ob)
    W, names = RG.transfer_from(src, P, k=6)
    _set(ob, W, names, smooth=smooth)


def skin_all(objs, arm):
    layout = RG.bone_layout()
    body = objs['body']
    # bone heat, then limit to 4 influences per vertex and normalize (game friendly)
    for o in [body] + objs["gloves"]:
        RG.heat_weights(o, arm)
        W, names = RG.weights_of(o)
        RG.assign(o, W, names)
    head_weights(objs['head'])
    hair_weights(objs['hair'], objs['hair_builder'], layout)
    rigid(objs['horns'], {'Head': 1.0})
    for o in objs['tie']:
        rigid(o, {'Head': 1.0})
    for o in objs['outfit']:
        nm = o.name
        if nm.startswith('Sleeve_'):
            sleeve_weights(o, 1 if nm.endswith('_L') else -1)
        elif nm.startswith('TailFlap'):
            chain_part(o, layout, ['Tail_1', 'Tail_2', 'Tail_3'], base='Hips', base_blend=0.05, extend=0.1)
        elif nm.startswith(('TasselCord', 'TasselCap', 'Tassel')):
            chain_part(o, layout, ['Tassel_1', 'Tassel_2'], base='Hips', base_blend=0.03, extend=0.1)
        elif nm.startswith(('Vision', 'CordOrnament', 'CordStone', 'CollarBell')):
            rigid_from(o, body)
        elif nm.startswith(('AnkleKnot',)):
            rigid_from(o, body)
        else:
            transfer(o, body)
    for o in [body, objs['head'], objs['hair'], objs['horns']] + objs['gloves'] + objs['tie'] + objs['outfit']:
        for m in [m for m in o.modifiers if m.type == 'ARMATURE']:
            o.modifiers.remove(m)
        RG.bind(o, arm)
