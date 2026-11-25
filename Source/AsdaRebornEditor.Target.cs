// Copyright (c) 2026 @sitxovski (Discord)
// AsdaReborn — editor target (includes asset importers).
using UnrealBuildTool;
using System.Collections.Generic;

public class AsdaRebornEditorTarget : TargetRules
{
	public AsdaRebornEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "AsdaCore", "AsdaFormats", "AsdaNet", "AsdaReborn", "AsdaImporters" });
	}
}
