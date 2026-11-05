"""Build the whole character in Blender.

    blender -b --factory-startup --python build.py -- [--no-export] [--preview DIR] [--fast]

Writes ../Ganyu.blend, ../Ganyu.fbx and ../Textures/*.png.
"""
import os
import sys
import time

sys.dont_write_bytecode = True
SRC = os.path.dirname(os.path.abspath(__file__))
if SRC not in sys.path:
    sys.path.insert(0, SRC)

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import common  # noqa: E402
import body  # noqa: E402
import head  # noqa: E402
import hair  # noqa: E402
import hands  # noqa: E402
import outfit  # noqa: E402
import textures  # noqa: E402
import facepaint  # noqa: E402
import clothtex  # noqa: E402
import materials as M  # noqa: E402
import texbake  # noqa: E402

ROOT = os.path.dirname(SRC)
TEX = os.path.join(ROOT, "Textures")
lin = textures.srgb_to_lin


def log(msg, t0=[time.time()]):
    print(f"[build {time.time() - t0[0]:6.1f}s] {msg}", flush=True)


def tex(path, non_color=False):
    return texbake.load_image(path, non_color)


def solid(name, srgb, shadow=(0.85, 0.82, 0.92), rim=0.10, metallic=0.0, roughness=0.6):
    return M.toon_material(name, color=tuple(lin(srgb)), shadow=shadow, rim=rim, metallic=metallic,
                           roughness=roughness)


def build_character():
    common.clear_scene()
    col = common.get_collection("Ganyu")
    os.makedirs(TEX, exist_ok=True)
    mats = {}

    # ---------------- body + tights ----------------
    ob_body, tp = body.build_body({'skin': bpy.data.materials.new("tmp_s"),
                                   'tights': bpy.data.materials.new("tmp_t")}, col)
    texbake.uv_unwrap(ob_body)
    pb, pm = textures.body_textures(ob_body, TEX, body)
    pk = textures.knit_normal(TEX)
    knit_rep = 1.0 / (0.012 * textures.surface_scale(ob_body))
    mats['skin'] = M.toon_material("M_Skin", image=tex(pb), shadow=(0.93, 0.78, 0.80))
    mats['tights'] = M.tights_material("M_Tights", tex(pm, True), tex(pk, True), knit_rep, base_image=tex(pb))
    ob_body.data.materials[0] = mats['skin']
    ob_body.data.materials[1] = mats['tights']
    for m in list(bpy.data.materials):
        if m.name.startswith("tmp_"):
            bpy.data.materials.remove(m)
    log(f"body: {len(ob_body.data.vertices)} verts, knit repeats {knit_rep:.1f}")

    # ---------------- head group ----------------
    pf = facepaint.face_texture(TEX, head.UV_SPAN, head.UV_Z0)
    mats['face'] = M.toon_material("M_Face", image=tex(pf), shadow=(0.93, 0.80, 0.82), rim=0.0)
    ob_head = head.build_head(mats['face'], col)
    ph = textures.hair_texture(TEX, hair.HAIR_V_LEN)
    mats["hair"] = M.toon_material("M_Hair", image=tex(ph), shadow=(0.80, 0.76, 0.97), rim=0.12)
    ob_hair, hb = hair.build_hair(mats['hair'], col)
    mats['horn'] = M.toon_material("M_Horn", image=tex(textures.horn_texture(TEX)), shadow=(0.60, 0.55, 0.65),
                                   rim=0.18)
    ob_horns = hair.build_horns(mats['horn'], col)
    mats['hairtie'] = M.toon_material("M_HairTie", image=tex(clothtex.hair_tie_texture(TEX)), shadow=(0.7, 0.65, 0.75))
    mats['black'] = solid("M_BlackCloth", (0.12, 0.10, 0.13), (0.7, 0.7, 0.8), 0.2)
    tie_objs = outfit.build_hair_tie(mats["hairtie"], mats["black"], col, hair.TIE)
    for o in tie_objs:
        common.decimate_to(o, 500)
    for o in [ob_head, ob_hair, ob_horns] + tie_objs:
        common.scale_about(o, head.HEAD_SCALE, head.HEAD_PIVOT)
    log(f"head group: head {len(ob_head.data.vertices)}, hair {len(ob_hair.data.vertices)} verts")

    # ---------------- gloves ----------------
    mats['glove'] = solid("M_Glove", (0.14, 0.12, 0.15), (0.60, 0.60, 0.72), 0.28, roughness=0.45)
    gloves = hands.build_gloves(mats['glove'], col)
    for g in gloves:
        common.decimate_to(g, 4500)
    log(f"gloves: {[len(g.data.vertices) for g in gloves]}")

    # ---------------- outfit ----------------
    mats['front'] = M.toon_material("M_FrontPanel", image=tex(clothtex.front_panel_texture(TEX)),
                                    shadow=(0.84, 0.82, 0.94))
    mats['back'] = M.toon_material("M_BackPanel", image=tex(clothtex.back_panel_texture(TEX)),
                                   shadow=(0.84, 0.80, 0.92))
    mats['tail'] = M.toon_material("M_Tail", image=tex(clothtex.tail_texture(TEX)), shadow=(0.75, 0.75, 0.92),
                                   rim=0.15)
    mats["sleeve"] = M.toon_material("M_Sleeve", image=tex(clothtex.sleeve_texture(TEX)),
                                     shadow=(0.93, 0.80, 0.90), rim=0.08)
    mats['collar'] = M.toon_material("M_Collar", image=tex(clothtex.collar_texture(TEX)), shadow=(0.7, 0.7, 0.8))
    mats['gold'] = solid("M_Gold", (0.92, 0.76, 0.45), (0.72, 0.58, 0.52), 0.35, metallic=1.0, roughness=0.35)
    mats['red'] = solid("M_RedCord", (0.66, 0.10, 0.14), (0.72, 0.62, 0.70), 0.12)
    mats['gem'] = solid("M_Gem", (0.45, 0.88, 0.95), (0.70, 0.80, 0.95), 0.45, roughness=0.15)
    mats['shoe'] = solid("M_ShoeUpper", (0.98, 0.93, 0.95), (0.86, 0.80, 0.90), 0.10)
    mats['sole'] = solid("M_ShoeSole", (0.27, 0.23, 0.26), (0.70, 0.68, 0.78), 0.10)
    parts = []
    parts.append(outfit.build_front_panel(mats['front'], col))
    parts.append(outfit.build_back_panel(mats['back'], col))
    parts += outfit.build_tail(mats['tail'], col)
    parts += outfit.build_sleeves(mats['sleeve'], col)
    parts.append(outfit.build_collar(mats['collar'], col))
    parts.append(outfit.build_cord(mats['red'], col))
    mats["strap"] = solid("M_Strap", (0.66, 0.50, 0.34), (0.75, 0.66, 0.66), 0.25, metallic=0.3, roughness=0.5)
    parts += outfit.build_thigh_straps(mats["strap"], col)
    bell = outfit.build_bell(mats['gold'], col)
    common.decimate_to(bell, 700)
    parts.append(bell)
    for o in outfit.build_cord_ornament(mats["gold"], mats["red"], col):
        common.decimate_to(o, 400)
        parts.append(o)
    shoes = outfit.build_shoes(mats['shoe'], mats['sole'], mats['red'], col)
    shoe_targets = {"Sole": 1300, "Heel": 500, "ToeCap": 1500, "HeelCounter": 1300}
    for o in shoes:
        for k, v in shoe_targets.items():
            if o.name.startswith("Shoe" + k + "_"):
                common.decimate_to(o, v)
        if "Knot" in o.name:
            common.decimate_to(o, 500)
    parts += shoes
    for o in outfit.build_gem(mats["gold"], mats["gem"], mats["red"], col):
        common.decimate_to(o, 800 if "Gem" not in o.name else 300)
        parts.append(o)
    parts += outfit.build_tassels(mats['red'], mats['gold'], col)
    for o in parts:
        o.data.calc_loop_triangles()
        log(f"    part {o.name}: {len(o.data.loop_triangles)} tris")
    log(f"outfit: {len(parts)} parts")

    objs = dict(body=ob_body, head=ob_head, hair=ob_hair, horns=ob_horns, tie=tie_objs, gloves=gloves,
                outfit=parts, hair_builder=hb, template=tp, materials=mats, collection=col)
    return objs


