#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapBuilder.h"

#include "SarkoBuildings.generated.h"

/** Which face of a building's footprint a door is in. N is the +Y edge. */
UENUM()
enum class ESarkoBuildingSide : uint8
{
	North,
	East,
	South,
	West
};

/**
 * A doorway: not a door. There is no door mechanic (ТЗ §13) — this declares a
 * gap in a wall, and the gap is all there is.
 */
USTRUCT()
struct FSarkoBuildingDoor
{
	GENERATED_BODY()

	/** Which wall. Ignored for an interior wall's door, which has only one wall to be in. */
	UPROPERTY()
	ESarkoBuildingSide Side = ESarkoBuildingSide::North;

	/**
	 * Signed distance along the wall from that wall's MIDPOINT, in local uu:
	 * local X for a north or south wall, local Y for an east or west one.
	 * Zero is centred, which is what most buildings want.
	 */
	UPROPERTY()
	float OffsetUU = 0.f;

	/** The clear opening. Never below SarkoMap::MinDoorwayUU. */
	UPROPERTY()
	float WidthUU = 300.f;
};

/**
 * A wall inside a building, dividing it into rooms, with an optional doorway.
 *
 * Axis-aligned only: a diagonal interior wall is rejected. Nothing in the ТЗ
 * needs one, and supporting them would mean the passage-clearance rule (the one
 * that keeps a room reachable) stops being computable by comparing two numbers.
 */
USTRUCT()
struct FSarkoBuildingInteriorWall
{
	GENERATED_BODY()

	/** Endpoints in the building's local frame, before yaw. */
	UPROPERTY()
	FVector2D From = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D To = FVector2D::ZeroVector;

	/**
	 * False makes this a solid divider.
	 *
	 * Legal, but not a licence to seal a room: the expander flood-fills the
	 * interior from the perimeter doorways and rejects any enclosed pocket
	 * bigger than SarkoMap::MinSealedRoomUU square. A solid divider is therefore
	 * fine when it only partly crosses the room, or when each side of it has its
	 * own perimeter doorway — and rejected when it walls off a closet nothing can
	 * walk into. There is no store-room case: a room Stage C can author a
	 * container into is a room the player must be able to reach.
	 */
	UPROPERTY()
	bool bHasDoor = false;

	/** Only Door.OffsetUU and Door.WidthUU are read; Side has no meaning here. */
	UPROPERTY()
	FSarkoBuildingDoor Door;
};

/**
 * One walkable building, declared once.
 *
 * This is the whole point of Stage B: ~20 buildings as ~20 of these instead of
 * ~400 hand-placed wall blocks. The expander below turns one of these into the
 * walls, and every invariant the ТЗ states about buildings (§13) is checked
 * there rather than trusted to the author.
 *
 * No roofs (the camera is above), no stairs, no second floor. The local frame:
 * origin at the centre of the footprint at floor level, SizeUU is the full
 * OUTER footprint, +Y is local north, +X local east, and Yaw turns the lot.
 */
USTRUCT()
struct FSarkoBuilding
{
	GENERATED_BODY()

	/** Required (ТЗ §18). Every emitted wall's id is derived from it. */
	UPROPERTY()
	FString Id;

	/** World position of the footprint's centre, at floor level. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Full outer footprint: X by Y, before yaw. */
	UPROPERTY()
	FVector2D SizeUU = FVector2D(2000.f, 1500.f);

	UPROPERTY()
	float Yaw = 0.f;

	/**
	 * Wall height. 350 uu by default, per spec §5.1: the pawn is ~176 uu, so a
	 * wall this tall cuts line of sight completely at ground level while the
	 * top-down camera still sees over it into the room.
	 */
	UPROPERTY()
	float WallHeightUU = 350.f;

	UPROPERTY()
	float WallThicknessUU = 30.f;

	/** Colour for every wall of this building (ТЗ §14). */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;

	/**
	 * Empty means a CLOSED building — the ТЗ's four "закрытых" entries — and it
	 * is legal. One door is NOT: ТЗ §13 requires two exits from anything the
	 * player can enter, or a bot in the doorway is a death sentence.
	 */
	UPROPERTY()
	TArray<FSarkoBuildingDoor> Doors;

