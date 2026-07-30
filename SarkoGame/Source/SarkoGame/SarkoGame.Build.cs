using UnrealBuildTool;

public class SarkoGame : ModuleRules
{
	public SarkoGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Flat module layout (no Public/ folder): the module root must be added
		// explicitly so quoted includes like "Core/SarkoRaidSettings.h" resolve.
		// With DefaultBuildSettings = V7, bLegacyPublicIncludePaths defaults to
		// false, so UBT no longer adds the module root to the include path itself.
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NavigationSystem",
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"HTTP"
		});
	}
}
