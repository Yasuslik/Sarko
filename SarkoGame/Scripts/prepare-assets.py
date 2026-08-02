"""
Blender step of the third-party asset pipeline. Run by Scripts/import-assets.sh.

Two jobs, and both exist because a downloaded mesh does not fit this project's
prop convention as it arrives:

 1. SPLIT A TREE INTO TRUNK AND CANOPY. Every Quaternius tree is ONE mesh with
    three material slots (Wood, Green, DarkGreen). The prop table needs the leafy
    part as a separate, non-colliding, FADEABLE part — see FSarkoPropPart::bCanopy
    — so this separates the foliage materials from the rest and writes two files.
    A tree that has no foliage material (the dead ones) stays a single file, which
    is exactly the tree_dead case the table already treats as canopy-less.

 2. NORMALISE EVERY EXPORTED MESH to the engine-primitive convention: geometry
    centred on its own origin and scaled to fit the -50..50 uu box. That is
    what /Engine/BasicShapes/Cube already is, so ASarkoPropField::AddPart's
    `Extent / 50` scale keeps working for imported meshes with no code change at
    all, and a part's Extent stays exactly its half-extent in world units — which
    is what the kind table and every extent assertion in the test suite mean.
    The cost is that an extent whose proportions do not match the mesh's own
    stretches it; the JSON written below reports each source's true dimensions so
    the table can be authored in proportion. Aspect ratios are NOT baked in on
    purpose: a stretched prop is a bug you can see, whereas a mesh that silently
    ignores its extent is a bug you cannot.

Usage: Blender -b -P prepare-assets.py -- <in-dir> <out-dir> <report.json>
"""

import bpy
import json
import os
import sys
from mathutils import Vector

# Material names that are foliage rather than structure. Quaternius names its
# flat-colour materials after the colour, so this is the whole classification:
# anything green (or a berry) is canopy, everything else is trunk.
FOLIAGE_MATERIALS = {"green", "darkgreen", "berry", "leaves", "lightgreen"}


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.objects):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def joined_import(path):
    """Imports an FBX and returns one joined mesh object."""
    bpy.ops.import_scene.fbx(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        return None
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def split_foliage(obj):
    """
    Returns (trunk_obj, canopy_obj); canopy is None when nothing is foliage.

    Separating by material and then re-joining the groups is the only reliable
    way to do this: Blender's separate-by-material makes one object per SLOT,
    and a tree has two green slots (Green and DarkGreen) that both belong to the
    same canopy.
    """
    slots = [s.material.name.lower() if s.material else "" for s in obj.material_slots]
    if not any(name in FOLIAGE_MATERIALS for name in slots):
        return obj, None
    if all(name in FOLIAGE_MATERIALS for name in slots):
        return None, obj

    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.mesh.separate(type="MATERIAL")
    # Names, not object references. bpy.ops.object.join() DELETES the objects it
    # absorbs, and a Python reference to a deleted object raises ReferenceError
    # on the next attribute access — so the second group must be looked up fresh
    # rather than held from before the first join.
    def is_foliage(obj):
        return bool(obj.material_slots and obj.material_slots[0].material
                    and obj.material_slots[0].material.name.lower() in FOLIAGE_MATERIALS)

    foliage_names = [o.name for o in bpy.context.scene.objects
                     if o.type == "MESH" and is_foliage(o)]
    trunk_names = [o.name for o in bpy.context.scene.objects
                   if o.type == "MESH" and not is_foliage(o)]

    def group(names):
        chosen = [bpy.context.scene.objects[n] for n in names if n in bpy.context.scene.objects]
        if not chosen:
            return None
        bpy.ops.object.select_all(action="DESELECT")
        for o in chosen:
            o.select_set(True)
        bpy.context.view_layer.objects.active = chosen[0]
        if len(chosen) > 1:
            bpy.ops.object.join()
        return bpy.context.view_layer.objects.active

    canopy = group(foliage_names)
    trunk = group(trunk_names)
    return trunk, canopy


def measure(obj):
    """World-space bounding box of an object, before normalisation."""
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
    hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    return lo, hi


def face_x(obj):
    """
    Turns the mesh so its longest HORIZONTAL axis is X.

    The kind table's convention is that the long side of a long prop is its
    Extent.X — car_wreck is 230 x 95, freight_car 700 x 150, log 330 x 55 — and
    every prop's authored yaw was chosen against that. Quaternius models a car
    and a log along Y, so without this every wreck on the highway would come out
    across the road instead of along it, and no yaw in the map file would say so.
    """
    lo, hi = measure(obj)
    size = hi - lo
    if size.y <= size.x:
        return
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.transform.rotate(value=1.5707963267948966, orient_axis="Z")
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)


