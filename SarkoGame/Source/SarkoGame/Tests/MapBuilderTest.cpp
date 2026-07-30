#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapBuilder.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSettingsHaveSaneDefaults,
	"Sarko.Config.SettingsHaveSaneDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSettingsHaveSaneDefaults::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings singleton resolves"), Settings);

	TestTrue(TEXT("raid lasts a positive number of seconds"), Settings->RaidDurationSeconds > 0.f);
	TestTrue(TEXT("map has a positive extent"), Settings->MapExtent > 0.f);
	TestTrue(TEXT("cover count is positive"), Settings->CoverCount > 0);
	TestTrue(TEXT("walk speed is positive"), Settings->WalkSpeed > 0.f);
	TestTrue(TEXT("weapon range is shorter than the map"), Settings->WeaponRangeUU < Settings->MapExtent);
	TestTrue(TEXT("weapon damage is positive and not instant death"), Settings->WeaponDamage > 0.f && Settings->WeaponDamage < 100.f);
	TestTrue(TEXT("aim assist is a nudge, not an aimbot"), Settings->AimConeHalfAngleDegrees > 0.f && Settings->AimConeHalfAngleDegrees <= 10.f);
	TestTrue(TEXT("magazine holds at least one round"), Settings->MagazineSize > 0);
	TestTrue(TEXT("reload time is positive and tactical"), Settings->ReloadSeconds > 0.f && Settings->ReloadSeconds < 10.f);
	TestTrue(TEXT("enemy hearing radius is positive"), Settings->EnemyHearingRadiusUU > 0.f);
	TestTrue(TEXT("enemy fire interval is positive"), Settings->EnemyFireIntervalSeconds > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLayoutIsDeterministic,
	"Sarko.Map.LayoutIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLayoutIsDeterministic::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	const FSarkoMapLayout A = SarkoMap::BuildLayout(4242, Settings);
	const FSarkoMapLayout B = SarkoMap::BuildLayout(4242, Settings);
	const FSarkoMapLayout Other = SarkoMap::BuildLayout(9999, Settings);

	TestEqual(TEXT("same seed gives the same cover count"), A.Cover.Num(), B.Cover.Num());
	if (A.Cover.Num() == B.Cover.Num() && A.Cover.Num() > 0)
	{
		bool bIdentical = true;
		for (int32 i = 0; i < A.Cover.Num(); ++i)
		{
			bIdentical &= A.Cover[i].Location.Equals(B.Cover[i].Location, 0.01f);
		}
		TestTrue(TEXT("same seed places cover identically"), bIdentical);
	}

	TestNotEqual(TEXT("a different seed gives a different first block"),
		A.Cover[0].Location.X, Other.Cover[0].Location.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLayoutRespectsBounds,
	"Sarko.Map.LayoutRespectsBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLayoutRespectsBounds::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FSarkoMapLayout Layout = SarkoMap::BuildLayout(7, Settings);

	TestEqual(TEXT("cover count matches the setting"), Layout.Cover.Num(), Settings.CoverCount);
	TestTrue(TEXT("there is somewhere to spawn the player"), Layout.PlayerStarts.Num() > 0);
	TestTrue(TEXT("there is somewhere to spawn an enemy"), Layout.EnemySpawns.Num() > 0);

	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		TestTrue(TEXT("cover stays inside the play area"),
			FMath::Abs(Block.Location.X) <= Settings.MapExtent && FMath::Abs(Block.Location.Y) <= Settings.MapExtent);
	}

	// A player who spawns inside a wall is the classic procedural-map bug.
	for (const FVector& Start : Layout.PlayerStarts)
	{
		for (const FSarkoCoverBlock& Block : Layout.Cover)
		{
			const float PlanarDistance = FVector::Dist2D(Start, Block.Location);
			TestTrue(TEXT("no spawn point sits inside a cover block"),
				PlanarDistance > Block.Extent.GetMax());
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
