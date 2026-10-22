"""Materials.

Every material carries two outputs:
  * 'ALL'   -> Principled BSDF with plain textures. This is what the FBX exporter reads
               (and what any PBR-ish game shader can start from).
  * 'EEVEE' -> a toon network used for the Blender viewport / preview renders
               (cel ramp with tinted shadows; the tights get a view-dependent denier shader).
"""
import bpy

from common import sock

# key light direction for the toon ramp is taken from the scene lights (Shader to RGB)


def _new_mat(name):
    mat = bpy.data.materials.get(name)
    if mat is not None:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new(name)
    nt = mat.node_tree
    nt.nodes.clear()
    return mat, nt


def _node(nt, kind, x, y, **props):
    n = nt.nodes.new(kind)
    n.location = (x, y)
    for k, v in props.items():
        setattr(n, k, v)
    return n


def _img_node(nt, img, x, y, non_color=False, uv_scale=None, interpolation='Linear'):
    tex = _node(nt, 'ShaderNodeTexImage', x, y)
    tex.image = img
    tex.interpolation = interpolation
    if non_color and img is not None:
        img.colorspace_settings.name = 'Non-Color'
    if uv_scale is not None:
        uvn = _node(nt, 'ShaderNodeTexCoord', x - 420, y)
        mp = _node(nt, 'ShaderNodeMapping', x - 220, y)
        mp.inputs['Scale'].default_value = (uv_scale, uv_scale, 1.0)
        nt.links.new(uvn.outputs['UV'], mp.inputs['Vector'])
        nt.links.new(mp.outputs['Vector'], tex.inputs['Vector'])
    return tex


def _mix(nt, x, y, blend='MIX', fac=None, a=None, b=None):
    m = _node(nt, 'ShaderNodeMix', x, y)
    m.data_type = 'RGBA'
    m.blend_type = blend
    if isinstance(fac, (int, float)):
        sock(m.inputs, 'Factor_Float').default_value = fac
    elif fac is not None:
        nt.links.new(fac, sock(m.inputs, 'Factor_Float'))
    for ident, v in (('A_Color', a), ('B_Color', b)):
        if v is None:
            continue
        if isinstance(v, (tuple, list)):
            sock(m.inputs, ident).default_value = (*v[:3], 1.0)
        else:
            nt.links.new(v, sock(m.inputs, ident))
    return m, sock(m.outputs, 'Result_Color')


def _math(nt, op, x, y, a=None, b=None, clamp=False):
    m = _node(nt, 'ShaderNodeMath', x, y)
    m.operation = op
    m.use_clamp = clamp
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def _toon_factor(nt, x, y, normal=None, soft=0.10, center=0.12):
    """0 in shadow, 1 lit. Uses Shader-to-RGB of a white diffuse (cast shadows included)."""
    dif = _node(nt, 'ShaderNodeBsdfDiffuse', x, y)
    dif.inputs['Color'].default_value = (1, 1, 1, 1)
    if normal is not None:
        nt.links.new(normal, dif.inputs['Normal'])
    s2r = _node(nt, 'ShaderNodeShaderToRGB', x + 180, y)
    nt.links.new(dif.outputs[0], s2r.inputs[0])
    bw = _node(nt, 'ShaderNodeRGBToBW', x + 360, y)
    nt.links.new(s2r.outputs['Color'], bw.inputs[0])
    mr = _node(nt, 'ShaderNodeMapRange', x + 540, y)
    mr.interpolation_type = 'SMOOTHSTEP'
    mr.inputs['From Min'].default_value = center - soft
    mr.inputs['From Max'].default_value = center + soft
    nt.links.new(bw.outputs[0], mr.inputs['Value'])
    return mr.outputs['Result']


def _rim(nt, x, y, normal=None, power=0.25):
    lw = _node(nt, 'ShaderNodeLayerWeight', x, y)
    lw.inputs['Blend'].default_value = power
    if normal is not None:
        nt.links.new(normal, lw.inputs['Normal'])
    mr = _node(nt, 'ShaderNodeMapRange', x + 180, y)
    mr.interpolation_type = 'SMOOTHSTEP'
    mr.inputs['From Min'].default_value = 0.55
    mr.inputs['From Max'].default_value = 0.85
    nt.links.new(lw.outputs['Fresnel'], mr.inputs['Value'])
    return mr.outputs['Result']


