// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"

UNiagaraNodeGetAttr::UNiagaraNodeGetAttr(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNiagaraNodeGetAttr::AllocateDefaultPins()
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	UScriptStruct* VectorStruct = FindObjectChecked<UScriptStruct>(UObject::StaticClass(), TEXT("Vector"));
	CreatePin(EGPD_Output, Schema->PC_Struct, TEXT(""), VectorStruct, false, false, AttrName.ToString());
}

FText UNiagaraNodeGetAttr::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Attribute"), FText::FromName(AttrName));
	return FText::Format(NSLOCTEXT("Niagara", "GetAttribute", "Get {Attribute}"), Args);
}

FLinearColor UNiagaraNodeGetAttr::GetNodeTitleColor() const
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	if(Pins.Num() > 0 && Pins[0] != NULL)
	{
		return Schema->GetPinTypeColor(Pins[0]->PinType);
	}
	else
	{
		return Super::GetNodeTitleColor();
	}
}
