// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#include "WidgetBlueprintEditorUtils.h"
#include "WidgetBlueprintEditor.h"
#include "Kismet2NameValidators.h"
#include "BlueprintEditorUtils.h"
#include "K2Node_Variable.h"
#include "WidgetTemplateClass.h"
#include "Factories.h"
#include "UnrealExporter.h"

#define LOCTEXT_NAMESPACE "UMG"

class FWidgetObjectTextFactory : public FCustomizableTextObjectFactory
{
public:
	FWidgetObjectTextFactory()
		: FCustomizableTextObjectFactory(GWarn)
	{
	}

	// FCustomizableTextObjectFactory implementation

	virtual bool CanCreateClass(UClass* ObjectClass) const override
	{
		const bool bIsWidget = ObjectClass->IsChildOf(UWidget::StaticClass());
		const bool bIsSlot = ObjectClass->IsChildOf(UPanelSlot::StaticClass());

		return bIsWidget || bIsSlot;
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		check(NewObject);

		// Add it to the new object map
		NewObjectMap.Add(NewObject->GetFName(), Cast<UWidget>(NewObject));

		// If this is a scene component and it has a parent
		UWidget* Widget = Cast<UWidget>(NewObject);
		if ( Widget && Widget->Slot )
		{
			// Add an entry to the child->parent name map
//			ParentMap.Add(NewObject->GetFName(), Widget->AttachParent->GetFName());

			// Clear this so it isn't used when constructing the new SCS node
			//Widget->AttachParent = NULL;
		}
	}

	// FCustomizableTextObjectFactory (end)

public:

	// Child->Parent name map
	TMap<FName, FName> ParentMap;

	// Name->Instance object mapping
	TMap<FName, UWidget*> NewObjectMap;
};

bool FWidgetBlueprintEditorUtils::RenameWidget(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, const FName& OldName, const FName& NewName)
{
	UWidgetBlueprint* Blueprint = BlueprintEditor->GetWidgetBlueprintObj();
	check(Blueprint);

	bool bRenamed = false;

	TSharedPtr<INameValidatorInterface> NameValidator = MakeShareable(new FKismetNameValidator(Blueprint));

	// NewName should be already validated. But one must make sure that NewTemplateName is also unique.
	const bool bUniqueNameForTemplate = ( EValidatorResult::Ok == NameValidator->IsValid(NewName) );

	const FString NewNameStr = NewName.ToString();
	const FString OldNameStr = OldName.ToString();

	UWidget* Widget = Blueprint->WidgetTree->FindWidget(OldNameStr);
	check(Widget);

	if ( Widget )
	{
		const FScopedTransaction Transaction(LOCTEXT("RenameWidget", "Rename Widget"));

		// Rename Template
		Blueprint->Modify();
		Widget->Modify();


		// Rename Preview before renaming the template widget so the preview widget can be found
		UWidget* WidgetPreview = FWidgetReference::FromTemplate(BlueprintEditor, Widget).GetPreview();
		if(WidgetPreview)
		{
			WidgetPreview->Rename(*NewNameStr);
		}

		// Find and update all variable references in the graph

		Widget->Rename(*NewNameStr);
	
		// Update Variable References
		TArray<UK2Node_Variable*> WidgetVarNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Variable>(Blueprint, WidgetVarNodes);
		for ( UK2Node_Variable* TestNode : WidgetVarNodes )
		{
			if ( TestNode && ( OldName == TestNode->GetVarName() ) )
			{
				UEdGraphPin* TestPin = TestNode->FindPin(OldNameStr);
				if ( TestPin && ( Widget->GetClass() == TestPin->PinType.PinSubCategoryObject.Get() ) )
				{
					TestNode->Modify();
					if ( TestNode->VariableReference.IsSelfContext() )
					{
						TestNode->VariableReference.SetSelfMember(NewName);
					}
					else
					{
						//TODO:
						UClass* ParentClass = TestNode->VariableReference.GetMemberParentClass((UClass*)NULL);
						TestNode->VariableReference.SetExternalMember(NewName, ParentClass);
					}
					TestPin->Modify();
					TestPin->PinName = NewNameStr;
				}
			}
		}

		// Update Event References to member variables
		TArray<UK2Node_ComponentBoundEvent*> EventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_ComponentBoundEvent>(Blueprint, EventNodes);
		for ( UK2Node_ComponentBoundEvent* EventNode : EventNodes )
		{
			if ( EventNode->ComponentPropertyName == OldName )
			{
				EventNode->ComponentPropertyName = NewName;
			}
		}

		// Find and update all binding references in the widget blueprint
		for ( FDelegateEditorBinding& Binding : Blueprint->Bindings )
		{
			if ( Binding.ObjectName == OldNameStr )
			{
				Binding.ObjectName = NewNameStr;
			}
		}

		// Update widget blueprint names
		for( FWidgetAnimation& WidgetAnimation : Blueprint->AnimationData )
		{
			for( FWidgetAnimationBinding& AnimBinding : WidgetAnimation.AnimationBindings )
			{
				if( AnimBinding.WidgetName == OldName )
				{
					AnimBinding.WidgetName = NewName;
				}
			}
		}

		// Validate child blueprints and adjust variable names to avoid a potential name collision
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewName);

		// Refresh references and flush editors
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		bRenamed = true;
	}

	return bRenamed;
}