def _outputs(nt, principled_color, toon_color, alpha=None, x=1600, y=0, normal_for_pbr=None,
             roughness=0.6, metallic=0.0, blend_alpha=None):
    # ---- export / PBR branch ----
    p = _node(nt, 'ShaderNodeBsdfPrincipled', x, y + 500)
    if isinstance(principled_color, (tuple, list)):
        p.inputs['Base Color'].default_value = (*principled_color[:3], 1.0)
    else:
        nt.links.new(principled_color, p.inputs['Base Color'])
    p.inputs['Roughness'].default_value = roughness
    p.inputs['Metallic'].default_value = metallic
    if normal_for_pbr is not None:
        nt.links.new(normal_for_pbr, p.inputs['Normal'])
    if alpha is not None:
        if isinstance(alpha, (int, float)):
            p.inputs['Alpha'].default_value = alpha
        else:
            nt.links.new(alpha, p.inputs['Alpha'])
    out_all = _node(nt, 'ShaderNodeOutputMaterial', x + 300, y + 500)
    out_all.target = 'ALL'
    nt.links.new(p.outputs[0], out_all.inputs['Surface'])
    # ---- toon branch (EEVEE) ----
    em = _node(nt, 'ShaderNodeEmission', x, y)
    nt.links.new(toon_color, em.inputs['Color'])
    shader = em.outputs[0]
    if alpha is not None and not isinstance(alpha, (int, float)):
        tr = _node(nt, 'ShaderNodeBsdfTransparent', x, y - 150)
        ms = _node(nt, 'ShaderNodeMixShader', x + 150, y)
        nt.links.new(alpha, ms.inputs[0])
        nt.links.new(tr.outputs[0], ms.inputs[1])
        nt.links.new(shader, ms.inputs[2])
        shader = ms.outputs[0]
    out_e = _node(nt, 'ShaderNodeOutputMaterial', x + 300, y)
    out_e.target = 'EEVEE'
    nt.links.new(shader, out_e.inputs['Surface'])
    out_e.is_active_output = True
    return p


def toon_material(name, color=None, image=None, shadow=(0.80, 0.74, 0.86), rim=0.10,
                  alpha_image=False, roughness=0.6, metallic=0.0, normal_image=None,
                  soft=0.10, double_sided=True, emission_boost=1.0):
    """Generic cel material. color: (r,g,b) in linear; image: bpy image for base color."""
    mat, nt = _new_mat(name)
    if image is not None:
        tex = _img_node(nt, image, -900, 200)
        base = tex.outputs['Color']
        alpha = tex.outputs['Alpha'] if alpha_image else None
    else:
        rgb = _node(nt, 'ShaderNodeRGB', -900, 200)
        rgb.outputs[0].default_value = (*color[:3], 1.0)
        base = rgb.outputs[0]
        alpha = None
    nmap = None
    if normal_image is not None:
        ntex = _img_node(nt, normal_image, -900, -300, non_color=True)
        nm = _node(nt, 'ShaderNodeNormalMap', -650, -300)
        nt.links.new(ntex.outputs['Color'], nm.inputs['Color'])
        nmap = nm.outputs['Normal']
    fac = _toon_factor(nt, -650, -50, normal=nmap, soft=soft)
    _, shad = _mix(nt, -100, 150, 'MULTIPLY', 1.0, base, tuple(shadow))
    _, lit = _mix(nt, 150, 150, 'MIX', fac, shad, base)
    col = lit
    if rim > 0:
        r = _rim(nt, -650, -250, normal=nmap)
        rf = _math(nt, 'MULTIPLY', 150, -150, r, fac)
        rf = _math(nt, 'MULTIPLY', 320, -150, rf, rim)
        _, col = _mix(nt, 450, 150, 'ADD', rf, col, (1.0, 1.0, 1.0))
    if emission_boost != 1.0:
        _, col = _mix(nt, 650, 150, 'MULTIPLY', 1.0, col, (emission_boost,) * 3)
    # the FBX exporter only reads image textures or plain socket values, not linked RGB nodes
    pbr_base = base if image is not None else tuple(color[:3])
    _outputs(nt, pbr_base, col, alpha=alpha, normal_for_pbr=nmap, roughness=roughness, metallic=metallic)
    mat.use_backface_culling = not double_sided
    if alpha_image:
        mat.surface_render_method = 'DITHERED'
    return mat


