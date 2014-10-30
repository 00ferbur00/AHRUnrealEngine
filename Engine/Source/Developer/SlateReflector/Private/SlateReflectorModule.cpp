// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "SlateReflectorPrivatePCH.h"
#include "ISlateReflectorModule.h"
#include "SDockTab.h"
#include "ModuleManager.h"


#define LOCTEXT_NAMESPACE "FSlateReflectorModule"


/**
 * Implements the SlateReflector module.
 */
class FSlateReflectorModule
	: public ISlateReflectorModule
{
public:

	// ISlateReflectorModule interface

	virtual TSharedRef<SWidget> GetWidgetReflector() override
	{
		TSharedPtr<SWidgetReflector> WidgetReflector = WidgetReflectorPtr.Pin();

		if (!WidgetReflector.IsValid())
		{
			WidgetReflector = SNew(SWidgetReflector);
			FSlateApplication::Get().SetWidgetReflector(WidgetReflector.ToSharedRef());
		}

		return WidgetReflector.ToSharedRef();
	}

	virtual void RegisterTabSpawner( const TSharedRef<FWorkspaceItem>& WorkspaceGroup ) override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner("WidgetReflector", FOnSpawnTab::CreateRaw(this, &FSlateReflectorModule::MakeWidgetReflectorTab) )
			.SetDisplayName(LOCTEXT("WidgetReflectorTitle", "Widget Reflector"))
			.SetTooltipText(LOCTEXT("WidgetReflectorTooltipText", "Open the Widget Reflector tab."))
			.SetGroup(WorkspaceGroup)
			.SetIcon(FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "WidgetReflector.TabIcon"));
	}

	virtual void UnregisterTabSpawner() 
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("WidgetReflector");
	}

public:

	// IModuleInterface interface

	virtual void StartupModule() override { }

	virtual void ShutdownModule() override
	{
		UnregisterTabSpawner();
	}

private:

	TSharedRef<SDockTab> MakeWidgetReflectorTab( const FSpawnTabArgs& )
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				GetWidgetReflector()
			];
	}

private:

	/** Holds the widget reflector singleton. */
	TWeakPtr<SWidgetReflector> WidgetReflectorPtr;
};


IMPLEMENT_MODULE(FSlateReflectorModule, SlateReflector);


#undef LOCTEXT_NAMESPACE
