#pragma once

#include "CoreMinimal.h"

// Included, not forward-declared: FSarkoItemStack is a USTRUCT held by value in
// FSarkoLootContainerSpot::FixedItems below, so the full type has to be here.
#include "Loot/SarkoItemCatalog.h"
#include "Map/SarkoBuildings.h"
#include "Map/SarkoMapBuilder.h"

#include "SarkoMapDefinition.generated.h"

/** A placeable object: a wreck, a fuel pump, a freight car. Kind picks the mesh. */
USTRUCT()
struct FSarkoMapProp
{
	GENERATED_BODY()

	/** Optional stable name (ТЗ §18). See FSarkoCoverBlock::Id. */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FName Kind;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	/** See FSarkoCoverBlock::bSkirt — scenery beyond the border, and the only
	 *  kind of entry allowed outside the playable bound. */
	UPROPERTY()
	bool bSkirt = false;
};

/** Where a lootable container sits, and how good its contents are. */
USTRUCT()
struct FSarkoLootContainerSpot
{
	GENERATED_BODY()

	/**
	 * Stable name (ТЗ §18). Required on a shipped map — a container is a row in
	 * the loot ledger, and an anonymous one cannot be audited against it. See
	 * SarkoMap::RequireIdentifiedEntries.
	 */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Tier;

	/**
	 * Exact contents instead of a roll, for the one-time tutorial raid (spec
	 * §6.5). Empty for every normal container, which is all of them today —
	 * Stage C authors the teaching layout against Bridge_West's geometry.
	 *
	 * Only consulted while the player's profile says `tutorial_completed` is
	 * false, so once the tutorial is over this is dead data rather than a
	 * guaranteed drop a player could farm forever.
	 *
	 * Validated against the item catalog at parse time: an id the catalog does
	 * not know would be refused by the backend's domain.ValidateRaidItems at
	 * result time, fifteen minutes into a raid, after the player has already been
	 * shown the item.
	 */
	UPROPERTY()
	TArray<FSarkoItemStack> FixedItems;
};

/** A place the player can leave the raid from. Mechanic lands in a later plan. */
USTRUCT()
struct FSarkoExtractionSpot
{
	GENERATED_BODY()

	/**
	 * Stable name (ТЗ §18), required on a shipped map. Distinct from Name below:
	 * Id is what code and reports say, Name is what the player is shown.
	 */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float RadiusUU = 400.f;

	UPROPERTY()
	FString Name;

	/**
	 * Seconds of raid clock before this zone will accept a dwell. Zero (the
	 * default, and every zone that omits it) means open from the first frame.
	 *
	 * Optional on purpose: three of the four zones on the shipped map are simply
	 * open, and a required field would have made every one of them say so. What
	 * it buys is a zone whose existence is a DECISION rather than a shortcut —
	 * the west cordon opens for the last five minutes of a fifteen-minute raid,
	 * which is exactly the window a player walking home from the depot is in.
	 *
	 * Enforced on the server (ASarkoRaidGameMode::ExtractTick, through
	 * SarkoExtract::IsZoneOpen). The HUD reads the same number out of the same
	 * file to draw the zone as inert, which is presentation and never authority.
	 */
	UPROPERTY()
	float OpensAfterSeconds = 0.f;
};

/** A bot spawn, tagged with the risk zone it belongs to. */
USTRUCT()
struct FSarkoBotSpot
{
	GENERATED_BODY()

	/** Stable name (ТЗ §18), required on a shipped map. */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Zone;
};

/** How an encounter's trigger decides the player has arrived. */
UENUM()
enum class ESarkoTriggerKind : uint8
{
	/**
	 * The only shape today, and an enum rather than an implied constant so a
	 * corridor trigger can be added later as an ADDITION and not a migration.
	 * Radius, not a box: `pos` + `radiusUU` is the exact shape the parser
	 * already knows from `extractions`, the POIs on this map are round-ish
	 * (a station forecourt, a depot yard), and a yaw'd box is a second thing to
	 * get wrong.
	 */
	Radius
};

/** One enemy an encounter may put on the map, authored point by authored point. */
USTRUCT()
struct FSarkoEncounterSpawn
{
	GENERATED_BODY()

	/** Stable name (ТЗ §18). Required — this is the id the spawn log prints. */
	UPROPERTY()
	FString Id;

	/**
	 * Where the pawn is CREATED. Never procedural: a spawn point that is a JSON
	 * row is a spawn point the map test can assert is not inside a wall, a
	 * building or a prop. "A bot spawned inside a wall" is called the oldest bug
	 * on this map in bridge.json's own notes, and it has recurred twice.
	 */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** A row of SarkoAI::GetBotArchetypes. Rejected at parse time if unknown. */
	UPROPERTY()
	FName Archetype;

