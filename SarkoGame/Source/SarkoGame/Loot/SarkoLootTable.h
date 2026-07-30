#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoLootTable.generated.h"

/** One possible drop: which item, how likely relative to its siblings, how many. */
USTRUCT()
struct FSarkoLootEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName Item;

	/** Relative weight within its tier. Always > 0; probabilities are weight/total. */
	UPROPERTY()
	float Weight = 1.f;

	UPROPERTY()
	int32 MinQuantity = 1;

	UPROPERTY()
	int32 MaxQuantity = 1;
};

/** What one container tier can hold. */
USTRUCT()
struct FSarkoLootTable
{
	GENERATED_BODY()

	UPROPERTY()
	FName Tier;

	UPROPERTY()
	int32 MinRolls = 1;

	UPROPERTY()
	int32 MaxRolls = 1;

	/**
	 * Chance the container is simply empty. ТЗ §30 caps this per tier: junk
	 * 0.15, common 0.08, med 0.15, and good/military never empty — walking
	 * across the ravine for a locked-and-empty military crate is the single
	 * most demoralising outcome an extraction map can produce.
	 */
	UPROPERTY()
	float EmptyChance = 0.f;

	UPROPERTY()
	TArray<FSarkoLootEntry> Entries;
};

/** Every tier's table. */
USTRUCT()
struct FSarkoLootTables
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoLootTable> Tables;

	/** Null for an unknown tier. A default tier would silently mis-loot a container. */
	const FSarkoLootTable* Find(FName Tier) const;
};

namespace SarkoLoot
{
	/** The five tiers every table file must define, in this order. */
	extern const FName TierJunk;
	extern const FName TierCommon;
	extern const FName TierMed;
	extern const FName TierGood;
	extern const FName TierMilitary;

	/**
	 * Parses the loot tables and validates them against the catalog. Pure.
	 *
	 * An item id that is not in the catalog is a load **error**, never a skipped
	 * entry (spec §4.1): a skip changes the drop table silently, and the symptom
	 * is "loot feels thin", which nobody can trace to a typo in a data file.
	 */
	bool ParseLootTables(const FString& Json, const FSarkoItemCatalog& Catalog,
		FSarkoLootTables& OutTables, FString& OutError);

	/** Reads Data/Loot/loot-tables.json from the project directory. */
	bool LoadLootTablesFromDisk(const FSarkoItemCatalog& Catalog, FSarkoLootTables& OutTables, FString& OutError);

	/** The process-wide tables, loaded on first use against GetItemCatalog(). Loud and empty on failure. */
	const FSarkoLootTables& GetLootTables();

	/**
	 * The stream seed for one container: `RaidSeed ^ ContainerIndex` (spec §4.2).
	 *
	 * XOR is done in uint32 and reinterpreted, because the backend's seed is
	 * `int64(rand.Uint32())` and therefore routinely has the sign bit set —
	 * signed arithmetic on that is undefined behaviour, and "undefined" here
	 * means two machines can disagree about what is in a crate.
	 */
	int32 ContainerSeed(int32 RaidSeed, int32 ContainerIndex);

	/**
	 * Rolls one container's contents. Called **only on the server, only at the
	 * moment the container is opened** (spec §4.2, §6.1): contents generated
	 * ahead of time would replicate to clients and be readable out of memory,
	 * which is a loot map.
	 *
	 * Deterministic in Stream, so the same raid seed and container index always
	 * produce the same haul and a retried transfer cannot duplicate loot.
	 */
	TArray<FSarkoItemStack> RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream);
}
