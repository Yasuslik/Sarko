using UnrealBuildTool;

public class SarkoGameEditorTarget : TargetRules
{
	public SarkoGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SarkoGame");
	}
}
