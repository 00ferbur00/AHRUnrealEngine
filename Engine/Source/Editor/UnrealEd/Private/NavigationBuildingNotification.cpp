// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "MainFrame.h"
#include "Kismet2/DebuggerCommands.h"
#include "EditorBuildUtils.h"
#include "NavigationBuildingNotification.h"

void FNavigationBuildingNotificationImpl::BuildStarted()
{
	UEditorEngine* const EEngine = Cast<UEditorEngine>(GEngine);
	const bool bUserRequestedBuild = (EEngine != NULL && (FEditorBuildUtils::IsBuildingNavigationFromUserRequest()));
	LastEnableTime = FPlatformTime::Seconds();

	if (NavigationBuildNotificationPtr.IsValid())
	{
		if (!bUserRequestedBuild)
		{
			return;
		}
		else
		{
			NavigationBuildNotificationPtr.Pin()->ExpireAndFadeout();
		}
	}

	if ( NavigationBuiltCompleteNotification.IsValid() )
	{
		NavigationBuiltCompleteNotification.Pin()->ExpireAndFadeout();
	}

	FNotificationInfo Info( NSLOCTEXT("NavigationBuild", "NavigationBuildingInProgress", "Building Navigation") );
	Info.bFireAndForget = false;
	Info.FadeOutDuration = 0.0f;
	Info.ExpireDuration = 0.0f;

	NavigationBuildNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	if (NavigationBuildNotificationPtr.IsValid())
	{
		NavigationBuildNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FNavigationBuildingNotificationImpl::BuildFinished()
{
	// Finished all requests! Notify the UI.
	TSharedPtr<SNotificationItem> NotificationItem = NavigationBuildNotificationPtr.Pin();
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetText( NSLOCTEXT("NavigationBuild", "NavigationBuildingComplete", "Navigation building done!") );
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();

		NavigationBuildNotificationPtr.Reset();
	}

	UEditorEngine* const EEngine = Cast<UEditorEngine>(GEngine);
	if (EEngine != NULL && (FEditorBuildUtils::IsBuildingNavigationFromUserRequest()))
	{
		FNotificationInfo Info( NSLOCTEXT("NavigationBuild", "NavigationBuildDoneMessage", "Navigation building completed.") );
		Info.bFireAndForget = false;
		Info.bUseThrobber = false;
		Info.FadeOutDuration = 0.0f;
		Info.ExpireDuration = 0.0f;
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			NSLOCTEXT("NavigationBuild", "NavigationBuildOk","Ok"),
			FText(),
			FSimpleDelegate::CreateRaw(this, &FNavigationBuildingNotificationImpl::ClearCompleteNotification)));

		NavigationBuiltCompleteNotification = FSlateNotificationManager::Get().AddNotification(Info);
		if (NavigationBuiltCompleteNotification.IsValid())
		{
			NavigationBuiltCompleteNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}

	FEditorBuildUtils::PathBuildingFinished();
}

void FNavigationBuildingNotificationImpl::ClearCompleteNotification()
{
	if ( NavigationBuiltCompleteNotification.IsValid() )
	{
		NavigationBuiltCompleteNotification.Pin()->SetCompletionState(SNotificationItem::CS_None);
		NavigationBuiltCompleteNotification.Pin()->ExpireAndFadeout();
		NavigationBuiltCompleteNotification.Reset();
	}
}

void FNavigationBuildingNotificationImpl::Tick(float DeltaTime)
{
#if WITH_NAVIGATION_GENERATOR
	if (FPlayWorldCommandCallbacks::IsInPIE_AndRunning())
	{
		return;
	}

	UEditorEngine* const EEngine = Cast<UEditorEngine>(GEngine);
	if (EEngine != NULL)
	{
		const bool bUserRequestedBuild = FEditorBuildUtils::IsBuildingNavigationFromUserRequest();
		FWorldContext &EditorContext = EEngine->GetEditorWorldContext();
		
		const bool bBuildInProgress = EditorContext.World() != NULL && EditorContext.World()->GetNavigationSystem() != NULL 
			&& EditorContext.World()->GetNavigationSystem()->IsNavigationBuildInProgress( GetDefault<ULevelEditorMiscSettings>()->bNavigationAutoUpdate ? true : false) == true;

		if (!bPreviouslyDetectedBuild && bBuildInProgress)
		{
			TimeOfStartedBuild = FPlatformTime::Seconds();
		}
		else if(bPreviouslyDetectedBuild && !bBuildInProgress)
		{
			TimeOfStoppedBuild = FPlatformTime::Seconds();
		}

		if( bBuildInProgress && bPreviouslyDetectedBuild && 
			!NavigationBuildNotificationPtr.IsValid() && 
			(bUserRequestedBuild || (!bUserRequestedBuild && (FPlatformTime::Seconds() - TimeOfStartedBuild) > 0.1))
		) 
		{
			BuildStarted();
		}
		// Disable the notification when we are no longer doing an async compile
		else if (!bBuildInProgress && !bPreviouslyDetectedBuild && (FPlatformTime::Seconds() - TimeOfStoppedBuild) > 1.0)
		{
			BuildFinished();
		}

		bPreviouslyDetectedBuild = bBuildInProgress;
	}
#endif
}

TStatId FNavigationBuildingNotificationImpl::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FNavigationBuildingNotificationImpl, STATGROUP_Tickables);
}

