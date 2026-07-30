#include "Loot/SarkoLootTable.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const FName SarkoLoot::TierJunk(TEXT("junk"));
const FName SarkoLoot::TierCommon(TEXT("common"));
const FName SarkoLoot::TierMed(TEXT("med"));
const FName SarkoLoot::TierGood(TEXT("good"));
const FName SarkoLoot::TierMilitary(TEXT("military"));

const FSarkoLootTable* FSarkoLootTables::Find(FName Tier) const
{
	return Tables.FindByPredicate([Tier](const FSarkoLootTable& Table) { return Table.Tier == Tier; });
}

namespace
{
	/** Reads {"min":a,"max":b} with a >= Floor and b >= a. */
	bool ReadRange(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32 Floor,
		int32& OutMin, int32& OutMax, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Range = nullptr;
		if (!Object->TryGetObjectField(Field, Range) || !Range)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an object"), *Field);
			return false;
		}
		double Min = 0.0;
		double Max = 0.0;
		if (!(*Range)->TryGetNumberField(TEXT("min"), Min) || !(*Range)->TryGetNumberField(TEXT("max"), Max))
		{
			OutError = FString::Printf(TEXT("'%s' needs both 'min' and 'max'"), *Field);
			return false;
		}
		if (Min < static_cast<double>(Floor) || Max < Min)
		{
			OutError = FString::Printf(TEXT("'%s' must satisfy %d <= min <= max, got min=%g max=%g"),
				*Field, Floor, Min, Max);
			return false;
		}
		OutMin = static_cast<int32>(Min);
		OutMax = static_cast<int32>(Max);
		return true;
	}
}

bool SarkoLoot::ParseLootTables(const FString& Json, const FSarkoItemCatalog& Catalog,
	FSarkoLootTables& OutTables, FString& OutError)
{
	OutTables = FSarkoLootTables();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	const TSharedPtr<FJsonObject>* Tiers = nullptr;
	if (!Root->TryGetObjectField(TEXT("tiers"), Tiers) || !Tiers)
	{
		OutError = TEXT("'tiers' is missing or not an object");
		return false;
	}

	// Fixed set, fixed order. A tier the map uses but the file omits would open
	// a container onto nothing, so absence is an error rather than a default.
	const TArray<FName> Required = { TierJunk, TierCommon, TierMed, TierGood, TierMilitary };

	for (const FName& Tier : Required)
	{
		const TSharedPtr<FJsonObject>* TierObject = nullptr;
		if (!(*Tiers)->TryGetObjectField(Tier.ToString(), TierObject) || !TierObject)
		{
			OutError = FString::Printf(TEXT("tier '%s' is missing"), *Tier.ToString());
			return false;
		}

		FSarkoLootTable Table;
		Table.Tier = Tier;

		if (!ReadRange(*TierObject, TEXT("rolls"), 1, Table.MinRolls, Table.MaxRolls, OutError))
		{
			OutError = FString::Printf(TEXT("tier '%s': %s"), *Tier.ToString(), *OutError);
			return false;
		}

		double EmptyChance = 0.0;
		if (!(*TierObject)->TryGetNumberField(TEXT("emptyChance"), EmptyChance) ||
			EmptyChance < 0.0 || EmptyChance > 1.0)
		{
			OutError = FString::Printf(TEXT("tier '%s': 'emptyChance' must be between 0 and 1"), *Tier.ToString());
			return false;
		}
		Table.EmptyChance = static_cast<float>(EmptyChance);

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!(*TierObject)->TryGetArrayField(TEXT("entries"), Entries) || !Entries || Entries->Num() == 0)
		{
			OutError = FString::Printf(TEXT("tier '%s': 'entries' is missing or empty"), *Tier.ToString());
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("tier '%s': 'entries' has a non-object member"), *Tier.ToString());
				return false;
			}

			FSarkoLootEntry Entry;

			FString ItemId;
			if (!(*Object)->TryGetStringField(TEXT("item"), ItemId) || ItemId.IsEmpty())
			{
				OutError = FString::Printf(TEXT("tier '%s': an entry has no 'item'"), *Tier.ToString());
				return false;
			}
			Entry.Item = FName(*ItemId);
			if (Catalog.Find(Entry.Item) == nullptr)
			{
				// Spec §4.1: an unknown id is a load error, not a silent skip.
				OutError = FString::Printf(TEXT("tier '%s': item '%s' is not in the item catalog"),
					*Tier.ToString(), *ItemId);
				return false;
			}

			double Weight = 0.0;
			if (!(*Object)->TryGetNumberField(TEXT("weight"), Weight) || Weight <= 0.0)
			{
				OutError = FString::Printf(TEXT("tier '%s': item '%s' needs a positive 'weight'"),
					*Tier.ToString(), *ItemId);
				return false;
			}
			Entry.Weight = static_cast<float>(Weight);

			if (!ReadRange(*Object, TEXT("qty"), 1, Entry.MinQuantity, Entry.MaxQuantity, OutError))
			{
				OutError = FString::Printf(TEXT("tier '%s', item '%s': %s"), *Tier.ToString(), *ItemId, *OutError);
				return false;
			}

			Table.Entries.Add(Entry);
		}

		OutTables.Tables.Add(Table);
	}

	return true;
}

