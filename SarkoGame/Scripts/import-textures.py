"""
Unreal step of the surface-texture pipeline. Run by Scripts/generate-textures.sh.

Two jobs:

 1. IMPORT the PNGs Scripts/generate-textures.py wrote into /Game/Generated/Textures,
    with the settings that make them detail maps rather than pictures: TC_Grayscale,
    sRGB OFF, TEXTUREGROUP_World, wrap addressing.

    sRGB off is the load-bearing one. These maps are a LINEAR MULTIPLIER on the
    palette colour, normalised so their mean texel is 128/255 = 0.502 and the
    mean multiplier is therefore exactly 1.0 — which is the whole reason a
    texture can be added without the palette's colours drifting. Decoded as
    sRGB, 0.502 becomes 0.216, the mean multiplier drops to 0.7, and every
    surface in the sector quietly goes a third darker than
    Map/SarkoMapPalette.cpp says it is. It would look like a lighting bug.

 2. BUILD THE MATERIAL, because there isn't one. Until now the project shipped
    zero authored assets and painted everything with dynamic instances of
    /Engine/BasicShapes/BasicShapeMaterial, which has a colour parameter and a
    roughness parameter and no texture input at all. M_SarkoSurface is that
    material plus a detail sample, built here node by node so it stays
    reproducible: delete Content/Generated and re-run this and you get it back.

WORLD-ALIGNED UVs, AND WHY. The floor is one engine cube scaled 400x on X and Y
and the cover blocks are the same cube at a hundred different scales, so mesh UVs
carry no consistent texel density whatsoever — a 0..1 UV stretched over 40000 uu
next to the same 0..1 over 200 uu. Tiling on mesh UVs would put metre-wide
gravel on the ground and micrometre gravel on a crate. The graph below therefore
derives UVs from world position, which makes texel density a property of the
WORLD (one number per surface, in uu) instead of a property of whatever mesh
happened to be used.

Two projections, blended by the vertex normal's Z:
  * flat faces (|Nz| ~ 1) get world XY — the top-down camera's own plane;
  * upright faces get (horizontal-along-the-wall, -Z), which is why rust streaks
    and bark fibre run vertically: the V axis of that projection IS world down.
The blend is sharp (0.45..0.65) so only genuinely diagonal faces — a rock's
shoulder, never a wall — sit in the transition. Cost is one texture sample plus
about a dozen ALU ops, and no per-frame work of any kind.

Usage: UnrealEditor-Cmd <project> -run=pythonscript -script=import-textures.py
       with SARKO_TEXTURE_SOURCE set to the directory holding the PNGs.
"""

import json
import os
import unreal

SOURCE = os.environ["SARKO_TEXTURE_SOURCE"]
TEXTURE_PATH = "/Game/Generated/Textures"
MATERIAL_PATH = "/Game/Generated/Materials"
MATERIAL_NAME = "M_SarkoSurface"

with open(os.path.join(SOURCE, "textures.json")) as handle:
    MANIFEST = json.load(handle)


# --------------------------------------------------------------------------
# 1. Textures
# --------------------------------------------------------------------------

tasks = []
for name in sorted(MANIFEST):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE, MANIFEST[name]["file"])
    task.destination_path = TEXTURE_PATH
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

report = {}
for name in sorted(MANIFEST):
    asset_name = os.path.splitext(MANIFEST[name]["file"])[0]
    path = "%s/%s.%s" % (TEXTURE_PATH, asset_name, asset_name)
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        unreal.log_error("SARKO_TEXTURE_IMPORT %s did not import as a Texture2D" % path)
        continue

    # Compression first, then sRGB: setting compression_settings can reset the
    # sRGB flag to the format's default, and the default for a grayscale texture
    # is the wrong one for a multiplier map.
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
    # Sharpened mips would put back exactly the high-frequency energy the
    # generator's band limits took out, which is the shimmer this art direction
    # is trying not to have.
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)

    report[name] = {
        "path": path,
        "size": [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()],
        "srgb": texture.get_editor_property("srgb"),
        "tileUU": MANIFEST[name]["tileUU"],
    }

FIRST_TEXTURE = unreal.EditorAssetLibrary.load_asset(report["Ground"]["path"]) if "Ground" in report else None


