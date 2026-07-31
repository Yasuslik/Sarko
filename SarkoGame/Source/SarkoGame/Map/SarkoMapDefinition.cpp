#include "Map/SarkoMapDefinition.h"

#include "Dom/JsonObject.h"
#include "Loot/SarkoItemCatalog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** An extraction zone smaller than this could be walked through without the
	 *  overlap ever registering, so it could never trigger. */
	constexpr float MinExtractionRadiusUU = 100.f;

	/** Reads a ["x","y","z"] array into a vector, naming the field and the index
	 *  of any element that fails to parse. Each element is read with the
	 *  try-form so a non-number is a named error instead of a silent 0.0. */
	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array) || !Array)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an array"), *Field);
			return false;
		}
		if (Array->Num() != 3)
		{
			OutError = FString::Printf(TEXT("'%s' must have exactly 3 numbers, found %d"), *Field, Array->Num());
			return false;
		}
		double Components[3] = { 0.0, 0.0, 0.0 };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!(*Array)[Index]->TryGetNumber(Components[Index]))
			{
				OutError = FString::Printf(TEXT("'%s[%d]' is not a number"), *Field, Index);
				return false;
			}
		}
		Out = FVector(
			static_cast<float>(Components[0]),
			static_cast<float>(Components[1]),
			static_cast<float>(Components[2]));
		return true;
	}

	/**
	 * Reads an optional array field. Absence is legitimate (the caller decides
	 * what an absent section means); a value present under this key but not an
	 * array is not, and must not be indistinguishable from zero entries.
	 */
	bool TryGetOptionalArrayField(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const TArray<TSharedPtr<FJsonValue>>*& OutArray, FString& OutError)
	{
		OutArray = nullptr;
		if (!Object->HasField(Field))
		{
			return true;
		}
		if (!Object->TryGetArrayField(Field, OutArray) || !OutArray)
		{
			OutError = FString::Printf(TEXT("'%s' is present but not an array"), *Field);
			return false;
		}
		return true;
	}

	/**
	 * Reads an optional numeric field. A genuinely absent field keeps Out at
	 * whatever default the caller preset it to; a field that is present but
	 * cannot be parsed as a number is a named error, not a silent 0.0.
	 */
	bool ReadOptionalNumber(const TSharedPtr<FJsonObject>& Object, const FString& Field, double& Out, FString& OutError)
	{
		if (!Object->HasField(Field))
		{
			return true;
		}
		if (!Object->TryGetNumberField(Field, Out))
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a number"), *Field);
			return false;
		}
		return true;
	}

	/**
	 * Reads an optional string field the same way: absent keeps the caller's
	 * default, present-but-not-a-string is a named error rather than silently
	 * collapsing to an empty string indistinguishable from "not set".
	 */
	bool ReadOptionalString(const TSharedPtr<FJsonObject>& Object, const FString& Field, FString& Out, FString& OutError)
	{
		if (!Object->HasField(Field))
		{
			return true;
		}
		if (!Object->TryGetStringField(Field, Out))
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a string"), *Field);
			return false;
		}
		return true;
	}

	/** Reads a yaw in degrees, defaulting to 0.0 when the field is absent. */
	bool ReadYaw(const TSharedPtr<FJsonObject>& Object, double& OutYaw, FString& OutError)
	{
		OutYaw = 0.0;
		return ReadOptionalNumber(Object, TEXT("yaw"), OutYaw, OutError);
	}
}

