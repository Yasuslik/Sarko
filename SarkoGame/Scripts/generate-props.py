"""
Blender step ONE of the procedural prop pipeline. Run by Scripts/generate-props.sh.

This is the project's own art: the props the free packs could not give us — a
Soviet-style АЗС canopy, rail rolling stock, industrial clutter and road
furniture — built out of boxes, prisms and cylinders by code, so the mesh is a
diff rather than a binary somebody once exported.

WHAT THIS SCRIPT IS NOT. It is not a modelling tool and it does not try to be
one: there are no bevels, no subdivision, no UV work and no materials. The
surface palette (M_SarkoSurface, /Game/Generated/Materials) does all of the
material work and ASarkoPropField assigns it to every slot, so a mesh here is
pure silhouette. That is also the budget: every prop below is between 28 and 264
triangles, which is what a phone wants and what a camera 1400 uu overhead can
actually resolve.

THE ONE DESIGN RULE, and every shape here is an answer to it: THE CAMERA LOOKS
STRAIGHT DOWN. A prop whose top-down silhouette is a featureless rectangle is a
box with extra steps, so each model earns its triangles ABOVE the waist —
the wagon is an open gondola (rim, ribs, dark interior, end decks), the canopy
roof carries a fascia frame and three ribs, the fence is posts with GAPS between
them, the sandbag stack shows its top course as eight separate bags, the spool
is round among rectangles. Detail on the sides of a thing is detail nobody in
this game ever sees.

Output: one FBX per prop in Art/Generated/Props, at real-world metres, +X along
the prop's long axis, origin on the ground at the footprint centre. Those files
ARE committed — the same deal Art/Generated's textures have: a generated artefact
next to the script that reproduces it byte-for-byte. Step two hands them to
Scripts/prepare-assets.py, which centres and normalises them into the -50..50 box
every kind-table extent is expressed in, exactly like a downloaded mesh.

Usage: Blender -b -P generate-props.py -- <out-dir> [report.json]
"""

import bmesh
import bpy
import json
import math
import os
import sys

from mathutils import Matrix, Vector


# ---------------------------------------------------------------------------
# The toolkit. Three primitives, because three is all these shapes need.
# ---------------------------------------------------------------------------

def box(bm, center, size, rot_z=0.0, rot_y=0.0):
	"""An axis-aligned box, optionally yawed or pitched, by centre and full size."""
	matrix = Matrix.Translation(Vector(center))
	if rot_z:
		matrix = matrix @ Matrix.Rotation(rot_z, 4, "Z")
	if rot_y:
		matrix = matrix @ Matrix.Rotation(rot_y, 4, "Y")
	matrix = matrix @ Matrix.Diagonal(Vector(size).to_4d())
	bmesh.ops.create_cube(bm, size=1.0, matrix=matrix)


def cyl(bm, center, radius, length, axis="Z", segments=12):
	"""
	A capped cylinder along one axis.

	cap_tris=False on purpose: an n-gon cap is ONE face, so a 12-sided drum costs
	24 side triangles and 20 cap ones instead of the 48 a triangle fan would add.
	Nothing here is deformed, so an n-gon is free.
	"""
	matrix = Matrix.Translation(Vector(center))
	if axis == "X":
		matrix = matrix @ Matrix.Rotation(math.radians(90.0), 4, "Y")
	elif axis == "Y":
		matrix = matrix @ Matrix.Rotation(math.radians(-90.0), 4, "X")
	try:
		bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
			radius1=radius, radius2=radius, depth=length, matrix=matrix)
	except TypeError:
		# Older bmesh spelt the radii "diameter1"/"diameter2" while still meaning
		# radii. Kept so this script is not pinned to one Blender minor version.
		bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
			diameter1=radius, diameter2=radius, depth=length, matrix=matrix)


