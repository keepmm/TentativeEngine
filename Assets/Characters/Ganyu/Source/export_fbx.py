"""FBX export (+ a quick re-import check) and the reference pose used for previews."""
import math

import bpy
import numpy as np
from mathutils import Matrix, Vector


def select_only(objs):
    ctx = bpy.context
    for o in ctx.view_layer.objects:
        if o is not None:
            o.select_set(False)
    for o in objs:
        o.select_set(True)
    ctx.view_layer.objects.active = objs[0]




def rotate_bone_world(arm, name, quat):
    """Rotate a pose bone about its head by a world/armature-space quaternion."""
    pb = arm.pose.bones[name]
    head = pb.head.copy()
    M = Matrix.Translation(head) @ quat.to_matrix().to_4x4() @ Matrix.Translation(-head) @ pb.matrix
    pb.matrix = M
    bpy.context.view_layer.update()


def aim_bone(arm, name, target_dir):
    pb = arm.pose.bones[name]
    cur = (pb.tail - pb.head).normalized()
    q = cur.rotation_difference(Vector(target_dir).normalized())
    rotate_bone_world(arm, name, q)


def pose_reference(arm):
    """Arms relaxed down along the body, like the reference shots."""
    for b in arm.pose.bones:
        b.rotation_mode = 'QUATERNION'
        b.rotation_quaternion = (1, 0, 0, 0)
        b.location = (0, 0, 0)
    bpy.context.view_layer.update()
    for s, sx in (('L', 1), ('R', -1)):
        aim_bone(arm, f'UpperArm.{s}', (0.16 * sx, 0.03, -1.0))
        aim_bone(arm, f'LowerArm.{s}', (0.10 * sx, -0.10, -1.0))
        pb = arm.pose.bones[f'LowerArm.{s}']
        axis = (pb.tail - pb.head).normalized()
        from mathutils import Quaternion
        rotate_bone_world(arm, f'Hand.{s}', Quaternion(axis, math.radians(-25 * sx)))


def rest_pose(arm):
    for b in arm.pose.bones:
        b.rotation_mode = 'QUATERNION'
        b.rotation_quaternion = (1, 0, 0, 0)
        b.location = (0, 0, 0)
        b.scale = (1, 1, 1)
    bpy.context.view_layer.update()


def check_fbx(path):
    """Import into an empty scene and report what came back."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    rep = []
    for o in bpy.data.objects:
        if o.type == "ARMATURE":
            ad = o.animation_data
            act = ad.action.name if ad and ad.action else None
            rep.append(f"armature {o.name}: {len(o.data.bones)} bones, scale {tuple(round(s, 3) for s in o.scale)}, action {act}")
        elif o.type == 'MESH':
            me = o.data
            mods = [m.type for m in o.modifiers]
            rep.append(f"mesh {o.name}: {len(me.vertices)} verts, {len(me.polygons)} faces, "
                       f"{len(o.vertex_groups)} groups, mats {[m.name for m in me.materials]}, mods {mods}")
    imgs = sorted(set(i.name.split(".")[0] for i in bpy.data.images))
    rep.append(f"images: {imgs}")
    rep.append(f"actions: {[(a.name, tuple(a.frame_range)) for a in bpy.data.actions]}")
    m = bpy.data.materials.get("M_Tights")
    if m:
        for n in m.node_tree.nodes:
            if n.type == "MAPPING":
                rep.append(f"M_Tights mapping scale: {tuple(round(v, 2) for v in n.inputs['Scale'].default_value)}")
    return rep


# (bone, local axis, amplitude deg, phase rad) on top of the reference pose
IDLE_SWAY = [
    ('Chest', 'X', 1.2, 0.0), ('UpperChest', 'X', 0.8, 0.3), ('Neck', 'X', -0.8, 0.6),
    ('Head', 'X', -1.0, 0.9), ('Head', 'Y', 1.2, 1.4),
    ('UpperArm.L', 'Z', 1.5, 0.5), ('UpperArm.R', 'Z', -1.5, 0.5),
    ('LowerArm.L', 'X', 2.0, 0.9), ('LowerArm.R', 'X', 2.0, 0.9),
]
for k in range(1, 7):
    IDLE_SWAY += [(f'Hair_Back_{k}', 'X', 1.2 + 0.35 * k, 0.45 * k), (f'Hair_Back_{k}', 'Z', 0.8 + 0.2 * k, 0.45 * k + 1.0)]
for s in ('L', 'R'):
    for k in range(1, 4):
        IDLE_SWAY.append((f'Hair_Side_{k}.{s}', 'Z', 1.5 + 0.5 * k, 0.5 * k + (0.3 if s == 'R' else 0.0)))
for k in range(1, 4):
    IDLE_SWAY.append((f'Tail_{k}', 'X', 1.5 + 0.5 * k, 0.5 * k + 0.7))
for k in range(1, 3):
    IDLE_SWAY.append((f'Tassel_{k}', 'X', 2.5 + k, 0.6 * k + 0.2))


def make_idle_action(arm, frames=60, step=5, fps=30):
    """Looping 2 s idle built on the reference pose (breathing + secondary sway)."""
    from mathutils import Quaternion
    scn = bpy.context.scene
    scn.render.fps = fps
    scn.frame_start = 0
    scn.frame_end = frames
    pose_reference(arm)
    base = {pb.name: pb.rotation_quaternion.copy() for pb in arm.pose.bones}
    axes = {'X': (1, 0, 0), 'Y': (0, 1, 0), 'Z': (0, 0, 1)}
    for f in range(0, frames + 1, step):
        t = 2 * math.pi * f / frames
        extra = {}
        for name, ax, amp, ph in IDLE_SWAY:
            q = Quaternion(axes[ax], math.radians(amp) * math.sin(t + ph))
            extra[name] = extra.get(name, Quaternion()) @ q
        for pb in arm.pose.bones:
            pb.rotation_quaternion = base[pb.name] @ extra.get(pb.name, Quaternion())
            pb.keyframe_insert("rotation_quaternion", frame=f)
    act = arm.animation_data.action
    act.name = "Idle"
    scn.frame_set(0)
    return act


def export_with_anim(path, arm, meshes, take="Idle"):
    select_only([arm] + list(meshes))
    scn = bpy.context.scene
    old_name = scn.name
    scn.name = take          # the baked take is named after the scene
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', axis_forward='-Z', axis_up='Y',
        bake_space_transform=True,  # mesh vertices stored Y-up in metres, mesh nodes = identity
        use_mesh_modifiers=True, mesh_smooth_type='FACE', use_triangles=True, use_tspace=True,
        add_leaf_bones=False, use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X',
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True, bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0, path_mode="COPY", embed_textures=True, use_custom_props=False)
    scn.name = old_name
