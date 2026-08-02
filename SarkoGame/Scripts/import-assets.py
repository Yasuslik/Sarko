"""
Unreal step of the third-party asset pipeline. Run by Scripts/import-assets.sh.

Imports the meshes Scripts/prepare-assets.py produced into
/Game/ThirdParty/<Pack>, then makes each one usable by ASarkoPropField:

 * BOX SIMPLE COLLISION. A HISM with QueryAndPhysics and no simple collision
   does not stop a character capsule — sweeps use simple collision, and complex
   collision only answers line traces. Without this every tree in the sector
   would be scenery you walk through, and nothing would say so. The box is the
   mesh's own bounds, which prepare-assets.py normalised to -50..50, so a part
   placed with Extent E collides in exactly the box the kind table declares.

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
CONTENT_ROOT = "/Game/ThirdParty"

manager = unreal.InterchangeManager.get_interchange_manager_scripted()
# EditorStaticMeshLibrary, not StaticMeshEditorSubsystem: the subsystem is not
# created in a commandlet (get_editor_subsystem returns None), while the
# library is a plain BlueprintFunctionLibrary and works headlessly.
mesh_tools = unreal.EditorStaticMeshLibrary

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

        # Every previous run's collision has to go first, or a re-import stacks a
        # second box on the first and the mesh quietly collides twice.
        mesh_tools.remove_collisions(asset)
        mesh_tools.add_simple_collisions(asset, unreal.ScriptCollisionShapeType.BOX)

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
            "materials": asset.get_editor_property("static_materials").__len__(),
        }

unreal.EditorAssetLibrary.save_directory(CONTENT_ROOT, False, True)

unreal.log("SARKO_IMPORT_REPORT " + json.dumps(report, sort_keys=True))
