// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WidgetGraphNode_Base.generated.h"

UCLASS(Abstract, MinimalAPI)
class UWidgetGraphNode_Base : public UK2Node
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=PinOptions, EditFixedSize)
	TArray<FOptionalPinFromProperty> ShowPinForProperties;

	//// UObject interface
	UMGEDITOR_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//// End of UObject interface

	//// UEdGraphNode interface
	UMGEDITOR_API virtual void AllocateDefaultPins() override;
	UMGEDITOR_API virtual FLinearColor GetNodeTitleColor() const override;
	//UMGEDITOR_API virtual FString GetDocumentationLink() const override;
	//UMGEDITOR_API virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	UMGEDITOR_API virtual bool ShowPaletteIconOnNode() const override { return false; }
	//	// End of UEdGraphNode interface


	// UK2Node interface
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }
	virtual bool ShouldShowNodeProperties() const override { return true; }
	virtual bool CanPlaceBreakpoints() const override { return false; }
	UMGEDITOR_API virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	UMGEDITOR_API virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const override;
	UMGEDITOR_API virtual void GetMenuEntries(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	// End of UK2Node interface

	// UWidgetGraphNode_Base interface

	// Gets the menu category this node belongs in
	UMGEDITOR_API virtual FString GetNodeCategory() const;

	// Create any output pins necessary for this node
	UMGEDITOR_API virtual void CreateOutputPins();

	UMGEDITOR_API void GetPinAssociatedProperty(UScriptStruct* NodeType, UEdGraphPin* InputPin, UProperty*& OutProperty, int32& OutIndex);

	// customize pin data based on the input
	virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const;

	/** Get the animation blueprint to which this node belongs */
	class UWidgetBlueprint* GetWidgetBlueprint() const;

	// UWidgetGraphNode_Base

protected:
	friend class FWidgetBlueprintCompiler;

	// Gets the animation FNode type represented by this ed graph node
	UMGEDITOR_API UScriptStruct* GetFNodeType() const;

	// Gets the animation FNode property represented by this ed graph node
	UMGEDITOR_API UStructProperty* GetFNodeProperty() const;

	void InternalPinCreation(TArray<UEdGraphPin*>* OldPins);

	TSharedPtr<FEdGraphSchemaAction_K2NewNode> CreateDefaultMenuEntry(FGraphContextMenuBuilder& ContextMenuBuilder) const;
};
