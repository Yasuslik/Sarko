#include "Loot/SarkoItemCatalog.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const FSarkoItemDef* FSarkoItemCatalog::Find(FName Id) const
{
	return Items.FindByPredicate([Id](const FSarkoItemDef& Def) { return Def.Id == Id; });
}

namespace
{
	/** Category names as they appear in items.json. Unknown is an error, not a default. */
	bool ParseCategory(const FString& Text, ESarkoItemCategory& Out)
	{
		static const TMap<FString, ESarkoItemCategory> Names = {
			{ TEXT("weapon"),       ESarkoItemCategory::Weapon },
			{ TEXT("ammo"),         ESarkoItemCategory::Ammo },
			{ TEXT("med"),          ESarkoItemCategory::Med },
			{ TEXT("junk"),         ESarkoItemCategory::Junk },
			{ TEXT("valuable"),     ESarkoItemCategory::Valuable },
			{ TEXT("vehicle_part"), ESarkoItemCategory::VehiclePart },
			{ TEXT("gear"),         ESarkoItemCategory::Gear },
			{ TEXT("consumable"),   ESarkoItemCategory::Consumable },
		};
		if (const ESarkoItemCategory* Found = Names.Find(Text))
		{
			Out = *Found;
			return true;
		}
		return false;
	}
}

bool SarkoLoot::ParseItemCatalog(const FString& Json, FSarkoItemCatalog& OutCatalog, FString& OutError)
{
	OutCatalog = FSarkoItemCatalog();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		OutError = TEXT("'items' is missing or not an array");
		return false;
	}
	if (Items->Num() == 0)
	{
		OutError = TEXT("'items' is empty — an empty catalog makes every loot table invalid");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object) || !Object)
		{
			OutError = TEXT("'items' contains a non-object entry");
			return false;
		}

		FSarkoItemDef Def;

		FString Id;
		if (!(*Object)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			OutError = TEXT("an item has no 'id'");
			return false;
		}
		Def.Id = FName(*Id);

		if (OutCatalog.Find(Def.Id) != nullptr)
		{
			OutError = FString::Printf(TEXT("item '%s' is defined twice"), *Id);
			return false;
		}

		if (!(*Object)->TryGetStringField(TEXT("name"), Def.Name) || Def.Name.IsEmpty())
		{
			OutError = FString::Printf(TEXT("item '%s' has no 'name'"), *Id);
			return false;
		}

		double StackSize = 0.0;
		if (!(*Object)->TryGetNumberField(TEXT("stackSize"), StackSize) || StackSize < 1.0)
		{
			OutError = FString::Printf(TEXT("item '%s': 'stackSize' is missing or less than 1"), *Id);
			return false;
		}
		Def.StackSize = static_cast<int32>(StackSize);

		// Required, never defaulted: an omitted size is a 1x1 item nobody meant to
		// author, and a 1x1 rifle is the "backpacks matter" rule quietly gone.
		const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
		if (!(*Object)->TryGetArrayField(TEXT("size"), Size) || !Size || Size->Num() != 2)
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'size' must be [width, height] in whole cells"), *Id);
			return false;
		}
		Def.Width = static_cast<int32>((*Size)[0]->AsNumber());
		Def.Height = static_cast<int32>((*Size)[1]->AsNumber());
		if (Def.Width < 1 || Def.Height < 1)
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'size' is %dx%d; both must be at least 1"), *Id, Def.Width, Def.Height);
			return false;
		}

		FString CategoryText;
		if (!(*Object)->TryGetStringField(TEXT("category"), CategoryText) || !ParseCategory(CategoryText, Def.Category))
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'category' must be weapon, ammo, med, junk, valuable, vehicle_part, gear or consumable"), *Id);
			return false;
		}

		OutCatalog.Items.Add(Def);
	}

	return true;
}

bool SarkoLoot::LoadItemCatalogFromDisk(FSarkoItemCatalog& OutCatalog, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Items") / TEXT("items.json");

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read the item catalog at %s"), *Path);
		return false;
	}
	if (!ParseItemCatalog(Json, OutCatalog, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}

const FSarkoItemCatalog& SarkoLoot::GetItemCatalog()
{
	// Loaded once per process. The catalog never changes at runtime, and every
	// loot roll and every HUD draw would otherwise re-read a file from disk.
	static FSarkoItemCatalog Catalog;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Error;
		if (!LoadItemCatalogFromDisk(Catalog, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoLoot: %s — no item will resolve and no container will yield loot"), *Error);
			Catalog = FSarkoItemCatalog();
		}
	}
	return Catalog;
}
