// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "LauncherServicesPrivatePCH.h"


bool FLauncherUATTask::FirstTimeCompile = true;

/* FLauncherWorker structors
 *****************************************************************************/

FLauncherWorker::FLauncherWorker( const ITargetDeviceProxyManagerRef& InDeviceProxyManager, const ILauncherProfileRef& InProfile )
	: DeviceProxyManager(InDeviceProxyManager)
	, Profile(InProfile)
	, Status(ELauncherWorkerStatus::Busy)
{
	CreateAndExecuteTasks(InProfile);
}


/* FRunnable overrides
 *****************************************************************************/

bool FLauncherWorker::Init( )
{
	return true;
}


uint32 FLauncherWorker::Run( )
{
	FString Line;

	LaunchStartTime = FPlatformTime::Seconds();

	// wait for tasks to be completed
	while (Status == ELauncherWorkerStatus::Busy)
	{
		FPlatformProcess::Sleep(0.0f);

		FString NewLine = FPlatformProcess::ReadPipe(ReadPipe);
		if (NewLine.Len() > 0)
		{
			// process the string to break it up in to lines
			Line += NewLine;
			TArray<FString> StringArray;
			int32 count = Line.ParseIntoArray(&StringArray, TEXT("\n"), true);
			if (count > 1)
			{
				for (int32 Index = 0; Index < count-1; ++Index)
				{
					StringArray[Index].TrimTrailing();
					OutputMessageReceived.Broadcast(StringArray[Index]);
				}
                Line = StringArray[count-1];
                if (NewLine.EndsWith(TEXT("\n")))
                    Line += TEXT("\n");
			}
		}

		if (TaskChain->IsChainFinished())
		{
			Status = ELauncherWorkerStatus::Completed;

			NewLine = FPlatformProcess::ReadPipe(ReadPipe);
			while (NewLine.Len() > 0)
			{
				// process the string to break it up in to lines
				Line += NewLine;
				TArray<FString> StringArray;
				int32 count = Line.ParseIntoArray(&StringArray, TEXT("\n"), true);
				if (count > 1)
				{
					for (int32 Index = 0; Index < count-1; ++Index)
					{
						StringArray[Index].TrimTrailing();
						OutputMessageReceived.Broadcast(StringArray[Index]);
					}
                    Line = StringArray[count-1];
                    if (NewLine.EndsWith(TEXT("\n")))
                        Line += TEXT("\n");
				}

				NewLine = FPlatformProcess::ReadPipe(ReadPipe);
			}

			// fire off the last line
			OutputMessageReceived.Broadcast(Line);

		}
	}

	// wait for tasks to be canceled
	if (Status == ELauncherWorkerStatus::Canceling)
	{
		TaskChain->Cancel();

		while (!TaskChain->IsChainFinished())
		{
			FPlatformProcess::Sleep(0.0);
		}		
	}

	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

	if (Status == ELauncherWorkerStatus::Canceling)
	{
		Status = ELauncherWorkerStatus::Canceled;
		LaunchCanceled.Broadcast(FPlatformTime::Seconds() - LaunchStartTime);
	}
	else
	{
		LaunchCompleted.Broadcast(TaskChain->Succeeded(), FPlatformTime::Seconds() - LaunchStartTime, TaskChain->ReturnCode());
	}

	return 0;
}


void FLauncherWorker::Stop( )
{
	Cancel();
}


/* ILauncherWorker overrides
 *****************************************************************************/

void FLauncherWorker::Cancel( )
{
	if (Status == ELauncherWorkerStatus::Busy)
	{
		Status = ELauncherWorkerStatus::Canceling;
	}
}


