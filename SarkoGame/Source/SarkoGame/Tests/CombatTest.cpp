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
		// One gap between them, at least. They no longer share a column — they sit
		// at two angles on one arc — so the separation is measured as a distance
		// between the two hit squares rather than as "one is above the other".
		const float BetweenButtons = FMath::Sqrt(Reload.ComputeSquaredDistanceToPoint(Interact.GetCenter()))
			- Interact.GetSize().X * 0.5f;
		TestTrue(*FString::Printf(TEXT("%s: %.0f pt of daylight between the two buttons"), *Where,
				BetweenButtons / Scale),
			BetweenButtons / Scale >= SarkoInput::ThumbButtonGapPt);

		// BOTH buttons are ROUND now, so both rects are the square the circle is
		// inscribed in. A rect that stopped being square would be a circle drawn
		// somewhere other than where it is pressed.
		TestTrue(*FString::Printf(TEXT("%s: the reload rect is square, for a round button"), *Where),
			FMath::IsNearlyEqual(Reload.GetSize().X, Reload.GetSize().Y, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: and so is the interact rect — it is round too now"), *Where),
			FMath::IsNearlyEqual(Interact.GetSize().X, Interact.GetSize().Y, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: one size for both, so one reach to learn"), *Where),
			FMath::IsNearlyEqual(Interact.GetSize().X, Reload.GetSize().X, 0.01f));

		// Both inside the safe frame, or a notch eats a control.
		TestTrue(*FString::Printf(TEXT("%s: both are inside the safe frame"), *Where),
			Safe.IsInside(Reload) && Safe.IsInside(Interact));

		// THE THUMB'S KEEP-OUT, and this is the rule that moved both buttons.
		//
		// The aim stick floats: the thumb lands somewhere inside the 26 pt home
		// ring and drives the stick up to 52 pt from wherever it landed, so every
		// position it can legitimately hold while aiming is inside a 78 pt disc
		// around the home. Reload used to sit 38 pt out — inside that disc, i.e.
		// under a thumb at full deflection — which is the half of spec §4.3
		// ("clear of the stick's own travel") that was false.
		//
		// Measured on the HIT SQUARE and not on the drawn circle, because it is the
		// square that steals a touch from the stick.
		const FVector2D Anchor = SarkoInput::AimStickHome(Safe, Scale);
		const float ToReload = FMath::Sqrt(Reload.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		const float ToInteract = FMath::Sqrt(Interact.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		const float KeepOut = SarkoInput::ThumbTravelKeepOutPt + SarkoInput::ThumbButtonGapPt;
		TestTrue(*FString::Printf(TEXT("%s: reload is %.0f pt out, clear of the thumb's travel"), *Where, ToReload),
			ToReload >= KeepOut);
		TestTrue(*FString::Printf(TEXT("%s: interact is %.0f pt out, clear of it too"), *Where, ToInteract),
			ToInteract >= KeepOut);
		// Still one rotation of the thumb, not a re-grip. The old column already
		// asked for 114 pt to reach the interact button; neither of these asks more.
		TestTrue(*FString::Printf(TEXT("%s: ...and both are still reachable"), *Where),
			ToReload <= 114.f && ToInteract <= 114.f);

		// Neither rect depends on game state — there is nothing to pass. That is
		// what makes "the interact button appearing must not shift the reload
		// button" structural rather than a promise.
		TestEqual(*FString::Printf(TEXT("%s: the reload rect is a pure function of the frame"), *Where),
			SarkoInput::ReloadButtonRect(Safe, Scale).Min.Y, Reload.Min.Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoThumbButtonsSitOnOneArc,
	"Sarko.Input.ThumbButtonsSitOnOneArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoThumbButtonsSitOnOneArc::RunTest(const FString& Parameters)
{
	// THE OWNER'S THIRD PLAYTEST: "the action one should be a round button, not a
	// square — it can all be placed around the aim stick's home." A thumb pivots
	// about where it rests, so two controls at ONE radius from that rest are the
	// same reach apart from a rotation; a column is one easy button and one you
	// stretch for.
	//
	// This is the geometric half of that sentence. That the cluster READS as
	// designed, and that the two buttons are tellable apart at a glance, is a
	// screenshot and a pair of eyes — the same division this project draws
	// everywhere around the HUD.
	const TArray<FVector2D> Viewports = {
		FVector2D(2556.f, 1179.f),   // iPhone 14/15 Pro landscape
		FVector2D(1280.f, 720.f),    // a small desktop window
		FVector2D(1560.f, 720.f),    // a cheap phone at 2x
	};

	for (const FVector2D& Viewport : Viewports)
	{
		const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
		const float Scale = SarkoUI::PointScaleForViewport(Viewport);
		const FVector2D Home = SarkoInput::AimStickHome(Safe, Scale);
		const FBox2D Reload = SarkoInput::ReloadButtonRect(Safe, Scale);
		const FBox2D Interact = SarkoInput::InteractButtonRect(Safe, Scale);
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Viewport.X, Viewport.Y);

		// ONE RADIUS. Measured to the centres, because that is what the arc is; the
		// buttons are the same size, so equal centres mean equal rims too.
		const float ToReload = FVector2D::Distance(Reload.GetCenter(), Home) / Scale;
		const float ToInteract = FVector2D::Distance(Interact.GetCenter(), Home) / Scale;
		TestTrue(*FString::Printf(TEXT("%s: reload is on the arc (%.1f pt)"), *Where, ToReload),
			FMath::IsNearlyEqual(ToReload, SarkoInput::ThumbArcRadiusPt, 0.05f));
		TestTrue(*FString::Printf(TEXT("%s: interact is on the SAME arc (%.1f pt)"), *Where, ToInteract),
			FMath::IsNearlyEqual(ToInteract, SarkoInput::ThumbArcRadiusPt, 0.05f));

		// TWO ANGLES, and each is the one the constant says it is. Canvas Y grows
		// downward and inward is -X, so this reverses SarkoInput::ThumbArcCentre
		// rather than restating it.
		auto DegreesInwardFromUp = [&Home, Scale](const FBox2D& Button)
		{
			const FVector2D Offset = (Button.GetCenter() - Home) / Scale;
			return FMath::RadiansToDegrees(FMath::Atan2(-Offset.X, -Offset.Y));
		};
		TestTrue(*FString::Printf(TEXT("%s: reload sits %.1f deg in from straight up"), *Where,
				DegreesInwardFromUp(Reload)),
			FMath::IsNearlyEqual(DegreesInwardFromUp(Reload), SarkoInput::ReloadButtonArcDegrees, 0.05f));
		TestTrue(*FString::Printf(TEXT("%s: interact sits %.1f deg in"), *Where,
				DegreesInwardFromUp(Interact)),
			FMath::IsNearlyEqual(DegreesInwardFromUp(Interact), SarkoInput::InteractButtonArcDegrees, 0.05f));

		// The arc is derived from the keep-out and not chosen, and it is sized off
		// the square's half-DIAGONAL: a corner of a hit square reaches nearer the
		// home than its edge does at every angle that is not on an axis, and sizing
		// this off the half-side left the reload square's corner one point inside
		// the thumb's travel.
		TestTrue(TEXT("the arc clears the keep-out by a gap, at any angle"),
			SarkoInput::ThumbArcRadiusPt - SarkoInput::ThumbButtonDiameterPt * 0.5f * UE_SQRT_2
				>= SarkoInput::ThumbTravelKeepOutPt + SarkoInput::ThumbButtonGapPt - 0.01f);

		// THE RESERVED SLOT. The interact button is contextual — it lights up when a
		// crate is in reach and goes blank when it is not — and its rect is computed
		// from the frame alone, so there is no state to pass and nothing that could
		// make the reload button move under a thumb when it appears. Asserted as
		// identity across repeated calls, which is the only shape "pure" has here.
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			TestTrue(*FString::Printf(TEXT("%s: the interact slot is reserved whether or not it is shown"), *Where),
				SarkoInput::InteractButtonRect(Safe, Scale).Min.Equals(Interact.Min, 0.001f));
			TestTrue(*FString::Printf(TEXT("%s: and the reload button never moves for it"), *Where),
				SarkoInput::ReloadButtonRect(Safe, Scale).Min.Equals(Reload.Min, 0.001f));
		}

		// Neither button may sit in the LEFT half, which belongs to the move thumb
		// and to the container panel. The arc swings inward, so this is the bound
		// the angles are actually spending.
		TestTrue(*FString::Printf(TEXT("%s: the whole cluster stays in the right half"), *Where),
			Reload.Min.X > Viewport.X * 0.5f && Interact.Min.X > Viewport.X * 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickHomesArePlacedAndClear,
	"Sarko.Input.StickHomesArePlacedAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickHomesArePlacedAndClear::RunTest(const FString& Parameters)
{
	// THE COMPLAINT THIS PINS. Both sticks float and neither was drawn until it
	// was touched, so a player looking at a fresh raid saw two buttons on the
	// right and nothing else — and reported that the sticks did not work. The
	// homes are the fix: a mark at each thumb's resting place, on screen before
	// anything is touched.
	//
	// Only the GEOMETRY is testable here. That the ring is actually drawn, and
	// that it reads as a control at arm's length, is a screenshot and a pair of
	// eyes — the same division this project already draws around the HUD.
	const TArray<FVector2D> Viewports = {
		FVector2D(2556.f, 1179.f),   // iPhone 14/15 Pro landscape
		FVector2D(1280.f, 720.f),    // a small desktop window
		FVector2D(1560.f, 720.f),    // a cheap phone at 2x
	};

	for (const FVector2D& Viewport : Viewports)
	{
		const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
		const float Scale = SarkoUI::PointScaleForViewport(Viewport);
		const FVector2D MoveHome = SarkoInput::MoveStickHome(Safe, Scale);
		const FVector2D AimHome = SarkoInput::AimStickHome(Safe, Scale);
		const float Ring = SarkoInput::StickHomeRingPt * Scale;
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Viewport.X, Viewport.Y);

		// The whole mark, not just its centre, or a home indicator clips it.
		const FBox2D MoveMark(MoveHome - FVector2D(Ring, Ring), MoveHome + FVector2D(Ring, Ring));
		const FBox2D AimMark(AimHome - FVector2D(Ring, Ring), AimHome + FVector2D(Ring, Ring));
		TestTrue(*FString::Printf(TEXT("%s: both home marks are inside the safe frame"), *Where),
			Safe.IsInside(MoveMark) && Safe.IsInside(AimMark));

		// One thumb each, and the halves are the classifier's own — which is the
		// point: a touch is still routed by WHICH HALF it lands in and never by how
		// near a home it is. The home is a hint, not a cage; the stick still floats
		// to the thumb.
		TestTrue(*FString::Printf(TEXT("%s: the move home is in the left half"), *Where),
			SarkoInput::IsLeftHalf(MoveHome, Viewport));
		TestFalse(*FString::Printf(TEXT("%s: the aim home is in the right half"), *Where),
			SarkoInput::IsLeftHalf(AimHome, Viewport));
		TestTrue(*FString::Printf(TEXT("%s: a touch nowhere near the move home is still a move touch"), *Where),
			SarkoInput::IsLeftHalf(FVector2D(Safe.Min.X + 4.f, Safe.Min.Y + 4.f), Viewport));

		// Mirrored. A player who learns one home has learned the other.
		TestTrue(*FString::Printf(TEXT("%s: the two homes share a row"), *Where),
			FMath::IsNearlyEqual(MoveHome.Y, AimHome.Y, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: and sit the same distance in from their own edges"), *Where),
			FMath::IsNearlyEqual(MoveHome.X - Safe.Min.X, Safe.Max.X - AimHome.X, 0.01f));

		// A mark a thumb can find: 52 pt across, above the 44 pt floor this project
		// holds its buttons to.
		TestTrue(*FString::Printf(TEXT("%s: the home mark is at least 44 pt across"), *Where),
			(Ring * 2.f) / Scale >= 44.f);

		// THE DERIVATION. Neither button has coordinates of its own: each is the aim
		// home plus one angle on one arc, and the arc itself is the stick's own
		// travel plus a gap plus the button's half-diagonal. So they cannot drift
		// away from the stick they belong to, and "around the aim stick's default
		// position" is arithmetic rather than a promise.
		const FBox2D Reload = SarkoInput::ReloadButtonRect(Safe, Scale);
		const FBox2D Interact = SarkoInput::InteractButtonRect(Safe, Scale);
		TestTrue(*FString::Printf(TEXT("%s: the reload button is inward of the aim home"), *Where),
			Reload.GetCenter().X < AimHome.X);
		TestTrue(*FString::Printf(TEXT("%s: and its whole square sits above it"), *Where),
			Reload.Max.Y < AimHome.Y);
		// The interact button is further round the SAME arc, level with the home
		// rather than above it — which is what makes the two tellable apart by
		// position alone, before either picture is read.
		TestTrue(*FString::Printf(TEXT("%s: the interact button is further inward still"), *Where),
			Interact.GetCenter().X < Reload.GetCenter().X);
		TestTrue(*FString::Printf(TEXT("%s: and level with the home rather than above it"), *Where),
			FMath::IsNearlyEqual(Interact.GetCenter().Y, AimHome.Y, 0.01f));

		// The home ring is nowhere near either of them any more. It used to clear
		// the reload button by exactly one 12 pt gap, back when the button sat 38 pt
		// out and INSIDE the stick's travel; the arc is sized off the travel now, so
		// the clearance from the ring is a consequence rather than the rule.
		const float ToReload = FMath::Sqrt(Reload.ComputeSquaredDistanceToPoint(AimHome)) / Scale;
		TestTrue(*FString::Printf(TEXT("%s: the home ring clears the reload button by %.1f pt"), *Where,
				ToReload - SarkoInput::StickHomeRingPt),
			ToReload - SarkoInput::StickHomeRingPt >= SarkoInput::ThumbButtonGapPt);

		// NOTHING SITS WHERE THE THUMB RESTS. Both buttons are strictly outside the
		// mark, so a thumb settling onto its home cannot land on a control.
		TestFalse(*FString::Printf(TEXT("%s: the reload button does not cover the aim home"), *Where),
			Reload.Intersect(AimMark));
		TestFalse(*FString::Printf(TEXT("%s: nor does the interact button"), *Where),
			Interact.Intersect(AimMark));
		// And the left thumb's home is clear of the right thumb's cluster entirely.
		TestFalse(*FString::Printf(TEXT("%s: the move home is clear of the thumb column"), *Where),
			MoveMark.Intersect(Reload) || MoveMark.Intersect(Interact));
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