bool SarkoLoot::LoadLootTablesFromDisk(const FSarkoItemCatalog& Catalog, FSarkoLootTables& OutTables, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Loot") / TEXT("loot-tables.json");

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read the loot tables at %s"), *Path);
		return false;
	}
	if (!ParseLootTables(Json, Catalog, OutTables, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}

const FSarkoLootTables& SarkoLoot::GetLootTables()
{
	static FSarkoLootTables Tables;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Error;
		if (!LoadLootTablesFromDisk(GetItemCatalog(), Tables, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoLoot: %s — every container will open empty"), *Error);
			Tables = FSarkoLootTables();
		}
	}
	return Tables;
}

int32 SarkoLoot::ContainerSeed(int32 RaidSeed, int32 ContainerIndex)
{
	// Unsigned XOR then reinterpret: the backend's seed is int64(rand.Uint32()),
	// so the sign bit is set about half the time, and signed overflow is UB.
	const uint32 Mixed = static_cast<uint32>(RaidSeed) ^ static_cast<uint32>(ContainerIndex);
	return static_cast<int32>(Mixed);
}

TArray<FSarkoItemStack> SarkoLoot::RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream)
{
	TArray<FSarkoItemStack> Out;

	// Empty check first, so emptyChance means "this container is empty" rather
	// than "one of its rolls produced nothing".
	if (Table.EmptyChance > 0.f && Stream.FRand() < Table.EmptyChance)
	{
		return Out;
	}
	if (Table.Entries.Num() == 0)
	{
		return Out;
	}

	float TotalWeight = 0.f;
	for (const FSarkoLootEntry& Entry : Table.Entries)
	{
		TotalWeight += Entry.Weight;
	}
	if (TotalWeight <= 0.f)
	{
		return Out;
	}

	const int32 Rolls = Stream.RandRange(Table.MinRolls, Table.MaxRolls);
	Out.Reserve(Rolls);

	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		float Pick = Stream.FRand() * TotalWeight;
		const FSarkoLootEntry* Chosen = &Table.Entries.Last();
		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			Pick -= Entry.Weight;
			if (Pick <= 0.f)
			{
				Chosen = &Entry;
				break;
			}
		}
		// Chosen defaults to the last entry so a float that never crosses zero
		// (FRand can return values arbitrarily close to 1) still drops something
		// instead of silently skipping a roll.
		Out.Add(FSarkoItemStack{ Chosen->Item, Stream.RandRange(Chosen->MinQuantity, Chosen->MaxQuantity) });
	}

	return Out;
}