void FWidgetBlueprintEditorUtils::CreateWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, FVector2D TargetLocation)
{
	BlueprintEditor->PasteDropLocation = TargetLocation;

	TSet<FWidgetReference> Widgets = BlueprintEditor->GetSelectedWidgets();
	UWidgetBlueprint* BP = BlueprintEditor->GetWidgetBlueprintObj();

	MenuBuilder.PushCommandList(BlueprintEditor->WidgetCommandList.ToSharedRef());

	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Actions");
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("WidgetTree_WrapWith", "Wrap With..."),
			LOCTEXT("WidgetTree_WrapWithToolTip", "Wraps the currently selected widgets inside of another container widget"),
			FNewMenuDelegate::CreateStatic(&FWidgetBlueprintEditorUtils::BuildWrapWithMenu, BP, Widgets)
			);
	}
	MenuBuilder.EndSection();

	MenuBuilder.PopCommandList();
}

void FWidgetBlueprintEditorUtils::DeleteWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	if ( Widgets.Num() > 0 )
	{
		const FScopedTransaction Transaction(LOCTEXT("RemoveWidget", "Remove Widget"));
		BP->WidgetTree->SetFlags(RF_Transactional);
		BP->WidgetTree->Modify();

		bool bRemoved = false;
		for ( FWidgetReference& Item : Widgets )
		{
			UWidget* WidgetTemplate = Item.GetTemplate();

			// Modify the widget's parent
			UPanelWidget* Parent = WidgetTemplate->GetParent();
			if ( Parent )
			{
				Parent->Modify();
			}
			
			// Modify the widget being removed.
			WidgetTemplate->Modify();

			bRemoved = BP->WidgetTree->RemoveWidget(WidgetTemplate);

			// Rename the removed widget to the transient package so that it doesn't conflict with future widgets sharing the same name.
			WidgetTemplate->Rename(NULL, NULL);

			// Rename all child widgets as well, to the transient package so that they don't conflict with future widgets sharing the same name.
			TArray<UWidget*> ChildWidgets;
			BP->WidgetTree->GetChildWidgets(WidgetTemplate, ChildWidgets);
			for ( UWidget* Widget : ChildWidgets )
			{
				Widget->Rename(NULL, NULL);
			}
		}

		//TODO UMG There needs to be an event for widget removal so that caches can be updated, and selection

		if ( bRemoved )
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		}
	}
}

void FWidgetBlueprintEditorUtils::BuildWrapWithMenu(FMenuBuilder& Menu, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	Menu.BeginSection("WrapWith", LOCTEXT("WidgetTree_WrapWith", "Wrap With..."));
	{
		for ( TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt )
		{
			UClass* WidgetClass = *ClassIt;
			if ( WidgetClass->IsChildOf(UPanelWidget::StaticClass()) && WidgetClass->HasAnyClassFlags(CLASS_Abstract) == false )
			{
				Menu.AddMenuEntry(
					WidgetClass->GetDisplayNameText(),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(
					FExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::WrapWidgets, BP, Widgets, WidgetClass),
					FCanExecuteAction()
					)
					);
			}
		}
	}
	Menu.EndSection();
}

