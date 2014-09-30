// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphPin.h" // for EBlueprintPinStyleType
#include "BlueprintEditorSettings.generated.h"


UCLASS(config=EditorUserSettings)
class BLUEPRINTGRAPH_API UBlueprintEditorSettings
	:	public UObject
{
	GENERATED_UCLASS_BODY()

// Style Settings
public:
	/** Should arrows indicating data/execution flow be drawn halfway along wires? */
	UPROPERTY(EditAnywhere, config, Category=VisualStyle, meta=(DisplayName="Draw midpoint arrows in Blueprints"))
	bool bDrawMidpointArrowsInBlueprints;

// UX Settings
public:
	/** Determines if lightweight tutorial text shows up at the top of empty blueprint graphs */
	UPROPERTY(EditAnywhere, config, Category=UserExperience)
	bool bShowGraphInstructionText;

	/** If enabled, will use the blueprint's (or output pin's) class to narrow down context menu results. */
	UPROPERTY(EditAnywhere, config, Category=UserExperience)
	bool bUseTargetContextForNodeMenu;

	/** If enabled, then ALL component functions are exposed to the context menu (when the contextual target is a component owner). Ignores "ExposeFunctionCategories" metadata for components. */
	UPROPERTY(EditAnywhere, config, Category=UserExperience, meta=(DisplayName="Context Menu: Expose All Sub-Component Functions"))
	bool bExposeAllMemberComponentFunctions;

// Developer Settings
public:
	/** If enabled, tooltips on action menu items will show the associated action's signature id (can be used to setup custom favorites menus).*/
	UPROPERTY(EditAnywhere, config, Category=DeveloperTools)
	bool bShowActionMenuItemSignatures;

// Perf Settings
public:
	/** The node template cache is used to speed up blueprint menuing. This determines the peak data size for that cache. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, config, Category=Performance, DisplayName="Node-Template Cache Cap (MB)", meta=(ClampMin="0", UIMin="0"))
	float NodeTemplateCacheCapMB;
};
