"""
Unreal step of the third-party asset pipeline. Run by Scripts/import-assets.sh.

Imports the meshes Scripts/prepare-assets.py produced into
/Game/ThirdParty/<Pack>, then makes each one usable by ASarkoPropField:

 * VERIFIES SIMPLE COLLISION. A HISM with QueryAndPhysics and no simple
   collision does not stop a character capsule — sweeps use simple collision,
   and complex collision only answers line traces. Without it every tree in the
   sector would be scenery you walk through, and nothing would say so.
   Interchange's mesh pipeline already fits one convex hull per mesh, which is
   both sufficient and tighter than the box this script used to add by hand (a
   box around a tree stops the player a metre from the trunk). So this does not
   generate collision, it CHECKS it: if a future pipeline default stops making
   hulls, that is a silent gameplay change and it should fail here rather than
   in a raid.

 * NANITE OFF and the SmallProp LOD group. This ships to phones.

Why Interchange and not -run=ImportAssets: the ImportAssets commandlet routes
FBX through the legacy UFbxFactory, whose logger opens the Message Log window
when the file produces any warning at all — and these files do (no smoothing
groups). Opening a Slate window inside a commandlet asserts on
FSlateApplication::Get(), so that path crashes on every one of these meshes
before writing anything. Interchange has a headless path and takes them silently.

Usage: UnrealEditor-Cmd <project> -run=pythonscript -script=import-assets.py
       -EnablePlugins=PythonScriptPlugin ... with SARKO_PREPARED set to the
       directory prepare-assets.py wrote.
"""

import json
import os
import unreal

PREPARED = os.environ["SARKO_PREPARED"]
# /Game/ThirdParty for the downloaded packs; Scripts/generate-props.sh points it
# at /Game/Generated for the meshes this project builds itself. The import is
# identical either way, which is the reason there is one script rather than two:
# a mesh we generated has to satisfy the same collision, Nanite and LOD contract
# as a mesh we downloaded, and the way to guarantee that is to run the same code.
CONTENT_ROOT = os.environ.get("SARKO_CONTENT_ROOT", "/Game/ThirdParty")

manager = unreal.InterchangeManager.get_interchange_manager_scripted()
params = unreal.ImportAssetParameters()
params.is_automated = True

report = {}

for pack in sorted(os.listdir(PREPARED)):
    pack_dir = os.path.join(PREPARED, pack)
    if not os.path.isdir(pack_dir):
        continue
    dest = "%s/%s" % (CONTENT_ROOT, pack)
    for entry in sorted(os.listdir(pack_dir)):
        if not entry.lower().endswith(".fbx"):
            continue
        source = unreal.InterchangeManager.create_source_data(os.path.join(pack_dir, entry))
        manager.import_asset(dest, source, params)

    for asset_path in unreal.EditorAssetLibrary.list_assets(dest, True, False):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(asset, unreal.StaticMesh):
            continue

        body = asset.get_editor_property("body_setup")
        geometry = body.get_editor_property("agg_geom") if body else None
        hulls = len(geometry.get_editor_property("convex_elems")) if geometry else 0
        boxes = len(geometry.get_editor_property("box_elems")) if geometry else 0
        if hulls + boxes == 0:
            unreal.log_error("SARKO_IMPORT no simple collision on %s — it would not stop a pawn"
                             % asset_path)

        nanite = asset.get_editor_property("nanite_settings")
        nanite.enabled = False
        asset.set_editor_property("nanite_settings", nanite)
        asset.set_editor_property("lod_group", "SmallProp")

        bounds = asset.get_bounds()
        extent = bounds.box_extent
        report[asset_path.split(".")[-1]] = {
            "path": asset_path,
            "extent": [extent.x, extent.y, extent.z],
            "origin": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
            # Triangle counts come from the Blender step, which counts the same
            # geometry before it is written and needs no editor API for it.
            "hulls": hulls,
            "boxes": boxes,
            "materials": asset.get_editor_property("static_materials").__len__(),
        }

unreal.EditorAssetLibrary.save_directory(CONTENT_ROOT, False, True)

unreal.log("SARKO_IMPORT_REPORT " + json.dumps(report, sort_keys=True))