def prism(bm, profile_yz, x0, x1):
	"""
	A 2D profile in the YZ plane, extruded along X.

	The jersey barrier is the reason this exists: its whole identity is the
	double-slope section, and a barrier built from stacked boxes is a wall.
	"""
	lo = [bm.verts.new((x0, y, z)) for (y, z) in profile_yz]
	hi = [bm.verts.new((x1, y, z)) for (y, z) in profile_yz]
	bm.faces.new(list(reversed(lo)))
	bm.faces.new(hi)
	count = len(profile_yz)
	for index in range(count):
		nxt = (index + 1) % count
		bm.faces.new((lo[index], lo[nxt], hi[nxt], hi[index]))


# ---------------------------------------------------------------------------
# The props. Every dimension below is METRES, and every one of them was chosen
# against the ~176 uu (1.76 m) pawn — see the kind table, which restates each
# prop's height as a multiple of it.
# ---------------------------------------------------------------------------

def gas_canopy_roof(bm):
	"""
	The АЗС canopy roof: 14 x 14 m, 1.6 m deep including its sign band.

	This is the prop the sector's own map file said could not exist — the note on
	bridge_gas_canopy_pad reads "A canopy roof cannot exist here: the camera is
	above and ТЗ §13 says the roof must not hide the player". That was true when
	the only options were "solid" and "absent". It stopped being true when the
	forest landed: FSarkoPropPart::bCanopy already hides a part when the local
	pawn walks under it, which is exactly and only what a filling-station roof
	needs. So the roof is a canopy in the engine's sense of the word.

	Read from above it must not be a slab, so it is a frame: a deep fascia band
	around all four edges standing proud of the deck, three ribs across it, and a
	raised sign band on the two road-facing sides.
	"""
	box(bm, (0, 0, 0.35), (13.4, 13.4, 0.70))            # deck
	for sign in (-1, 1):
		box(bm, (sign * 6.85, 0, 0.55), (0.30, 14.0, 1.10))  # fascia, east/west
		box(bm, (0, sign * 6.85, 0.55), (14.0, 0.30, 1.10))  # fascia, north/south
		box(bm, (sign * 6.85, 0, 1.35), (0.30, 9.0, 0.50))   # the sign band
	for x in (-4.5, 0.0, 4.5):
		box(bm, (x, 0, 0.85), (0.50, 13.4, 0.30))            # roof ribs
	return "GasCanopyRoof"


def gas_canopy_pillar(bm):
	"""A 5 m canopy stanchion: plinth, shaft, capital. The plinth is what makes it
	read as a column rather than a post at the one angle the game ever sees."""
	box(bm, (0, 0, 0.20), (0.90, 0.90, 0.40))
	box(bm, (0, 0, 2.60), (0.56, 0.56, 4.40))
	box(bm, (0, 0, 4.90), (0.80, 0.80, 0.20))
	return "GasCanopyPillar"


def fuel_pump(bm):
	"""
	A dispenser: plinth, body, display head, topper, and the BOOM — the arm that
	carries the hose out over the car.

	The boom is the whole point. A fuel pump seen from directly overhead is a
	60 x 40 cm rectangle and so is a bin; the arm sticking a metre out to one
	side is the only part of a pump that says "pump" from 1400 uu up.
	"""
	box(bm, (0, 0, 0.07), (0.74, 0.62, 0.14))     # plinth
	box(bm, (0, 0, 0.86), (0.62, 0.50, 1.44))     # body
	box(bm, (0, 0, 1.72), (0.70, 0.56, 0.28))     # display head
	box(bm, (0, 0, 1.92), (0.52, 0.42, 0.12))     # topper
	box(bm, (0.46, 0, 1.52), (0.62, 0.12, 0.12))  # boom
	box(bm, (0.72, 0, 1.28), (0.16, 0.16, 0.52))  # nozzle holster
	return "FuelPump"


