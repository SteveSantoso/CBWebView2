// Copyright 2026-Present SteveSantoso. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

// WebView2Utils is the plugin's native integration layer for Win32, WinComp, and WebView2 Runtime functionality.
public class WebView2Utils : ModuleRules
{
	public WebView2Utils(ReadOnlyTargetRules Target) : base(Target)
	{
		// Use C++20 and explicit PCHs to keep WinRT and WebView2 headers manageable.
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseUnity = false;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;

		// The plugin does not use C++/WinRT coroutines, so disable them explicitly to avoid fallback to experimental/coroutine on older MSVC toolchains.
		PublicDefinitions.Add("WINRT_NO_COROUTINES=1");

		// Public dependencies consumed by outer modules and public headers.
		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"InputCore",
				"UMG",
				"WebView2",
				"DeveloperSettings"
			});

		// Private dependencies used only within this module implementation.
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"ApplicationCore",
				"Projects",
				"Json",
				"RHI",
				"RenderCore",
				"D3D11RHI",
				"D3D12RHI"
			});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// d3dx12.h include paths required by ID3D12DynamicRHI.h.
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");

			PublicSystemLibraries.Add("D3D11.lib");
			PublicSystemLibraries.Add("D3D12.lib");
			PublicSystemLibraries.Add("DXGI.lib");
			PublicSystemLibraries.Add("Imm32.lib");
			PublicSystemLibraries.Add("Dwmapi.lib");
			PublicSystemLibraries.Add("WindowsApp.lib");
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Resources", "transparency_check.js"), StagedFileType.NonUFS);
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Resources", "input_event_bridge.js"), StagedFileType.NonUFS);

			// WinRT headers come from the system SDK.
			PublicIncludePaths.Add(Path.Combine(
				Target.WindowsPlatform.WindowsSdkDir,
				"Include",
				Target.WindowsPlatform.WindowsSdkVersion,
				"cppwinrt"));

			PublicIncludePaths.Add(Path.Combine(
				Target.WindowsPlatform.WindowsSdkDir,
				"Include",
				Target.WindowsPlatform.WindowsSdkVersion,
				"winrt"));
		}
	}
}