	/**
	 * Where this bot HOLDS, once it exists — distinct from Location on purpose.
	 * The spawn point has to satisfy "far from the player and out of sight at
	 * this instant"; the post has to be the interesting ground at the POI. On a
	 * map where those two can be the same point they are, and where they cannot
	 * (a spawn door that only qualifies from one approach) they are not.
	 */
	UPROPERTY()
	FVector2D PostPos = FVector2D::ZeroVector;

	/** How far from PostPos this bot may wander. See USarkoRaidSettings::AIPatrolLeashUU. */
	UPROPERTY()
	float LeashUU = 1400.f;
};

/** Where the player has to walk for an encounter to happen. */
USTRUCT()
struct FSarkoEncounterTrigger
{
	GENERATED_BODY()

	UPROPERTY()
	ESarkoTriggerKind Kind = ESarkoTriggerKind::Radius;

	UPROPERTY()
	FVector2D Location = FVector2D::ZeroVector;

	UPROPERTY()
	float RadiusUU = 0.f;

	/**
	 * Hysteresis, and it must exceed RadiusUU. The trigger only becomes armable
	 * again once the player has been FURTHER than this from it — otherwise a
	 * player loitering on the boundary pumps the system, arming and disarming
	 * several times a second.
	 */
	UPROPERTY()
	float ArmAfterUU = 0.f;
};

/** One authored event: the player walks somewhere, and enemies are at the building. */
USTRUCT()
struct FSarkoEncounter
{
	GENERATED_BODY()

	/** Stable name (ТЗ §18), required, unique across the whole file. */
	UPROPERTY()
	FString Id;

	/**
	 * Tie-break when two triggers arm in the same evaluation — lower fires
	 * first. This is what makes "the first fight is the gas station" data rather
	 * than luck, and it is what `firstFightMaxAlive` is measured against.
	 */
	UPROPERTY()
	int32 Order = 0;

	/** Charged against the per-raid budget when this encounter fires. Never refunded. */
	UPROPERTY()
	int32 BudgetCost = 1;

	/** Ceiling for THIS encounter alone; the budget is the ceiling for the raid. */
	UPROPERTY()
	int32 MaxAlive = 1;

	/** Tutorial encounters are one-shot; a normal raid may re-arm a POI. */
	UPROPERTY()
	bool bOneShot = true;

	/**
	 * NOT part of the tutorial's curriculum.
	 *
	 * A tutorial raid activates only the rows where this is false, in `order`:
	 * the first raid a person plays teaches one enemy, then one, then two, and a
	 * shuffled curriculum is not a curriculum. A normal raid activates
	 * everything and shuffles the activation order against the raid seed, which
	 * is what stops raid 2 from being raid 1 (spec §5).
	 *
	 * The name is `optional` and not `tutorialOnly` because that is the honest
	 * direction of the exception. Every row on the map is available in a normal
	 * raid; what varies is whether the TUTORIAL is allowed to reach it. Flagging
	 * the teaching rows instead would have said the opposite — that the three are
	 * withheld from normal play — and they are not.
	 */
	UPROPERTY()
	bool bOptional = false;

	UPROPERTY()
	FSarkoEncounterTrigger Trigger;

	UPROPERTY()
	TArray<FSarkoEncounterSpawn> Spawns;
};

/**
 * How many enemies a raid of each kind is allowed, for the whole raid.
 *
 * The primary object of the encounter system is this counter, not the triggers:
 * "three to five enemies for the whole tutorial" is a raid-scoped number, and a
 * trigger that fires when the budget is spent must do nothing, silently and
 * correctly.
 */
USTRUCT()
struct FSarkoEncounterBudget
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Tutorial = 0;

	UPROPERTY()
	int32 Normal = 0;

	/**
	 * The ceiling on the FIRST fight of a raid, whatever that encounter's own
	 * maxAlive says. One. It is non-negotiable and it saved the game once
	 * already: eight bots that all heard the same shot turned a firefight into
	 * an execution.
	 */
	UPROPERTY()
	int32 FirstFightMaxAlive = 0;

	/** False when the map authored no `encounterBudget` at all — then nothing spawns. */
	UPROPERTY()
	bool bAuthored = false;
};

/**
 * A whole hand-authored map, exactly as it appears in the data file.
 *
 * Distinct from FSarkoMapLayout on purpose: the definition is what a designer
 * writes and can include things the spawner does not care about (extraction
 * names, container tiers, zone tags), while the layout is the reduced form the
 * existing spawn code already consumes.
 */
