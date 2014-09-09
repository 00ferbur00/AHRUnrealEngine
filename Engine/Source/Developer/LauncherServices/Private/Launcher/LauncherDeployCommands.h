// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Implements the launcher command arguments for deploying a game to a device
 */
class FLauncherDeployGameToDeviceCommand
	: public FLauncherUATCommand
{
public:
	FLauncherDeployGameToDeviceCommand(const ITargetDeviceProxyRef& InDeviceProxy, FName InFlavor, const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook, const FString& InCmdLine)
		: DeviceProxy(InDeviceProxy)
		, Flavor(InFlavor)
		, TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
		, LauncherCommandLine(InCmdLine)
	{ }

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskName", "Deploying content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskDesc", "Deploying content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString StagePath;
		if (FPaths::IsRelative(ChainState.Profile->GetProjectBasePath()))
		{
			StagePath = FPaths::ConvertRelativePathToFull(FString(TEXT("../../../"))) / ChainState.Profile->GetProjectBasePath();
		}
		else
		{
			StagePath = ChainState.Profile->GetProjectBasePath();
		}
		StagePath = StagePath / FString(TEXT("Saved/StagedBuilds"));

		// build UAT command line parameters
		FString CommandLine;

		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() > 0)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}

		CommandLine = FString::Printf(TEXT(" -deploy -skipstage -stagingdirectory=\"%s\" -cmdline=\"%s -InstanceName=\'Deployer (%s)\' -Messaging\""),
			*StagePath,
			*InitialMap,
			*TargetPlatform.PlatformName());
		
#if !PLATFORM_MAC
		if (TargetPlatform.PlatformName() != TEXT("IOS"))
#endif
		{
			CommandLine += FString::Printf(TEXT(" -device=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));
		}

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(false),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""),
			*(TEXT(" ") + LauncherCommandLine));

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

	// Holds a pointer to the device proxy to deploy to.
	ITargetDeviceProxyPtr DeviceProxy;

	// Holds the name of the flavor of Target Device to use.
	FName Flavor;

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;

	// holds the additional command line form the launcher
	FString LauncherCommandLine;
};


/**
 * Implements the launcher command arguments for deploying a server to a device
 */