def freight_wagon(bm):
	"""
	A four-axle open gondola (полувагон), 14 x 3 x 4 m — the standard Soviet
	freight wagon and the one that reads best from above, because an OPEN wagon
	shows a rim, a dark interior and a floor where a boxcar shows a roof.

	Replaces a 1400 x 300 x 400 uu cube. The extent is unchanged to the unit, so
	not one of the twelve wagons already parked on the siding moves.
	"""
	for sign in (-1, 1):
		box(bm, (sign * 4.3, 0, 0.32), (2.80, 2.20, 0.64))    # bogies
		box(bm, (sign * 6.20, 0, 2.55), (0.20, 2.80, 2.60))   # end walls
		box(bm, (0, sign * 1.40, 2.55), (12.6, 0.20, 2.60))   # side walls
		box(bm, (0, sign * 1.35, 3.93), (13.0, 0.30, 0.14))   # coaming, long
		box(bm, (sign * 6.25, 0, 3.93), (0.30, 3.00, 0.14))   # coaming, ends
		box(bm, (sign * 6.65, 0, 1.26), (0.70, 1.60, 0.12))   # end decks
		for side in (-1, 1):
			box(bm, (sign * 6.65, side * 0.70, 1.85), (0.10, 0.10, 1.06))  # handrail posts
			box(bm, (sign * 2.4, side * 1.36, 2.55), (0.24, 0.28, 2.60))   # side ribs
	box(bm, (0, 0, 0.92), (13.4, 2.60, 0.56))     # sill
	box(bm, (0, 0, 1.28), (12.6, 2.50, 0.16))     # floor
	return "FreightWagon"


def tank_wagon(bm):
	"""
	A rail fuel tanker, 13 x 2.8 x 4 m: barrel on saddles, walkway either side of
	a centre dome, end decks, ladder.

	Its job in the depot is CONTRAST. Three gondolas in a row read as one object;
	a cylinder with a disc in the middle of it, parked among them, is instantly a
	different thing, and that is what makes a siding read as rolling stock rather
	than as repeated cover.
	"""
	for sign in (-1, 1):
		box(bm, (sign * 4.2, 0, 0.32), (2.80, 2.20, 0.64))   # bogies
		box(bm, (sign * 6.10, 0, 1.26), (0.80, 2.00, 0.12))  # end decks
		box(bm, (sign * 3.0, 0, 1.45), (1.30, 2.80, 0.50))   # saddles
		box(bm, (sign * 3.0, 0, 3.90), (4.20, 0.90, 0.10))   # walkway
		box(bm, (5.45, sign * 0.60, 2.50), (0.08, 0.08, 2.60))  # ladder stiles
	box(bm, (0, 0, 0.92), (12.4, 1.60, 0.56))     # underframe
	cyl(bm, (0, 0, 2.55), 1.30, 10.6, axis="X", segments=12)   # the barrel
	cyl(bm, (0, 0, 3.92), 0.55, 0.16, axis="Z", segments=8)    # the manway dome
	return "TankWagon"


def jersey_barrier(bm):
	"""
	A jersey barrier, 3.16 x 0.6 x 1.16 m, as its real extruded section: wide
	foot, a hard kick at 34 cm, then a gentle taper to a narrow top.

	From above a barrier IS mostly a bar — but a tapered one shows both of its
	sloped faces to an overhead light where a box shows one flat top, so a row of
	them reads as a row rather than as a painted line. The two lifting lugs are
	the only thing on it that breaks the length.
	"""
	profile = [
		(0.300, 0.00), (0.300, 0.08), (0.160, 0.34), (0.125, 1.16),
		(-0.125, 1.16), (-0.160, 0.34), (-0.300, 0.08), (-0.300, 0.00),
	]
	prism(bm, profile, -1.58, 1.58)
	for sign in (-1, 1):
		box(bm, (sign * 0.55, 0, 1.12), (0.34, 0.14, 0.08))
	return "JerseyBarrier"


