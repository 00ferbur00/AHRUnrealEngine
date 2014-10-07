// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "LogVisualizerPCH.h"
#include "CollisionDebugDrawingPublic.h"
#include "LogVisualizerDebugActor.h"

#if ENABLE_VISUAL_LOG
//////////////////////////////////////////////////////////////////////////

void FLogVisualizer::SummonUI(UWorld* InWorld) 
{
	UE_LOG(LogLogVisualizer, Log, TEXT("Opening LogVisualizer..."));

	if( IsInGameThread() )
	{
		if (LogWindow.IsValid() && World.IsValid() && World == InWorld)
		{
			return;
		}

		World = InWorld;
		FVisualLog& VisualLog = FVisualLog::GetStatic();
		VisualLog.RegisterNewLogsObserver(FVisualLog::FOnNewLogCreatedDelegate::CreateRaw(this, &FLogVisualizer::OnNewLog));
		PullDataFromVisualLog(VisualLog);

		// Give window to slate
		if (!LogWindow.IsValid())
		{
			// Make a window
			TSharedRef<SWindow> NewWindow = SNew(SWindow)
				.ClientSize(FVector2D(720,768))
				.Title( NSLOCTEXT("LogVisualizer", "WindowTitle", "Log Visualizer") )
				[
					SNew(SLogVisualizer, this)
				];

			LogWindow = FSlateApplication::Get().AddWindow(NewWindow);
		}
		
		//@TODO fill Logs array with whatever is there in FVisualLog instance
	}
	else
	{
		UE_LOG(LogLogVisualizer, Warning, TEXT("FLogVisualizer::SummonUI: Not in game thread."));
	}
}

void FLogVisualizer::CloseUI(UWorld* InWorld) 
{
	UE_LOG(LogLogVisualizer, Log, TEXT("Opening LogVisualizer..."));

	if( IsInGameThread() )
	{
		if (LogWindow.IsValid() && (World.IsValid() == false || World == InWorld))
		{
			DebugActor = NULL;

			CleanUp();
			FSlateApplication::Get().RequestDestroyWindow(LogWindow.Pin().ToSharedRef());
		}
	}
	else
	{
		UE_LOG(LogLogVisualizer, Warning, TEXT("FLogVisualizer::CloseUI: Not in game thread."));
	}
}

bool FLogVisualizer::IsOpenUI(UWorld* InWorld)
{
	if (LogWindow.IsValid() && World.IsValid() && World == InWorld)
	{
		return true;
	}

	return false;
}

void FLogVisualizer::CleanUp()
{
	FVisualLog::GetStatic().ClearNewLogsObserver();
}

class AActor* FLogVisualizer::GetHelperActor(class UWorld* InWorld)
{
	UWorld* ActorWorld = DebugActor.IsValid() ? DebugActor->GetWorld() : NULL;
	if (DebugActor.IsValid() && ActorWorld == InWorld)
	{
		return DebugActor.Get();
	}

	for (TActorIterator<ALogVisualizerDebugActor> It(InWorld); It; ++It)
	{
		ALogVisualizerDebugActor* LogVisualizerDebugActor = *It;

		DebugActor = LogVisualizerDebugActor;
		return LogVisualizerDebugActor;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.bNoCollisionFail = true;
	SpawnInfo.Name = *FString::Printf(TEXT("LogVisualizerDebugActor"));
	DebugActor = InWorld->SpawnActor<ALogVisualizerDebugActor>(SpawnInfo);

	return DebugActor.Get();
}

void FLogVisualizer::PullDataFromVisualLog(const FVisualLog& VisualLog)
{
	Logs.Reset();
	const FVisualLog::FLogsMap* LogsMap = VisualLog.GetLogs();
	for (FVisualLog::FLogsMap::TConstIterator MapIt(*LogsMap); MapIt; ++MapIt)
	{
		Logs.Add(MapIt.Value());
		LogAddedEvent.Broadcast();
	}
}

void FLogVisualizer::OnNewLog(const AActor* Actor, TSharedPtr<FActorsVisLog> Log)
{
	Logs.Add(Log);
	LogAddedEvent.Broadcast();
}

void FLogVisualizer::AddLoadedLog(TSharedPtr<FActorsVisLog> Log)
{
	for (int32 Index = 0; Index < Logs.Num(); ++Index)
	{
		if (Logs[Index]->Name == Log->Name)
		{
			Logs[Index]->Entries.Append(Log->Entries);
			LogAddedEvent.Broadcast();
			return;
		}
	}

	if (Log.IsValid() && Log->Entries.Num() > 0)
	{
		Logs.Add(Log);
		LogAddedEvent.Broadcast();
	}
}

bool FLogVisualizer::IsRecording()
{
	return FVisualLogger::Get().IsRecording();
}

void FLogVisualizer::SetIsRecording(bool bNewRecording)
{
	FVisualLogger::Get().SetIsRecording(bNewRecording);
}

int32 FLogVisualizer::GetLogIndexForActor(const AActor* Actor)
{
	int32 ResultIndex = INDEX_NONE;
	if (!Actor)
	{
		return INDEX_NONE;
	}
	const FString FullName = Actor->GetFullName();
	TSharedPtr<FActorsVisLog>* Log = Logs.GetData();
	for (int32 LogIndex = 0; LogIndex < Logs.Num(); ++LogIndex, ++Log)
	{
		if ((*Log).IsValid() && (*Log)->FullName == FullName)
		{
			ResultIndex = LogIndex;
			break;
		}
	}

	return ResultIndex;
}

#endif //ENABLE_VISUAL_LOG
