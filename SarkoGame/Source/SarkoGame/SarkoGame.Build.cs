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
			"HTTP",
			// The shelter menu is Slate built in C++ (SNew), which is code rather
			// than an asset — UMG widgets are .uasset and this project ships none.
			// Both are already reachable transitively through Engine's public
			// dependencies; listing them means an Engine change cannot silently
			// take them away.
			"Slate",
			"SlateCore"
		});
	}
}
