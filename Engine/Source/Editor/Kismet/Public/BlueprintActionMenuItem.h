// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h" // for FEdGraphSchemaAction
#include "SlateColor.h"
#include "BlueprintNodeBinder.h" // for IBlueprintNodeBinder::FBindingSet
#include "BlueprintActionMenuItem.generated.h"

// Forward declarations
class  UBlueprintNodeSpawner;
struct FSlateBrush;
struct FBlueprintActionUiSpec;
struct FBlueprintActionContext;

/**
 * Wrapper around a UBlueprintNodeSpawner, which takes care of specialized
 * node spawning. This class should not be sub-classed, any special handling
 * should be done inside a UBlueprintNodeSpawner subclass, which will be
 * invoked from this class (separated to divide ui and node-spawning).
 */
USTRUCT()
struct FBlueprintActionMenuItem : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();
	static FName StaticGetTypeId() { static FName const TypeId("FBlueprintActionMenuItem"); return TypeId; }

public:	
	/** Constructors */
	FBlueprintActionMenuItem(UBlueprintNodeSpawner const* NodeSpawner = nullptr) : Action(NodeSpawner), IconTint(FLinearColor::White), IconBrush(nullptr) {}
	FBlueprintActionMenuItem(UBlueprintNodeSpawner const* NodeSpawner, FBlueprintActionUiSpec const& UiDef, IBlueprintNodeBinder::FBindingSet const& Bindings = IBlueprintNodeBinder::FBindingSet());
	
	// FEdGraphSchemaAction interface
	virtual FName         GetTypeId() const final { return StaticGetTypeId(); }
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, FVector2D const Location, bool bSelectNewNode = true) final;
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, FVector2D const Location, bool bSelectNewNode = true) final;
	virtual void          AddReferencedObjects(FReferenceCollector& Collector) final;
	// End FEdGraphSchemaAction interface

	/** @return */
	UBlueprintNodeSpawner const* GetRawAction() const;

	void AppendBindings(const FBlueprintActionContext& Context, IBlueprintNodeBinder::FBindingSet const& BindingSet);

	/**
	 * Retrieves the icon brush for this menu entry (to be displayed alongside
	 * in the menu).
	 *
	 * @param  ColorOut	An output parameter that's filled in with the color to tint the brush with.
	 * @return An slate brush to be used for this menu item in the action menu.
	 */
	FSlateBrush const* GetMenuIcon(FSlateColor& ColorOut);

	

private:
	/** Specialized node-spawner, that comprises the action portion of this menu entry. */
	UBlueprintNodeSpawner const* Action;
	/** Tint to return along with the icon brush. */
	FSlateColor IconTint;
	/** Brush that should be used for the icon on this menu item. */
	FSlateBrush const* IconBrush;
	/** */
	IBlueprintNodeBinder::FBindingSet Bindings;
};