def normalise(obj):
    """
    Centres the mesh on its origin and stretches it to exactly a 1-unit cube.

    One BLENDER unit, which is 100 unreal units: this FBX round-trip carries a
    metre as an unreal metre, so a mesh a hundred Blender units across arrives in
    the editor ten thousand uu wide. Measured, not assumed — the first run of
    this script normalised to 100 and every imported mesh reported a 5000 uu
    half-extent.

    Per axis, NOT uniformly, and that is the whole point: it makes the mesh's
    imported bounds -50..50 uu on every axis, which is precisely what
    /Engine/BasicShapes/Cube is. `Extent / 50` in ASarkoPropField::AddPart then
    produces a prop whose world half-extents ARE its Extent — the same claim the
    kind table and every extent assertion in the suite already make. (The
    collision is a convex hull fitted at import, so what the player actually
    touches is contained by that box: tighter, never larger.)
    A uniform fit would keep the mesh's proportions and break that claim on two
    axes out of three, which is a silent lie in a number the tests trust.

    The shape is preserved instead by AUTHORING: the report written by this
    script gives each mesh's true proportions so the table can pick an extent in
    them. What is distorted is visible; what is unmeasurable is not.
    """
    lo, hi = measure(obj)
    size = hi - lo
    centre = (hi + lo) * 0.5

    obj.location = -centre
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    obj.scale = (1.0 / (size.x or 1.0), 1.0 / (size.y or 1.0), 1.0 / (size.z or 1.0))
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return lo, hi, size, centre


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True,
                             mesh_smooth_type="FACE", add_leaf_bones=False)


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def process(source, out_dir, report):
    name = os.path.splitext(os.path.basename(source))[0]
    clear_scene()
    obj = joined_import(source)
    if obj is None:
        print("SARKO_PREPARE skip (no mesh):", source)
        return
    # Orient BEFORE splitting, so a trunk and its canopy are turned together and
    # the offsets the report derives from their centres stay a matched pair.
    face_x(obj)
    trunk, canopy = split_foliage(obj)

    for piece, suffix in ((trunk, ""), (canopy, "_Canopy")):
        if piece is None:
            continue
        out_name = name + suffix
        tris = triangles(piece)
        lo, hi, size, centre = normalise(piece)
        export(piece, os.path.join(out_dir, out_name + ".fbx"))
        report[out_name] = {
            "source": os.path.basename(source),
            "triangles": tris,
            # Source-space numbers, so the kind table can be authored in the
            # mesh's own proportions and a canopy can be offset above its trunk.
            "size": [size.x, size.y, size.z],
            "centre": [centre.x, centre.y, centre.z],
            "min": [lo.x, lo.y, lo.z],
            "max": [hi.x, hi.y, hi.z],
        }
        print("SARKO_PREPARE", out_name, "tris=%d" % tris,
              "size=%.3f,%.3f,%.3f" % (size.x, size.y, size.z))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    in_dir, out_dir, report_path = argv[0], argv[1], argv[2]
    os.makedirs(out_dir, exist_ok=True)
    report = {}
    for entry in sorted(os.listdir(in_dir)):
        if entry.lower().endswith(".fbx"):
            process(os.path.join(in_dir, entry), out_dir, report)
    existing = {}
    if os.path.exists(report_path):
        with open(report_path) as handle:
            existing = json.load(handle)
    existing.update(report)
    with open(report_path, "w") as handle:
        json.dump(existing, handle, indent=1, sort_keys=True)


main()