USTRUCT()
struct FSarkoMapDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	float ExtentUU = 0.f;

	UPROPERTY()
	float RaidDurationSeconds = 0.f;

	UPROPERTY()
	TArray<FSarkoCoverBlock> Blocks;

	/**
	 * Walkable buildings, one declaration each. ToLayout expands them into
	 * Layout.Cover alongside the authored blocks — there is no second spawn
	 * path and no building actor, because a building IS its walls.
	 */
	UPROPERTY()
	TArray<FSarkoBuilding> Buildings;

	UPROPERTY()
	TArray<FSarkoMapProp> Props;

	UPROPERTY()
	TArray<FSarkoLootContainerSpot> Containers;

	UPROPERTY()
	TArray<FTransform> PlayerSpawns;

	/**
	 * Ids for PlayerSpawns, index-aligned. A player spawn is an FTransform —
	 * an engine type with nowhere to put a name — so its id rides alongside.
	 * ParseDefinition always appends to both arrays in the same iteration, and
	 * a test pins that the lengths agree.
	 */
	UPROPERTY()
	TArray<FString> PlayerSpawnIds;

	/**
	 * Statically posted bots — the pre-encounter shape, kept because non-tutorial
	 * content still uses it. The shipped tutorial map authors none: on that map
	 * every enemy arrives through Encounters below.
	 */
	UPROPERTY()
	TArray<FSarkoBotSpot> BotSpawns;

	UPROPERTY()
	TArray<FSarkoExtractionSpot> Extractions;

	UPROPERTY()
	TArray<FSarkoEncounter> Encounters;

	UPROPERTY()
	FSarkoEncounterBudget EncounterBudget;
};

namespace SarkoMap
{
	/**
	 * Parses a map file. Pure: text in, definition out, no disk and no world,
	 * which is what lets the schema be tested from string literals.
	 *
	 * Every failure sets OutError to something that names the problem. A map
	 * file is hand-edited, so it will be broken eventually, and the worst
	 * outcome is a silent empty map — the game launches with nothing in it and
	 * no clue why.
	 */
	bool ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError);

	/** Reduces a definition to the layout the existing spawn code consumes. */
	FSarkoMapLayout ToLayout(const FSarkoMapDefinition& Definition);

	/**
	 * Reads Data/Maps/<MapId>.json from the project directory. Stricter than
	 * ParseDefinition: a map that ships must also satisfy
	 * RequireIdentifiedEntries.
	 */
	bool LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError);

	/**
	 * Every id in the definition, in file order. Fails (naming the id) on a
	 * duplicate: ids are a single namespace across the whole file, because the
	 * thing that reads them — a report, a test, a person — does not know or
	 * care which section an object was declared in.
	 */
	bool CollectIds(const FSarkoMapDefinition& Definition, TArray<FString>& OutIds, FString& OutError);

	/**
	 * The stricter rule for a map that ships: every container, player spawn,
	 * bot spawn, extraction and building must be named. Enforced by
	 * LoadDefinitionFromDisk, not by ParseDefinition — a test fixture built
	 * from a string literal has no reason to name anything, and making the
	 * pure parser strict here would break every fixture in the suite and the
	 * promise that an older map file still loads.
	 */
	bool RequireIdentifiedEntries(const FSarkoMapDefinition& Definition, FString& OutError);

	/**
	 * Every PLAYABLE entry is inside |x|, |y| <= ExtentUU; every entry flagged
	 * `skirt` is inside ExtentUU + SkirtMarginUU.
	 *
	 * Enforced by ParseDefinition — by the parser, on every map, including
	 * fixtures — because the edge skirt made the alternative untenable. Before
	 * the skirt this rule lived as `CheckInside` inside
	 * Sarko.Map.BridgeMapIsValid: one assertion, over one file, for one map. The
	 * skirt needs geometry OUTSIDE the sector, and the two ways to allow that
	 * were to widen the bound or to name the exception. Widening it would have
	 * let a container, an encounter door or a player spawn out there too —
	 * silently, because the only thing that ever checked was a test about the
	 * bridge — and a piece of gameplay in the void is content that cannot be
	 * reached, played or debugged.
	 *
	 * So: the exception is a field a row has to write, it applies to scenery
	 * only (blocks and props are the only structs that carry it), and the
	 * protection is now stronger than the thing it replaced rather than weaker.
	 * A playable entry outside the bound is a named parse error that says which
	 * entry and by how much.
	 */
	bool CheckPlayableBounds(const FSarkoMapDefinition& Definition, FString& OutError);
}
