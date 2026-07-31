#include "Misc/AutomationTest.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Pawn/SarkoBody.h"
#include "Pawn/SarkoCharacterAnim.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAnimStatePriority,
	"Sarko.Visuals.AnimStatePriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAnimStatePriority::RunTest(const FString& Parameters)
{
	using namespace SarkoAnimation;

	// Death outranks everything. A corpse that is seen reloading, running or
	// standing up into an idle is the single worst-looking bug in this feature,
	// and it is exactly what a naive "whichever changed last" would produce.
	TestEqual(TEXT("a dead pawn is dead even mid-reload"),
		static_cast<int32>(ChooseState(/*bDead*/ true, /*bReloading*/ true, /*bFiring*/ true, /*bMoving*/ true, /*bAiming*/ true)),
		static_cast<int32>(ESarkoAnimState::Death));

	TestEqual(TEXT("reloading outranks a shot and movement"),
		static_cast<int32>(ChooseState(false, true, true, true, true)), static_cast<int32>(ESarkoAnimState::Reload));

	// A shot only shows while standing: with no montage there is no upper-body
	// layer, so a firing pose replaces the run cycle outright.
	TestEqual(TEXT("a standing pawn shows the shot"),
		static_cast<int32>(ChooseState(false, false, true, false, true)), static_cast<int32>(ESarkoAnimState::Fire));
	TestEqual(TEXT("a running pawn keeps running through a shot"),
		static_cast<int32>(ChooseState(false, false, true, true, true)), static_cast<int32>(ESarkoAnimState::Jog));

	TestEqual(TEXT("movement without a shot jogs"),
		static_cast<int32>(ChooseState(false, false, false, true, false)), static_cast<int32>(ESarkoAnimState::Jog));
	TestEqual(TEXT("standing still while aiming shows the aim pose"),
		static_cast<int32>(ChooseState(false, false, false, false, true)), static_cast<int32>(ESarkoAnimState::IdleAiming));
	TestEqual(TEXT("standing still without aiming idles"),
		static_cast<int32>(ChooseState(false, false, false, false, false)), static_cast<int32>(ESarkoAnimState::Idle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEightWayDirection,
	"Sarko.Visuals.EightWayDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEightWayDirection::RunTest(const FString& Parameters)
{
	using namespace SarkoAnimation;

	// Facing +X and travelling +X is straight ahead. Get this wrong and every
	// pawn in the game runs its backwards cycle forwards.
	TestEqual(TEXT("travelling along the facing is forward"),
		EightWayIndex(RelativeYaw(FVector(400.f, 0.f, 0.f), 0.f)), 0);

	// Facing +X, travelling +Y is a strafe to the *right*: a positive yaw turns
	// clockwise seen from above, which is the only view this game has.
	TestEqual(TEXT("travelling 90 degrees clockwise of the facing is a right strafe"),
		EightWayIndex(RelativeYaw(FVector(0.f, 400.f, 0.f), 0.f)), 2);
	TestEqual(TEXT("travelling 90 degrees anticlockwise of the facing is a left strafe"),
		EightWayIndex(RelativeYaw(FVector(0.f, -400.f, 0.f), 0.f)), 6);
	TestEqual(TEXT("travelling opposite the facing is backwards"),
		EightWayIndex(RelativeYaw(FVector(-400.f, 0.f, 0.f), 0.f)), 4);
	TestEqual(TEXT("forward-right is the diagonal between them"),
		EightWayIndex(RelativeYaw(FVector(400.f, 400.f, 0.f), 0.f)), 1);

	// The pawn's own rotation has to be subtracted out, or an enemy that has
	// turned to chase would pick its animation from world axes.
	TestEqual(TEXT("a rotated pawn travelling along its own facing is still forward"),
		EightWayIndex(RelativeYaw(FVector(0.f, 400.f, 0.f), 90.f)), 0);

	// Vertical velocity must not leak into the choice; a pawn stepping off a
	// crate is not strafing.
	TestEqual(TEXT("vertical velocity is ignored"),
		EightWayIndex(RelativeYaw(FVector(400.f, 0.f, -900.f), 0.f)), 0);

	// Every index the jog table is indexed with must be in range.
	for (int32 Degrees = -180; Degrees <= 180; Degrees += 7)
	{
		const int32 Index = EightWayIndex(static_cast<float>(Degrees));
		TestTrue(TEXT("every angle maps inside the directional set"), Index >= 0 && Index < EightWayCount);
	}

	// A stationary pawn must not extract a direction from noise.
	TestEqual(TEXT("a still pawn has no travel direction"),
		RelativeYaw(FVector(0.f, 0.f, 0.f), 137.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoReloadAnimationFillsTheReload,
	"Sarko.Visuals.ReloadAnimationFillsTheReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReloadAnimationFillsTheReload::RunTest(const FString& Parameters)
{
	using namespace SarkoAnimation;

	// A 1.1 s sequence stretched over a 2.2 s reload plays at half speed, so the
	// animation ends when the ammo counter refills instead of freezing halfway.
	TestEqual(TEXT("a short sequence is slowed to fill the reload"),
		PlayRateForDuration(1.1f, 2.2f), 0.5f, 0.001f);
	TestEqual(TEXT("a long sequence is sped up to fit"),
		PlayRateForDuration(4.4f, 2.2f), 2.f, 0.001f);
	TestEqual(TEXT("a matching sequence plays at normal speed"),
		PlayRateForDuration(2.2f, 2.2f), 1.f, 0.001f);

	// Degenerate inputs must not produce a zero, negative or infinite play rate:
	// a zero rate is a frozen pawn and a negative one runs the reload backwards.
	TestEqual(TEXT("a zero-length sequence falls back to normal speed"),
		PlayRateForDuration(0.f, 2.2f), 1.f, 0.001f);
	TestEqual(TEXT("a zero target falls back to normal speed"),
		PlayRateForDuration(2.2f, 0.f), 1.f, 0.001f);
	TestTrue(TEXT("an absurd ratio is clamped, never infinite"),
		PlayRateForDuration(1000.f, 0.001f) <= 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDeathVariantIsStablePerPawn,
	"Sarko.Visuals.DeathVariantIsStablePerPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDeathVariantIsStablePerPawn::RunTest(const FString& Parameters)
{
	TSet<int32> Seen;
	for (uint32 Id = 0; Id < 64; ++Id)
	{
		const int32 Variant = SarkoAnimation::DeathVariantForPawn(Id);
		TestTrue(TEXT("the variant is always a real sequence"), Variant >= 0 && Variant < 3);
		// Same pawn, same pose, every time it is asked — a corpse must not
		// re-roll its own death.
		TestEqual(TEXT("the choice is deterministic"), SarkoAnimation::DeathVariantForPawn(Id), Variant);
		Seen.Add(Variant);
	}
	TestTrue(TEXT("more than one death pose is actually reachable"), Seen.Num() > 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCharacterAssetsResolve,
	"Sarko.Visuals.CharacterAssetsResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCharacterAssetsResolve::RunTest(const FString& Parameters)
{
	// Every asset is reached by a literal string path, so a moved, renamed or
	// missing file is a runtime "the character is invisible" and nothing else.
	// This is the only part of the feature -nullrhi can check: it cannot see
	// rendering, but it can prove the objects load.
	const TArray<FString> Paths = SarkoAnimation::AllAssetPaths();
	TestTrue(TEXT("there is an asset list to check at all"), Paths.Num() > 10);

	for (const FString& Path : Paths)
	{
		UObject* Loaded = LoadObject<UObject>(nullptr, *Path);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' loads"), *Path), Loaded))
		{
			continue;
		}

		if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Loaded))
		{
			// Single-node mode plays one full-body sequence. An *additive*
			// sequence describes an offset from a base pose and only means
			// anything inside an AnimInstance graph; played on its own it
			// deforms the mesh from the reference pose and looks like a bug.
			// MM_Pistol_Fire is exactly such an asset, which is why it is not
			// in this list — this assertion is what stops it being added back.
			TestEqual(*FString::Printf(TEXT("'%s' is a full-body sequence, not additive"), *Path),
				static_cast<int32>(Sequence->AdditiveAnimType), static_cast<int32>(AAT_None));

			// A sequence authored for another skeleton silently plays as a
			// reference pose — the T-pose failure this whole feature exists to
			// avoid.
			TestNotNull(*FString::Printf(TEXT("'%s' has a skeleton"), *Path), Sequence->GetSkeleton());
		}
	}

	// The meshes and the animations must agree on a skeleton, or every pose is a
	// no-op. Checked explicitly rather than trusting that they came from one
	// upstream project, because they were copied in by hand.
	const USkeletalMesh* PlayerMesh = LoadObject<USkeletalMesh>(nullptr, SarkoBody::MeshPathForSide(SarkoBody::ESide::Player));
	const USkeletalMesh* EnemyMesh = LoadObject<USkeletalMesh>(nullptr, SarkoBody::MeshPathForSide(SarkoBody::ESide::Enemy));
	const UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));

	if (PlayerMesh && EnemyMesh && Idle)
	{
		TestTrue(TEXT("friend and foe are two different meshes"), PlayerMesh != EnemyMesh);
		TestTrue(TEXT("the player's mesh shares the animations' skeleton"), PlayerMesh->GetSkeleton() == Idle->GetSkeleton());
		TestTrue(TEXT("the enemy's mesh shares the animations' skeleton"), EnemyMesh->GetSkeleton() == Idle->GetSkeleton());

		// A mesh whose materials failed to resolve renders as the default
		// checkerboard or plain white — "нету текстур" all over again. The
		// copied assets reference their materials by a path that no longer
		// exists, so this is the assertion that proves the redirect in
		// DefaultEngine.ini is doing its job.
		TestTrue(TEXT("the player's mesh has at least one material"), PlayerMesh->GetMaterials().Num() > 0);
		for (const FSkeletalMaterial& Material : PlayerMesh->GetMaterials())
		{
			TestNotNull(TEXT("every player material slot resolved to a real material"), Material.MaterialInterface.Get());
		}
		for (const FSkeletalMaterial& Material : EnemyMesh->GetMaterials())
		{
			TestNotNull(TEXT("every enemy material slot resolved to a real material"), Material.MaterialInterface.Get());
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
