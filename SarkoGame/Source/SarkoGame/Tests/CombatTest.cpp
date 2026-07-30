#include "Misc/AutomationTest.h"

#include "Pawn/SarkoHealthComponent.h"

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
	Weapon->ResetForTest(30);
	TestTrue(TEXT("a full, non-reloading weapon can fire"), Weapon->CanFire());

	Weapon->SetReloadingForTest(true);
	TestFalse(TEXT("a full magazine still cannot fire mid-reload"), Weapon->CanFire());
	TestTrue(TEXT("IsReloading reports the reloading state"), Weapon->IsReloading());

	Weapon->SetReloadingForTest(false);
	TestTrue(TEXT("firing resumes once the reload seam clears"), Weapon->CanFire());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