def crate(bm):
	"""
	A 1.4 m shipping crate: boarded body, four corner battens, a lid frame and
	one diagonal brace ACROSS THE LID.

	The diagonal is on the top face and nowhere else, which is the whole trick:
	it is the one detail of a crate the game's camera can see, and it turns
	forty-three identical grey cubes into forty-three crates.
	"""
	box(bm, (0, 0, 0.70), (1.24, 1.24, 1.36))     # body
	for sx in (-1, 1):
		for sy in (-1, 1):
			box(bm, (sx * 0.62, sy * 0.62, 0.70), (0.16, 0.16, 1.40))  # corner battens
	for sign in (-1, 1):
		box(bm, (0, sign * 0.60, 1.36), (1.40, 0.16, 0.08))  # lid frame
		box(bm, (sign * 0.60, 0, 1.36), (0.16, 1.40, 0.08))
	box(bm, (0, 0, 1.365), (1.75, 0.12, 0.07), rot_z=math.radians(45.0))
	return "Crate"


def fence_broken(bm):
	"""
	8 m of derelict paling fence: five posts, a top rail, a bottom rail with a
	hole in it, six surviving boards and one hanging off at 14 degrees.

	The GAPS are the content. A fence is the kind the map uses to say "somebody
	kept this land once", and a solid 800 x 24 uu bar says "wall" instead; a line
	of posts with daylight between them says fence from any height. The extent is
	unchanged, so the thirteen placed sections do not move — and collision is
	still the mesh's hull, so it blocks as completely as the box did. The holes
	are for the eye, not for bullets, and that is deliberate: a fence you can
	shoot through but not walk through is a rule nobody can see.
	"""
	for x in (-3.92, -1.96, 0.0, 1.96, 3.92):
		box(bm, (x, 0, 0.92), (0.16, 0.24, 1.84))            # posts
	box(bm, (0, 0.06, 1.50), (7.84, 0.10, 0.12))             # top rail, intact
	box(bm, (-2.00, 0.06, 0.42), (3.70, 0.10, 0.12))         # bottom rail, broken
	box(bm, (2.05, 0.06, 0.42), (3.60, 0.10, 0.12))
	for x in (-3.6, -3.0, -2.4, 0.2, 0.8, 3.4):
		box(bm, (x, -0.03, 0.90), (0.50, 0.06, 1.66))        # surviving boards
	box(bm, (1.6, -0.03, 0.88), (0.50, 0.06, 1.66), rot_y=math.radians(14.0))
	return "FenceBroken"


def sandbag_stack(bm):
	"""
	A sandbag emplacement, 3.8 x 1.48 x 1.16 m.

	Modelled as a solid core plus the courses the camera can actually see: eight
	separate bags on TOP, three along the front face. Every bag below the top
	course would be triangles spent on a surface that is never in frame — a
	fully-bagged wall is ninety boxes for a silhouette identical to this one.
	"""
	box(bm, (0, 0, 0.50), (3.60, 1.34, 1.00))                # core
	for x in (-1.45, -0.48, 0.48, 1.45):
		for y in (-0.36, 0.36):
			box(bm, (x, y, 1.08), (0.90, 0.66, 0.16))        # top course
	for x in (-1.2, 0.0, 1.2):
		box(bm, (x, 0.67, 0.55), (0.90, 0.24, 0.30))         # front course
	return "SandbagStack"


def barrel_fallen(bm):
	"""
	A 200-litre drum on its side, 1.15 m long.

	It exists as its own mesh for a reason that is pure engine: FSarkoPropPart
	carries a yaw and nothing else, so a barrel cannot be TIPPED by authoring —
	a fallen barrel has to be modelled fallen or not exist. The rolling hoops
	give it two bands across an otherwise plain cylinder from above.
	"""
	cyl(bm, (0, 0, 0.47), 0.44, 1.15, axis="X", segments=10)
	for sign in (-1, 1):
		cyl(bm, (sign * 0.30, 0, 0.47), 0.47, 0.08, axis="X", segments=10)
	return "BarrelFallen"


