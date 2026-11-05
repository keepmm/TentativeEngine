"""Preview rendering: cameras matching the reference shots, soft studio light, ground shadow."""
import math
import os

import bpy
import numpy as np

from common import sock

VIEWS = {
    # name: (azimuth deg around Z measured from the front (-Y) toward +X, elevation deg)
    'front': (0.0, 4.0),
    'front34': (-18.0, 6.0),   # like reference 2 (seen slightly from her right-front)
    'right': (-90.0, 4.0),     # reference 1: her right side, facing image right
    'back': (180.0, 4.0),      # reference 3
    'left': (90.0, 4.0),       # reference 4: her left side, facing image left
}


def setup_world(bg=(0.955, 0.955, 0.955), ambient=0.0):
    """Background colour seen by the camera only; lighting comes from the key sun."""
    scn = bpy.context.scene
    world = bpy.data.worlds.get("PreviewWorld") or bpy.data.worlds.new("PreviewWorld")
    scn.world = world
    nt = world.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    lp = nt.nodes.new('ShaderNodeLightPath')
    cam_bg = nt.nodes.new('ShaderNodeBackground')
    cam_bg.inputs['Color'].default_value = (*bg, 1.0)
    amb = nt.nodes.new('ShaderNodeBackground')
    amb.inputs['Color'].default_value = (1, 1, 1, 1)
    amb.inputs['Strength'].default_value = ambient
    mix = nt.nodes.new('ShaderNodeMixShader')
    nt.links.new(lp.outputs['Is Camera Ray'], mix.inputs[0])
    nt.links.new(amb.outputs[0], mix.inputs[1])
    nt.links.new(cam_bg.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs['Surface'])
    scn.view_settings.view_transform = 'Standard'
    scn.view_settings.look = 'None'
    scn.render.engine = 'BLENDER_EEVEE'
    scn.eevee.taa_render_samples = 32
    scn.render.film_transparent = False
def add_light(name, rot_deg, energy, color=(1, 1, 1), angle=8.0):
    ld = bpy.data.lights.get(name) or bpy.data.lights.new(name, 'SUN')
    ld.energy = energy
    ld.color = color
    ld.angle = math.radians(angle)
    ob = bpy.data.objects.get(name) or bpy.data.objects.new(name, ld)
    if ob.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(ob)
    ob.rotation_euler = [math.radians(a) for a in rot_deg]
    return ob


def add_ground():
    if bpy.data.objects.get("PreviewGround"):
        return bpy.data.objects["PreviewGround"]
    me = bpy.data.meshes.new("PreviewGround")
    s = 4.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new("PreviewGround", me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new("PreviewGround")
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    # shadow catcher-ish: diffuse -> shader to rgb -> map to background brightness
    dif = nt.nodes.new('ShaderNodeBsdfDiffuse')
    dif.inputs['Color'].default_value = (1, 1, 1, 1)
    s2r = nt.nodes.new('ShaderNodeShaderToRGB')
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.62, 0.62, 0.64, 1)
    ramp.color_ramp.elements[0].color = (0.70, 0.70, 0.72, 1)
    ramp.color_ramp.elements[1].position = 0.17
    ramp.color_ramp.elements[1].color = (0.955, 0.955, 0.955, 1)
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(dif.outputs[0], s2r.inputs[0])
    nt.links.new(s2r.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], em.inputs[0])
    nt.links.new(em.outputs[0], out.inputs[0])
    me.materials.append(mat)
    return ob


def setup_lights():
    add_light("KeyLight", (40, 0, 0), 1.0, (1.0, 0.99, 0.97), angle=6)


def aim_key_light(az_deg, side_deg=-25.0, elev_deg=50.0):
    """Key light comes from the camera side (like the reference shots), raised and offset."""
    key = bpy.data.objects.get("KeyLight")
    if key is None:
        return
    # sun points along its local -Z; rotation X tilts from straight down toward -Y (front)
    key.rotation_euler = (math.radians(90.0 - elev_deg), 0.0, math.radians(az_deg + side_deg))


def camera_for(view, target=(0, 0, 0.82), dist=5.2, lens=85.0, ortho_scale=None):
    az, el = VIEWS[view] if isinstance(view, str) else view
    cam_d = bpy.data.cameras.get("PreviewCam") or bpy.data.cameras.new("PreviewCam")
    cam_d.lens = lens
    if ortho_scale:
        cam_d.type = 'ORTHO'
        cam_d.ortho_scale = ortho_scale
    else:
        cam_d.type = 'PERSP'
    cam = bpy.data.objects.get("PreviewCam") or bpy.data.objects.new("PreviewCam", cam_d)
    if cam.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(cam)
    a = math.radians(az)
    e = math.radians(el)
    tx, ty, tz = target
    pos = (tx + dist * math.sin(a) * math.cos(e), ty - dist * math.cos(a) * math.cos(e), tz + dist * math.sin(e))
    cam.location = pos
    d = np.array(target) - np.array(pos)
    yaw = math.atan2(-d[0], d[1])
    pitch = math.atan2(d[2], math.hypot(d[0], d[1]))
    cam.rotation_euler = (math.pi / 2 + pitch, 0.0, yaw)
    bpy.context.scene.camera = cam
    return cam


def render(path, res=(600, 1000), samples=32):
    scn = bpy.context.scene
    scn.render.resolution_x, scn.render.resolution_y = res
    scn.render.resolution_percentage = 100
    scn.eevee.taa_render_samples = samples
    scn.render.filepath = path
    scn.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)


def render_views(out_dir, prefix, views=('front34', 'right', 'back', 'left'), res=(600, 1000),
                 target=(0, 0.02, 0.84), dist=4.5, lens=85.0, ortho_scale=None):
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    for v in views:
        camera_for(v, target=target, dist=dist, lens=lens, ortho_scale=ortho_scale)
        aim_key_light(VIEWS[v][0] if isinstance(v, str) else v[0])
        p = os.path.join(out_dir, f"{prefix}_{v}.png")
        render(p, res)
        paths.append(p)
    return paths


def debug_material(name, color, wire=True):
    mat = bpy.data.materials.new(name)
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    dif = nt.nodes.new('ShaderNodeBsdfDiffuse')
    dif.inputs['Color'].default_value = (*color, 1)
    s2r = nt.nodes.new('ShaderNodeShaderToRGB')
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.interpolation = 'EASE'
    ramp.color_ramp.elements[0].position = 0.25
    ramp.color_ramp.elements[0].color = (0.55, 0.55, 0.62, 1)
    ramp.color_ramp.elements[1].position = 0.6
    ramp.color_ramp.elements[1].color = (1, 1, 1, 1)
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    sock(mul.inputs, 'Factor_Float').default_value = 1.0
    sock(mul.inputs, 'A_Color').default_value = (*color, 1)
    nt.links.new(dif.outputs[0], s2r.inputs[0])
    nt.links.new(s2r.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], sock(mul.inputs, 'B_Color'))
    col = sock(mul.outputs, 'Result_Color')
    if wire:
        wf = nt.nodes.new('ShaderNodeWireframe')
        wf.use_pixel_size = True
        wf.inputs[0].default_value = 0.6
        mix = nt.nodes.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        nt.links.new(wf.outputs[0], sock(mix.inputs, 'Factor_Float'))
        nt.links.new(col, sock(mix.inputs, 'A_Color'))
        sock(mix.inputs, 'B_Color').default_value = (0.1, 0.1, 0.15, 1)
        col = sock(mix.outputs, 'Result_Color')
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(col, em.inputs[0])
    nt.links.new(em.outputs[0], out.inputs[0])
    return mat