void FWidgetBlueprintEditorUtils::WrapWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets, UClass* WidgetClass)
{
	TSharedPtr<FWidgetTemplateClass> Template = MakeShareable(new FWidgetTemplateClass(WidgetClass));

	UPanelWidget* NewWrapperWidget = CastChecked<UPanelWidget>(Template->Create(BP->WidgetTree));

	//TODO UMG When wrapping multiple widgets, how will that work?
	for ( FWidgetReference& Item : Widgets )
	{
		int32 OutIndex;
		UPanelWidget* CurrentParent = BP->WidgetTree->FindWidgetParent(Item.GetTemplate(), OutIndex);
		if ( CurrentParent )
		{
			CurrentParent->ReplaceChildAt(OutIndex, NewWrapperWidget);

			NewWrapperWidget->AddChild(Item.GetTemplate());
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
}

void FWidgetBlueprintEditorUtils::CutWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	CopyWidgets(BP, Widgets);
	DeleteWidgets(BP, Widgets);
}

void FWidgetBlueprintEditorUtils::CopyWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	TSet<UWidget*> CopyableWidets;
	for ( const FWidgetReference& Widget : Widgets )
	{
		UWidget* ParentWidget = Widget.GetTemplate();
		CopyableWidets.Add(ParentWidget);

		UWidget::GatherAllChildren(ParentWidget, CopyableWidets);
	}

	FString ExportedText;
	FWidgetBlueprintEditorUtils::ExportWidgetsToText(CopyableWidets, /*out*/ ExportedText);
	FPlatformMisc::ClipboardCopy(*ExportedText);
}

void FWidgetBlueprintEditorUtils::ExportWidgetsToText(TSet<UWidget*> WidgetsToExport, /*out*/ FString& ExportedText)
{
	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;

	// Export each of the selected nodes
	UObject* LastOuter = NULL;
	for ( UWidget* Widget : WidgetsToExport )
	{
		// The nodes should all be from the same scope
		UObject* ThisOuter = Widget->GetOuter();
		check(( LastOuter == ThisOuter ) || ( LastOuter == NULL ));
		LastOuter = ThisOuter;

		UExporter::ExportToOutputDevice(&Context, Widget, NULL, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, ThisOuter);
	}

	ExportedText = Archive;
}

