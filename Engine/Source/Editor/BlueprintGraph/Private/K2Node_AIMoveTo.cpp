// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "BlueprintGraphPrivatePCH.h"
#include "CompilerResultsLog.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Blueprint/AIAsyncTaskBlueprintProxy.h"
#include "K2Node_AIMoveTo.h"
#include "EditorCategoryUtils.h"

#define LOCTEXT_NAMESPACE "K2Node_AIMoveTo"

UK2Node_AIMoveTo::UK2Node_AIMoveTo(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UAIBlueprintHelperLibrary, CreateMoveToProxyObject);
	ProxyFactoryClass = UAIBlueprintHelperLibrary::StaticClass();
	ProxyClass = UAIAsyncTaskBlueprintProxy::StaticClass();
}

FText UK2Node_AIMoveTo::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::AI);
}

FString UK2Node_AIMoveTo::GetTooltip() const
{
	return TEXT("Simple order for Pawn with AIController to move to a specific location");
}

FText UK2Node_AIMoveTo::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AIMoveTo", "AI MoveTo");
}

#undef LOCTEXT_NAMESPACE


