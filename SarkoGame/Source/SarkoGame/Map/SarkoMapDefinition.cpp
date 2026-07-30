#include "Map/SarkoMapDefinition.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Reads a ["x","y","z"] array into a vector, naming the field on failure. */
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
		Out = FVector(
			static_cast<float>((*Array)[0]->AsNumber()),
			static_cast<float>((*Array)[1]->AsNumber()),
			static_cast<float>((*Array)[2]->AsNumber()));
		return true;
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
	if (Root->TryGetArrayField(TEXT("blocks"), Blocks) && Blocks)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Blocks)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'blocks' contains a non-object entry");
				return false;
			}
			FSarkoCoverBlock Block;
			if (!ReadVector(*Object, TEXT("pos"), Block.Location, OutError) ||
				!ReadVector(*Object, TEXT("extent"), Block.Extent, OutError))
			{
				OutError = FString::Printf(TEXT("block: %s"), *OutError);
				return false;
			}
			Block.Rotation = FRotator(0.f, static_cast<float>((*Object)->GetNumberField(TEXT("yaw"))), 0.f);
			OutDefinition.Blocks.Add(Block);
		}
	}

	// props
	const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
	if (Root->TryGetArrayField(TEXT("props"), Props) && Props)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Props)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'props' contains a non-object entry");
				return false;
			}
			FSarkoMapProp Prop;
			if (!ReadVector(*Object, TEXT("pos"), Prop.Location, OutError))
			{
				OutError = FString::Printf(TEXT("prop: %s"), *OutError);
				return false;
			}
			Prop.Kind = FName(*(*Object)->GetStringField(TEXT("kind")));
			Prop.Yaw = static_cast<float>((*Object)->GetNumberField(TEXT("yaw")));
			OutDefinition.Props.Add(Prop);
		}
	}

	// containers
	const TArray<TSharedPtr<FJsonValue>>* Containers = nullptr;
	if (Root->TryGetArrayField(TEXT("containers"), Containers) && Containers)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Containers)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'containers' contains a non-object entry");
				return false;
			}
			FSarkoLootContainerSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("container: %s"), *OutError);
				return false;
			}
			Spot.Tier = FName(*(*Object)->GetStringField(TEXT("tier")));
			OutDefinition.Containers.Add(Spot);
		}
	}

	// playerSpawns — at least one, or there is nowhere to put the player
	const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
	if (Root->TryGetArrayField(TEXT("playerSpawns"), Spawns) && Spawns)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Spawns)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'playerSpawns' contains a non-object entry");
				return false;
			}
			FVector Location;
			if (!ReadVector(*Object, TEXT("pos"), Location, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawn: %s"), *OutError);
				return false;
			}
			const FRotator Rotation(0.f, static_cast<float>((*Object)->GetNumberField(TEXT("yaw"))), 0.f);
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
	if (Root->TryGetArrayField(TEXT("botSpawns"), Bots) && Bots)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Bots)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'botSpawns' contains a non-object entry");
				return false;
			}
			FSarkoBotSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawn: %s"), *OutError);
				return false;
			}
			Spot.Zone = FName(*(*Object)->GetStringField(TEXT("zone")));
			OutDefinition.BotSpawns.Add(Spot);
		}
	}

	// extractions
	const TArray<TSharedPtr<FJsonValue>>* Extractions = nullptr;
	if (Root->TryGetArrayField(TEXT("extractions"), Extractions) && Extractions)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Extractions)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'extractions' contains a non-object entry");
				return false;
			}
			FSarkoExtractionSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("extraction: %s"), *OutError);
				return false;
			}
			Spot.RadiusUU = static_cast<float>((*Object)->GetNumberField(TEXT("radiusUU")));
			(*Object)->TryGetStringField(TEXT("name"), Spot.Name);
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
