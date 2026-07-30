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

#endif // WITH_AUTOMATION_TESTS