# --------------------------------------------------------------------------
# 2. The material
# --------------------------------------------------------------------------

editing = unreal.MaterialEditingLibrary
material_object_path = "%s/%s.%s" % (MATERIAL_PATH, MATERIAL_NAME, MATERIAL_NAME)

# Rebuilt from scratch every run rather than edited in place. An
# already-existing material would accumulate a second copy of every node the
# code below adds, and the difference between "nine nodes" and "eighteen nodes
# where nine are orphaned" is invisible in a commandlet log.
if unreal.EditorAssetLibrary.does_asset_exist(material_object_path):
    unreal.EditorAssetLibrary.delete_asset(material_object_path)

material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    MATERIAL_NAME, MATERIAL_PATH, unreal.Material, unreal.MaterialFactoryNew())

# The single most important flag on this asset. ASarkoPropField paints instanced
# (HISM) components with it, and a material without this compiles no instanced
# vertex-factory permutation — every tree, rock and wreck in the sector would
# fall back to the default material and lose its colour. Silently.
material.set_editor_property("used_with_instanced_static_meshes", True)
material.set_editor_property("two_sided", False)


def node(kind, x, y, **properties):
    expression = editing.create_material_expression(material, kind, x, y)
    for key, value in properties.items():
        expression.set_editor_property(key, value)
    return expression


def link(source, source_output, target, target_input):
    editing.connect_material_expressions(source, source_output, target, target_input)


def constant(value, x, y):
    return node(unreal.MaterialExpressionConstant, x, y, r=value)


def mask(source, x, y, r=False, g=False, b=False):
    expression = node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
    link(source, "", expression, "Input")
    return expression


def binary(kind, x, y, a, a_out, b, b_out):
    expression = node(kind, x, y)
    link(a, a_out, expression, "A")
    link(b, b_out, expression, "B")
    return expression


