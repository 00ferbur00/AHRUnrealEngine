// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class ScriptPlugin : ModuleRules
	{
		public ScriptPlugin(TargetInfo Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {					
					//"Programs/UnrealHeaderTool/Public",
					// ... add other public include paths required here ...
				}
				);

			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					// ... add other public dependencies that you statically link with here ...
				}
				);

			if (UEBuildConfiguration.bBuildEditor == true)
			{

				PublicDependencyModuleNames.AddRange(
					new string[] 
					{
						"UnrealEd", 
					}
				);

			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					// ... add private dependencies that you statically link with here ...
				}
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);

			if (!UnrealBuildTool.BuildingRocket())
			{
				var LuaPath = Path.Combine("..", "Plugins", "Script", "ScriptPlugin", "Source", "ScriptPlugin", "lua-5.2.3");
				if (Directory.Exists(LuaPath))
				{
					Definitions.Add("WITH_LUA=1");
					PrivateIncludePaths.Add(Path.Combine("ScriptPlugin", "lua-5.2.3", "src"));
				}
			}
		}
	}
}