bool SarkoMap::ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	OutDefinition = FSarkoMapDefinition();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	if (!Root->TryGetStringField(TEXT("id"), OutDefinition.Id) || OutDefinition.Id.IsEmpty())
	{
		OutError = TEXT("'id' is missing or empty");
		return false;
	}

	double Extent = 0.0;
	if (!Root->TryGetNumberField(TEXT("extentUU"), Extent) || Extent <= 0.0)
	{
		OutError = TEXT("'extentUU' is missing or not positive");
		return false;
	}
	OutDefinition.ExtentUU = static_cast<float>(Extent);

	double Duration = 0.0;
	if (!Root->TryGetNumberField(TEXT("raidDurationSeconds"), Duration) || Duration <= 0.0)
	{
		OutError = TEXT("'raidDurationSeconds' is missing or not positive");
		return false;
	}
	OutDefinition.RaidDurationSeconds = static_cast<float>(Duration);

	// blocks
	const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("blocks"), Blocks, OutError))
	{
		return false;
	}
	if (Blocks)
	{
		for (int32 Index = 0; Index < Blocks->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Blocks)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("blocks[%d]: not an object"), Index);
				return false;
			}
			FSarkoCoverBlock Block;
			if (!ReadVector(*Object, TEXT("pos"), Block.Location, OutError) ||
				!ReadVector(*Object, TEXT("extent"), Block.Extent, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			// A block with a non-positive extent describes geometry that either
			// cannot exist or has collapsed to nothing, and is never intentional.
			if (Block.Extent.X <= 0.f || Block.Extent.Y <= 0.f || Block.Extent.Z <= 0.f)
			{
				OutError = FString::Printf(TEXT("blocks[%d]: 'extent' components must all be positive, found (%.1f, %.1f, %.1f)"),
					Index, Block.Extent.X, Block.Extent.Y, Block.Extent.Z);
				return false;
			}
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			Block.Rotation = FRotator(0.f, static_cast<float>(Yaw), 0.f);
			OutDefinition.Blocks.Add(Block);
		}
	}

	// props
	const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("props"), Props, OutError))
	{
		return false;
	}
	if (Props)
	{
		for (int32 Index = 0; Index < Props->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Props)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("props[%d]: not an object"), Index);
				return false;
			}
			FSarkoMapProp Prop;
			if (!ReadVector(*Object, TEXT("pos"), Prop.Location, OutError))
			{
				OutError = FString::Printf(TEXT("props[%d]: %s"), Index, *OutError);
				return false;
			}
			// An empty/missing kind means no mesh is chosen downstream, which is
			// the same silent no-op that already cost a session once.
			FString Kind;
			if (!(*Object)->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
			{
				OutError = FString::Printf(TEXT("props[%d]: 'kind' is missing or empty"), Index);
				return false;
			}
			Prop.Kind = FName(*Kind);
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("props[%d]: %s"), Index, *OutError);
				return false;
			}
			Prop.Yaw = static_cast<float>(Yaw);
			OutDefinition.Props.Add(Prop);
		}
	}

	// containers
	const TArray<TSharedPtr<FJsonValue>>* Containers = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("containers"), Containers, OutError))
	{
		return false;
	}
	if (Containers)
	{
		for (int32 Index = 0; Index < Containers->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Containers)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("containers[%d]: not an object"), Index);
				return false;
			}
			FSarkoLootContainerSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			// Unlike a prop's kind, nothing downstream reads a container's tier
			// yet (ToLayout drops Containers entirely), so an absent tier is
			// left as NAME_None rather than rejected. A present-but-wrong-typed
			// value is still a named error, matching the rule for every other
			// optional field above.
			FString Tier;
			if (!ReadOptionalString(*Object, TEXT("tier"), Tier, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			Spot.Tier = FName(*Tier);

			// fixedItems — optional, and every failure is named. A silently
			// shortened list is a teaching beat that quietly stops happening.
			const TArray<TSharedPtr<FJsonValue>>* FixedItems = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("fixedItems"), FixedItems, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			if (FixedItems)
			{
				// An empty list is a named error, not "no fixed items". Written to
				// mean "this container is empty during the tutorial" it would do the
				// opposite: FSarkoLootContainerSpot::FixedItems would be empty,
				// indistinguishable from an absent key, so SarkoLoot::RollContainerFor
				// falls through to a seeded roll and the container hands out random
				// loot. It would also cost Stage C its acceptance signal —
				// SetTutorialLoot counts containers with Num() > 0, so the "none of N
				// containers carries fixedItems" Warning would keep firing for a map
				// that had in fact been authored.
				if (FixedItems->Num() == 0)
				{
					OutError = FString::Printf(
						TEXT("containers[%d]: 'fixedItems' is present but empty — an empty list cannot mean 'holds nothing' (the container would roll against the raid seed instead); omit the key to roll, or list what it holds"),
						Index);
					return false;
				}
				for (int32 ItemIndex = 0; ItemIndex < FixedItems->Num(); ++ItemIndex)
				{
					const TSharedPtr<FJsonObject>* ItemObject = nullptr;
					if (!(*FixedItems)[ItemIndex]->TryGetObject(ItemObject) || !ItemObject)
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: not an object"),
							Index, ItemIndex);
						return false;
					}
					FString ItemId;
					if (!(*ItemObject)->TryGetStringField(TEXT("item"), ItemId) || ItemId.IsEmpty())
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: 'item' is missing or empty"),
							Index, ItemIndex);
						return false;
					}
					double Quantity = 0.0;
					if (!(*ItemObject)->TryGetNumberField(TEXT("qty"), Quantity) || Quantity < 1.0)
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d] ('%s'): 'qty' is missing or less than 1"),
							Index, ItemIndex, *ItemId);
						return false;
					}
					// JSON has one number type, so 1.7 sails through the check above
					// and is then truncated to 1 by the cast below — the author asks
					// for one thing and the tutorial hands out another, with nothing
					// logged. Every other malformation here is named; this one is too.
					if (Quantity != FMath::TruncToDouble(Quantity))
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d] ('%s'): 'qty' must be a whole number, not %g — a fraction would be silently truncated"),
							Index, ItemIndex, *ItemId, Quantity);
						return false;
					}
					// Checked here, at load, and not at loot time: the backend's
					// plausibility gate rejects an unknown id and would reject the
					// whole haul at the end of the raid instead.
					if (SarkoLoot::GetItemCatalog().Find(FName(*ItemId)) == nullptr)
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d]: '%s' is not in Data/Items/items.json"),
							Index, ItemIndex, *ItemId);
						return false;
					}
					Spot.FixedItems.Add(FSarkoItemStack{ FName(*ItemId), static_cast<int32>(Quantity) });
				}
			}

			OutDefinition.Containers.Add(Spot);
		}
	}

	// playerSpawns — at least one, or there is nowhere to put the player
	const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("playerSpawns"), Spawns, OutError))
	{
		return false;
	}
	if (Spawns)
	{
		for (int32 Index = 0; Index < Spawns->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Spawns)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: not an object"), Index);
				return false;
			}
			FVector Location;
			if (!ReadVector(*Object, TEXT("pos"), Location, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			const FRotator Rotation(0.f, static_cast<float>(Yaw), 0.f);
			OutDefinition.PlayerSpawns.Add(FTransform(Rotation, Location));
		}
	}
	if (OutDefinition.PlayerSpawns.Num() == 0)
	{
		OutError = TEXT("'playerSpawns' must contain at least one entry");
		return false;
	}

	// botSpawns
	const TArray<TSharedPtr<FJsonValue>>* Bots = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("botSpawns"), Bots, OutError))
	{
		return false;
	}
	if (Bots)
	{
		for (int32 Index = 0; Index < Bots->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Bots)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: not an object"), Index);
				return false;
			}
			FSarkoBotSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			// Same reasoning as a container's tier: nothing downstream reads
			// Zone yet, so absence defaults to NAME_None; a wrong-typed value
			// is still a named error.
			FString Zone;
			if (!ReadOptionalString(*Object, TEXT("zone"), Zone, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			Spot.Zone = FName(*Zone);
			OutDefinition.BotSpawns.Add(Spot);
		}
	}

	// extractions
	const TArray<TSharedPtr<FJsonValue>>* Extractions = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("extractions"), Extractions, OutError))
	{
		return false;
	}
	if (Extractions)
	{
		for (int32 Index = 0; Index < Extractions->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Extractions)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("extractions[%d]: not an object"), Index);
				return false;
			}
			FSarkoExtractionSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			// Absence keeps the struct's own default (400 uu); presence must
			// parse and clear a sane minimum, or the zone could be too small
			// to ever register an overlap.
			double RadiusUU = static_cast<double>(Spot.RadiusUU);
			if (!ReadOptionalNumber(*Object, TEXT("radiusUU"), RadiusUU, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			if (RadiusUU < MinExtractionRadiusUU)
			{
				OutError = FString::Printf(TEXT("extractions[%d]: 'radiusUU' must be at least %.0f, found %.1f"),
					Index, MinExtractionRadiusUU, RadiusUU);
				return false;
			}
			Spot.RadiusUU = static_cast<float>(RadiusUU);
			if (!ReadOptionalString(*Object, TEXT("name"), Spot.Name, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			OutDefinition.Extractions.Add(Spot);
		}
	}

	return true;
}

FSarkoMapLayout SarkoMap::ToLayout(const FSarkoMapDefinition& Definition)
{
	FSarkoMapLayout Layout;
	Layout.Extent = Definition.ExtentUU;
	Layout.Cover = Definition.Blocks;

	Layout.PlayerStarts.Reserve(Definition.PlayerSpawns.Num());
	for (const FTransform& Spawn : Definition.PlayerSpawns)
	{
		Layout.PlayerStarts.Add(Spawn.GetLocation());
	}

	Layout.EnemySpawns.Reserve(Definition.BotSpawns.Num());
	for (const FSarkoBotSpot& Spot : Definition.BotSpawns)
	{
		Layout.EnemySpawns.Add(Spot.Location);
	}

	return Layout;
}

bool SarkoMap::LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Maps") / (MapId + TEXT(".json"));

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read map file at %s"), *Path);
		return false;
	}
	if (!ParseDefinition(Json, OutDefinition, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}
