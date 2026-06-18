// Copyright 2026-Present SteveSantoso. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

// The external WebView2 module only wires the official SDK headers, libraries, and loader DLL into UE's build system.
public class WebView2 : ModuleRules
{
    protected readonly string ViewVersion = "Microsoft.Web.WebView2.1.0.2895-prerelease";
    
    public WebView2(ReadOnlyTargetRules Target) : base(Target)
    {
        // This module does not build source targets; it only declares third-party libraries.
        Type = ModuleType.External;
        
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Expose the official SDK headers and link libraries.
            PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory,ViewVersion,"include"));
            string LibPath = Path.Combine(ModuleDirectory, ViewVersion, "lib");
            
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "WebView2Loader.dll.lib"));
			PublicDelayLoadDLLs.Add("WebView2Loader.dll");

			string SrcWebView2LoaderDll = Path.Combine(ModuleDirectory, "Dependency", "WebView2Loader.dll");
			RuntimeDependencies.Add("$(TargetOutputDir)/WebView2Loader.dll", SrcWebView2LoaderDll);
        }
    }
}