def tights_material(name, mask_image, knit_image, knit_scale, base_image=None,
                    tights_color=(0.018, 0.011, 0.020), skin_color=(0.80, 0.50, 0.50),
                    shadow=(0.70, 0.64, 0.80), sheen=(0.10, 0.085, 0.12), knit_strength=0.35,
                    amount=0.20):
    """Denier tights: skin shows through where the fabric faces the viewer and is stretched;
    grazing angles look through more fabric layers and go dark.

        clear  = mask.R                               (per texel, stretch-dependent, 0..1)
        sheer  = amount * clear ** (1 / max(N.V, eps))
        albedo = lerp(tights_color, skin_color, sheer)
    """
    mat, nt = _new_mat(name)
    # knit normal (tiled)
    ktex = _img_node(nt, knit_image, -1500, -500, non_color=True, uv_scale=knit_scale)
    nm = _node(nt, 'ShaderNodeNormalMap', -1250, -500)
    nm.inputs['Strength'].default_value = knit_strength
    nt.links.new(ktex.outputs['Color'], nm.inputs['Color'])
    N = nm.outputs['Normal']
    # sheer term
    mtex = _img_node(nt, mask_image, -1500, 300, non_color=True)
    sep = _node(nt, 'ShaderNodeSeparateColor', -1250, 300)
    nt.links.new(mtex.outputs['Color'], sep.inputs['Color'])
    clear = sep.outputs['Red']
    geo = _node(nt, 'ShaderNodeNewGeometry', -1500, 0)
    dot = _node(nt, 'ShaderNodeVectorMath', -1250, 0)
    dot.operation = 'DOT_PRODUCT'
    nt.links.new(N, dot.inputs[0])
    nt.links.new(geo.outputs['Incoming'], dot.inputs[1])
    ndv = _math(nt, 'MAXIMUM', -1050, 0, dot.outputs['Value'], 0.04)
    inv = _math(nt, 'DIVIDE', -880, 0, 1.0, ndv)
    sheer = _math(nt, 'POWER', -700, 100, clear, inv)
    sheer = _math(nt, 'MULTIPLY', -560, 100, sheer, amount)
    _, albedo = _mix(nt, -450, 200, 'MIX', sheer, tuple(tights_color), tuple(skin_color))
    # toon lighting
    fac = _toon_factor(nt, -1250, -250, normal=N, soft=0.12)
    _, shad = _mix(nt, -150, 200, 'MULTIPLY', 1.0, albedo, tuple(shadow))
    _, lit = _mix(nt, 80, 200, 'MIX', fac, shad, albedo)
    # fabric sheen at grazing angles on the lit side
    lw = _node(nt, 'ShaderNodeLayerWeight', -450, -400)
    lw.inputs['Blend'].default_value = 0.45
    nt.links.new(N, lw.inputs['Normal'])
    sh = _math(nt, 'POWER', -250, -400, lw.outputs['Facing'], 3.0)
    sh = _math(nt, 'MULTIPLY', -80, -400, sh, fac)
    _, col = _mix(nt, 250, 200, 'ADD', sh, lit, tuple(sheen))
    # export branch: approximate albedo texture (view independent)
    if base_image is not None:
        btex = _img_node(nt, base_image, 900, 900)
        pbr_col = btex.outputs['Color']
    else:
        pbr_col = tuple(tights_color)
    p = _outputs(nt, pbr_col, col, normal_for_pbr=N, roughness=0.55)
    p.inputs['Sheen Weight'].default_value = 0.4
    p.inputs['Sheen Tint'].default_value = (*sheen, 1.0)
    return mat
