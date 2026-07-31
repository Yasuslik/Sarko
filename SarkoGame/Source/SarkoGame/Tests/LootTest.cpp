#include "Misc/AutomationTest.h"

#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoLootTable.h"
#include "Map/SarkoMapDefinition.h"

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

namespace
{
	/** A catalog the table fixtures below can be validated against. */
	FSarkoItemCatalog FixtureCatalog()
	{
		FSarkoItemCatalog Catalog;
		FString Error;
		SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error);
		return Catalog;
	}

	const FString GoodTablesJson = TEXT(R"({
		"tiers": {
			"junk":     { "rolls": {"min":1,"max":2}, "emptyChance": 0.15, "entries": [ {"item":"scrap_metal","weight":70,"qty":{"min":1,"max":3}}, {"item":"medkit","weight":30,"qty":{"min":1,"max":1}} ] },
			"common":   { "rolls": {"min":1,"max":2}, "emptyChance": 0.08, "entries": [ {"item":"scrap_metal","weight":60,"qty":{"min":1,"max":4}}, {"item":"ammo_9mm","weight":40,"qty":{"min":8,"max":16}} ] },
			"med":      { "rolls": {"min":1,"max":3}, "emptyChance": 0.10, "entries": [ {"item":"medkit","weight":100,"qty":{"min":1,"max":2}} ] },
			"good":     { "rolls": {"min":2,"max":3}, "emptyChance": 0.0,  "entries": [ {"item":"ammo_9mm","weight":50,"qty":{"min":10,"max":24}}, {"item":"chain","weight":2,"qty":{"min":1,"max":1}} ] },
			"military": { "rolls": {"min":2,"max":4}, "emptyChance": 0.0,  "entries": [ {"item":"pistol","weight":20,"qty":{"min":1,"max":1}}, {"item":"ammo_9mm","weight":80,"qty":{"min":16,"max":32}} ] }
		}
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootTablesParse,
	"Sarko.Loot.TablesParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootTablesParse::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	TestTrue(TEXT("well-formed tables parse"), SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("all five tiers are present"), Tables.Tables.Num(), 5);

	const FSarkoLootTable* Military = Tables.Find(TEXT("military"));
	if (!Military)
	{
		AddError(TEXT("the military tier did not resolve"));
		return false;
	}
	TestEqual(TEXT("rolls.min is read"), Military->MinRolls, 2);
	TestEqual(TEXT("rolls.max is read"), Military->MaxRolls, 4);
	TestEqual(TEXT("emptyChance is read"), Military->EmptyChance, 0.f);
	TestEqual(TEXT("both entries are read"), Military->Entries.Num(), 2);
	TestEqual(TEXT("entry weight is read"), Military->Entries[0].Weight, 20.f);
	TestEqual(TEXT("qty.min is read"), Military->Entries[1].MinQuantity, 16);
	TestEqual(TEXT("qty.max is read"), Military->Entries[1].MaxQuantity, 32);

	TestNull(TEXT("an unknown tier resolves to nothing"), Tables.Find(TEXT("legendary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootTablesRejectBadInput,
	"Sarko.Loot.TablesRejectBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootTablesRejectBadInput::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();

	// Spec §4.1: "Unknown item id in any loot table = load error, not a silent
	// skip." A skipped entry is a container that quietly yields less than the
	// designer wrote, which no test and no playthrough would ever localise.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"), TEXT("{{{") },
		{ TEXT("no tiers object"), TEXT(R"({"stuff":{}})") },
		{ TEXT("a tier is missing"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("unknown item id"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"plutonium","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("no entries"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[]}}})") },
		{ TEXT("zero weight"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":0,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("qty max below min"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":3,"max":1}}]}}})") },
		{ TEXT("rolls max below min"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":3,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("emptyChance above 1"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":1.5,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoLootTables Tables;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoLoot::ParseLootTables(Case.Value, Catalog, Tables, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootRollIsDeterministicPerContainer,
	"Sarko.Loot.RollIsDeterministicPerContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootRollIsDeterministicPerContainer::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	if (!SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("fixture tables failed to parse: %s"), *Error));
		return false;
	}
	const FSarkoLootTable& Military = *Tables.Find(TEXT("military"));

	// A fixed salt, because determinism is only claimed for a fixed one: the real
	// salt is generated per raid on the authority (ASarkoRaidGameMode::LootSalt) and
	// never leaves it, which is what stops a client precomputing the loot map from
	// the replicated seed and the tables shipped in its own build. 64 bits, and both
	// halves non-zero, so a mix that quietly dropped one half would not pass as
	// working here.
	constexpr int64 Salt = 0x5A17C0DE0BADF00Dll;

	// Same raid seed, same container index and same salt must give the same
	// contents, forever: the whole reason the seed is replicated is that the server
	// can re-derive a roll without storing it, and a raid that re-rolls on retry is
	// a duplication bug.
	FRandomStream A(SarkoLoot::ContainerSeed(12345, 7, Salt));
	FRandomStream B(SarkoLoot::ContainerSeed(12345, 7, Salt));
	const TArray<FSarkoItemStack> First = SarkoLoot::RollContainer(Military, A);
	const TArray<FSarkoItemStack> Second = SarkoLoot::RollContainer(Military, B);

	TestEqual(TEXT("the same seed and index give the same number of stacks"), First.Num(), Second.Num());
	for (int32 i = 0; i < FMath::Min(First.Num(), Second.Num()); ++i)
	{
		TestEqual(TEXT("the same item"), First[i].Item, Second[i].Item);
		TestEqual(TEXT("the same quantity"), First[i].Quantity, Second[i].Quantity);
	}

	// The salt has to actually participate, or it is decoration and the loot map is
	// still free: the same raid and container under a different salt must land on a
	// different stream. Checked across many indices rather than one, because any
	// single pair can collide by chance.
	int32 SaltChanged = 0;
	int32 HighHalfChanged = 0;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		if (SarkoLoot::ContainerSeed(12345, Index, Salt) != SarkoLoot::ContainerSeed(12345, Index, Salt + 1))
		{
			++SaltChanged;
		}
		// And the *high* half has to participate too, separately: the salt is 64
		// bits precisely so one observed roll cannot pin it, and a mix that only
		// folded in the low word would be a 32-bit salt wearing a wider type.
		if (SarkoLoot::ContainerSeed(12345, Index, Salt) !=
			SarkoLoot::ContainerSeed(12345, Index, Salt ^ (1ll << 40)))
		{
			++HighHalfChanged;
		}
	}
	TestEqual(TEXT("changing the salt changes every container's stream seed"), SaltChanged, 64);
	TestEqual(TEXT("the salt's high 32 bits are not ignored"), HighHalfChanged, 64);

	// And the unsalted relationship must be gone: with a salt of zero the old
	// `Seed ^ Index` would still be recoverable, so this pins that the mix is not
	// a bare XOR a client could invert.
	TestNotEqual(TEXT("the stream seed is not the plain seed-xor-index a client could compute"),
		SarkoLoot::ContainerSeed(12345, 7, 0), 12345 ^ 7);

	// Different containers in the same raid must not all hold the same thing.
	int32 Distinct = 0;
	for (int32 Index = 0; Index < 42; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(12345, Index, Salt));
		const TArray<FSarkoItemStack> Roll = SarkoLoot::RollContainer(Military, Stream);
		if (Roll.Num() != First.Num() || (Roll.Num() > 0 && Roll[0].Quantity != First[0].Quantity))
		{
			++Distinct;
		}
	}
	TestTrue(TEXT("42 containers do not all roll identically"), Distinct > 10);

	// ContainerSeed must not overflow-trap on the largest seeds the backend
	// sends: StartRaid returns int64(rand.Uint32()), so about half of all real
	// seeds arrive with the sign bit set. The salt is unsigned-hostile in the same
	// way, so it gets the extreme too.
	FRandomStream Wrapped(SarkoLoot::ContainerSeed(MIN_int32, 41, MIN_int64));
	TestTrue(TEXT("a negative seed and salt still produce a usable stream"),
		SarkoLoot::RollContainer(Military, Wrapped).Num() >= Military.MinRolls);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRollObeysTheTableBounds,
	"Sarko.Loot.RollObeysTheTableBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRollObeysTheTableBounds::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	if (!SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("fixture tables failed to parse: %s"), *Error));
		return false;
	}

	// Over a thousand rolls: a stack outside the table's declared range, an item
	// not in the table, or a good/military container coming up empty are all
	// silent-in-play, obvious-in-aggregate faults.
	const FSarkoLootTable& Good = *Tables.Find(TEXT("good"));
	int32 Empties = 0;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(999, Index, /*LootSalt*/ 0x1234));
		const TArray<FSarkoItemStack> Roll = SarkoLoot::RollContainer(Good, Stream);
		if (Roll.Num() == 0)
		{
			++Empties;
			continue;
		}
		TestTrue(TEXT("roll count is within rolls.min/max"),
			Roll.Num() >= Good.MinRolls && Roll.Num() <= Good.MaxRolls);
		for (const FSarkoItemStack& Stack : Roll)
		{
			const FSarkoLootEntry* Entry = Good.Entries.FindByPredicate(
				[&Stack](const FSarkoLootEntry& E) { return E.Item == Stack.Item; });
			TestNotNull(TEXT("every rolled item is in the table"), Entry);
			if (Entry)
			{
				TestTrue(TEXT("quantity is within qty.min/max"),
					Stack.Quantity >= Entry->MinQuantity && Stack.Quantity <= Entry->MaxQuantity);
			}
		}
	}
	TestEqual(TEXT("a good container is never empty (ТЗ §30)"), Empties, 0);

	// A junk container's empty rate must sit near its declared chance, not
	// wildly off: an emptyChance that does nothing is a table nobody tuned.
	const FSarkoLootTable& Junk = *Tables.Find(TEXT("junk"));
	int32 JunkEmpties = 0;
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(7, Index, /*LootSalt*/ 0x1234));
		if (SarkoLoot::RollContainer(Junk, Stream).Num() == 0)
		{
			++JunkEmpties;
		}
	}
	const float Rate = static_cast<float>(JunkEmpties) / 2000.f;
	TestTrue(FString::Printf(TEXT("junk empties at roughly 15%% (got %.1f%%)"), Rate * 100.f),
		Rate > 0.10f && Rate < 0.21f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRealLootTablesObeyTheDesignRules,
	"Sarko.Loot.RealLootTablesObeyTheDesignRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRealLootTablesObeyTheDesignRules::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error))
	{
		AddError(FString::Printf(TEXT("items.json failed to load: %s"), *Error));
		return false;
	}
	FSarkoLootTables Tables;
	if (!SarkoLoot::LoadLootTablesFromDisk(Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("loot-tables.json failed to load: %s"), *Error));
		return false;
	}

	// ТЗ §30, verbatim as rules.
	const auto EmptyCap = [](FName Tier) -> float
	{
		if (Tier == TEXT("junk"))     { return 0.15f; }
		if (Tier == TEXT("common"))   { return 0.08f; }
		if (Tier == TEXT("med"))      { return 0.15f; }
		return 0.f; // good and military are never empty
	};

	for (const FSarkoLootTable& Table : Tables.Tables)
	{
		TestTrue(FString::Printf(TEXT("'%s' emptyChance is within its cap"), *Table.Tier.ToString()),
			Table.EmptyChance <= EmptyCap(Table.Tier) + KINDA_SMALL_NUMBER);

		float TotalWeight = 0.f;
		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			TotalWeight += Entry.Weight;
		}
		TestTrue(FString::Printf(TEXT("'%s' has positive total weight"), *Table.Tier.ToString()), TotalWeight > 0.f);

		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			const FSarkoItemDef* Def = Catalog.Find(Entry.Item);
			TestNotNull(*FString::Printf(TEXT("'%s' entry '%s' is a catalog item"),
				*Table.Tier.ToString(), *Entry.Item.ToString()), Def);
			if (!Def)
			{
				continue;
			}

			// The med tier is medicine, not a shortcut to a weapon or a bicycle.
			if (Table.Tier == TEXT("med"))
			{
				TestTrue(*FString::Printf(TEXT("med tier does not yield '%s' (a %s)"),
					*Entry.Item.ToString(), *Entry.Item.ToString()),
					Def->Category != ESarkoItemCategory::Weapon && Def->Category != ESarkoItemCategory::VehiclePart);
			}

			// "Bicycle parts appear only as rare singles" — otherwise the first
			// sector completes a vehicle tier in one or two raids and the whole
			// garage progression is over before it starts.
			if (Def->Category == ESarkoItemCategory::VehiclePart)
			{
				TestEqual(*FString::Printf(TEXT("vehicle part '%s' drops as a single"), *Entry.Item.ToString()),
					Entry.MaxQuantity, 1);
				TestTrue(*FString::Printf(TEXT("vehicle part '%s' is rare in '%s' (%.1f%% of weight)"),
					*Entry.Item.ToString(), *Table.Tier.ToString(), 100.f * Entry.Weight / TotalWeight),
					Entry.Weight / TotalWeight <= 0.03f);
			}
		}
	}

	// Every tier the shipped map actually uses must have a table, or those
	// containers open to nothing at all.
	FSarkoMapDefinition Map;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestNotNull(*FString::Printf(TEXT("bridge.json tier '%s' has a loot table"), *Spot.Tier.ToString()),
			Tables.Find(Spot.Tier));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackpackStacksAndOverflows,
	"Sarko.Loot.BackpackStacksAndOverflows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackpackStacksAndOverflows::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog(); // pistol/1, ammo_9mm/60, medkit/3, scrap_metal/10, chain/1
	TArray<FSarkoItemStack> Slots;

	// Stacking: 25 rounds of ammo (stackSize 60) is one slot, not 25.
	TestEqual(TEXT("25 ammo all fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 25), 0);
	TestEqual(TEXT("and occupies one slot"), Slots.Num(), 1);
	TestEqual(TEXT("with the right quantity"), Slots[0].Quantity, 25);

	// Topping up the same stack does not open a second slot.
	TestEqual(TEXT("30 more ammo fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 30), 0);
	TestEqual(TEXT("still one slot"), Slots.Num(), 1);
	TestEqual(TEXT("55 rounds"), Slots[0].Quantity, 55);

	// Past the stack size, a second slot opens and carries the remainder.
	TestEqual(TEXT("20 more ammo fits, spilling into a new stack"),
		SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 20), 0);
	TestEqual(TEXT("two slots now"), Slots.Num(), 2);
	TestEqual(TEXT("the first is full"), Slots[0].Quantity, 60);
	TestEqual(TEXT("the second holds the rest"), Slots[1].Quantity, 15);

	// A non-stacking item takes one whole slot each.
	TArray<FSarkoItemStack> Pistols;
	TestEqual(TEXT("three pistols fit"), SarkoLoot::AddToBackpack(Pistols, Catalog, 12, TEXT("pistol"), 3), 0);
	TestEqual(TEXT("in three slots (stackSize 1)"), Pistols.Num(), 3);

	// Overflow: what does not fit is reported, and nothing is invented.
	TArray<FSarkoItemStack> Small;
	const int32 Leftover = SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), 5);
	TestEqual(TEXT("only two pistols fit in two slots"), Small.Num(), 2);
	TestEqual(TEXT("three are reported as leftover"), Leftover, 3);

	// Spec §4.3: overflow stays in the container, so the caller must be able to
	// tell exactly how much it kept. Silently dropping the remainder would look
	// identical to a full transfer and lose loot without a word.
	int32 Total = 0;
	for (const FSarkoItemStack& Stack : Small)
	{
		Total += Stack.Quantity;
	}
	TestEqual(TEXT("what fit plus what did not equals what was offered"), Total + Leftover, 5);

	// A quantity that is zero or negative changes nothing, and an unknown item
	// is refused whole rather than added with a guessed stack size.
	TestEqual(TEXT("zero quantity is a no-op"), SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), 0), 0);
	TestEqual(TEXT("negative quantity is a no-op"), SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), -4), 0);
	TestEqual(TEXT("an unknown item is refused entirely"),
		SarkoLoot::AddToBackpack(Small, Catalog, 12, TEXT("plutonium"), 3), 3);
	TestEqual(TEXT("and did not touch the slots"), Small.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackpackFillsPartialStacksBeforeOpeningSlots,
	"Sarko.Loot.BackpackFillsPartialStacksBeforeOpeningSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackpackFillsPartialStacksBeforeOpeningSlots::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();

	// Two partial medkit stacks (stackSize 3) plus one more medkit must top up
	// an existing stack rather than open a third slot — otherwise a 12-slot
	// backpack fills up with half-empty stacks and the limit means nothing.
	TArray<FSarkoItemStack> Slots;
	Slots.Add(FSarkoItemStack{ TEXT("medkit"), 1 });
	Slots.Add(FSarkoItemStack{ TEXT("scrap_metal"), 2 });

	TestEqual(TEXT("one medkit fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("medkit"), 1), 0);
	TestEqual(TEXT("no new slot was opened"), Slots.Num(), 2);
	TestEqual(TEXT("the partial medkit stack grew"), Slots[0].Quantity, 2);

	// A full backpack of partial stacks still accepts a top-up: the limit is
	// slots, not items.
	TArray<FSarkoItemStack> Full;
	Full.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 10 });
	TestEqual(TEXT("a one-slot backpack still accepts more ammo"),
		SarkoLoot::AddToBackpack(Full, Catalog, 1, TEXT("ammo_9mm"), 40), 0);
	TestEqual(TEXT("still one slot"), Full.Num(), 1);
	TestEqual(TEXT("50 rounds"), Full[0].Quantity, 50);
	// But it refuses a different item, because that would need a second slot.
	TestEqual(TEXT("a one-slot backpack refuses a different item"),
		SarkoLoot::AddToBackpack(Full, Catalog, 1, TEXT("medkit"), 1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoContainersMayCarryFixedItems,
	"Sarko.Loot.ContainersMayCarryFixedItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoContainersMayCarryFixedItems::RunTest(const FString& Parameters)
{
	// The tutorial's static layout (spec §6.5) is authored in the map file, one
	// optional list per container. Stage C writes the real one; this pins the
	// schema so it cannot drift under it.
	const FString Json = TEXT(R"({
		"id": "t",
		"extentUU": 1000,
		"raidDurationSeconds": 600,
		"playerSpawns": [{ "pos": [0, 0, 100] }],
		"containers": [
			{ "pos": [100, 0, 0], "tier": "junk",
			  "fixedItems": [{ "item": "scrap_metal", "qty": 3 }, { "item": "duct_tape", "qty": 1 }] },
			{ "pos": [200, 0, 0], "tier": "common" }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("fixedItems parses"), SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("both containers are read"), Definition.Containers.Num(), 2);

	TestEqual(TEXT("the authored list is read in order"), Definition.Containers[0].FixedItems.Num(), 2);
	TestEqual(TEXT("the first fixed item's id survives"),
		Definition.Containers[0].FixedItems[0].Item, FName(TEXT("scrap_metal")));
	TestEqual(TEXT("the first fixed item's quantity survives"),
		Definition.Containers[0].FixedItems[0].Quantity, 3);

	// Absent is the normal case and means "roll this one" even in tutorial mode.
	TestEqual(TEXT("a container with no fixedItems has an empty list"),
		Definition.Containers[1].FixedItems.Num(), 0);

	// Every failure mode is a named error, never a silently shortened list: a
	// dropped entry is a teaching beat that quietly stops happening, and the
	// symptom is "the tutorial feels thin", which nobody can trace to a data file.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not an array"),        TEXT(R"("fixedItems": 7)") },
		{ TEXT("entry not an object"), TEXT(R"("fixedItems": ["scrap_metal"])") },
		{ TEXT("no item id"),          TEXT(R"("fixedItems": [{ "qty": 2 }])") },
		{ TEXT("empty item id"),       TEXT(R"("fixedItems": [{ "item": "", "qty": 2 }])") },
		{ TEXT("qty missing"),         TEXT(R"("fixedItems": [{ "item": "scrap_metal" }])") },
		{ TEXT("qty zero"),            TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": 0 }])") },
		{ TEXT("qty negative"),        TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": -1 }])") },
		// An id the catalog does not know would be rejected by the backend's
		// domain.ValidateRaidItems at result time — fifteen minutes into a raid,
		// having already been shown to the player. Caught at load instead.
		{ TEXT("unknown item id"),     TEXT(R"("fixedItems": [{ "item": "unobtanium", "qty": 1 }])") },
		// An empty list is the one malformation that looks like success. It parses
		// to the same empty FixedItems as an absent key, so RollContainerFor falls
		// through to a seeded roll: an author writing [] to mean "this crate is
		// empty during the tutorial" gets random loot instead. It also costs
		// Stage C its acceptance signal, since SetTutorialLoot counts containers
		// with Num() > 0 and would keep warning that nothing is authored.
		{ TEXT("empty list"),          TEXT(R"("fixedItems": [])") },
		// JSON has a single number type, so 1.7 clears the "less than 1" check
		// above and is then truncated to 1 by the cast — the only malformation
		// here that used to change what the player receives without saying so.
		{ TEXT("fractional qty"),      TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": 1.7 }])") },
		// The silent half of the same defect. TJsonValueString overrides
		// TryGetNumber and runs the text through LexTryParseString, so a quoted
		// numeral used to parse as that number: the container really did hand out
		// three pieces of scrap and nothing anywhere said the file was malformed.
		// The row deliberately uses a *valid* quantity — "qty": "abc" was already
		// rejected because the text does not parse, which is why that case proves
		// nothing about the type check.
		{ TEXT("qty is a quoted numeral"), TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": "3" }])") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		const FString Bad = FString::Printf(TEXT(R"({
			"id": "t", "extentUU": 1000, "raidDurationSeconds": 600,
			"playerSpawns": [{ "pos": [0, 0, 100] }],
			"containers": [{ "pos": [100, 0, 0], "tier": "junk", %s }]
		})"), *Case.Value);

		FSarkoMapDefinition Rejected;
		FString BadError;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Bad, Rejected, BadError));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), BadError.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialLootIgnoresTheRandomStream,
	"Sarko.Loot.TutorialLootIgnoresTheRandomStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialLootIgnoresTheRandomStream::RunTest(const FString& Parameters)
{
	FSarkoLootContainerSpot Spot;
	Spot.Tier = SarkoLoot::TierJunk;
	Spot.FixedItems = {
		FSarkoItemStack{ FName(TEXT("scrap_metal")), 3 },
		FSarkoItemStack{ FName(TEXT("duct_tape")), 1 },
	};

	// A real table, so the test proves the fixed list *wins* rather than that
	// there was nothing to roll.
	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierJunk);
	if (!Table)
	{
		AddError(TEXT("the junk tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	// Two wildly different streams must produce byte-identical results: "static"
	// means the seed cannot reach it at all, which is what makes dying and
	// replaying the tutorial show the same layout (spec §6.5).
	FRandomStream StreamA(1);
	FRandomStream StreamB(0x7FFFFFFF);
	const TArray<FSarkoItemStack> FromA = SarkoLoot::RollContainerFor(Spot, *Table, StreamA, /*bTutorialLoot*/ true);
	const TArray<FSarkoItemStack> FromB = SarkoLoot::RollContainerFor(Spot, *Table, StreamB, /*bTutorialLoot*/ true);

	TestEqual(TEXT("the fixed list is returned whole"), FromA.Num(), 2);
	TestEqual(TEXT("two different streams give the same static loot"), FromA.Num(), FromB.Num());
	for (int32 Index = 0; Index < FromA.Num(); ++Index)
	{
		TestEqual(TEXT("same item at the same position"), FromA[Index].Item, FromB[Index].Item);
		TestEqual(TEXT("same quantity"), FromA[Index].Quantity, FromB[Index].Quantity);
	}
	TestEqual(TEXT("the authored order is preserved"), FromA[0].Item, FName(TEXT("scrap_metal")));
	TestEqual(TEXT("the authored quantity is preserved"), FromA[0].Quantity, 3);

	// Fixed lists must clear the backend's plausibility gate, which is the same
	// gate a rolled haul clears (spec §6.5: "fixed lists pass the same
	// plausibility gate"). The client-side half of that is the 12-slot backpack.
	TestTrue(TEXT("a fixed list fits a backpack"), FromA.Num() <= 12);
	for (const FSarkoItemStack& Stack : FromA)
	{
		TestNotNull(TEXT("every fixed item is in the catalog"),
			SarkoLoot::GetItemCatalog().Find(Stack.Item));
		TestTrue(TEXT("every fixed quantity is positive"), Stack.Quantity > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialModeFallsBackToRollingWhenNothingIsAuthored,
	"Sarko.Loot.TutorialModeFallsBackToRollingWhenNothingIsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialModeFallsBackToRollingWhenNothingIsAuthored::RunTest(const FString& Parameters)
{
	// The mechanism ships in Stage A.5; the authored layout is Stage C's. Between
	// the two, `bridge.json` carried no fixedItems at all. Stage C authored all
	// nineteen, so this is now the rule for a container that is added later and
	// forgotten, and for any future map: tutorial mode with nothing authored still
	// rolls normally — and the game mode logs one Warning per raid naming the count,
	// so the gap is visible rather than assumed. Stage C's acceptance bar was that
	// Warning stopping, and it has.
	FSarkoLootContainerSpot Unauthored;
	Unauthored.Tier = SarkoLoot::TierJunk;

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierJunk);
	if (!Table)
	{
		AddError(TEXT("the junk tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	FRandomStream Tutorial(4242);
	FRandomStream Normal(4242);
	const TArray<FSarkoItemStack> InTutorial =
		SarkoLoot::RollContainerFor(Unauthored, *Table, Tutorial, /*bTutorialLoot*/ true);
	const TArray<FSarkoItemStack> Rolled =
		SarkoLoot::RollContainerFor(Unauthored, *Table, Normal, /*bTutorialLoot*/ false);

	TestEqual(TEXT("an unauthored container rolls identically in either mode"), InTutorial.Num(), Rolled.Num());
	for (int32 Index = 0; Index < InTutorial.Num(); ++Index)
	{
		TestEqual(TEXT("same item"), InTutorial[Index].Item, Rolled[Index].Item);
		TestEqual(TEXT("same quantity"), InTutorial[Index].Quantity, Rolled[Index].Quantity);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNormalModeIgnoresAuthoredFixedItems,
	"Sarko.Loot.NormalModeIgnoresAuthoredFixedItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNormalModeIgnoresAuthoredFixedItems::RunTest(const FString& Parameters)
{
	// The other half of the branch, and the half that protects the economy: once
	// tutorial_completed is set, an authored fixedItems list must be dead data.
	// Otherwise the teaching layout — which contains a guaranteed military crate —
	// becomes a farmable route forever.
	FSarkoLootContainerSpot Spot;
	Spot.Tier = SarkoLoot::TierMilitary;
	Spot.FixedItems = { FSarkoItemStack{ FName(TEXT("bike_frame")), 1 } };

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierMilitary);
	if (!Table)
	{
		AddError(TEXT("the military tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	// Ten different streams, because a single roll could coincidentally match the
	// fixed list and pass a weaker version of this test.
	int32 MatchedTheFixedList = 0;
	for (int32 Seed = 1; Seed <= 10; ++Seed)
	{
		FRandomStream Stream(Seed * 7919);
		const TArray<FSarkoItemStack> Out = SarkoLoot::RollContainerFor(Spot, *Table, Stream, /*bTutorialLoot*/ false);
		FRandomStream Same(Seed * 7919);
		const TArray<FSarkoItemStack> Reference = SarkoLoot::RollContainer(*Table, Same);

		TestEqual(TEXT("normal mode is exactly RollContainer"), Out.Num(), Reference.Num());
		for (int32 Index = 0; Index < Out.Num(); ++Index)
		{
			TestEqual(TEXT("same item as an unbranched roll"), Out[Index].Item, Reference[Index].Item);
			TestEqual(TEXT("same quantity as an unbranched roll"), Out[Index].Quantity, Reference[Index].Quantity);
		}
		if (Out.Num() == 1 && Out[0].Item == FName(TEXT("bike_frame")) && Out[0].Quantity == 1)
		{
			++MatchedTheFixedList;
		}
	}
	TestTrue(TEXT("at least one of ten rolls differs from the fixed list, so the branch is really off"),
		MatchedTheFixedList < 10);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
