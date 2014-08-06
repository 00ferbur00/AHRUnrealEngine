// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Developer/AssetTools/Public/IAssetTypeActions.h"
#include "Editor/GraphEditor/Public/DiffResults.h"
#include "GraphEditor.h"

struct FMatchName
{
	FMatchName(const FString& InName)
	: Name(InName)
	{
	}

	bool operator() (const UObject* Object)
	{
		return Object->GetName() == Name;
	}

	FString const& Name;
};

/** Individual Diff item shown in the list of diffs */
struct FDiffResultItem : public TSharedFromThis<FDiffResultItem>
{
	FDiffResultItem(FDiffSingleResult InResult) : Result(InResult){}

	FDiffSingleResult Result;

	TSharedRef<SWidget> KISMET_API GenerateWidget() const;
};

/*List item that entry for a graph*/
struct KISMET_API FListItemGraphToDiff: public TSharedFromThis<FListItemGraphToDiff>
{
	FListItemGraphToDiff(class SBlueprintDiff* Diff, class UEdGraph* GraphOld, class UEdGraph* GraphNew, const FRevisionInfo& RevisionOld, const FRevisionInfo& RevisionNew);
	~FListItemGraphToDiff();

	/*Generate Widget for list item*/
	TSharedRef<SWidget> GenerateWidget() ;

	/*Get tooltip for list item */
	FText   GetToolTip() ;

	/*Get old(left) graph*/
	UEdGraph* GetGraphOld()const{return GraphOld;}

	/*Get new(right) graph*/
	UEdGraph* GetGraphNew()const{return GraphNew;}

private:

	/*Get icon to use by graph name*/
	static const FSlateBrush* GetIconForGraph(UEdGraph* Graph);

	/*Diff widget*/
	class SBlueprintDiff* Diff;

	/*The old graph(left)*/
	class UEdGraph*	GraphOld;

	/*The new graph(right)*/
	class UEdGraph* GraphNew;

	/*Description of Old and new graph*/
	FRevisionInfo	RevisionOld, RevisionNew;

	//////////////////////////////////////////////////////////////////////////
	// Diff list
	//////////////////////////////////////////////////////////////////////////

	typedef TSharedPtr<struct FDiffResultItem>	FSharedDiffOnGraph;
	typedef SListView<FSharedDiffOnGraph >		SListViewType;

public:

	/** Called when the Newer Graph is modified*/
	void OnGraphChanged( const FEdGraphEditAction& Action) ;

	/** Generate list of differences*/
	TSharedRef<SWidget> GenerateDiffListWidget() ;

	/** Build up the Diff Source Array*/
	void BuildDiffSourceArray();

	/** Called when user clicks on a new graph list item */
	void OnSelectionChanged(FSharedDiffOnGraph Item, ESelectInfo::Type SelectionType);

	/** Called when user presses key within the diff view */
	void KeyWasPressed( const FKeyboardEvent& InKeyboardEvent);

private:
	
	/** Called when user clicks button to go to next difference in graph */
	void NextDiff();

	/** Called when user clicks button to go to prev difference in graph */
	void PrevDiff();

	/** Get Index of the current diff that is selected */
	int32 GetCurrentDiffIndex() ;

	/* Called when a new row is being generated */
	TSharedRef<ITableRow> OnGenerateRow(FSharedDiffOnGraph ParamItem, const TSharedRef<STableViewBase>& OwnerTable );

	/** ListView of differences */
	TSharedPtr<SListViewType> DiffList;

	/** Source for list view */
	TArray<FSharedDiffOnGraph> DiffListSource;

	/** Key commands processed by this widget */
	TSharedPtr< FUICommandList > KeyCommands;
};

/*panel used to display the blueprint*/
struct KISMET_API FDiffPanel
{
	FDiffPanel();

	/*Generate this panel based on the specified graph*/
	void	GeneratePanel(UEdGraph* Graph, UEdGraph* GraphToDiff);

	/*Get the title to show at the top of the panel*/
	FString GetTitle() const;

	/* Called when user hits keyboard shortcut to copy nodes*/
	void CopySelectedNodes();

	/*Gets whatever nodes are selected in the Graph Editor*/
	FGraphPanelSelectionSet GetSelectedNodes() const;

	/*Can user copy any of the selected nodes?*/
	bool CanCopyNodes() const;

	/*Functions used to focus/find a particular change in a diff result*/
	void FocusDiff(UEdGraphPin& Pin);
	void FocusDiff(UEdGraphNode& Node);

	/*The blueprint that owns the graph we are showing*/
	const class UBlueprint*				Blueprint;