def merge(objs):
    """Join into the final game meshes: Body, Head, Hair, Outfit."""
    body_ob = common.join_objects([objs['body']] + objs['gloves'], "Body")
    hair_ob = common.join_objects([objs['hair'], objs['horns']] + objs['tie'], "Hair")
    outfit_ob = common.join_objects(objs['outfit'], "Outfit")
    head_ob = objs['head']
    for o in (body_ob, hair_ob, outfit_ob, head_ob):
        o.data.update()
    return dict(Body=body_ob, Head=head_ob, Hair=hair_ob, Outfit=outfit_ob)


def stats(meshes):
    tot = 0
    for name, o in meshes.items():
        o.data.calc_loop_triangles()
        n = len(o.data.loop_triangles)
        tot += n
        log(f"  {name}: {len(o.data.vertices)} verts, {n} tris, {len(o.data.materials)} materials")
    log(f"  total tris: {tot}")


def main(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    import rig
    import skin
    import export_fbx
    objs = build_character()
    arm = rig.build_armature(objs["collection"])
    log(f"armature: {len(arm.data.bones)} bones")
    skin.skin_all(objs, arm)
    log("skinned")
    meshes = merge(objs)
    stats(meshes)
    blend = os.path.join(ROOT, "Ganyu.blend")
    fbx = os.path.join(ROOT, "Ganyu.fbx")
    if "--preview" in args:
        import preview
        out = args[args.index("--preview") + 1]
        preview.setup_world()
        preview.setup_lights()
        preview.add_ground()
        preview.render_views(out, "rest", views=("front", "right", "back", "left"), res=(500, 900))
        export_fbx.pose_reference(arm)
        preview.render_views(out, "pose", views=("front34", "right", "back", "left"), res=(500, 900))
        export_fbx.rest_pose(arm)
    export_fbx.make_idle_action(arm)
    log("idle action created")
    if "--save" in args:
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=blend)
        bpy.ops.file.make_paths_relative()
        bpy.ops.wm.save_mainfile()
        log(f"saved {blend}")
    if "--export" in args:
        export_fbx.export_with_anim(fbx, arm, list(meshes.values()))
        log(f"exported {fbx}")
        for line in export_fbx.check_fbx(fbx):
            log("  fbx: " + line)
    return objs, meshes


if __name__ == "__main__":
    main(sys.argv)
