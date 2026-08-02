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

ONE projection — world XY, the top-down camera's own plane — and that is the
second design. The first blended world XY on flat faces with an
(along-the-wall, -Z) projection on upright ones, chosen by the vertex normal's Z
so that streaks would run down a wall. It works, it is only a dozen ALU, and it
put a wood-grain whorl on all fifty-one car wrecks on the highway: a car body is
a CURVED mesh, so abs(Nz) sweeps through the blend band across the bonnet and
the roof, and interpolating between two UV SETS inside that band draws the
normal's own iso-contours. In the road frame the wrecks looked varnished.
Blending the two SAMPLES instead of the two UVs fixes it and costs a second
texture fetch on every pixel in the sector, which is not a trade a phone should
make for the sides of things a top-down camera barely sees.

So: world XY everywhere. A vertical face gets the pattern extruded along Z —
i.e. vertical streaks — which is what bark fibre, rust runoff and wall staining
all look like anyway, and it is why every map the generator writes is now
isotropic: the VERTICALITY comes from the projection, and the texture only has
to supply the horizontal frequency. Texel density on an upright face is
undefined along Z and exactly right across it. Cost is one texture sample and
two divides, and no per-frame work of any kind.

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
    """
    Connects, and REFUSES TO BE IGNORED.

    connect_material_expressions returns False and says nothing when it cannot
    find the input name, and that is how this material shipped broken once: the
    single-input nodes here (ComponentMask, Abs, Saturate, OneMinus, Clamp) have
    an input whose name is EMPTY, not "Input", so every one of those connections
    was a silent no-op. The graph looked right in a node dump — the nodes were
    all there and the named A/B connections had all landed — and the material
    failed to translate, so every surface in the sector fell back to the engine's
    default grey. Water and the extraction pads still looked correct, because
    they are the surfaces that never got this material.

    A hard failure here costs one line and removes that entire class of bug.
    """
    if not editing.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError("could not connect %s.%s -> %s.%s"
                           % (type(source).__name__, source_output or "<default>",
                              type(target).__name__, target_input or "<default>"))


def constant(value, x, y):
    return node(unreal.MaterialExpressionConstant, x, y, r=value)


def mask(source, x, y, r=False, g=False, b=False):
    expression = node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
    # "" and not "Input": these nodes name their one input with an empty FName.
    link(source, "", expression, "")
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
world = node(unreal.MaterialExpressionWorldPosition, -1700, 100)
world_xy = mask(world, -1450, 100, r=True, g=True)
uv = binary(unreal.MaterialExpressionDivide, -1200, 100, world_xy, "", tile, "")

# --- the sample -----------------------------------------------------------
detail = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, 100,
              parameter_name="Detail",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
if FIRST_TEXTURE:
    detail.set_editor_property("texture", FIRST_TEXTURE)
link(uv, "", detail, "UVs")

# --- base colour: palette x (1 +- strength) -------------------------------
low = node(unreal.MaterialExpressionOneMinus, -700, -200)
link(strength, "", low, "")
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
link(rough_sum, "", rough_final, "")

if not editing.connect_material_property(base_colour, "", unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError("could not connect base colour")
if not editing.connect_material_property(rough_final, "", unreal.MaterialProperty.MP_ROUGHNESS):
    raise RuntimeError("could not connect roughness")

editing.recompile_material(material)

# Proof that it TRANSLATED, not just that the nodes are wired.
#
# A material that fails to translate is not an error anyone sees: the asset
# saves, its parameters are all readable, every automation test that inspects it
# passes — and at runtime the renderer quietly substitutes the engine's default
# grey for every primitive wearing it. The whole sector went flat grey once and
# the only evidence anywhere was one Display-level line about a "Conservative
# Shader Layout".
#
# The probe is STRUCTURAL rather than a query about the compiled shader: a
# commandlet with no RHI does not build a shader map, so anything that reports on
# one answers "nothing" whether the material is fine or ruined.
#
# Every input pin in this graph is meant to be driven. The two exceptions are
# listed by name and are the pins whose unconnected defaults are the intent — the
# sampler's mip-bias switch, and the clamp bounds set through min_default and
# max_default above. Anything else left dangling is a translation error waiting
# to happen, and translation errors here are invisible: the asset saves, its
# parameters read back correctly, and the renderer silently swaps in the default
# grey for every primitive wearing it.
OPTIONAL_PINS = {"Apply View MipBias", "Min", "Max"}

for expression in editing.get_material_expressions(material):
    pins = editing.get_material_expression_input_names(expression)
    sources = editing.get_inputs_for_material_expression(material, expression)
    for pin, source in zip(pins, sources):
        if str(pin) in OPTIONAL_PINS:
            continue
        if source is None:
            raise RuntimeError("%s has nothing driving its '%s' input — M_SarkoSurface would not translate"
                               % (type(expression).__name__, pin or "<default>"))

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
