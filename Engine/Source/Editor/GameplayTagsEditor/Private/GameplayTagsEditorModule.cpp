// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "GameplayTagsEditorModulePrivatePCH.h"
#include "GameplayTagsGraphPanelPinFactory.h"
#include "GameplayTagContainerCustomization.h"
#include "GameplayTagCustomization.h"
#include "GameplayTagsSettings.h"
#include "Settings.h"

#define LOCTEXT_NAMESPACE "GameplayTagEditor"

class FGameplayTagsEditorModule : public IGameplayTagsEditorModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface
};

IMPLEMENT_MODULE( FGameplayTagsEditorModule, GameplayTagsEditor )

void FGameplayTagsEditorModule::StartupModule()
{
	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("GameplayTagContainer", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGameplayTagContainerCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("GameplayTag", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGameplayTagCustomization::MakeInstance));

	TSharedPtr<FGameplayTagsGraphPanelPinFactory> GameplayTagsGraphPanelPinFactory = MakeShareable( new FGameplayTagsGraphPanelPinFactory() );
	FEdGraphUtilities::RegisterVisualPinFactory(GameplayTagsGraphPanelPinFactory);

	if (ISettingsModule* SettingsModule = ISettingsModule::Get())
	{
		SettingsModule->RegisterSettings("Project", "Project", "GameplayTags",
			LOCTEXT("GameplayTagSettingsName", "GameplayTags"),
			LOCTEXT("GameplayTagSettingsNameDesc", "GameplayTag Settings"),
			GetMutableDefault<UGameplayTagsSettings>()
			);
	}
}

void FGameplayTagsEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	
	
	if (ISettingsModule* SettingsModule = ISettingsModule::Get())
	{
		SettingsModule->UnregisterSettings("Project", "Project", "GameplayTags");
	}
}

#undef LOCTEXT_NAMESPACE