	/*The border around the graph editor, used to change the content when new graphs are set */
	TSharedPtr<SBorder>				GraphEditorBorder;

	/*The graph editor which does the work of displaying the graph*/
	TWeakPtr<class SGraphEditor>	GraphEditor;

	/*Revision information for this blueprint */
	FRevisionInfo					RevisionInfo;

	/*A name identifying which asset this panel is displaying */
	bool							bShowAssetName;

	/*The panel stores the last pin that was focused on by the user, so that it can clear the visual style when selection changes*/
	UEdGraphPin*					LastFocusedPin;
private:
	/*Command list for this diff panel*/
	TSharedPtr<FUICommandList> GraphEditorCommands;
};

/* Visual Diff between two Blueprints*/
class  KISMET_API SBlueprintDiff: public SCompoundWidget
{

public:
	DECLARE_DELEGATE_TwoParams( FOpenInDefaults, const class UBlueprint* , const class UBlueprint* );

	SLATE_BEGIN_ARGS( SBlueprintDiff ){}
			SLATE_ARGUMENT( const class UBlueprint*, BlueprintOld )
			SLATE_ARGUMENT( const class UBlueprint*, BlueprintNew )
			SLATE_ARGUMENT( struct FRevisionInfo, OldRevision )
			SLATE_ARGUMENT( struct FRevisionInfo, NewRevision )
			SLATE_ARGUMENT( bool, ShowAssetNames )
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Called when a new Graph is clicked on by user */
	void OnGraphChanged(FListItemGraphToDiff* Diff);

	/** Helper function for generating an empty widget */
	static TSharedRef<SWidget> DefaultEmptyPanel();

protected:
	/* Need to process keys for shortcuts to buttons */
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyboardEvent& InKeyboardEvent ) override;


	typedef TSharedPtr<FListItemGraphToDiff>	FGraphToDiff;
	typedef SListView<FGraphToDiff >	SListViewType;

	/** Bring these revisions of graph into focus on main display*/
	void FocusOnGraphRevisions( class UEdGraph* GraphOld, class UEdGraph* GraphNew , FListItemGraphToDiff* Diff);

	/*Create a list item entry graph that exists in at least one of the blueprints */
	void CreateGraphEntry(class UEdGraph* GraphOld, class UEdGraph* GraphNew);

	/* Called when a new row is being generated */
	TSharedRef<ITableRow> OnGenerateRow(FGraphToDiff ParamItem, const TSharedRef<STableViewBase>& OwnerTable );

	/*Called when user clicks on a new graph list item */
	void OnSelectionChanged(FGraphToDiff Item, ESelectInfo::Type SelectionType);

	void OnDiffListSelectionChanged(const TSharedPtr<struct FDiffResultItem>& TheDiff, FListItemGraphToDiff* GraphDiffer);
		
	/** Disable the focus on a particular pin */
	void DisablePinDiffFocus();

	/*User toggles the option to lock the views between the two blueprints */
	FReply	OnToggleLockView();

	/*Reset the graph editor, called when user switches graphs to display*/
	void ResetGraphEditors();

	/*Get the image to show for the toggle lock option*/
	const FSlateBrush* GetLockViewImage() const;

	/* This buffer stores the currently displayed results */
	TArray< FGraphToDiff> Graphs;

	/** Get Graph editor associated with this Graph */
	FDiffPanel& GetDiffPanelForNode(UEdGraphNode& Node);

	/** Event handler that updates the graph view when user selects a new graph */
	void HandleGraphChanged( const FString& GraphName );

	TSharedRef<SWidget> GenerateGraphPanel();
	TSharedRef<SWidget> GenerateDefaultsPanel();
	TSharedRef<SWidget> GenerateComponentsPanel();

	/** Accessor and event handler for toggling between diff view modes (defaults, components, graph view, interface, macro): */
	void SetCurrentMode(FName NewMode);
	FName GetCurrentMode() const { return CurrentMode; }

	FName CurrentMode;

	/*The two panels used to show the old & new revision*/ 
	FDiffPanel				PanelOld, PanelNew;
	
	/** If the two views should be locked */
	bool	bLockViews;

	/** Border Widget, inside is the current graphs being diffed, we can replace content to change the graph*/
	TSharedPtr<SBorder>	DiffListBorder;

	/** Contents widget that we swap when mode changes (defaults, components, etc) */
	TSharedPtr<SBorder> ModeContents;

	/** The ListView containing the graphs the user can select */
	TSharedPtr<SListViewType>	GraphsToDiff;

	friend struct FListItemGraphToDiff;
};