void FWidgetBlueprintEditorUtils::PasteWidgets(UWidgetBlueprint* BP, FWidgetReference ParentWidgetRef, FVector2D PasteLocation)
{
	const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());

	UPanelWidget* ParentWidget = NULL;
	
	if ( ParentWidgetRef.IsValid() )
	{
		ParentWidget = CastChecked<UPanelWidget>(ParentWidgetRef.GetTemplate());
	}
	
	// TODO UMG Find paste parent, may not be the selected widget...  Maybe it should be the parent of the copied widget until,
	// we do a paste here, from a right click menu.

	if ( !ParentWidget )
	{
		// If we already have a root widget, then we can't replace the root.
		if ( BP->WidgetTree->RootWidget )
		{
			return;
		}
	}

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformMisc::ClipboardPaste(TextToImport);

	// Import the nodes
	TSet<UWidget*> PastedWidgets;
	FWidgetBlueprintEditorUtils::ImportWidgetsFromText(BP, TextToImport, /*out*/ PastedWidgets);

	// Ignore an empty set of widget paste data.
	if ( PastedWidgets.Num() == 0 )
	{
		return;
	}

	TArray<UWidget*> RootPasteWidgets;
	for ( UWidget* NewWidget : PastedWidgets )
	{
		// Widgets with a null parent mean that they were the root most widget of their selection set when
		// they were copied and thus we need to paste only the root most widgets.  All their children will be added
		// automatically.
		if ( NewWidget->GetParent() == NULL )
		{
			RootPasteWidgets.Add(NewWidget);
		}
	}

	// If there isn't a root widget and we're copying multiple root widgets, then we need to add a container root
	// to hold the pasted data since multiple root widgets isn't permitted.
	if ( !ParentWidget && RootPasteWidgets.Num() > 1 )
	{
		ParentWidget = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		BP->WidgetTree->Modify();
		BP->WidgetTree->RootWidget = ParentWidget;
	}

	if ( ParentWidget )
	{
		if ( !ParentWidget->CanHaveMultipleChildren() )
		{
			if ( ParentWidget->GetChildrenCount() > 0 || RootPasteWidgets.Num() > 1 )
			{
				UCanvasPanel* PasteContainer = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
				//TODO UMG The new container could be tiny, unless filling the space.
				UPanelSlot* Slot = ParentWidget->AddChild(PasteContainer);
				ParentWidget = PasteContainer;
			}
		}

		ParentWidget->Modify();

		for ( UWidget* NewWidget : RootPasteWidgets )
		{
			UPanelSlot* Slot = ParentWidget->AddChild(NewWidget);
			if ( Slot )
			{
				Slot->SetDesiredPosition(PasteLocation);
			}
			//TODO UMG - The paste location needs to be relative from the most upper left hand corner of other widgets in their container.
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
	else
	{
		check(RootPasteWidgets.Num() == 1)
		// If we've arrived here, we must be creating the root widget from paste data, and there can only be
		// one item in the paste data by now.
		BP->WidgetTree->Modify();

		for ( UWidget* NewWidget : RootPasteWidgets )
		{
			BP->WidgetTree->RootWidget = NewWidget;
			break;
		}
		
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
}

void FWidgetBlueprintEditorUtils::ImportWidgetsFromText(UWidgetBlueprint* BP, const FString& TextToImport, /*out*/ TSet<UWidget*>& ImportedWidgetSet)
{
	// We create our own transient package here so that we can deserialize the data in isolation and ensure unreferenced
	// objects not part of the deserialization set are unresolved.
	UPackage* TempPackage = ConstructObject<UPackage>(UPackage::StaticClass(), nullptr, TEXT("/Engine/UMG/Editor/Transient"), RF_Transient);
	TempPackage->AddToRoot();

	// Turn the text buffer into objects
	FWidgetObjectTextFactory Factory;
	Factory.ProcessBuffer(TempPackage, RF_Transactional, TextToImport);

	for ( auto& Entry : Factory.NewObjectMap )
	{
		UWidget* Widget = Entry.Value;

		ImportedWidgetSet.Add(Widget);

		Widget->SetFlags(RF_Transactional);

		// If there is an existing widget with the same name, rename the newly placed widget.
		if ( FindObject<UObject>(BP->WidgetTree, *Widget->GetName()) )
		{
			Widget->Rename(nullptr, BP->WidgetTree);
		}
		else
		{
			Widget->Rename(*Widget->GetName(), BP->WidgetTree);
		}
	}

	// Remove the temp package from the root now that it has served its purpose.
	TempPackage->RemoveFromRoot();
}

void FWidgetBlueprintEditorUtils::ExportPropertiesToText(UObject* Object, TMap<FName, FString>& ExportedProperties)
{
	if ( Object )
	{
		for ( TFieldIterator<UProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt )
		{
			UProperty* Property = *PropertyIt;

			// Don't serialize out object properties, we just want value data.
			if ( !Property->IsA<UObjectProperty>() )
			{
				FString ValueText;
				if ( Property->ExportText_InContainer(0, ValueText, Object, Object, Object, PPF_IncludeTransient) )
				{
					ExportedProperties.Add(Property->GetFName(), ValueText);
				}
			}
		}
	}
}

void FWidgetBlueprintEditorUtils::ImportPropertiesFromText(UObject* Object, const TMap<FName, FString>& ExportedProperties)
{
	if ( Object )
	{
		for ( const auto& Entry : ExportedProperties )
		{
			if ( UProperty* Property = FindField<UProperty>(Object->GetClass(), Entry.Key) )
			{
				Property->ImportText(*Entry.Value, Property->ContainerPtrToValuePtr<uint8>(Object), 0, Object);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