def cable_spool(bm):
	"""
	A 2.2 m cable drum standing on its flanges: two discs, a hub, and the coil
	wound between them.

	The one ROUND thing in a vocabulary of rectangles, which is exactly why it is
	worth a draw call: in a yard of wagons, crates and barriers a pair of curved
	rims is legible at a glance and at any yaw. At 2.2 m it also clears the pawn,
	so it is a sight blocker rather than clutter.
	"""
	for sign in (-1, 1):
		cyl(bm, (0, sign * 0.54, 1.10), 1.10, 0.12, axis="Y", segments=10)  # flanges
	cyl(bm, (0, 0, 1.10), 0.86, 0.94, axis="Y", segments=12)                # the coil
	cyl(bm, (0, 0, 1.10), 0.42, 1.00, axis="Y", segments=8)                 # hub
	return "CableSpool"


BUILDERS = [
	gas_canopy_roof,
	gas_canopy_pillar,
	fuel_pump,
	freight_wagon,
	tank_wagon,
	jersey_barrier,
	crate,
	fence_broken,
	sandbag_stack,
	barrel_fallen,
	cable_spool,
]


# ---------------------------------------------------------------------------

def clear_scene():
	bpy.ops.object.select_all(action="SELECT")
	bpy.ops.object.delete()
	for block in (bpy.data.meshes, bpy.data.objects):
		for item in list(block):
			if item.users == 0:
				block.remove(item)


def build(builder, out_dir, report):
	bm = bmesh.new()
	name = builder(bm)
	# Recalculated rather than authored: create_cube, create_cone and prism()
	# each have their own idea of winding, and a mesh with mixed normals is
	# black on one side in every renderer that ever opens it.
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
	mesh = bpy.data.meshes.new(name)
	bm.to_mesh(mesh)
	bm.free()

	obj = bpy.data.objects.new(name, mesh)
	bpy.context.collection.objects.link(obj)

	lo = Vector((min(v.co.x for v in mesh.vertices), min(v.co.y for v in mesh.vertices),
		min(v.co.z for v in mesh.vertices)))
	hi = Vector((max(v.co.x for v in mesh.vertices), max(v.co.y for v in mesh.vertices),
		max(v.co.z for v in mesh.vertices)))
	size = hi - lo
	tris = sum(len(p.vertices) - 2 for p in mesh.polygons)

	bpy.ops.object.select_all(action="DESELECT")
	obj.select_set(True)
	bpy.context.view_layer.objects.active = obj
	bpy.ops.export_scene.fbx(filepath=os.path.join(out_dir, name + ".fbx"),
		use_selection=True, apply_unit_scale=True, mesh_smooth_type="FACE",
		add_leaf_bones=False)

	report[name] = {
		"triangles": tris,
		"size": [size.x, size.y, size.z],
		"min": [lo.x, lo.y, lo.z],
		"max": [hi.x, hi.y, hi.z],
	}
	print("SARKO_GENERATE %s tris=%d size=%.2f,%.2f,%.2f m"
		% (name, tris, size.x, size.y, size.z))

	# The long horizontal axis must be X, because Scripts/prepare-assets.py turns
	# any mesh that is longer in Y and every authored yaw in bridge.json was
	# chosen against "the long side is Extent.X". Checked here rather than left
	# to the next step, where the rotation would be silent.
	if size.y > size.x + 1e-6:
		print("SARKO_GENERATE ERROR %s is longer in Y than X and would be turned" % name)

	bpy.data.objects.remove(obj, do_unlink=True)
	bpy.data.meshes.remove(mesh)


def main():
	argv = sys.argv[sys.argv.index("--") + 1:]
	out_dir = argv[0]
	report_path = argv[1] if len(argv) > 1 else os.path.join(out_dir, "props.json")
	os.makedirs(out_dir, exist_ok=True)
	clear_scene()

	report = {}
	for builder in BUILDERS:
		build(builder, out_dir, report)

	with open(report_path, "w") as handle:
		json.dump(report, handle, indent=1, sort_keys=True)
	print("SARKO_GENERATE wrote %d props to %s" % (len(report), out_dir))


main()
