// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

/** Query delegate to see if we are in picking mode */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnIsPicking, FName& /* OutWidgetNameToHighlight */);

class UEditorTutorial;
struct FTutorialContent;
class STutorialRoot;

/** Contains properties defining a "welcome" tutorial, which is auto-shown once.*/
struct FWelcomeTutorialProperties
{
	/** If set, call this delegate to return the proper properties struct. */
	DECLARE_DELEGATE_RetVal_OneParam(FWelcomeTutorialProperties const*, FWelcomeTutorialPropertiesChooserDelegate, UObject*);
	FWelcomeTutorialPropertiesChooserDelegate ChooserDelegate;

	/** Location of the tutorial doc files */
	FString TutorialPath;

	/** Ini setting name for the "have I seen this welcome screen" variable */
	FString SeenOnceSettingName;

	FGuid SurveyGuid;

	FWelcomeTutorialProperties(FString InTutorialPath, FString InSeenOnceSettingName)
		: TutorialPath(InTutorialPath), SeenOnceSettingName(InSeenOnceSettingName)
	{}

	FWelcomeTutorialProperties(FString InTutorialPath, FString InSeenOnceSettingName, FString SurveyGUIDString)
		: TutorialPath(InTutorialPath), SeenOnceSettingName(InSeenOnceSettingName)
	{
		FGuid::ParseExact(SurveyGUIDString, EGuidFormats::DigitsWithHyphens, SurveyGuid);
	}

	FWelcomeTutorialProperties(FWelcomeTutorialPropertiesChooserDelegate InChooserDelegate)
		: ChooserDelegate(InChooserDelegate)
	{}

	FWelcomeTutorialProperties() {}
};

class FIntroTutorials : public IIntroTutorials
{
public:
	static const FString IntroTutorialConfigSection;
	static const FString DisableTutorialsSettingName;
	static const FString InEditorTutorialPath;
	static const FString WelcomeTutorialPath;
	static const FString InEditorGamifiedTutorialPath;
	static const FString HomePath;
	static const FString BlueprintHomePath;
	static const FString TemplateOverviewPath;

	static const FWelcomeTutorialProperties UE4WelcomeTutorial;
	static const FWelcomeTutorialProperties BlueprintHomeTutorial;
	static const FWelcomeTutorialProperties ClassBlueprintWelcomeTutorial;
	static const FWelcomeTutorialProperties MacroLibraryBlueprintWelcomeTutorial;
	static const FWelcomeTutorialProperties InterfaceBlueprintWelcomeTutorial;
	static const FWelcomeTutorialProperties LevelScriptBlueprintWelcomeTutorial;
	static const FWelcomeTutorialProperties AddCodeToProjectWelcomeTutorial;
	static const FWelcomeTutorialProperties MatineeEditorWelcomeTutorial;
	static const FWelcomeTutorialProperties TemplateOverview;

	// ctor
	FIntroTutorials();

	/** Get the delegate used to check for whether we are picking widgets */
	FOnIsPicking& OnIsPicking();

	void GoToPreviousStage();

	void GoToNextStage(TWeakPtr<SWindow> InNavigationWindow);

private:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Add the menu extension for summoning the tutorial */
	void AddSummonTutorialsMenuExtension(FMenuBuilder& MenuBuilder);