# --- parameters -----------------------------------------------------------
colour = node(unreal.MaterialExpressionVectorParameter, -1500, -400,
              parameter_name="Color", default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
roughness = node(unreal.MaterialExpressionScalarParameter, -1500, -280,
                 parameter_name="Roughness", default_value=0.9)
tile = node(unreal.MaterialExpressionScalarParameter, -1900, 200,
            parameter_name="DetailTileUU", default_value=1000.0)
strength = node(unreal.MaterialExpressionScalarParameter, -900, -160,
                parameter_name="DetailStrength", default_value=0.0)
rough_swing = node(unreal.MaterialExpressionScalarParameter, -900, 480,
                   parameter_name="DetailRoughness", default_value=0.0)

# Defaults of zero on both strengths are deliberate: an instance that forgets to
# set them renders exactly the flat colour this project shipped before, which is
# the right thing for a parameter nobody set to do.

# --- world-aligned UVs ----------------------------------------------------
world = node(unreal.MaterialExpressionWorldPosition, -2300, 0)
normal = node(unreal.MaterialExpressionVertexNormalWS, -2300, 420)

world_xy = mask(world, -2050, -60, r=True, g=True)
world_x = mask(world, -2050, 40, r=True)
world_y = mask(world, -2050, 120, g=True)
world_z = mask(world, -2050, 300, b=True)
normal_x = mask(normal, -2050, 480, r=True)
normal_y = mask(normal, -2050, 560, g=True)
normal_z = mask(normal, -2050, 640, b=True)

top_uv = binary(unreal.MaterialExpressionDivide, -1650, -60, world_xy, "", tile, "")

# The wall-tangent coordinate: x*Ny - y*Nx. Signed, not abs(): with absolute
# values this expression is CONSTANT along any wall at 45 degrees — its gradient
# projects to zero in the wall's own plane — and that wall renders as one smeared
# stripe. The signed form has unit gradient on every vertical face regardless of
# its yaw, which is what keeps texel density the same on a rotated wall as on an
# axis-aligned one.
along_x = binary(unreal.MaterialExpressionMultiply, -1800, 60, world_x, "", normal_y, "")
along_y = binary(unreal.MaterialExpressionMultiply, -1800, 160, world_y, "", normal_x, "")
along = binary(unreal.MaterialExpressionSubtract, -1650, 100, along_x, "", along_y, "")
side_u = binary(unreal.MaterialExpressionDivide, -1500, 100, along, "", tile, "")

# Negated so that world up is texture up: V grows downward in an image, and the
# streaks in the rust map and the fibre in the bark map are authored to run down
# the V axis.
down = binary(unreal.MaterialExpressionMultiply, -1800, 300, world_z, "", constant(-1.0, -1950, 340), "")
side_v = binary(unreal.MaterialExpressionDivide, -1500, 300, down, "", tile, "")
side_uv = binary(unreal.MaterialExpressionAppendVector, -1350, 200, side_u, "", side_v, "")

# The blend. abs(Nz) 0.45..0.65 — narrow, so only a genuinely diagonal face sits
# in it. Interpolating between two UV SETS distorts inside the transition, and
# the way to make that harmless is to keep the transition to surfaces nobody
# reads texel density on.
flatness = node(unreal.MaterialExpressionAbs, -1900, 640)
link(normal_z, "", flatness, "Input")
biased = binary(unreal.MaterialExpressionSubtract, -1750, 640, flatness, "", constant(0.45, -1900, 720), "")
scaled = binary(unreal.MaterialExpressionMultiply, -1600, 640, biased, "", constant(5.0, -1750, 720), "")
blend = node(unreal.MaterialExpressionSaturate, -1450, 640)
link(scaled, "", blend, "Input")

uv = node(unreal.MaterialExpressionLinearInterpolate, -1200, 100)
link(side_uv, "", uv, "A")
link(top_uv, "", uv, "B")
link(blend, "", uv, "Alpha")

# --- the sample -----------------------------------------------------------
detail = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, 100,
              parameter_name="Detail",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
if FIRST_TEXTURE:
    detail.set_editor_property("texture", FIRST_TEXTURE)
link(uv, "", detail, "UVs")

# --- base colour: palette x (1 +- strength) -------------------------------
low = node(unreal.MaterialExpressionOneMinus, -700, -200)
link(strength, "", low, "Input")
high = binary(unreal.MaterialExpressionAdd, -700, -100, constant(1.0, -850, -60), "", strength, "")

multiplier = node(unreal.MaterialExpressionLinearInterpolate, -500, -150)
link(low, "", multiplier, "A")
link(high, "", multiplier, "B")
link(detail, "R", multiplier, "Alpha")

base_colour = binary(unreal.MaterialExpressionMultiply, -300, -300, colour, "", multiplier, "")

# --- roughness: palette +- swing ------------------------------------------
centred = binary(unreal.MaterialExpressionSubtract, -700, 400, detail, "R", constant(0.5, -850, 440), "")
swing = binary(unreal.MaterialExpressionMultiply, -550, 400, centred, "", rough_swing, "")
rough_sum = binary(unreal.MaterialExpressionAdd, -400, 340, roughness, "", swing, "")
# Clamped, because a surface's palette roughness plus a full swing can leave the
# legal range and an out-of-range roughness is a specular artefact rather than an
# error message.
# min_default/max_default, not min/max: Min and Max on this node are input PINS
# and are read-only from script; the defaults are what an unconnected pin uses.
rough_final = node(unreal.MaterialExpressionClamp, -250, 340, min_default=0.05, max_default=1.0)
link(rough_sum, "", rough_final, "Input")

editing.connect_material_property(base_colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
editing.connect_material_property(rough_final, "", unreal.MaterialProperty.MP_ROUGHNESS)

editing.recompile_material(material)

unreal.EditorAssetLibrary.save_directory("/Game/Generated", False, True)

report["_material"] = {
    "path": material_object_path,
    "parameters": ["Color", "Roughness", "Detail", "DetailTileUU", "DetailStrength", "DetailRoughness"],
    "instancedStaticMeshes": material.get_editor_property("used_with_instanced_static_meshes"),
}
# Both, because they land in different places: unreal.log goes to the engine log
# and print() to the commandlet's stdout, which is the one generate-textures.sh
# greps. Only having the first made a successful run look like a silent one.
summary = "SARKO_TEXTURE_IMPORT_REPORT " + json.dumps(report, sort_keys=True)
unreal.log(summary)
print(summary)
