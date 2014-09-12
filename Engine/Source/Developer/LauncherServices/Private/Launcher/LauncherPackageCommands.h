// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Implements the launcher command arguments for staging a game build
 */
class FLauncherStageGameCommand
	: public FLauncherUATCommand
{
public:
	FLauncherStageGameCommand( const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook )
		: TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
        , CookCommand(InCook)
	{
	}

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherStageTaskName", "Staging content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherStageTaskDesc", "Staging content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString CommandLine;
		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() == 1)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}
		CommandLine = FString::Printf(TEXT(" -stage"));
        
		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");
        
		// Determine if we should force PAK for the current configuration+platform+cook mode
		if (ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook)
		{
			if (ChainState.Profile->IsPackingWithUnrealPak() || (TargetPlatform.PlatformName() == TEXT("LinuxServer")) || (TargetPlatform.PlatformName() == TEXT("PS4")) || TargetPlatform.PlatformName().StartsWith(TEXT("Android")))
			{
				CommandLine += TEXT(" -pak");
			}
		}

        CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
										*InitialMap);
        
		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(true),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *CookCommand->GetAdditionalArguments(ChainState) : TEXT(""),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

private:

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};


/**
 * Implements the launcher command arguments for staging a server build
 */
class FLauncherStageServerCommand
	: public FLauncherUATCommand
{
public:
	FLauncherStageServerCommand( const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook )
		: TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
	{
	}

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherStageTaskName", "Staging content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherStageTaskDesc", "Staging content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		FString CommandLine;
		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() == 1)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}

		FString Platform = TEXT("Win64");
		if (TargetPlatform.PlatformName() == TEXT("LinuxServer") || TargetPlatform.PlatformName() == TEXT("LinuxNoEditor") || TargetPlatform.PlatformName() == TEXT("Linux"))
		{
			Platform = TEXT("Linux");
		}
		else if (TargetPlatform.PlatformName() == TEXT("WindowsServer") || TargetPlatform.PlatformName() == TEXT("WindowsNoEditor") || TargetPlatform.PlatformName() == TEXT("Windows"))
		{
			Platform = TEXT("Win64");
		}
		CommandLine = FString::Printf(TEXT(" -noclient -server -skipcook -stage -serverplatform=%s"),
			*Platform);

		if (ChainState.Profile->GetDefaultLaunchRole()->GetInstanceType() == ELauncherProfileRoleInstanceTypes::DedicatedServer)
		{
			CommandLine += " -dedicatedserver";
		}

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");
 
		// if it is PS4 or Android and cook by the book, make sure to add -pak
		if ((TargetPlatform.PlatformName() == TEXT("PS4") || TargetPlatform.PlatformName() == TEXT("LinuxServer") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook)
		{
			CommandLine += TEXT(" -pak");
		}

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(true),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

private:

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};


/**
 * Implements the launcher command arguments for packaging a game build
 */
class FLauncherPackageGameCommand
	: public FLauncherUATCommand
{
public:
	FLauncherPackageGameCommand( const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook )
		: TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
	{
	}

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherPackageTaskName", "Packaging content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherPackageTaskDesc", "Packaging content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString CommandLine;
		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() == 1)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}
		CommandLine = FString::Printf(TEXT(" -stage -package"));

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");
 
		// if it is PS4 or Android and cook by the book, make sure to add -pak
		if ((TargetPlatform.PlatformName() == TEXT("PS4") || TargetPlatform.PlatformName() == TEXT("Android") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook)
		{
			CommandLine += TEXT(" -pak");
		}

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		auto PackageDirectory = ChainState.Profile->GetPackageDirectory();
		if (PackageDirectory.Len() > 0)
		{
			CommandLine += FString::Printf(TEXT(" -stagingdirectory=\"%s\""), *PackageDirectory);
		}

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(true),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

	virtual bool PreExecute(FLauncherTaskChainState& ChainState) override
	{
		// disable the device check
		const_cast<ITargetPlatform&>(TargetPlatform).EnableDeviceCheck(false);
		return true;
	}

	virtual bool PostExecute(FLauncherTaskChainState& ChainState) override
	{
		// disable the device check
		const_cast<ITargetPlatform&>(TargetPlatform).EnableDeviceCheck(true);
		return true;
	}

private:

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};


/**
 * Implements the launcher command arguments for packaging a server build
 */
class FLauncherPackageServerCommand
	: public FLauncherUATCommand
{
public:
	FLauncherPackageServerCommand( const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook )
		: TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
	{
	}

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherPackageTaskName", "Packaging content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherPackageTaskDesc", "Packaging content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString CommandLine;
		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() == 1)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}

		FString Platform = TEXT("Win64");
		if (TargetPlatform.PlatformName() == TEXT("LinuxServer") || TargetPlatform.PlatformName() == TEXT("LinuxNoEditor") || TargetPlatform.PlatformName() == TEXT("Linux"))
		{
			Platform = TEXT("Linux");
		}
		else if (TargetPlatform.PlatformName() == TEXT("WindowsServer") || TargetPlatform.PlatformName() == TEXT("WindowsNoEditor") || TargetPlatform.PlatformName() == TEXT("Windows"))
		{
			Platform = TEXT("Win64");
		}
		CommandLine = FString::Printf(TEXT(" -noclient -server -skipcook -stage -package -serverplatform=%s"),
			*Platform);

		if (ChainState.Profile->GetDefaultLaunchRole()->GetInstanceType() == ELauncherProfileRoleInstanceTypes::DedicatedServer)
		{
			CommandLine += " -dedicatedserver";
		}

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");
 
		// if it is PS4 and cook by the book, make sure to add -pak
		if ((TargetPlatform.PlatformName() == TEXT("PS4") || TargetPlatform.PlatformName() == TEXT("LinuxServer") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook)
		{
			CommandLine += TEXT(" -pak");
		}

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(true),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

private:

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;
   
	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};
