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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLayoutIsSeedPortable,
	"Sarko.Map.LayoutIsSeedPortable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLayoutIsSeedPortable::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// Two independent BuildLayout calls from the same seed stand in for two
	// separate machines (server and a client) each generating the map locally
	// instead of receiving it over the network. If any array in the layout
	// ever drifted between them, players would be placed differently on
	// different machines — this pins the property the whole networking fix
	// depends on.
	const FSarkoMapLayout A = SarkoMap::BuildLayout(2026, Settings);
	const FSarkoMapLayout B = SarkoMap::BuildLayout(2026, Settings);

	TestEqual(TEXT("same seed gives the same cover count"), A.Cover.Num(), B.Cover.Num());
	TestEqual(TEXT("same seed gives the same player start count"), A.PlayerStarts.Num(), B.PlayerStarts.Num());
	TestEqual(TEXT("same seed gives the same enemy spawn count"), A.EnemySpawns.Num(), B.EnemySpawns.Num());

	for (int32 Index = 0; Index < FMath::Min(A.Cover.Num(), B.Cover.Num()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("cover block %d location matches"), Index),
			A.Cover[Index].Location.Equals(B.Cover[Index].Location, 0.01f));
		TestTrue(FString::Printf(TEXT("cover block %d rotation matches"), Index),
			A.Cover[Index].Rotation.Equals(B.Cover[Index].Rotation, 0.01f));
		TestTrue(FString::Printf(TEXT("cover block %d extent matches"), Index),
			A.Cover[Index].Extent.Equals(B.Cover[Index].Extent, 0.01f));
	}

	for (int32 Index = 0; Index < FMath::Min(A.PlayerStarts.Num(), B.PlayerStarts.Num()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("player start %d matches"), Index),
			A.PlayerStarts[Index].Equals(B.PlayerStarts[Index], 0.01f));
	}

	for (int32 Index = 0; Index < FMath::Min(A.EnemySpawns.Num(), B.EnemySpawns.Num()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("enemy spawn %d matches"), Index),
			A.EnemySpawns[Index].Equals(B.EnemySpawns[Index], 0.01f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSpawnPointsClearCoverEvenWhenCrowded,
	"Sarko.Map.SpawnPointsClearCoverEvenWhenCrowded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSpawnPointsClearCoverEvenWhenCrowded::RunTest(const FString& Parameters)
{
	// A copied settings object, not the project default: cranking CoverCount
	// up against a small MapExtent is the only way to reach PickClearPoint's
	// rejection-sampling fallback deliberately, since the helper itself is a
	// private implementation detail of SarkoMapBuilder.cpp with no test seam.
	// Each cover block's footprint-plus-clearance radius (>=750uu) already
	// exceeds this map's half-extent, so with 30 of them packed into a
	// 1600x1600uu square, naive 64-attempt rejection sampling is expected to
	// exhaust on effectively every spawn point.
	USarkoRaidSettings* CrowdedSettings = NewObject<USarkoRaidSettings>();
	CrowdedSettings->MapExtent = 800.f;
	CrowdedSettings->CoverCount = 30;

	const FSarkoMapLayout Layout = SarkoMap::BuildLayout(555, *CrowdedSettings);

	TestTrue(TEXT("there is somewhere to spawn the player"), Layout.PlayerStarts.Num() > 0);

	// This is the property PickClearPoint's fallback must guarantee, not just
	// make likely: even when rejection sampling is exhausted, no player start
	// may end up literally inside a cover block's footprint.
	for (const FVector& Start : Layout.PlayerStarts)
	{
		for (const FSarkoCoverBlock& Block : Layout.Cover)
		{
			const float PlanarDistance = FVector::Dist2D(Start, Block.Location);
			TestTrue(TEXT("no spawn point sits inside a cover block, even under a crowded configuration"),
				PlanarDistance > Block.Extent.GetMax());
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
