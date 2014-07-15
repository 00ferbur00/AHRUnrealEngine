// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlatformInfo.h"


/**
 * class for UAT launcher tasks.
 */
static const FString ConfigStrings[] = { TEXT("Unknown"), TEXT("Debug"), TEXT("DebugGame"), TEXT("Development"), TEXT("Shipping"), TEXT("Test") };

class FLauncherUATTask
	: public FLauncherTask
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InCommand - the command used for the task
	 * @param InTargetPlatform - the target platform for the task
	 * @param InName - The name of the task.
	 */
	FLauncherUATTask( const TSharedPtr<FLauncherUATCommand>& InCommand, const ITargetPlatform& InTargetPlatform, const FString& InName, void* InReadPipe, void* InWritePipe, const FString& InEditorExe )
		: FLauncherTask(InName, InCommand->GetDesc(), InReadPipe, InWritePipe)
		, TaskCommand(InCommand)
		, TargetPlatform(InTargetPlatform)
		, EditorExe(InEditorExe)
	{
		NoCompile = TEXT(" -nocompile");
	}

	static bool FirstTimeCompile;
protected:

	/**
	 * Performs the actual task.
	 *
	 * @param ChainState - Holds the state of the task chain.
	 *
	 * @return true if the task completed successfully, false otherwise.
	 */
	virtual bool PerformTask( FLauncherTaskChainState& ChainState ) override
	{
		if (FirstTimeCompile)
		{
			NoCompile = !FParse::Param( FCommandLine::Get(), TEXT("development") ) ? TEXT(" -nocompile") : TEXT("");
			FirstTimeCompile = false;
		}

		// spawn a UAT process to cook the data
		// UAT executable
		FString ExecutablePath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() + FString(TEXT("Build")) / TEXT("BatchFiles"));
#if PLATFORM_MAC
		FString Executable = TEXT("RunUAT.command");
#elif PLATFORM_LINUX
		FString Executable = TEXT("RunUAT.sh");
#else
		FString Executable = TEXT("RunUAT.bat");
#endif

		const PlatformInfo::FPlatformInfo& PlatformInfo = TargetPlatform.GetPlatformInfo();

		// switch server and no editor platforms to the proper type
		FName PlatformName = PlatformInfo.TargetPlatformName;
		if (PlatformName == FName("LinuxServer") || PlatformName == FName("LinuxNoEditor"))
		{
			PlatformName = FName("Linux");
		}
		else if (PlatformName == FName("WindowsServer") || PlatformName == FName("WindowsNoEditor") || PlatformName == FName("Windows"))
		{
			PlatformName = FName("Win64");
		}
        else if (PlatformName == FName("MacNoEditor"))
        {
            PlatformName = FName("Mac");
        }

		// Append any extra UAT flags specified for this platform flavor
		FString OptionalParams;
		if (!PlatformInfo.UATCommandLine.IsEmpty())
		{
			OptionalParams += TEXT(" ");
			OptionalParams += PlatformInfo.UATCommandLine;
		}

        // check for rocket
        FString Rocket;
        if ( FRocketSupport::IsRocket() )
        {
            Rocket = TEXT(" -rocket" );
        }
        
		// base UAT command arguments
		FString CommandLine;
		FString ProjectPath = *ChainState.Profile->GetProjectPath();
		ProjectPath = FPaths::ConvertRelativePathToFull(ProjectPath);
		CommandLine = FString::Printf(TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=%s -clientconfig=%s -serverconfig=%s"),
			*ProjectPath,
			*PlatformName.ToString(),
			*ConfigStrings[ChainState.Profile->GetBuildConfiguration()],
			*ConfigStrings[ChainState.Profile->GetBuildConfiguration()]);

		CommandLine += NoCompile;
        CommandLine += Rocket;
		CommandLine += OptionalParams;

		// specify the path to the editor exe if necessary
		if(EditorExe.Len() > 0)
		{
			CommandLine += FString::Printf(TEXT(" -ue4exe=\"%s\""), *EditorExe);
		}

		// specialized command arguments for this particular task
		CommandLine += TaskCommand->GetArguments(ChainState);

		// pre-execute any command
		if (!TaskCommand->PreExecute(ChainState))
		{
			return false;
		}

		// launch UAT and monitor its progress
		FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*(ExecutablePath / Executable), *CommandLine, false, true, true, NULL, 0, *ExecutablePath, WritePipe);

		while (!TaskCommand->IsComplete() || FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			if (GetStatus() == ELauncherTaskStatus::Canceling)
			{
				FPlatformProcess::TerminateProc(ProcessHandle, true);
				return false;
			}

			FPlatformProcess::Sleep(0.25);
		}

		int32 ReturnCode;
		if (!FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode))
		{
			return false;
		}

		if (!TaskCommand->PostExecute(ChainState))
		{
			return false;
		}

		return (ReturnCode == 0);
	}

private:

	// command for this task
	const TSharedPtr<FLauncherUATCommand> TaskCommand;

	// Holds a pointer to the target platform.
	const ITargetPlatform& TargetPlatform;

	// Holds the no compile flag
	FString NoCompile;

	// The editor executable that UAT should use
	FString EditorExe;
};
