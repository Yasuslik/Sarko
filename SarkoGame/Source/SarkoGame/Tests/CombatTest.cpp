#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidSettings.h"
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
	// Spec §4.2: hold past the dead zone and it keeps firing; a quick flick that
	// never crosses the dead zone still fires once on release.
	const float DeadZone = 0.35f;
	TestFalse(TEXT("a resting thumb does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D::ZeroVector, DeadZone));
	TestFalse(TEXT("a re-grip inside the dead zone aims but does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.2f, 0.f), DeadZone));
	TestTrue(TEXT("past the dead zone it fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.4f, 0.f), DeadZone));
	TestTrue(TEXT("full deflection fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.f, -1.f), DeadZone));

	// The firing threshold must be HIGHER than the movement dead zone: an
	// accidental small deflection while re-gripping should aim, never shoot.
	TestTrue(TEXT("the fire dead zone is above the move dead zone"),
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
	using ESarkoReloadState = SarkoUI::ESarkoReloadState;
	TestTrue(TEXT("a full magazine is ready"),
		SarkoUI::ReloadStateFor(30, 30, false) == ESarkoReloadState::Ready);
	TestTrue(TEXT("exactly a third is still ready — the boundary belongs to ready"),
		SarkoUI::ReloadStateFor(10, 30, false) == ESarkoReloadState::Ready);
	TestTrue(TEXT("below a third is low"),
		SarkoUI::ReloadStateFor(9, 30, false) == ESarkoReloadState::Low);
	TestTrue(TEXT("empty is empty"),
		SarkoUI::ReloadStateFor(0, 30, false) == ESarkoReloadState::Empty);
	TestTrue(TEXT("reloading outranks everything, including empty"),
		SarkoUI::ReloadStateFor(0, 30, true) == ESarkoReloadState::Reloading);

	// A zero magazine size is a broken config, not a divide by zero.
	TestTrue(TEXT("a zero-size magazine does not divide by zero"),
		SarkoUI::ReloadStateFor(0, 0, false) == ESarkoReloadState::Empty);
	TestTrue(TEXT("...and a non-empty one against a broken size nags rather than lies"),
		SarkoUI::ReloadStateFor(5, 0, false) == ESarkoReloadState::Low);

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