	UPROPERTY()
	TArray<FSarkoBuildingInteriorWall> InteriorWalls;
};

namespace SarkoMap
{
	/** ТЗ §13's floor for any opening the player walks through. */
	constexpr float MinDoorwayUU = 250.f;

	/** ТЗ §13 prefers 300-350; below this the expander logs a warning, not an error. */
	constexpr float PreferredDoorwayUU = 300.f;

	/** ТЗ §13's floor for the clear space between two parallel interior walls. */
	constexpr float MinInteriorPassageUU = 250.f;

	/**
	 * The shortest piece of wall the expander will emit: the stub left at the end
	 * of a wall, and the pier left between two doorways on one wall.
	 *
	 * Not the wall thickness, which is what it used to be, and which let a 30 uu
	 * fleck through — a 30x30x350 splinter beside a 176 uu pawn does not read as
	 * masonry, it reads as a rendering artefact, and two doorways one thickness
	 * apart produce a free-standing 30x30 post with a gap either side (which
	 * Sarko.Map.ClosedBuildingIsSealed already declares is not a wall). 120 uu is
	 * two thirds of the pawn's own width and reads as a deliberate pier; the max
	 * with Thickness keeps the rule meaningful for a wall thicker than that.
	 */
	constexpr float MinWallStubUU = 120.f;

	/**
	 * The largest enclosed pocket of interior the reachability check tolerates.
	 *
	 * A pocket smaller than this square is a sliver between two walls — geometry,
	 * not a room. Anything bigger is somewhere Stage C could author a container
	 * into and nobody could ever walk to, which is loot that does not exist and
	 * nothing failing.
	 */
	constexpr float MinSealedRoomUU = 150.f;

	/** Absolute floor for a wall: it must break the line of sight of a ~176 uu pawn. */
	constexpr float MinWallHeightUU = 200.f;

	/**
	 * Turns one building declaration into wall blocks.
	 *
	 * Pure and deterministic: same input, same output, same order, on every
	 * machine — the map is expanded independently on server and client, so a
	 * wall in a different place on each is a desync that presents as a physics
	 * bug. RESETS OutBlocks; on failure leaves it empty and names the problem
	 * (with the building's id) in OutError.
	 *
	 * Emission order is fixed: north, east, south, west, then interior walls in
	 * author order, each side's segments running from its negative end.
	 */
	bool ExpandBuilding(const FSarkoBuilding& Building, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError);

	/** ExpandBuilding over a list, accumulating in author order. Resets OutBlocks. */
	bool ExpandBuildings(const TArray<FSarkoBuilding>& Buildings, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError);

	/**
	 * Whether a point in the horizontal plane is inside any block's footprint.
	 * Horizontal only: everything here is a full-height wall or a flat surface,
	 * and the question being asked is always "can the pawn be here".
	 */
	bool IsPointInsideBlocksXY(const FVector2D& Point, const TArray<FSarkoCoverBlock>& Blocks);

	/**
	 * Whether two blocks' footprints intersect, honouring both yaws.
	 *
	 * Lives here rather than in a test file because "no wall overlaps another
	 * wall" is an invariant of the expander, and an invariant checked by a
	 * predicate the expander does not share is an invariant that can drift — the
	 * axis-aligned version this replaces could not see a rotated building at all,
	 * so the 45-degree case was unassertable.
	 *
	 * A separating-axis test over the four face normals, exact for two rectangles.
	 * Slack is how deep an intersection has to be to count: two walls that butt
	 * face to face are separated at any positive slack, which is the whole point
	 * of the shortening the expander does.
	 */
	bool BlocksOverlapXY(const FSarkoCoverBlock& A, const FSarkoCoverBlock& B, float Slack = 0.01f);

	/** "N"/"north", "E"/"east", "S"/"south", "W"/"west", case-insensitive. */
	bool ParseBuildingSide(const FString& Name, ESarkoBuildingSide& Out);
}
