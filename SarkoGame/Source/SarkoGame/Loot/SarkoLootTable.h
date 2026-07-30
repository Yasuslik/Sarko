#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

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
	 * The stream seed for one container: the raid seed, the container index and a
	 * **server-only** salt, avalanche-mixed together (spec §4.2).
	 *
	 * The salt is why this takes three arguments instead of the two spec §4.2
	 * asks for. `RaidSeed ^ ContainerIndex` alone is client-derivable: RaidSeed is
	 * replicated (it is what tells a client the raid has begun), the tables ship
	 * inside the build, and RollContainer is pure — so any client could enumerate
	 * every container's contents before opening one, which is a loot map handed
	 * out for free. ASarkoRaidGameMode::LootSalt is generated on the authority at
	 * raid start and never replicated, so a client has no way to run this
	 * function over the right input.
	 *
	 * All arithmetic is done in uint32 and reinterpreted at the end, because the
	 * backend's seed is `int64(rand.Uint32())` and therefore routinely has the
	 * sign bit set — signed overflow is undefined behaviour, and "undefined" here
	 * means two machines can disagree about what is in a crate.
	 *
	 * The salt is 64 bits, and both halves are folded in at different stages of the
	 * mix. A 32-bit salt was sweepable: the two other inputs are known, so one
	 * observed roll pins the single salt that produces it, and pinning it hands
	 * back every other container in the raid. At 64 bits one observed roll
	 * constrains the salt to roughly 2^32 candidates instead of one, so it cannot
	 * be resolved from a single crate. The honest bound, stated rather than
	 * implied: this is obscurity sized to a raid, not a cipher — two or three
	 * observed rolls in the same raid still pin the salt offline, and what limits
	 * the damage is that the salt is regenerated per raid, so nothing learned from
	 * one raid carries into the next.
	 *
	 * Deterministic: a fixed (RaidSeed, ContainerIndex, LootSalt) is always the
	 * same stream, which is what lets the server re-derive a roll instead of
	 * storing it. Tests pass a fixed salt for exactly that reason.
	 */
	int32 ContainerSeed(int32 RaidSeed, int32 ContainerIndex, int64 LootSalt);

	/**
	 * Rolls one container's contents. Called **only on the server, only at the
	 * moment the container is opened** (spec §4.2, §6.1).
	 *
	 * Rolling lazily is not by itself what keeps a client from knowing what is in
	 * a crate — ContainerSeed's server-only salt is. What lazy rolling buys is
	 * that nothing ever holds a full map of the raid's contents in the first
	 * place, on either side: contents rolled up front would sit in server memory,
	 * and any later decision to replicate or log them would leak the whole map at
	 * once rather than one opened crate at a time.
	 *
	 * Deterministic in Stream, so the same stream seed always produces the same
	 * haul and a retried transfer cannot duplicate loot.
	 */
	TArray<FSarkoItemStack> RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream);

	/** What one completed loot channel actually moved. */
	struct FSarkoLootPayout
	{
		/** Units that made it into the backpack. */
		int32 Taken = 0;

		/** Units that did not fit and stay in the container, unrecoverable (spec §4.3). */
		int32 LeftBehind = 0;

		/** False when the container was already emptied: nothing was credited and nothing was marked. */
		bool bCredited = false;
	};

	/**
	 * The completion half of one loot channel: the rule that decides whether a
	 * finished channel pays out at all, and the order in which it does.
	 *
	 * Pure and world-free — it is handed its two effects rather than reaching for
	 * a backpack component or a game state — so the invariant that protects the
	 * economy is unit tested with no world, no actor and no network:
	 *
	 *  - an already-emptied container credits **nothing**, so one roll can never
	 *    be credited twice (the roll is deterministic, so a second payout would
	 *    be the same items again, out of thin air);
	 *  - Credit runs before Mark, because Mark is what makes the container
	 *    ineligible — marking first would make the credit unreachable;
	 *  - Mark runs **unconditionally** once credit has been attempted, including
	 *    when part or all of the roll did not fit. Spec §4.3 allows partial loot,
	 *    and leaving a partly-emptied container openable would re-run the same
	 *    deterministic roll and duplicate the part already taken.
	 *
	 * @param Rolled         this container's contents, from RollContainer
	 * @param bAlreadyLooted the authority's own looted bit for this index
	 * @param Credit         moves one stack into the backpack; returns the quantity that did not fit
	 * @param Mark           records the container as emptied
	 */
	FSarkoLootPayout CompleteLootChannel(const TArray<FSarkoItemStack>& Rolled, bool bAlreadyLooted,
		TFunctionRef<int32(FName /*Item*/, int32 /*Quantity*/)> Credit, TFunctionRef<void()> Mark);
}