int32 FLauncherWorker::GetTasks( TArray<ILauncherTaskPtr>& OutTasks ) const
{
	OutTasks.Reset();

	if (TaskChain.IsValid())
	{
		TQueue<TSharedPtr<FLauncherTask> > Queue;

		Queue.Enqueue(TaskChain);

		TSharedPtr<FLauncherTask> Task;

		// breadth first traversal
		while (Queue.Dequeue(Task))
		{
			OutTasks.Add(Task);

			const TArray<TSharedPtr<FLauncherTask> >& Continuations = Task->GetContinuations();

			for (int32 ContinuationIndex = 0; ContinuationIndex < Continuations.Num(); ++ContinuationIndex)
			{
				Queue.Enqueue(Continuations[ContinuationIndex]);
			}
		}
	}

	return OutTasks.Num();
}


void FLauncherWorker::OnTaskStarted(const FString& TaskName)
{
	StageStartTime = FPlatformTime::Seconds();
	StageStarted.Broadcast(TaskName);
}


void FLauncherWorker::OnTaskCompleted(const FString& TaskName)
{
	StageCompleted.Broadcast(TaskName, FPlatformTime::Seconds() - StageStartTime);
}


/* FLauncherWorker implementation
 *****************************************************************************/

void FLauncherWorker::CreateAndExecuteTasks( const ILauncherProfileRef& InProfile )
{
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

	// create task chains
	TaskChain = MakeShareable(new FLauncherVerifyProfileTask());
	TArray<FString> Platforms;
	if (InProfile->GetCookMode() == ELauncherProfileCookModes::ByTheBook || InProfile->IsBuilding())
	{
		Platforms = InProfile->GetCookedPlatforms();
	}
	TSharedPtr<FLauncherTask> PerPlatformTask = TaskChain;
	TSharedPtr<FLauncherTask> PerPlatformBuildTask = NULL;
	TSharedPtr<FLauncherTask> PerPlatformCookTask = NULL;
	TSharedPtr<FLauncherTask> PerPlatformPackageTask = NULL;
	TSharedPtr<FLauncherTask> PerPlatformDeviceTask = NULL;
	TSharedPtr<FLauncherTask> FirstPlatformBuildTask = NULL;
	TSharedPtr<FLauncherTask> FirstPlatformCookTask = NULL;
	TSharedPtr<FLauncherTask> FirstPlatformPackageTask = NULL;
	TSharedPtr<FLauncherTask> FirstPlatformDeviceTask = NULL;

	FLauncherUATTask::FirstTimeCompile = true;

	// determine deployment platforms
	ILauncherDeviceGroupPtr DeviceGroup = InProfile->GetDeployedDeviceGroup();
	FName Variant = NAME_None;

	if (DeviceGroup.IsValid() && Platforms.Num() < 1)
	{
		const TArray<FString>& Devices = DeviceGroup->GetDeviceIDs();
		// for each deployed device...
		for (int32 DeviceIndex = 0; DeviceIndex < Devices.Num(); ++DeviceIndex)
		{
			const FString& DeviceId = Devices[DeviceIndex];

			ITargetDeviceProxyPtr DeviceProxy = DeviceProxyManager->FindProxyDeviceForTargetDevice(DeviceId);

			if (DeviceProxy.IsValid())
			{
				// add the platform
				Variant = DeviceProxy->GetTargetDeviceVariant(DeviceId);
				Platforms.AddUnique(DeviceProxy->GetTargetPlatformName(Variant));
			}			
		}
	}


#if !WITH_EDITOR
	// can't cook by the book in the editor if we are not in the editor...
	check( InProfile->GetCookMode() != ELauncherProfileCookModes::ByTheBookInEditor );
#endif


	// for each desired platform...
	for (int32 PlatformIndex = 0; PlatformIndex < Platforms.Num(); ++PlatformIndex)
	{
		const FString& TargetPlatformName = Platforms[PlatformIndex];
		const ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(TargetPlatformName);

		if (TargetPlatform == nullptr)
		{
			continue;
		}

		// ... build the editor and game ...
		if (InProfile->IsBuilding())
		{
			TSharedPtr<FLauncherUATCommand> Command = NULL;
			if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
			{
				Command = MakeShareable(new FLauncherBuildServerCommand(*TargetPlatform));
			}
			else
			{
				Command = MakeShareable(new FLauncherBuildGameCommand(*TargetPlatform));
			}
			TSharedPtr<FLauncherTask> BuildTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
			BuildTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
			BuildTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
			if (!PerPlatformBuildTask.IsValid())
			{
				PerPlatformBuildTask = BuildTask;
				FirstPlatformBuildTask = BuildTask;
			}
			else
			{
				PerPlatformBuildTask->AddContinuation(BuildTask);
				PerPlatformBuildTask = BuildTask;
			}
		}

		// ... cook the build...
		TSharedPtr<FLauncherUATCommand> CookCommand = NULL;
		if (InProfile->GetCookMode() == ELauncherProfileCookModes::ByTheBook)
		{
			TSharedPtr<FLauncherUATCommand> Command = NULL;

			if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
			{
				Command = MakeShareable(new FLauncherCookServerCommand(*TargetPlatform));
			}
			else
			{
				Command = MakeShareable(new FLauncherCookGameCommand(*TargetPlatform));
			}
			CookCommand = Command;
			TSharedPtr<FLauncherTask> CookTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
			CookTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
			CookTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
			if (!PerPlatformCookTask.IsValid())
			{
				PerPlatformCookTask = CookTask;
				FirstPlatformCookTask = CookTask;
			}
			else
			{
				PerPlatformCookTask->AddContinuation(CookTask);
				PerPlatformCookTask = CookTask;
			}
		}
        // ... start a per-platform file server, if necessary...
        else if (InProfile->GetCookMode() == ELauncherProfileCookModes::OnTheFly)
        {
            TSharedPtr<FLauncherTask> FileServerTask = NULL; //PerPlatformFileServerTasks.FindOrAdd(TargetPlatformName);
            
            if (!FileServerTask.IsValid())
            {
                TSharedPtr<FLauncherUATCommand> Command = NULL;
                if (InProfile->GetLaunchMode() == ELauncherProfileLaunchModes::DoNotLaunch)
                {
                    Command = MakeShareable(new FLauncherStandAloneCookOnTheFlyCommand(*TargetPlatform));
                }
                else
                {
                    Command = MakeShareable(new FLauncherCookOnTheFlyCommand(*TargetPlatform));
                }
                CookCommand = Command;
            }
        }
		else if ( InProfile->GetCookMode() == ELauncherProfileCookModes::ByTheBookInEditor )
		{
			// need a command which will wait for the cook to finish
			class FWaitForCookInEditorToFinish : public FLauncherTask
			{
			public:
				FWaitForCookInEditorToFinish() : FLauncherTask( FString(TEXT("CookByTheBookInEditor")), FString(TEXT("CookByTheBookInEditorDesk")), NULL, NULL)
				{
				}
				virtual bool PerformTask( FLauncherTaskChainState& ChainState ) override
				{
					while ( !ChainState.Profile->OnIsCookFinished().Execute() )
					{
						FPlatformProcess::Sleep( 0.1f );
					}
					return true;
				}
			};

			TSharedPtr<FLauncherTask> Command = MakeShareable( new FWaitForCookInEditorToFinish() );

			if (!PerPlatformCookTask.IsValid())
			{
				PerPlatformCookTask = Command;
				FirstPlatformCookTask = Command;
			}
			else
			{
				PerPlatformCookTask->AddContinuation(Command);
				PerPlatformCookTask = Command;
			}
		}
		// ... package the build...
		if (InProfile->GetPackagingMode() != ELauncherProfilePackagingModes::DoNotPackage || ((TargetPlatform->PlatformName() == TEXT("IOS") || TargetPlatform->PlatformName() == TEXT("HTML5")) && InProfile->GetDeploymentMode() != ELauncherProfileDeploymentModes::CopyRepository && InProfile->GetDeploymentMode() != ELauncherProfileDeploymentModes::DoNotDeploy))
		{
			TSharedPtr<FLauncherUATCommand> Command = NULL;
			if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
			{
				Command = MakeShareable(new FLauncherPackageServerCommand(*TargetPlatform, CookCommand));
			}
			else
			{
				Command = MakeShareable(new FLauncherPackageGameCommand(*TargetPlatform, CookCommand));
			}
			
			TSharedPtr<FLauncherTask> PackageTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
			PackageTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
			PackageTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
			if (!PerPlatformPackageTask.IsValid())
			{
				PerPlatformPackageTask = PackageTask;
				FirstPlatformPackageTask = PackageTask;
			}
			else
			{
				PerPlatformPackageTask->AddContinuation(PackageTask);
				PerPlatformPackageTask = PackageTask;
			}
		}
		else if (InProfile->GetDeploymentMode() != ELauncherProfileDeploymentModes::DoNotDeploy && InProfile->GetDeploymentMode() != ELauncherProfileDeploymentModes::CopyRepository)
		{
			TSharedPtr<FLauncherUATCommand> Command = NULL;
			if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
			{
				Command = MakeShareable(new FLauncherStageServerCommand(*TargetPlatform, CookCommand));
			}
			else
			{
				Command = MakeShareable(new FLauncherStageGameCommand(*TargetPlatform, CookCommand));
			}

			TSharedPtr<FLauncherTask> StageTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
			StageTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
			StageTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
			if (!PerPlatformPackageTask.IsValid())
			{
				PerPlatformPackageTask = StageTask;
				FirstPlatformPackageTask = StageTask;
			}
			else
			{
				PerPlatformPackageTask->AddContinuation(StageTask);
				PerPlatformPackageTask = StageTask;
			}
		}
		
		// ... and deploy the build
		if (InProfile->GetDeploymentMode() != ELauncherProfileDeploymentModes::DoNotDeploy)
		{
			if (!DeviceGroup.IsValid())
			{
				continue;
			}

			TMap<FString, TSharedPtr<FLauncherTask> > PerPlatformFileServerTasks;

			const TArray<FString>& Devices = DeviceGroup->GetDeviceIDs();

			// for each deployed device...
			for (int32 DeviceIndex = 0; DeviceIndex < Devices.Num(); ++DeviceIndex)
			{
				const FString& DeviceId = Devices[DeviceIndex];
				
				// @Todo: this can probably been cleaned up if the variant class is exposed to the user (not 100% sure i want that yet)
				//			Deeper investigation / refactoring may make this validation obsolete.
				ITargetDeviceProxyPtr DeviceProxyPtr = DeviceProxyManager->FindProxyDeviceForTargetDevice(DeviceId);
				if (!DeviceProxyPtr.IsValid() || DeviceProxyPtr->GetTargetPlatformName(DeviceProxyPtr->GetTargetDeviceVariant(DeviceId)) != TargetPlatformName)
				{
					continue;
				}
				ITargetDeviceProxyRef DeviceProxy = DeviceProxyPtr.ToSharedRef();

				TSharedPtr<FLauncherTask> PerDeviceTask;

				FString LaunchCommandLine = InProfile->GetLaunchMode() != ELauncherProfileLaunchModes::DoNotLaunch ? InProfile->GetDefaultLaunchRole()->GetCommandLine() : TEXT("");
				// ... start a per-platform file server, if necessary...
				if (InProfile->GetDeploymentMode() == ELauncherProfileDeploymentModes::FileServer)
				{
					TSharedPtr<FLauncherTask> FileServerTask = NULL; //PerPlatformFileServerTasks.FindOrAdd(TargetPlatformName);

					if (!FileServerTask.IsValid())
					{
						TSharedPtr<FLauncherUATCommand> Command = NULL;
						if (InProfile->GetLaunchMode() == ELauncherProfileLaunchModes::DoNotLaunch)
						{
							Command = MakeShareable(new FLauncherStandAloneCookOnTheFlyCommand(*TargetPlatform));
						}
						else
						{
							Command = MakeShareable(new FLauncherCookOnTheFlyCommand(*TargetPlatform));
						}
						CookCommand = Command;
						FileServerTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
						FileServerTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
						FileServerTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
					}

					if (!PerPlatformDeviceTask.IsValid())
					{
						PerPlatformDeviceTask = FileServerTask;
						FirstPlatformDeviceTask = FileServerTask;
					}
					else
					{
						PerPlatformDeviceTask->AddContinuation(FileServerTask);
						PerPlatformDeviceTask = FileServerTask;
					}
				}

				// ... deploy the build to the device...
				if (InProfile->GetDeploymentMode() == ELauncherProfileDeploymentModes::CopyToDevice)
				{
					TSharedPtr<FLauncherUATCommand> Command = NULL;
					if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
					{
						Command = MakeShareable(new FLauncherDeployServerToDeviceCommand(DeviceProxy, Variant, *TargetPlatform, CookCommand));
					}
					else
					{
						Command = MakeShareable(new FLauncherDeployGameToDeviceCommand(DeviceProxy, Variant, *TargetPlatform, CookCommand, LaunchCommandLine));
					}
					TSharedPtr<FLauncherTask> DeployTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
					DeployTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
					DeployTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);

					if (!PerPlatformDeviceTask.IsValid())
					{
						PerPlatformDeviceTask = DeployTask;
						FirstPlatformDeviceTask = DeployTask;
					}
					else
					{
						PerPlatformDeviceTask->AddContinuation(DeployTask);
						PerPlatformDeviceTask = DeployTask;
					}
				}
				else if (InProfile->GetDeploymentMode() == ELauncherProfileDeploymentModes::CopyRepository)
				{
					TSharedPtr<FLauncherUATCommand> Command = NULL;
					if (TargetPlatform->PlatformName() == TEXT("WindowsServer") || TargetPlatform->PlatformName() == TEXT("LinuxServer"))
					{
						Command = MakeShareable(new FLauncherDeployServerPackageToDeviceCommand(DeviceProxy, Variant, *TargetPlatform, CookCommand));
					}
					else
					{
						Command = MakeShareable(new FLauncherDeployGamePackageToDeviceCommand(DeviceProxy, Variant, *TargetPlatform, CookCommand, LaunchCommandLine));
					}
					TSharedPtr<FLauncherTask> DeployTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
					DeployTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
					DeployTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);

					if (!PerPlatformDeviceTask.IsValid())
					{
						PerPlatformDeviceTask = DeployTask;
						FirstPlatformDeviceTask = DeployTask;
					}
					else
					{
						PerPlatformDeviceTask->AddContinuation(DeployTask);
						PerPlatformDeviceTask = DeployTask;
					}
				}
				else if (TargetPlatform->PlatformName() == TEXT("XboxOne") || TargetPlatform->PlatformName() == TEXT("IOS") || TargetPlatform->PlatformName().StartsWith(TEXT("Android")))
				{
					TSharedPtr<FLauncherUATCommand> Command = MakeShareable(new FLauncherDeployGameToDeviceCommand(DeviceProxy, Variant, *TargetPlatform, CookCommand, LaunchCommandLine));
					TSharedPtr<FLauncherTask> DeployTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
					DeployTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
					DeployTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);

					if (!PerPlatformDeviceTask.IsValid())
					{
						PerPlatformDeviceTask = DeployTask;
						FirstPlatformDeviceTask = DeployTask;
					}
					else
					{
						PerPlatformDeviceTask->AddContinuation(DeployTask);
						PerPlatformDeviceTask = DeployTask;
					}
				}

				// ... and then launch the build...
				if (InProfile->GetLaunchMode() != ELauncherProfileLaunchModes::DoNotLaunch)
				{
					TArray<ILauncherProfileLaunchRolePtr> Roles;

					if (InProfile->GetLaunchRolesFor(DeviceId, Roles) > 0)
					{
						// ... for every role assigned to that device
						for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
						{
							TSharedPtr<FLauncherUATCommand> Command;
							if (Roles[RoleIndex]->GetInstanceType() == ELauncherProfileRoleInstanceTypes::StandaloneClient)
							{
								Command = MakeShareable(new FLauncherLaunchGameCommand(DeviceProxy, Variant, *TargetPlatform, Roles[RoleIndex].ToSharedRef(), CookCommand));
							}
							else if (Roles[RoleIndex]->GetInstanceType() == ELauncherProfileRoleInstanceTypes::DedicatedServer)
							{
								Command = MakeShareable(new FLauncherLaunchDedicatedServerCommand(DeviceProxy, Variant, *TargetPlatform, Roles[RoleIndex].ToSharedRef(), CookCommand));
							}
								
							TSharedPtr<FLauncherTask> PerRoleTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
							PerRoleTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
							PerRoleTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);

							if (!PerPlatformDeviceTask.IsValid())
							{
								PerPlatformDeviceTask = PerRoleTask;
								FirstPlatformDeviceTask = PerRoleTask;
							}
							else
							{
								PerPlatformDeviceTask->AddContinuation(PerRoleTask);
								PerPlatformDeviceTask = PerRoleTask;
							}
						}
					}
				}
			}
		}
		else if (InProfile->GetCookMode() == ELauncherProfileCookModes::OnTheFly)
		{
//			TMap<FString, TSharedPtr<FLauncherTask> > PerPlatformFileServerTasks;
			TSharedPtr<FLauncherTask> FileServerTask = NULL; //PerPlatformFileServerTasks.FindOrAdd(TargetPlatformName);

			if (!FileServerTask.IsValid())
			{
				TSharedPtr<FLauncherUATCommand> Command = MakeShareable(new FLauncherStandAloneCookOnTheFlyCommand(*TargetPlatform));
				FileServerTask = MakeShareable(new FLauncherUATTask(Command, *TargetPlatform, Command->GetName(), ReadPipe, WritePipe, InProfile->GetEditorExe()));
				FileServerTask->OnStarted().AddRaw(this, &FLauncherWorker::OnTaskStarted);
				FileServerTask->OnCompleted().AddRaw(this, &FLauncherWorker::OnTaskCompleted);
			}

			if (!PerPlatformDeviceTask.IsValid())
			{
				PerPlatformDeviceTask = FileServerTask;
				FirstPlatformDeviceTask = FileServerTask;
			}
			else
			{
				PerPlatformDeviceTask->AddContinuation(FileServerTask);
				PerPlatformDeviceTask = FileServerTask;
			}
		}
	}

	if (FirstPlatformBuildTask.IsValid())
	{
		TaskChain->AddContinuation(FirstPlatformBuildTask);
	}
	else
	{
		PerPlatformBuildTask = TaskChain;
	}
	if (FirstPlatformCookTask.IsValid())
	{
		PerPlatformBuildTask->AddContinuation(FirstPlatformCookTask);
	}
	else
	{
		PerPlatformCookTask = PerPlatformBuildTask;
	}
	if (FirstPlatformPackageTask.IsValid())
	{
		PerPlatformCookTask->AddContinuation(FirstPlatformPackageTask);
	}
	else
	{
		PerPlatformPackageTask = PerPlatformCookTask;
	}
	if (FirstPlatformDeviceTask.IsValid())
	{
		PerPlatformPackageTask->AddContinuation(FirstPlatformDeviceTask);
	}

	// execute the chain
	FLauncherTaskChainState ChainState;

	ChainState.Profile = InProfile;
	ChainState.SessionId = FGuid::NewGuid();

	TaskChain->Execute(ChainState);
}
