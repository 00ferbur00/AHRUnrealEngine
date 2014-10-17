// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE4Game : ModuleRules
{
	public UE4Game(TargetInfo Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Landscape");
	
		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OnlineSubsystem", "OnlineSubsystemUtils" });
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemFacebook");
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemIOS");
			DynamicallyLoadedModuleNames.Add("IOSAdvertising");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OnlineSubsystem", "OnlineSubsystemUtils" });
			DynamicallyLoadedModuleNames.Add("AndroidAdvertising");
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemGooglePlay");
		}
	}
}
