"""Render the preview images from Ganyu.blend.

    blender -b ../Ganyu.blend --python render_previews.py -- [out_dir]
"""
import os
import sys

sys.dont_write_bytecode = True
SRC = os.path.dirname(os.path.abspath(__file__))
if SRC not in sys.path:
    sys.path.insert(0, SRC)

import bpy  # noqa: E402

import preview  # noqa: E402
import export_fbx  # noqa: E402

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = args[0] if args else os.path.join(os.path.dirname(SRC), "Preview")
os.makedirs(OUT, exist_ok=True)

preview.setup_world()
preview.setup_lights()
preview.add_ground()
arm = bpy.data.objects["Armature"]
arm.hide_render = True
if arm.animation_data:
    arm.animation_data.action = None   # pose by hand below (the Idle action would override it)

# same four angles as the reference shots, arms relaxed
export_fbx.pose_reference(arm)
preview.render_views(OUT, "Ganyu", views=('front34', 'right', 'back', 'left'), res=(800, 1400))
for name, tgt, az, el, dist in (("Face", (0, 0.0, 1.47), -20, 4, 1.3),
                                ("Tights", (0, 0.0, 0.55), -25, 6, 3.0),
                                ("Torso", (0, 0.0, 1.10), -25, 6, 2.0)):
    preview.camera_for((az, el), target=tgt, dist=dist, lens=85)
    preview.aim_key_light(az)
    preview.render(os.path.join(OUT, f"Ganyu_{name}.png"), (900, 900), samples=48)
# rest pose (as stored in the FBX)
export_fbx.rest_pose(arm)
preview.render_views(OUT, "Ganyu_RestPose", views=('front',), res=(1000, 1100))
print("previews written to", OUT)
