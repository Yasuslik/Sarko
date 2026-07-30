#include "Misc/AutomationTest.h"

#include "Loot/SarkoItemCatalog.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	const FString GoodCatalogJson = TEXT(R"({
		"items": [
			{ "id": "pistol",      "name": "Пістолет",     "stackSize": 1,  "category": "weapon" },
			{ "id": "ammo_9mm",    "name": "Патрони 9мм",   "stackSize": 60, "category": "ammo" },
			{ "id": "medkit",      "name": "Аптечка",       "stackSize": 3,  "category": "med" },
			{ "id": "scrap_metal", "name": "Металолом",     "stackSize": 10, "category": "junk" },
			{ "id": "chain",       "name": "Ланцюг",        "stackSize": 1,  "category": "vehicle_part" }
		]
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoItemCatalogParses,
	"Sarko.Loot.ItemCatalogParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoItemCatalogParses::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	TestTrue(TEXT("a well-formed catalog parses"), SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("all five items are read"), Catalog.Items.Num(), 5);

	const FSarkoItemDef* Ammo = Catalog.Find(TEXT("ammo_9mm"));
	if (!Ammo)
	{
		AddError(TEXT("ammo_9mm did not resolve, so every field check below is meaningless"));
		return false;
	}
	TestEqual(TEXT("the UA display name survives"), Ammo->Name, FString(TEXT("Патрони 9мм")));
	TestEqual(TEXT("stack size survives"), Ammo->StackSize, 60);
	TestTrue(TEXT("category is read, not defaulted"), Ammo->Category == ESarkoItemCategory::Ammo);

	TestTrue(TEXT("a vehicle part is categorised as one"),
		Catalog.Find(TEXT("chain")) && Catalog.Find(TEXT("chain"))->Category == ESarkoItemCategory::VehiclePart);
	TestNull(TEXT("an unknown id resolves to nothing, never to a default item"), Catalog.Find(TEXT("nonsense")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoItemCatalogRejectsBadInput,
	"Sarko.Loot.ItemCatalogRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoItemCatalogRejectsBadInput::RunTest(const FString& Parameters)
{
	// The catalog is the wire contract with the backend. A silently-accepted
	// broken entry means an item that exists on the client, does not exist in
	// the backend's known-items set, and makes /v1/raid/result reject the whole
	// haul at the end of a raid — the worst possible moment to find out.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"),           TEXT("{{{") },
		{ TEXT("no items array"),     TEXT(R"({"stuff":[]})") },
		{ TEXT("empty items array"),  TEXT(R"({"items":[]})") },
		{ TEXT("missing id"),         TEXT(R"({"items":[{"name":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("empty id"),           TEXT(R"({"items":[{"id":"","name":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("missing name"),       TEXT(R"({"items":[{"id":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("zero stack size"),    TEXT(R"({"items":[{"id":"x","name":"x","stackSize":0,"category":"junk"}]})") },
		{ TEXT("unknown category"),   TEXT(R"({"items":[{"id":"x","name":"x","stackSize":1,"category":"cheese"}]})") },
		{ TEXT("duplicate id"),       TEXT(R"({"items":[{"id":"x","name":"x","stackSize":1,"category":"junk"},{"id":"x","name":"y","stackSize":1,"category":"junk"}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoItemCatalog Catalog;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoLoot::ParseItemCatalog(Case.Value, Catalog, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRealItemCatalogIsUsable,
	"Sarko.Loot.RealItemCatalogIsUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRealItemCatalogIsUsable::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error))
	{
		AddError(FString::Printf(TEXT("Data/Items/items.json failed to load: %s"), *Error));
		return false;
	}

	// The starter kit the backend grants at registration must exist here, or the
	// shelter shows a player three items it cannot name.
	for (const FName Required : { FName(TEXT("pistol")), FName(TEXT("ammo_9mm")), FName(TEXT("medkit")) })
	{
		TestNotNull(*FString::Printf(TEXT("starter-kit item '%s' is in the catalog"), *Required.ToString()),
			Catalog.Find(Required));
	}

	// The bicycle recipe in sarko-api/internal/domain/garage.go is the only place
	// the backend names item ids of its own. Those three must be findable in a
	// raid or the first garage step is unreachable.
	for (const FName Part : { FName(TEXT("bike_frame")), FName(TEXT("wheel_small")), FName(TEXT("chain")) })
	{
		const FSarkoItemDef* Def = Catalog.Find(Part);
		TestNotNull(*FString::Printf(TEXT("bicycle part '%s' is in the catalog"), *Part.ToString()), Def);
		if (Def)
		{
			TestTrue(*FString::Printf(TEXT("'%s' is categorised as a vehicle part"), *Part.ToString()),
				Def->Category == ESarkoItemCategory::VehiclePart);
		}
	}

	// item_id is capped at 64 characters by domain.ValidateStacks; anything
	// longer is rejected by the backend at result time.
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		TestTrue(*FString::Printf(TEXT("'%s' fits the backend's 64-char item_id cap"), *Def.Id.ToString()),
			Def.Id.ToString().Len() <= 64);
		TestFalse(*FString::Printf(TEXT("'%s' has a display name"), *Def.Id.ToString()), Def.Name.IsEmpty());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
