// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "BlueprintGraphPrivatePCH.h"
#include "KismetCompiler.h"
#include "Kismet/KismetNodeHelperLibrary.h"
#include "K2Node_CastByteToEnum.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"

const FString UK2Node_CastByteToEnum::ByteInputPinName = TEXT("Byte");

UK2Node_CastByteToEnum::UK2Node_CastByteToEnum(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
}

void UK2Node_CastByteToEnum::AllocateDefaultPins()
{
	check(Enum);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	
	CreatePin(EGPD_Input, Schema->PC_Byte, TEXT(""), NULL, false, false, ByteInputPinName);
	CreatePin(EGPD_Output, Schema->PC_Byte, TEXT(""), Enum, false, false, Schema->PN_ReturnValue);
}

FString UK2Node_CastByteToEnum::GetTooltip() const
{
	return FString::Printf( 
		*NSLOCTEXT("K2Node", "CastByteToEnum_Tooltip", "Byte to Enum %s").ToString(),
		*Enum->GetName());
}

FText UK2Node_CastByteToEnum::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(GetTooltip());
}

FText UK2Node_CastByteToEnum::GetCompactNodeTitle() const
{
	return NSLOCTEXT("K2Node", "CastSymbol", "\x2022");
}

FName UK2Node_CastByteToEnum::GetFunctionName() const
{
	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetNodeHelperLibrary, GetValidIndex);
	return FunctionName;
}

void UK2Node_CastByteToEnum::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (CompilerContext.bIsFullCompile && bSafe)
	{
		const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

		// FUNCTION NODE
		const FName FunctionName = GetFunctionName();
		const UFunction* Function = UKismetNodeHelperLibrary::StaticClass()->FindFunctionByName(FunctionName);
		check(NULL != Function);
		UK2Node_CallFunction* CallValidation = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph); 
		CallValidation->SetFromFunction(Function);
		CallValidation->AllocateDefaultPins();
		check(CallValidation->IsNodePure());

		// FUNCTION ENUM PIN
		UEdGraphPin* FunctionEnumPin = CallValidation->FindPinChecked(TEXT("Enum"));
		Schema->TrySetDefaultObject(*FunctionEnumPin, Enum);
		check(FunctionEnumPin->DefaultObject == Enum);

		// FUNCTION INPUT BYTE PIN
		UEdGraphPin* OrgInputPin = FindPinChecked(ByteInputPinName);
		UEdGraphPin* FunctionIndexPin = CallValidation->FindPinChecked(TEXT("EnumeratorIndex"));
		check(EGPD_Input == FunctionIndexPin->Direction && Schema->PC_Byte == FunctionIndexPin->PinType.PinCategory);
		CompilerContext.MovePinLinksToIntermediate(*OrgInputPin, *FunctionIndexPin);

		// UNSAFE CAST NODE
		UK2Node_CastByteToEnum* UsafeCast = CompilerContext.SpawnIntermediateNode<UK2Node_CastByteToEnum>(this, SourceGraph); 
		UsafeCast->Enum = Enum;
		UsafeCast->bSafe = false;
		UsafeCast->AllocateDefaultPins();

		// UNSAFE CAST INPUT
		UEdGraphPin* CastInputPin = UsafeCast->FindPinChecked(ByteInputPinName);
		UEdGraphPin* FunctionReturnPin = CallValidation->GetReturnValuePin();
		check(NULL != FunctionReturnPin);
		Schema->TryCreateConnection(CastInputPin, FunctionReturnPin);

		// OPUTPUT PIN
		UEdGraphPin* OrgReturnPin = FindPinChecked(Schema->PN_ReturnValue);
		UEdGraphPin* NewReturnPin = UsafeCast->FindPinChecked(Schema->PN_ReturnValue);
		CompilerContext.MovePinLinksToIntermediate(*OrgReturnPin, *NewReturnPin);

		BreakAllNodeLinks();
	}
}

class FKCHandler_CastByteToEnum : public FNodeHandlingFunctor
{
public:
	FKCHandler_CastByteToEnum(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		FNodeHandlingFunctor::RegisterNets(Context, Node); //handle literals

		UEdGraphPin* InPin = Node->FindPinChecked(UK2Node_CastByteToEnum::ByteInputPinName);
		UEdGraphPin* Net = FEdGraphUtilities::GetNetFromPin(InPin);
		if (Context.NetMap.Find(Net) == NULL)
		{
			FBPTerminal* Term = new (Context.IsEventGraph() ? Context.EventGraphLocals : Context.Locals) FBPTerminal();
			Term->CopyFromPin(Net, Context.NetNameMap->MakeValidName(Net));
			Context.NetMap.Add(Net, Term);
		}

		FBPTerminal** ValueSource = Context.NetMap.Find(Net);
		check(ValueSource && *ValueSource);
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		UEdGraphPin* OutPin = Node->FindPinChecked(Schema->PN_ReturnValue);
		if (ensure(Context.NetMap.Find(OutPin) == NULL))
		{
			Context.NetMap.Add(OutPin, *ValueSource);
		}
	}
};

FNodeHandlingFunctor* UK2Node_CastByteToEnum::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	if (!bSafe)
	{
		return new FKCHandler_CastByteToEnum(CompilerContext);
	}
	return NULL;
}

void UK2Node_CastByteToEnum::GetMenuActions(TArray<UBlueprintNodeSpawner*>& ActionListOut) const
{
	for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
	{
		UEnum const* Enum = (*EnumIt);
		// we only want to add global "standalone" enums here; those belonging to a 
		// certain class should instead be associated with that class (so when 
		// the class is modified we can easily handle any enums that were changed).
		//
		// @TODO: don't love how this code is essentially duplicated in BlueprintActionDatabase.cpp, for class enums
		bool bIsStandaloneEnum = Enum->GetOuter()->IsA(UPackage::StaticClass());

		if (!bIsStandaloneEnum || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
		{
			continue;
		}

		auto CustomizeEnumNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UEnum> EnumPtr)
		{
			UK2Node_CastByteToEnum* EnumNode = CastChecked<UK2Node_CastByteToEnum>(NewNode);
			if (EnumPtr.IsValid())
			{
				EnumNode->Enum = EnumPtr.Get();
			}
			EnumNode->bSafe = true;
		};

		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionListOut.Add(NodeSpawner);

		TWeakObjectPtr<UEnum> EnumPtr = Enum;
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeEnumNodeLambda, EnumPtr);
	}
}

FText UK2Node_CastByteToEnum::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Enum);
}
