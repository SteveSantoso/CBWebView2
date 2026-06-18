// Copyright 2026-Present SteveSantoso. All Rights Reserved.
using UnrealBuildTool;

// CBWebView2 is the public-facing UMG / Slate module for the plugin.
// It only declares dependencies and does not own the native WebView2 integration details.
public class CBWebView2 : ModuleRules
{
	public CBWebView2(ReadOnlyTargetRules Target) : base(Target)
	{
		// Use explicit or shared PCHs to match standard UE module conventions.
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseUnity = false;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;

		// Dependencies exposed to this module's public headers.
		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"UMG",
				"WebView2Utils"
			});

		// Dependencies used only by this module implementation.
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"ApplicationCore",
				"Json"
			});
	}
}