class FLauncherDeployServerToDeviceCommand
	: public FLauncherUATCommand
{
public:

	FLauncherDeployServerToDeviceCommand( const ITargetDeviceProxyRef& InDeviceProxy, FName InFlavor, const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook )
		: DeviceProxy(InDeviceProxy)
		, Flavor(InFlavor)
		, TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
	{ }

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskName", "Deploying content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskDesc", "Deploying content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString StagePath = FPaths::ConvertRelativePathToFull(ChainState.Profile->GetProjectBasePath() + FString(TEXT("StagedBuilds")));

		// build UAT command line parameters
		FString CommandLine;

		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() > 0)
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
		CommandLine = FString::Printf(TEXT(" -noclient -server -deploy -skipstage -serverplatform=%s -stagingdirectory=\"%s\" -cmdline=\"%s -InstanceName=\"Deployer (%s)\" -Messaging\""),
			*Platform,
			*StagePath,
			*InitialMap,
			*TargetPlatform.PlatformName());
		CommandLine += FString::Printf(TEXT(" -device=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));
		CommandLine += FString::Printf(TEXT(" -serverdevice=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");

		if (TargetPlatform.RequiresUserCredentials())
		{
			CommandLine += FString::Printf(TEXT(" -deviceuser=%s -devicepass=%s"), *DeviceProxy->GetDeviceUser(), *DeviceProxy->GetDeviceUserPassword());
		}

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(false),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

private:

	// Holds a pointer to the device proxy to deploy to.
	ITargetDeviceProxyPtr DeviceProxy;

	// Holds the name of the flavor of Target Device to use.
	FName Flavor;

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};


/**
 * Implements the launcher command arguments for deploying a game package to a device
 */
class FLauncherDeployGamePackageToDeviceCommand
	: public FLauncherUATCommand
{
public:

	FLauncherDeployGamePackageToDeviceCommand(const ITargetDeviceProxyRef& InDeviceProxy, FName InFlavor, const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook, const FString& InCmdLine)
		: DeviceProxy(InDeviceProxy)
		, Flavor(InFlavor)
		, TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
		, LauncherCommandLine(InCmdLine)
	{ }

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskName", "Deploying content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskDesc", "Deploying content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString CommandLine;

		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() > 0)
		{
			InitialMap = ChainState.Profile->GetCookedMaps()[0];
		}

		CommandLine = FString::Printf(TEXT(" -deploy -skipstage -stagingdirectory=\"%s\" -cmdline=\"%s -InstanceName=\'Deployer (%s)\' -Messaging\""),
			*ChainState.Profile->GetPackageDirectory(),
			*InitialMap,
			*TargetPlatform.PlatformName());
		CommandLine += FString::Printf(TEXT(" -device=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(false),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""),
			*(TEXT(" ") + LauncherCommandLine));
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

	// Holds a pointer to the device proxy to deploy to.
	ITargetDeviceProxyPtr DeviceProxy;

	// Holds the name of the flavor of Target Device to use.
	FName Flavor;

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;

	// holds the additional command line form the launcher
	FString LauncherCommandLine;
};


/**
 * Implements the launcher command arguments for deploying a server package to a device
 */
class FLauncherDeployServerPackageToDeviceCommand
	: public FLauncherUATCommand
{
public:

	FLauncherDeployServerPackageToDeviceCommand(const ITargetDeviceProxyRef& InDeviceProxy, FName InFlavor, const ITargetPlatform& InTargetPlatform, const TSharedPtr<FLauncherUATCommand>& InCook)
		: DeviceProxy(InDeviceProxy)
		, Flavor(InFlavor)
		, TargetPlatform(InTargetPlatform)
		, InstanceId(FGuid::NewGuid())
		, CookCommand(InCook)
	{ }

	virtual FString GetName() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskName", "Deploying content").ToString();
	}

	virtual FString GetDesc() const override
	{
		return NSLOCTEXT("FLauncherTask", "LauncherDeployTaskDesc", "Deploying content for ").ToString() + TargetPlatform.PlatformName();
	}

	virtual FString GetArguments(FLauncherTaskChainState& ChainState) const override
	{
		// build UAT command line parameters
		FString CommandLine;

		FString InitialMap = ChainState.Profile->GetDefaultLaunchRole()->GetInitialMap();
		if (InitialMap.IsEmpty() && ChainState.Profile->GetCookedMaps().Num() > 0)
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
		CommandLine = FString::Printf(TEXT(" -noclient -server -deploy -skipstage -serverplatform=%s -stagingdirectory=\"%s\" -cmdline=\"%s -InstanceName=\"Deployer (%s)\" -Messaging\""),
			*Platform,
			*ChainState.Profile->GetPackageDirectory(),
			*InitialMap,
			*TargetPlatform.PlatformName());
		CommandLine += FString::Printf(TEXT(" -device=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));
		CommandLine += FString::Printf(TEXT(" -serverdevice=\"%s\""), *DeviceProxy->GetTargetDeviceId(Flavor));

		// cook dependency arguments
		CommandLine += CookCommand.IsValid() ? CookCommand->GetDependencyArguments(ChainState) : TEXT(" -skipcook");

		if (TargetPlatform.RequiresUserCredentials())
		{
			CommandLine += FString::Printf(TEXT(" -deviceuser=%s -devicepass=%s"), *DeviceProxy->GetDeviceUser(), *DeviceProxy->GetDeviceUserPassword());
		}

		CommandLine += FString::Printf(TEXT(" -cmdline=\"%s -Messaging\""),
			*InitialMap);

		CommandLine += FString::Printf(TEXT(" -addcmdline=\"%s -InstanceId=%s -SessionId=%s -SessionOwner=%s -SessionName='%s'%s%s%s\""),
			*InitialMap,
			*InstanceId.ToString(),
			*ChainState.SessionId.ToString(),
			FPlatformProcess::UserName(false),
			*ChainState.Profile->GetName(),
			CookCommand.IsValid() ? *(TEXT(" ") + CookCommand->GetAdditionalArguments(ChainState)) : TEXT(""),
			((TargetPlatform.PlatformName() == TEXT("PS4") || ChainState.Profile->IsPackingWithUnrealPak())
			&& ChainState.Profile->GetCookMode() == ELauncherProfileCookModes::ByTheBook) ? TEXT(" -pak") : TEXT(""),
			ChainState.Profile->GetLaunchRoles().Num() > 0 ? (ChainState.Profile->GetLaunchRoles()[0]->IsVsyncEnabled() ? TEXT(" -vsync") : TEXT("")) : TEXT(""));

		return CommandLine;
	}

private:

	// Holds a pointer to the device proxy to deploy to.
	ITargetDeviceProxyPtr DeviceProxy;

	// Holds the name of the flavor of Target Device to use.
	FName Flavor;

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the identifier of the launched instance.
	FGuid InstanceId;

	// cook command used for this build
	const TSharedPtr<FLauncherUATCommand> CookCommand;
};