	/** Add a menu extender to summon context-sensitive Blueprints page */
	TSharedRef<FExtender> AddSummonBlueprintTutorialsMenuExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> EditingObjects) const;

	/** Add a menu entry to summon context-sensitive Blueprints page */
	void AddSummonBlueprintTutorialsMenuExtension(FMenuBuilder& MenuBuilder, UObject* PrimaryObject);

	/** Event to be called when the main frame is loaded */
	void MainFrameLoad(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow);

	/** Summon a tutorial with the supplied path */
	void SummonTutorialWindowForPage(const FString& Path);

	/** Summon tutorial home page to front */
	void SummonTutorialHome();

	/** Summon blueprint tutorial home page to front */
	void SummonBlueprintTutorialHome(UObject* Asset, bool bForceWelcome);

	/** Event to be called when Tutorial window is closed. */
	void OnTutorialWindowClosed(const TSharedRef<SWindow>& Window);

	/** Called when tutorial is dismissed, either when finished or aborted. */
	void OnTutorialDismissed() const;

	/** Event to be called when any asset editor is opened */
	void OnAssetEditorOpened(UObject* Asset);

	/** Events to call when editor changes state in various ways */
	void OnAddCodeToProjectDialogOpened();
	void OnNewProjectDialogOpened();
	void OnMatineeEditorOpened();
	void OnEditorModeChanged(FEdMode* Mode, bool bEnteringMode);

	/** Events to call when opening the compiler fails */
	void HandleCompilerNotFound();

	/** Events to call when SDK isn't installed */
	void HandleSDKNotInstalled(const FString& PlatformName, const FString& DocLink);

	bool MaybeOpenWelcomeTutorial(const FWelcomeTutorialProperties& TutorialProperties);
	bool MaybeOpenWelcomeTutorial(const FString& TutorialPath, const FString& ConfigSettingName);
	void ResetWelcomeTutorials() const;

	template< typename KeyType >
	void ResetTutorialPropertyMap(TMap<KeyType, FWelcomeTutorialProperties> Map) const;
	void ResetTutorial(const FWelcomeTutorialProperties& TutProps) const;
	bool HasSeenTutorial(const FWelcomeTutorialProperties& TutProps) const;

	FWelcomeTutorialProperties const* ChooseBlueprintWelcomeTutorial(UObject* BlueprintObject);
	FWelcomeTutorialProperties const* ChooseBlueprintWelcomeTutorial(UObject* BlueprintObject, bool bForceWelcome);
	FWelcomeTutorialProperties const* FindAssetEditorTutorialProperties(UClass const* Class) const;

	FString AnalyticsEventNameFromTutorialPath(const FString& TutorialPath) const;

	/** Delegate for home button visibility */
	EVisibility GetHomeButtonVisibility() const;

	/** Handle linking between tutorials */
	FString HandleGotoNextTutorial(const FString& InCurrentPage) const;

	void SummonTutorialBrowser(TSharedRef<SWindow> InWindow, const FString& InFilter = TEXT(""));

public:

	// IIntroTutorials interface
	virtual void RegisterTutorialForAssetEditor(UClass* AssetClass, const FString& TutorialDocPath, const FString& TutorialHasBeenSeenSettingName, const FString& SurveyGUIDString) override;
	virtual void UnregisterTutorialForAssetEditor(UClass* AssetClass) override;
	virtual void LaunchTutorial(UEditorTutorial* Tutorial, bool bRestart = true, TWeakPtr<SWindow> InNavigationWindow = nullptr) override;
	// End of IIntroTutorials interface
private:
	/** The tab id of the tutorial tab */
	static const FName IntroTutorialTabName;

	/** The extender to pass to the level editor to extend it's window menu */
	TSharedPtr<FExtender> MainMenuExtender;

	/** The extender to pass to the blueprint editor to extend it's window menu */
	TSharedPtr<FExtender> BlueprintEditorExtender;

	/** The tutorial window that we use to display the tutorials */
	TWeakPtr<SWindow> TutorialWindow;

	/** Widget used to display tutorial */
	TWeakPtr<class SIntroTutorials> TutorialWidget;

	/** The root window that we will always try to parent the tutorial window to */
	TWeakPtr<SWindow> RootWindow;

	/** Whether tutorials are disabled altogether */
	bool bDisableTutorials;

	TMap<FString, FGuid> TutorialSurveyMap;

	TMap<UClass*, FWelcomeTutorialProperties> AssetEditorTutorialPropertyMap;

	TMap<FName, FWelcomeTutorialProperties> EditorModeTutorialPropertyMap;

	/** The current object we are using as a basis for displaying a tutorial */
	UClass* CurrentObjectClass;

	bool bEnablePostTutorialSurveys;

	/** Map of what tutorial to go to at the end of another */
	TMap<FString, FString> TutorialChainMap;

	/** should we be clearing the 'have seen this tutorial' flag? (controlled by -tutorials on the command line) */
	bool bDesireResettingTutorialSeenFlagOnLoad;

	/** Delegate used to determine whether we are in picking mode */
	FOnIsPicking OnIsPickingDelegate;

	/** Root widget for tutorial overlay system */
	TSharedPtr<STutorialRoot> TutorialRoot;
};