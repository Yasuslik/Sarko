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

	UPROPERTY()
	TArray<FSarkoBotSpot> BotSpawns;

	UPROPERTY()
	TArray<FSarkoExtractionSpot> Extractions;
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
}
