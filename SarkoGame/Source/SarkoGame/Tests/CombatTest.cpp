#include "Misc/AutomationTest.h"

#include "AI/SarkoBotArchetypes.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidSettings.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoItemGrid.h"
#include "Map/SarkoMapDefinition.h"
#include "Pawn/SarkoHealthComponent.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHealthDamageAndDeath,
	"Sarko.Combat.HealthDamageAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHealthDamageAndDeath::RunTest(const FString& Parameters)
{
	USarkoHealthComponent* Health = NewObject<USarkoHealthComponent>();
	Health->ResetForTest(100.f);

	TestFalse(TEXT("a fresh pawn is alive"), Health->IsDead());
	TestEqual(TEXT("health starts full"), Health->GetHealth(), 100.f);

	Health->ApplyDamage(30.f, nullptr);
	TestEqual(TEXT("damage subtracts"), Health->GetHealth(), 70.f);
	TestFalse(TEXT("still alive at 70"), Health->IsDead());

	int32 DeathCount = 0;
	Health->OnDied.AddLambda([&DeathCount](AActor*) { ++DeathCount; });

	Health->ApplyDamage(1000.f, nullptr);
	TestEqual(TEXT("health floors at zero rather than going negative"), Health->GetHealth(), 0.f);
	TestTrue(TEXT("the pawn is dead"), Health->IsDead());
	TestEqual(TEXT("death fires exactly once"), DeathCount, 1);

	// Overkill on a corpse must not fire the delegate again — that would double
	// every death-driven consequence, including losing a raid.
	Health->ApplyDamage(50.f, nullptr);
	TestEqual(TEXT("death does not fire twice"), DeathCount, 1);

	// Healing is not a mechanic in this slice, but negative damage must not heal.
	Health->ApplyDamage(-25.f, nullptr);
	TestEqual(TEXT("negative damage cannot resurrect or heal"), Health->GetHealth(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFriendFoeDistinction,
	"Sarko.Combat.FriendFoeDistinction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFriendFoeDistinction::RunTest(const FString& Parameters)
{
	// The simplest thing that works for a slice with exactly two sides: a
	// candidate on the shooter's own team is never a foe, one on the other
	// team always is.
	using namespace SarkoCombat;

	TestFalse(TEXT("an enemy is not a foe to another enemy"), IsFoe(ESarkoTeam::Enemy, ESarkoTeam::Enemy));
	TestFalse(TEXT("the player is not a foe to themselves"), IsFoe(ESarkoTeam::Player, ESarkoTeam::Player));
	TestTrue(TEXT("an enemy is a foe to the player"), IsFoe(ESarkoTeam::Enemy, ESarkoTeam::Player));
	TestTrue(TEXT("the player is a foe to an enemy"), IsFoe(ESarkoTeam::Player, ESarkoTeam::Enemy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheTutorialsAmmoBudgetIsWinnable,
	"Sarko.Map.TheTutorialsAmmoBudgetIsWinnable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheTutorialsAmmoBudgetIsWinnable::RunTest(const FString& Parameters)
{
	// Spec §1 made the route's authored rounds a real budget: reload now spends
	// `ammo_9mm` out of the grid, SarkoBackend::WireLoadout sends an EMPTY loadout
	// (so a raid begins with an empty bag and nothing else), and the tutorial's
	// fixedItems are therefore every round the player will ever have.
	//
	// This is arithmetic over that data, not a playthrough — and it exists because
	// the failure it guards against is silent: deleting a crate or retuning
	// WeaponDamage would leave a tutorial that cannot be finished with a pistol,
	// and nothing would say so until someone played it to the end.
	FSarkoMapDefinition Map;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	if (!Settings)
	{
		AddError(TEXT("settings did not resolve"));
		return false;
	}

	// Supply: the starting magazine plus every authored round on the route.
	int32 RouteRounds = 0;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		for (const FSarkoItemStack& Stack : Spot.FixedItems)
		{
			if (Stack.Item == FName(TEXT("ammo_9mm")))
			{
				RouteRounds += Stack.Quantity;
			}
		}
	}
	const int32 StartingRounds =
		SarkoCombat::StartingRounds(Settings->StartingMagazineRounds, Settings->MagazineSize);
	const int32 Supply = StartingRounds + RouteRounds;

	// Demand: every bot a TUTORIAL raid can meet, at the hits its archetype's
	// health costs against the player's damage. maxAlive summed over the tutorial's
	// one-shot encounters is exactly how many bots a full clear meets.
	//
	// Non-optional rows only, since the rotation (spec §5): the five optional rows
	// are unreachable in a tutorial raid by construction, and counting them here
	// would price the authored 46 rounds against fights the first raid cannot have.
	// A normal raid meets more — that is the point of the rotation — and it is not
	// this test's subject, because a normal raid starts with whatever the player
	// carried out of the stash and the map authors none of it.
	const float Damage = FMath::Max(1.f, Settings->WeaponDamage);
	int32 HitsNeeded = 0;
	int32 Bots = 0;
	for (const FSarkoEncounter& Encounter : Map.Encounters)
	{
		if (Encounter.bOptional)
		{
			continue;
		}
		// The worst archetype among this encounter's doors, taken maxAlive times:
		// which door fires is a runtime choice, so the budget must survive the
		// toughest one every time.
		float ToughestHealth = 0.f;
		for (const FSarkoEncounterSpawn& Spawn : Encounter.Spawns)
		{
			FSarkoBotArchetype Archetype;
			if (SarkoAI::FindBotArchetype(Spawn.Archetype, Archetype))
			{
				ToughestHealth = FMath::Max(ToughestHealth, Archetype.MaxHealth);
			}
		}
		const int32 HitsPerBot = FMath::CeilToInt(ToughestHealth / Damage);
		HitsNeeded += HitsPerBot * FMath::Max(0, Encounter.MaxAlive);
		Bots += FMath::Max(0, Encounter.MaxAlive);
	}

	TestTrue(TEXT("the route authors ammunition at all"), RouteRounds > 0);
	TestTrue(TEXT("the tutorial has fights to spend it on"), HitsNeeded > 0);

	// The bound. A player who lands every shot needs HitsNeeded rounds; the honest
	// question is how badly they may shoot and still finish, and the answer has to
	// leave room for a first-timer on a phone. Two thirds missed is the line: at a
	// 66% miss rate a full clear costs three times the hits, and the route must
	// still cover it.
	const int32 RoundsAtTwoThirdsMissed = HitsNeeded * 3;
	TestTrue(*FString::Printf(
			TEXT("%d rounds (%d in the magazine + %d on the route) covers %d bots at %d hits, and still covers %d — a 66%% miss rate"),
			Supply, StartingRounds, RouteRounds, Bots, HitsNeeded, RoundsAtTwoThirdsMissed),
		Supply >= RoundsAtTwoThirdsMissed);

	// And the other direction, because a budget nobody can run out of is not a
	// budget: perfect shooting must not leave the player with a spare magazine per
	// bot, or the scarcity the stage just built is invisible on the map that
	// teaches it.
	TestTrue(*FString::Printf(
			TEXT("%d rounds is a budget, not a surplus: under %d bots' worth of full magazines"),
			Supply, Bots),
		Supply < Bots * Settings->MagazineSize * 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

#include "Combat/SarkoWeapon.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAimAssistIsANudgeNotAnAimbot,
	"Sarko.Combat.AimAssistIsANudgeNotAnAimbot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAimAssistIsANudgeNotAnAimbot::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Forward(1.f, 0.f, 0.f);

	// A target just outside the thumb's precision but inside the cone: snap.
	const TArray<FVector> NearTarget = { FVector(1000.f, 40.f, 0.f) };
	const FVector Assisted = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, NearTarget);
	TestTrue(TEXT("a target inside the cone pulls the shot"), Assisted.Y > 0.f);

	// A target far outside the cone must be ignored, or this is an aimbot.
	const TArray<FVector> FarTarget = { FVector(1000.f, 900.f, 0.f) };
	const FVector Unassisted = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, FarTarget);
	TestTrue(TEXT("a target outside the cone is ignored"), Unassisted.Equals(Forward, 0.001f));

	// With no targets the direction is returned untouched.
	TestTrue(TEXT("no targets means no change"),
		SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, {}).Equals(Forward, 0.001f));

	// With two candidates inside the cone, the nearer one wins.
	const TArray<FVector> TwoTargets = { FVector(3000.f, -60.f, 0.f), FVector(800.f, 30.f, 0.f) };
	const FVector Nearest = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, TwoTargets);
	TestTrue(TEXT("the nearest in-cone target wins"), Nearest.Y > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAimAssistEdgeCases,
	"Sarko.Combat.AimAssistEdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAimAssistEdgeCases::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Forward(1.f, 0.f, 0.f);

	// A candidate exactly at the origin has no direction to it at all — this
	// must be skipped, not crash or produce a NaN direction.
	const TArray<FVector> Coincident = { Origin };
	const FVector CoincidentResult = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, Coincident);
	TestTrue(TEXT("a coincident candidate is skipped, not chosen"), CoincidentResult.Equals(Forward, 0.001f));
	TestFalse(TEXT("a coincident candidate never produces NaN"), CoincidentResult.ContainsNaN());

	// A zero input direction is degenerate on the caller's side (this is
	// exactly what ServerFire must bail on before ever reaching this
	// function) but ApplyAimAssist itself must still handle it without
	// crashing, returning it unchanged.
	const TArray<FVector> SomeTarget = { FVector(500.f, 10.f, 0.f) };
	const FVector ZeroDirResult = SarkoCombat::ApplyAimAssist(Origin, FVector::ZeroVector, 6.f, SomeTarget);
	TestTrue(TEXT("a zero input direction is returned unchanged, not assisted"), ZeroDirResult.IsNearlyZero());

	// A near candidate outside the cone must not beat a far candidate inside
	// it — being in-cone at all is the gate; raw distance only ranks among
	// candidates that already passed it. This is the mixed case the
	// existing "two targets" test (both in-cone) never exercised.
	const TArray<FVector> Mixed = { FVector(500.f, 500.f, 0.f) /*near, 45 deg, out of cone*/, FVector(3000.f, 50.f, 0.f) /*far, ~1 deg, in cone*/ };
	const FVector MixedResult = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, Mixed);
	// The far in-cone candidate is nearly straight ahead (~1 deg off axis, Y
	// component ~0.017 once normalized); the near out-of-cone one is 45 deg
	// off axis (Y component ~0.707). A generous 0.1 cutoff cleanly separates
	// "won by the in-cone candidate" from "won by the out-of-cone one".
	TestTrue(TEXT("the far in-cone candidate wins over the near out-of-cone one"), MixedResult.Y > 0.f && MixedResult.Y < 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAimAssistIgnoresMuzzleHeightAtCloseRange,
	"Sarko.Combat.AimAssistIgnoresMuzzleHeightAtCloseRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAimAssistIgnoresMuzzleHeightAtCloseRange::RunTest(const FString& Parameters)
{
	// The muzzle sits 40uu above the ground while every candidate is a pawn
	// *centre* (Z=0), so a 3D cone comparison carries a constant vertical
	// bias that only matters at close range: at 6 deg half-angle, anything
	// closer than 40/tan(6 deg) =~ 380uu reads as outside the cone even
	// dead-centre. This target is comfortably inside the cone horizontally
	// (2.86 deg) but the 40uu Z offset alone pushes the naive 3D angle to
	// 8.11 deg, outside a 6 deg cone — so this only passes once the
	// comparison is horizontal.
	const FVector Origin(0.f, 0.f, 40.f);
	const FVector Aim(1.f, 0.f, 0.f);
	const TArray<FVector> CloseOffCentreTarget = { FVector(300.f, 15.f, 0.f) };

	const FVector Result = SarkoCombat::ApplyAimAssist(Origin, Aim, 6.f, CloseOffCentreTarget);
	TestFalse(TEXT("a close, horizontally in-cone target is not defeated by the muzzle's vertical offset"),
		Result.Equals(Aim, 0.0001f));
	TestTrue(TEXT("the close, horizontally in-cone target pulls the aim"), Result.Y > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNormalizeFireDirection,
	"Sarko.Combat.NormalizeFireDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNormalizeFireDirection::RunTest(const FString& Parameters)
{
	// The guard ServerFire relies on to stop a modified client from sending
	// an oversized Direction and getting a trace far beyond WeaponRangeUU: a
	// non-unit input must come back unit length, and a degenerate input must
	// come back as an unambiguous zero rather than something a careless
	// caller might trace with anyway.
	const FVector Oversized = SarkoCombat::NormalizeFireDirection(FVector(1000.f, 0.f, 0.f));
	TestTrue(TEXT("an oversized direction is normalized to unit length"), FMath::IsNearlyEqual(Oversized.Size(), 1.f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("normalizing preserves the direction"), Oversized.Equals(FVector(1.f, 0.f, 0.f), KINDA_SMALL_NUMBER));

	const FVector Zero = SarkoCombat::NormalizeFireDirection(FVector::ZeroVector);
	TestTrue(TEXT("a zero direction stays zero"), Zero.IsNearlyZero());

	const FVector TinyNonZero = SarkoCombat::NormalizeFireDirection(FVector(KINDA_SMALL_NUMBER * 0.01f, 0.f, 0.f));
	TestTrue(TEXT("a degenerate near-zero direction is treated as zero, not amplified"), TinyNonZero.IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMagazineGatesFiring,
	"Sarko.Combat.MagazineGatesFiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMagazineGatesFiring::RunTest(const FString& Parameters)
{
	USarkoWeaponComponent* Weapon = NewObject<USarkoWeaponComponent>();
	Weapon->ResetForTest(3);

	TestTrue(TEXT("a loaded weapon can fire"), Weapon->CanFire());

	Weapon->ConsumeRoundForTest();
	Weapon->ConsumeRoundForTest();
	Weapon->ConsumeRoundForTest();
	TestEqual(TEXT("the magazine empties"), Weapon->GetAmmoInMagazine(), 0);
	TestFalse(TEXT("an empty weapon cannot fire"), Weapon->CanFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoReloadSpendsTheReserve,
	"Sarko.Combat.ReloadSpendsTheReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReloadSpendsTheReserve::RunTest(const FString& Parameters)
{
	// THE KEYSTONE OF THE SCARCITY STAGE (spec §1), as arithmetic. Reload used to
	// assign MagazineSize unconditionally, so every number the realism stage
	// shipped — the eight-round magazine, the route's 46 authored rounds, the
	// halved loot weights — described an infinite supply.
	TestEqual(TEXT("a plentiful bag fills the magazine"), SarkoCombat::ReloadAmount(0, 8, 60), 8);
	TestEqual(TEXT("a partial magazine takes only the room it has"),
		SarkoCombat::ReloadAmount(3, 8, 60), 5);
	TestEqual(TEXT("a full magazine takes nothing"), SarkoCombat::ReloadAmount(8, 8, 60), 0);

	// PARTIAL RELOADS ARE THE POINT, not an edge case: three rounds in the bag
	// load three rounds, and the player walks into the next fight knowing it.
	TestEqual(TEXT("three rounds in the bag load three rounds"), SarkoCombat::ReloadAmount(0, 8, 3), 3);
	TestEqual(TEXT("one round in the bag is still a reload"), SarkoCombat::ReloadAmount(0, 8, 1), 1);
	TestEqual(TEXT("an empty bag loads nothing — the dry-click path"),
		SarkoCombat::ReloadAmount(0, 8, 0), 0);

	// Hostile and broken inputs, clamped rather than trusted. The over-full case
	// is the one that matters: a negative transfer would run backwards through a
	// caller that only knows how to add rounds.
	TestEqual(TEXT("a magazine holding more than it should never transfers backwards"),
		SarkoCombat::ReloadAmount(50, 8, 60), 0);
	TestEqual(TEXT("a negative reserve is none, not a gift"), SarkoCombat::ReloadAmount(0, 8, -20), 0);
	TestEqual(TEXT("a broken magazine size yields nothing"), SarkoCombat::ReloadAmount(0, 0, 60), 0);
	TestEqual(TEXT("a negative magazine size yields nothing"), SarkoCombat::ReloadAmount(0, -8, 60), 0);

	// The grid half of the same transfer: what comes OUT, and what is left behind.
	// RemoveFromGrid is the exact inverse of AddToGrid and the only way ammo ever
	// leaves the bag other than consuming or dying.
	TArray<FSarkoItemStack> Bag = {
		FSarkoItemStack{ TEXT("medkit"), 1 },
		FSarkoItemStack{ TEXT("ammo_9mm"), 60 },
		FSarkoItemStack{ TEXT("ammo_9mm"), 5 },
	};
	TestEqual(TEXT("the reserve is every stack of the id, summed"),
		SarkoGrid::CountItem(Bag, TEXT("ammo_9mm")), 65);

	// Backwards: the trailing stack is the least-full one, and draining it first
	// keeps the earlier stacks whole and the earlier cells still.
	TestEqual(TEXT("a five-round reload takes five"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("ammo_9mm"), 5), 5);
	TestEqual(TEXT("...and the drained trailing stack is gone, not left at zero"), Bag.Num(), 2);
	TestEqual(TEXT("...leaving the full stack untouched"), Bag[1].Quantity, 60);
	TestEqual(TEXT("...and the medkit exactly where it was"), Bag[0].Item, FName(TEXT("medkit")));

	// Asking for more than there is takes everything and says so, rather than
	// failing: that IS the partial reload, one layer down.
	TestEqual(TEXT("asking for more than the bag holds takes what there is"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("ammo_9mm"), 100), 60);
	TestEqual(TEXT("...and the empty bag holds only the medkit"), Bag.Num(), 1);
	TestEqual(TEXT("an empty bag gives nothing"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("ammo_9mm"), 8), 0);
	TestEqual(TEXT("a zero request is a no-op, not an error"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("medkit"), 0), 0);
	TestEqual(TEXT("a negative request is a no-op too"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("medkit"), -3), 0);
	TestEqual(TEXT("...and nothing was touched by either"), Bag.Num(), 1);
	TestEqual(TEXT("an id that is not there removes nothing"),
		SarkoGrid::RemoveFromGrid(Bag, TEXT("bike_frame"), 5), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoExtractionCreditsTheMagazine,
	"Sarko.Combat.ExtractionCreditsTheMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoExtractionCreditsTheMagazine::RunTest(const FString& Parameters)
{
	// Spec §1's extraction question, answered YES. Rounds in the gun are rounds
	// the player found and did not spend; losing them for having reloaded before
	// walking to the pad would make "reload before extracting" a strictly wrong
	// move, which is the opposite of the lesson. Death still loses everything —
	// ClearOnDeath is untouched and the magazine goes with the pawn.
	USarkoWeaponComponent* Weapon = NewObject<USarkoWeaponComponent>();
	Weapon->ResetForTest(6);

	TestEqual(TEXT("unloading reports what was in the magazine"), Weapon->UnloadMagazine(), 6);
	TestEqual(TEXT("...and empties it, so the credit is exactly-once by construction"),
		Weapon->GetAmmoInMagazine(), 0);
	TestEqual(TEXT("a second unload pays nothing, however the outcome path is re-entered"),
		Weapon->UnloadMagazine(), 0);

	// FOLDED BACK INTO THE STACK, through the same placement rule everything else
	// uses: AddItem tops a partial stack up for free and only opens a rectangle
	// when there is room, so the credit can never claim a cell the bag does not
	// have — which is what keeps domain.FitsCarryGrid from answering 400 on a
	// legitimate haul.
	//
	// Exercised through SarkoGrid::AddToGrid rather than USarkoBackpackComponent::
	// AddItem, which is the one line AddItem consists of: AddItem refuses a
	// component with no owner outright (its authority guard has no HasAuthority to
	// ask), and a NewObject component has none. The rule under test is the pure
	// one either way — and it is the same call the extraction path makes.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridPage> Pages = SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));

	TArray<FSarkoItemStack> Bag = { FSarkoItemStack{ TEXT("ammo_9mm"), 40 } };
	TestEqual(TEXT("six rounds fold into the open stack with nothing left over"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pages, SarkoLoot::AmmoItemId, 6), 0);
	TestEqual(TEXT("...as one stack of 46, not a second cell"), Bag.Num(), 1);
	TestEqual(TEXT("...carrying every round"), SarkoGrid::CountItem(Bag, SarkoLoot::AmmoItemId), 46);

	// And the case that made AddToGrid the right call rather than appending to the
	// haul: a bag with no room refuses what it cannot hold, so the submission can
	// never claim a cell that does not exist. Those rounds are lost — the same
	// rule every other overflow on the loot path already follows.
	TArray<FSarkoItemStack> Full;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		Full.Add(FSarkoItemStack{ TEXT("medkit"), 3 });
	}
	TestEqual(TEXT("a bag with no cell left refuses the magazine's rounds rather than forging a thirteenth"),
		SarkoGrid::AddToGrid(Full, Catalog, Pages, SarkoLoot::AmmoItemId, 6), 6);
	TestEqual(TEXT("...and nothing was added"), Full.Num(), 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheRaidStartsOnAPartialMagazine,
	"Sarko.Combat.TheRaidStartsOnAPartialMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheRaidStartsOnAPartialMagazine::RunTest(const FString& Parameters)
{
	// Spec §3. Auto-reload is gone, so the reload button has to be pressed once
	// somewhere safe before it has to be pressed under fire — and the only thing
	// on the route that can teach it for free is the magazine the raid begins
	// with. This is that arithmetic, and the shipped configuration honouring it.
	TestEqual(TEXT("the configured three of eight is three"), SarkoCombat::StartingRounds(3, 8), 3);
	TestEqual(TEXT("negative means a full magazine"), SarkoCombat::StartingRounds(-1, 8), 8);
	TestEqual(TEXT("more than the magazine holds is a full magazine, not a deeper one"),
		SarkoCombat::StartingRounds(50, 8), 8);
	TestEqual(TEXT("zero is a legal (empty) start"), SarkoCombat::StartingRounds(0, 8), 0);
	TestEqual(TEXT("a broken magazine size yields nothing, never a negative count"),
		SarkoCombat::StartingRounds(3, 0), 0);

	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);
	if (Settings)
	{
		const int32 Start = SarkoCombat::StartingRounds(Settings->StartingMagazineRounds, Settings->MagazineSize);
		TestTrue(TEXT("the shipped raid starts on a PARTIAL magazine, or the lesson never happens"),
			Start > 0 && Start < Settings->MagazineSize);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoReloadingGatesFiring,
	"Sarko.Combat.ReloadingGatesFiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReloadingGatesFiring::RunTest(const FString& Parameters)
{
	// A full magazine must still refuse to fire while a reload is in flight —
	// this is the seam that pins bReloading as a CanFire gate independently of
	// ammo count, since ammo alone (FSarkoMagazineGatesFiring above) cannot
	// exercise this branch.
	USarkoWeaponComponent* Weapon = NewObject<USarkoWeaponComponent>();
	Weapon->ResetForTest(8);
	TestTrue(TEXT("a full, non-reloading weapon can fire"), Weapon->CanFire());

	Weapon->SetReloadingForTest(true);
	TestFalse(TEXT("a full magazine still cannot fire mid-reload"), Weapon->CanFire());
	TestTrue(TEXT("IsReloading reports the reloading state"), Weapon->IsReloading());

	Weapon->SetReloadingForTest(false);
	TestTrue(TEXT("firing resumes once the reload seam clears"), Weapon->CanFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoThumbControlsDoNotOverlap,
	"Sarko.Input.ThumbControlsDoNotOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoThumbControlsDoNotOverlap::RunTest(const FString& Parameters)
{
	// Spec §5: "Reload button placement fights the interact button for the same
	// thumb arc. They must never occupy the same rectangle, and the interact
	// button appearing must not shift the reload button — a control that moves is
	// a control you mis-press."
	//
	// Checked at three viewports, because a rect derived from a fraction can pass
	// on a phone and collapse in a small desktop window — which is exactly what
	// the fraction these two rects replaced did.
	const TArray<FVector2D> Viewports = {
		FVector2D(2556.f, 1179.f),   // iPhone 14/15 Pro landscape
		FVector2D(1280.f, 720.f),    // a small desktop window
		FVector2D(1560.f, 720.f),    // a cheap phone at 2x
	};

	for (const FVector2D& Viewport : Viewports)
	{
		const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
		const float Scale = SarkoUI::PointScaleForViewport(Viewport);
		const FBox2D Reload = SarkoInput::ReloadButtonRect(Safe, Scale);
		const FBox2D Interact = SarkoInput::InteractButtonRect(Safe, Scale);

		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Viewport.X, Viewport.Y);

		// 44 pt in BOTH dimensions, in POINTS — which is why the rects take a
		// scale: a rule written in points cannot be checked against pixels.
		TestTrue(*FString::Printf(TEXT("%s: the reload button clears 44 pt"), *Where),
			Reload.GetSize().X / Scale >= 44.f && Reload.GetSize().Y / Scale >= 44.f);
		TestTrue(*FString::Printf(TEXT("%s: the interact button clears 44 pt"), *Where),
			Interact.GetSize().X / Scale >= 44.f && Interact.GetSize().Y / Scale >= 44.f);

		// Never the same rectangle. FBox2D::Intersect rejects only strict
		// separation, so this also rejects two buttons flush against each other.
		TestFalse(*FString::Printf(TEXT("%s: they do not overlap"), *Where),
			Reload.Intersect(Interact));
		TestTrue(*FString::Printf(TEXT("%s: interact is ABOVE reload, in the same column"), *Where),
			Interact.Max.Y < Reload.Min.Y);
		TestEqual(*FString::Printf(TEXT("%s: right-aligned to the same edge"), *Where),
			Interact.Max.X, Reload.Max.X);

		// Both inside the safe frame, or a notch eats a control.
		TestTrue(*FString::Printf(TEXT("%s: both are inside the safe frame"), *Where),
			Safe.IsInside(Reload) && Safe.IsInside(Interact));

		// The thumb arc. Reload is inside the aim thumb's ~45 pt travel, so it is
		// pressed without the thumb leaving its post; interact is deliberately
		// OUTSIDE it, so working the stick can never brush it, and still inside a
		// landscape thumb's full reach. That asymmetry is the design: reload is a
		// mid-fight reflex, interact is a decision you have already stopped to make.
		const FVector2D Anchor = SarkoInput::RightThumbAnchor(Safe, Scale);
		const float ToReload = FMath::Sqrt(Reload.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		const float ToInteract = FMath::Sqrt(Interact.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		TestTrue(*FString::Printf(TEXT("%s: reload is %.0f pt from the thumb, inside its arc"), *Where, ToReload),
			ToReload <= 45.f);
		TestTrue(*FString::Printf(TEXT("%s: interact is %.0f pt away, outside the stick's arc"), *Where, ToInteract),
			ToInteract > 45.f);
		TestTrue(*FString::Printf(TEXT("%s: ...but still reachable"), *Where), ToInteract <= 150.f);

		// Neither rect depends on game state — there is nothing to pass. That is
		// what makes "the interact button appearing must not shift the reload
		// button" structural rather than a promise.
		TestEqual(*FString::Printf(TEXT("%s: the reload rect is a pure function of the frame"), *Where),
			SarkoInput::ReloadButtonRect(Safe, Scale).Min.Y, Reload.Min.Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHoldTheAimStickToFire,
	"Sarko.Input.HoldTheAimStickToFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHoldTheAimStickToFire::RunTest(const FString& Parameters)
{
	// Spec §4.2: hold past the threshold and it keeps firing. Holding SHORT of it
	// aims and fires nothing — and that half of the sentence is new. Lifting the
	// thumb fires nothing either, at any deflection; see
	// Sarko.Input.ReleasingTheAimStickNeverFires for why the flick's shot went.
	//
	// The threshold is read from the settings rather than written here, so a test
	// against a literal cannot go on passing while the shipped rule moves. It is
	// 0.70; it was 0.35 until the first phone playtest.
	const float DeadZone = GetDefault<USarkoRaidSettings>()->AimFireDeadZone;
	TestFalse(TEXT("a resting thumb does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D::ZeroVector, DeadZone));
	TestFalse(TEXT("a re-grip does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.2f, 0.f), DeadZone));
	// THE ONE THAT CHANGED. 0.4 of the travel used to be a burst; it is now the
	// middle of the aim band, and the difference is an eight-round magazine.
	TestFalse(TEXT("a deflection that only means 'look over there' does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.4f, 0.f), DeadZone));
	TestFalse(TEXT("...nor does one just short of the ring"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.69f, 0.f), DeadZone));
	TestTrue(TEXT("past the threshold it fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.8f, 0.f), DeadZone));
	TestTrue(TEXT("full deflection fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.f, -1.f), DeadZone));

	// The firing threshold must be HIGHER than the movement dead zone, and by
	// enough to leave a band you can hold: an accidental small deflection while
	// re-gripping should aim, and so should a deliberate large one.
	TestTrue(TEXT("the fire threshold is above the move dead zone"),
		GetDefault<USarkoRaidSettings>()->AimFireDeadZone
			> GetDefault<USarkoRaidSettings>()->MoveStickDeadZone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoReloadButtonSaysWhatTheMagazineIs,
	"Sarko.UI.ReloadButtonSaysWhatTheMagazineIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReloadButtonSaysWhatTheMagazineIs::RunTest(const FString& Parameters)
{
	// Spec §4.3: "Shows state: the magazine count lives on it, it goes amber
	// below a third, and it pulses when empty. The player should never have to
	// look at two places to know they need to reload."
	// The magazine is EIGHT since the realism retune (spec §5), so this is stated
	// in the rounds the player actually has: eight full, three is still ready,
	// two is amber. Three matters — it is what the raid starts on.
	//
	// THE RESERVE IS NOW THE FOURTH ARGUMENT (spec §1), and it gates every urgent
	// state, because urgency on this button means "press me" and a press with an
	// empty bag moves nothing. A full magazine over an empty bag is calm; an empty
	// magazine over an empty bag is the one state that is neither calm nor
	// actionable, and it has its own colour rather than borrowing the pulse.
	using ESarkoReloadState = SarkoUI::ESarkoReloadState;
	TestTrue(TEXT("a full eight-round magazine is ready"),
		SarkoUI::ReloadStateFor(8, 8, false, 24) == ESarkoReloadState::Ready);
	TestTrue(TEXT("three of eight — the raid's starting magazine — is still ready"),
		SarkoUI::ReloadStateFor(3, 8, false, 24) == ESarkoReloadState::Ready);
	TestTrue(TEXT("below a third is low"),
		SarkoUI::ReloadStateFor(2, 8, false, 24) == ESarkoReloadState::Low);
	TestTrue(TEXT("empty with rounds in the bag is empty — press me"),
		SarkoUI::ReloadStateFor(0, 8, false, 24) == ESarkoReloadState::Empty);
	TestTrue(TEXT("one round in the bag is still worth pressing for"),
		SarkoUI::ReloadStateFor(0, 8, false, 1) == ESarkoReloadState::Empty);
	TestTrue(TEXT("reloading outranks everything, including empty"),
		SarkoUI::ReloadStateFor(0, 8, true, 24) == ESarkoReloadState::Reloading);
	TestTrue(TEXT("...and including dry: a reload in flight is the fact, whatever the bag holds"),
		SarkoUI::ReloadStateFor(0, 8, true, 0) == ESarkoReloadState::Reloading);
	// The boundary rule itself, at a size where a third is a whole number.
	TestTrue(TEXT("exactly a third is still ready — the boundary belongs to ready"),
		SarkoUI::ReloadStateFor(10, 30, false, 60) == ESarkoReloadState::Ready);
	TestTrue(TEXT("...and one below it is not"),
		SarkoUI::ReloadStateFor(9, 30, false, 60) == ESarkoReloadState::Low);

	// THE EMPTY BAG, in its three shapes.
	TestTrue(TEXT("empty magazine over an empty bag is DRY — the real emergency, and no press fixes it"),
		SarkoUI::ReloadStateFor(0, 8, false, 0) == ESarkoReloadState::Dry);
	TestTrue(TEXT("a full magazine over an empty bag is calm, not a warning"),
		SarkoUI::ReloadStateFor(8, 8, false, 0) == ESarkoReloadState::Ready);
	TestTrue(TEXT("...and so is a LOW magazine over an empty bag: amber would beg for a press that does nothing"),
		SarkoUI::ReloadStateFor(2, 8, false, 0) == ESarkoReloadState::Ready);
	TestTrue(TEXT("a negative reserve is read as none, never as ammo"),
		SarkoUI::ReloadStateFor(0, 8, false, -5) == ESarkoReloadState::Dry);

	// A zero magazine size is a broken config, not a divide by zero.
	TestTrue(TEXT("a zero-size magazine with a bag behind it does not divide by zero"),
		SarkoUI::ReloadStateFor(0, 0, false, 24) == ESarkoReloadState::Empty);
	TestTrue(TEXT("...and a non-empty one against a broken size nags rather than lies"),
		SarkoUI::ReloadStateFor(5, 0, false, 24) == ESarkoReloadState::Low);

	// The pulse is bounded and never fully transparent, or "empty" flickers into
	// looking like "absent".
	for (float Time = 0.f; Time < 4.f; Time += 0.137f)
	{
		const float Alpha = SarkoUI::ReloadPulseAlpha(Time);
		TestTrue(TEXT("the empty pulse stays visible"), Alpha >= 0.25f && Alpha <= 0.65f);
	}

	// The interact button says what it will DO. A generic label in a game with
	// two actions is a guess.
	TestEqual(TEXT("a crate in reach"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::Search), FString(TEXT("ОБШУКАТИ")));
	TestEqual(TEXT("a panel open"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::Close), FString(TEXT("ЗАКРИТИ")));
	TestEqual(TEXT("and the seam for a dwell that is not a press yet"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::Extract), FString(TEXT("ЕВАКУАЦІЯ")));
	TestTrue(TEXT("nothing in reach carries no label at all"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::None).IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
