#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Loot/SarkoItemCatalog.h"
#include "Pawn/SarkoSurvival.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMetersDrainAndClamp,
	"Sarko.Survival.MetersDrainAndClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMetersDrainAndClamp::RunTest(const FString& Parameters)
{
	// The arithmetic the whole feature rests on, in the units it is authored in:
	// a rate per MINUTE against a delta in SECONDS. Getting that conversion wrong
	// is a mechanic that either never moves or empties in fifteen seconds, and
	// neither reads as a bug until someone plays a full raid.
	TestEqual(TEXT("2.5/min for a minute is 2.5"),
		SarkoSurvival::DrainMeter(100.f, 2.5f, 60.f), 97.5f, 0.001f);
	TestEqual(TEXT("3.3/min for fifteen minutes is 49.5"),
		SarkoSurvival::DrainMeter(100.f, 3.3f, 900.f), 50.5f, 0.001f);

	// Never lethal, so zero is a floor and not a death.
	TestEqual(TEXT("a meter stops at zero, however long the raid runs"),
		SarkoSurvival::DrainMeter(2.f, 3.3f, 3600.f), 0.f, 0.001f);
	TestEqual(TEXT("a zero delta changes nothing"),
		SarkoSurvival::DrainMeter(45.f, 3.3f, 0.f), 45.f, 0.001f);

	// And nothing may push a meter past full: a second bottle is only worth what
	// there is room for.
	TestEqual(TEXT("drinking past full stops at full"),
		SarkoSurvival::ApplyToMeter(80.f, 50.f), 100.f, 0.001f);
	TestEqual(TEXT("a cost cannot push a meter below zero"),
		SarkoSurvival::ApplyToMeter(5.f, -15.f), 0.f, 0.001f);
	TestEqual(TEXT("a plain restore is a plain restore"),
		SarkoSurvival::ApplyToMeter(45.f, 50.f), 95.f, 0.001f);

	// THE SHIPPED START, which is the number the whole design turns on. Starting
	// full would mean neither meter ever crosses 30 % inside a raid and the
	// mechanic never reads at all.
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);
	if (Settings)
	{
		const float RaidMinutes = 15.f;
		const float WaterAtEnd = SarkoSurvival::DrainMeter(
			Settings->WaterStartPercent, Settings->WaterDrainPerMinute, RaidMinutes * 60.f);
		const float FoodAtEnd = SarkoSurvival::DrainMeter(
			Settings->FoodStartPercent, Settings->FoodDrainPerMinute, RaidMinutes * 60.f);
		TestTrue(TEXT("thirst crosses the penalty threshold inside a 15-minute raid"),
			WaterAtEnd <= Settings->SurvivalLowPercent);
		TestTrue(TEXT("hunger crosses it too"),
			FoodAtEnd <= Settings->SurvivalLowPercent);
		// Hunger never empties, thirst does — which is the asymmetry the route is
		// built around. A player who never drinks reaches zero water at about
		// thirteen and a half minutes and is left with one low meter, not two, so
		// regeneration is halved rather than stopped and the raid is still
		// winnable. Reaching zero costs nothing further: neither meter is lethal.
		TestTrue(TEXT("hunger never empties inside a raid"), FoodAtEnd > 0.f);
		TestTrue(TEXT("thirst does, which is why the route hands the player a bottle"),
			WaterAtEnd <= 0.f);
		// A raid begins out of trouble on both meters, or the penalty is the
		// starting state rather than something the player walks into.
		TestTrue(TEXT("both meters start above the threshold"),
			Settings->FoodStartPercent > Settings->SurvivalLowPercent
				&& Settings->WaterStartPercent > Settings->SurvivalLowPercent);
		TestTrue(TEXT("thirst is the faster meter, so water is the one the player learns first"),
			Settings->WaterDrainPerMinute > Settings->FoodDrainPerMinute);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRegenIsGatedByCombatAndByTheMeters,
	"Sarko.Survival.RegenIsGatedByCombatAndByTheMeters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRegenIsGatedByCombatAndByTheMeters::RunTest(const FString& Parameters)
{
	// Out-of-combat regeneration is introduced with hunger and thirst because it
	// is the thing they gate — so these two rules are one rule and are tested as
	// one. Base 1.5 hp/s, 8 s out of combat, 30 % threshold: the shipped values,
	// written as literals so a config change has to come past this test.
	const float Base = 1.5f;
	const float Delay = 8.f;
	const float Low = 30.f;

	TestEqual(TEXT("full meters, well out of combat: the full rate"),
		SarkoSurvival::RegenPerSecond(Base, 20.f, Delay, 55.f, 45.f, Low), 1.5f, 0.0001f);

	// COMBAT WINS OVER EVERYTHING. The medkit is the in-combat answer and this
	// must never compete with it.
	TestEqual(TEXT("one second after being shot: nothing"),
		SarkoSurvival::RegenPerSecond(Base, 1.f, Delay, 100.f, 100.f, Low), 0.f, 0.0001f);
	TestEqual(TEXT("the delay boundary belongs to the calm side"),
		SarkoSurvival::RegenPerSecond(Base, 8.f, Delay, 100.f, 100.f, Low), 1.5f, 0.0001f);
	TestEqual(TEXT("...and a hair before it does not"),
		SarkoSurvival::RegenPerSecond(Base, 7.99f, Delay, 100.f, 100.f, Low), 0.f, 0.0001f);

	// THE PENALTY, which is the only thing hunger and thirst do.
	TestEqual(TEXT("thirst at the threshold halves it — the boundary is inclusive"),
		SarkoSurvival::RegenPerSecond(Base, 20.f, Delay, 55.f, 30.f, Low), 0.75f, 0.0001f);
	TestEqual(TEXT("hunger low halves it just the same"),
		SarkoSurvival::RegenPerSecond(Base, 20.f, Delay, 12.f, 80.f, Low), 0.75f, 0.0001f);
	TestEqual(TEXT("both low stops it entirely — ignoring both meters costs the whole mechanic"),
		SarkoSurvival::RegenPerSecond(Base, 20.f, Delay, 10.f, 5.f, Low), 0.f, 0.0001f);
	TestEqual(TEXT("a hair above the threshold is not low"),
		SarkoSurvival::RegenPerSecond(Base, 20.f, Delay, 30.01f, 30.01f, Low), 1.5f, 0.0001f);

	// A regen turned off in config is off, not negative.
	TestEqual(TEXT("a zero base rate regenerates nothing"),
		SarkoSurvival::RegenPerSecond(0.f, 20.f, Delay, 100.f, 100.f, Low), 0.f, 0.0001f);

	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	if (Settings)
	{
		// It must be far too slow to be felt in a fight, or it becomes a second
		// medkit that costs nothing. A scav deals WeaponDamage every
		// EnemyFireIntervalSeconds; regen must be an order of magnitude under it.
		const float EnemyDps = Settings->WeaponDamage / FMath::Max(0.1f, Settings->EnemyFireIntervalSeconds);
		TestTrue(FString::Printf(
				TEXT("regen (%.2f hp/s) is at least ten times slower than incoming fire (%.1f hp/s)"),
				Settings->HealthRegenPerSecond, EnemyDps),
			Settings->HealthRegenPerSecond * 10.f < EnemyDps);
		// And it must cost real time, or the wound was never a wound. A hundred
		// health at 1.5 hp/s is 66 seconds — about the walk home.
		const float SecondsToFull = 100.f / FMath::Max(0.01f, Settings->HealthRegenPerSecond);
		TestTrue(FString::Printf(TEXT("a full recovery takes %.0f s, which is a real fraction of a raid"),
				SecondsToFull),
			SecondsToFull >= 30.f && SecondsToFull <= 180.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoConsumablesAreATableAndACategory,
	"Sarko.Survival.ConsumablesAreATableAndACategory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoConsumablesAreATableAndACategory::RunTest(const FString& Parameters)
{
	// The category says "this can be used"; the id says what using it DOES. Both
	// halves are required, and this is the test that keeps them from drifting —
	// a consumable added to items.json without an effect row here is refused by
	// the server and never given a button by the panel, which is the safe
	// direction but has to be deliberate rather than a surprise.
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);

	SarkoSurvival::FConsumeEffect Effect;

	TestTrue(TEXT("water_bottle is consumable"),
		SarkoSurvival::ConsumableEffectFor(TEXT("water_bottle"), Effect));
	TestTrue(TEXT("...and it restores WATER and nothing else"),
		Effect.Water > 0.f && Effect.Food == 0.f && Effect.Heal == 0.f);

	TestTrue(TEXT("canned_food is consumable"),
		SarkoSurvival::ConsumableEffectFor(TEXT("canned_food"), Effect));
	TestTrue(TEXT("...and it restores FOOD and nothing else"),
		Effect.Food > 0.f && Effect.Water == 0.f && Effect.Heal == 0.f);

	// Vodka is the one with a bill. A heal that also helped thirst would make the
	// water bottle pointless, and a heal with no cost would make the medkit
	// pointless — so it heals a little and takes water with it.
	TestTrue(TEXT("vodka is consumable"),
		SarkoSurvival::ConsumableEffectFor(TEXT("vodka"), Effect));
	TestTrue(TEXT("...and it is a small heal with a thirst cost, never a drink"),
		Effect.Heal > 0.f && Effect.Water < 0.f && Effect.Food == 0.f);
	if (Settings)
	{
		TestTrue(TEXT("vodka heals less than a bottle restores, so it is never the better drink"),
			Effect.Heal < Settings->WaterBottleRestoresWater);
	}

	// Everything else is not a verb, whatever else it is.
	for (const TCHAR* NotConsumable : { TEXT("medkit"), TEXT("bandage"), TEXT("ammo_9mm"),
			TEXT("pistol"), TEXT("backpack"), TEXT("scrap_metal"), TEXT("cigarettes"),
			TEXT("bike_frame"), TEXT("not_an_item_at_all") })
	{
		TestFalse(FString::Printf(TEXT("'%s' cannot be eaten"), NotConsumable),
			SarkoSurvival::ConsumableEffectFor(FName(NotConsumable), Effect));
	}

	// Both directions, like the size table: every catalog row in the consumable
	// CATEGORY must have an effect, or the panel offers a button the server
	// refuses — the exact "I tapped and nothing happened" the panel spent a
	// commit eliminating.
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!TestTrue(TEXT("the shipped catalog loads"), SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error)))
	{
		AddError(Error);
		return false;
	}
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		if (Def.Category != ESarkoItemCategory::Consumable)
		{
			continue;
		}
		TestTrue(*FString::Printf(
				TEXT("'%s' is category consumable, so it must have an effect row"), *Def.Id.ToString()),
			SarkoSurvival::ConsumableEffectFor(Def.Id, Effect));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoConsumingSpendsExactlyOneUnit,
	"Sarko.Survival.ConsumingSpendsExactlyOneUnit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoConsumingSpendsExactlyOneUnit::RunTest(const FString& Parameters)
{
	// The server's own validation chain, exercised on a component with no world:
	// the index is hostile input and the cells are the server's copy.
	USarkoSurvivalComponent* Survival = NewObject<USarkoSurvivalComponent>();
	Survival->ResetForTest(55.f, 45.f);

	TArray<FSarkoItemStack> Bag = {
		FSarkoItemStack{ TEXT("water_bottle"), 2 },
		FSarkoItemStack{ TEXT("scrap_metal"), 3 },
	};

	TestFalse(TEXT("an index past the end is refused, and spends nothing"),
		Survival->ConsumeFromBag(Bag, 7));
	TestFalse(TEXT("a negative index is refused"), Survival->ConsumeFromBag(Bag, -1));
	TestFalse(TEXT("scrap metal is not a drink"), Survival->ConsumeFromBag(Bag, 1));
	TestEqual(TEXT("...and none of that touched the cells"), Bag.Num(), 2);
	TestEqual(TEXT("...or the meter"), Survival->GetWaterExact(), 45.f, 0.001f);

	TestTrue(TEXT("drinking the bottle works"), Survival->ConsumeFromBag(Bag, 0));
	TestEqual(TEXT("exactly one unit is spent"), Bag[0].Quantity, 1);
	TestEqual(TEXT("the stack stays while units remain"), Bag.Num(), 2);
	TestEqual(TEXT("and the meter moved by the bottle's value"),
		Survival->GetWaterExact(),
		FMath::Min(100.f, 45.f + GetDefault<USarkoRaidSettings>()->WaterBottleRestoresWater), 0.001f);

	TestTrue(TEXT("the last unit drinks too"), Survival->ConsumeFromBag(Bag, 0));
	TestEqual(TEXT("and the empty stack leaves the grid"), Bag.Num(), 1);
	TestEqual(TEXT("leaving the scrap where it was"), Bag[0].Item, FName(TEXT("scrap_metal")));

	// Vodka's cost, on the meter and not on some invisible ledger.
	Survival->ResetForTest(20.f, 20.f);
	TArray<FSarkoItemStack> Drinker = { FSarkoItemStack{ TEXT("vodka"), 1 } };
	TestTrue(TEXT("vodka is drunk"), Survival->ConsumeFromBag(Drinker, 0));
	TestEqual(TEXT("and it costs water"), Survival->GetWaterExact(),
		FMath::Max(0.f, 20.f - GetDefault<USarkoRaidSettings>()->VodkaCostsWater), 0.001f);
	TestEqual(TEXT("hunger is not vodka's business"), Survival->GetFoodExact(), 20.f, 0.001f);
	TestEqual(TEXT("and the bottle is gone"), Drinker.Num(), 0);
	return true;
}

#endif
