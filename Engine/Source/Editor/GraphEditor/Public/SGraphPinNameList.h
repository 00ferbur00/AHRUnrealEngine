// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SGraphPin.h"
#include "SNameComboBox.h"

class GRAPHEDITOR_API SGraphPinNameList : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinNameList) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj, const TArray<TSharedPtr<FName>>& InNameList);

protected:

	/**
	 *	Function to create class specific widget.
	 *
	 *	@return Reference to the newly created widget object
	 */
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;

private:

	/**
	 *	Function to get current string associated with the combo box selection
	 *
	 *	@return currently selected string
	 */
	FString OnGetText() const;

	/**
	 *	Function to generate the list of indexes from the enum object
	 *
	 *	@param OutComboBoxIndexes - Int array reference to store the list of indexes
	 */
	void GenerateComboBoxIndexes( TArray< TSharedPtr<int32> >& OutComboBoxIndexes );

	/**
	 *	Function to set the newly selected index
	 *
	 * @param NewSelection The newly selected item in the combo box
	 * @param SelectInfo Provides context on how the selection changed
	 */
	void ComboBoxSelectionChanged(TSharedPtr<FName> StringItem, ESelectInfo::Type SelectInfo);

	/**
	 * Returns the friendly name of the enum at index EnumIndex
	 *
	 * @param EnumIndex	- The index of the enum to return the friendly name for
	 */
	FString OnGetFriendlyName(int32 EnumIndex);

	TSharedPtr<class SNameComboBox>	ComboBox;

	/** The actual list of FName values to choose from */
	TArray<TSharedPtr<FName>> NameList;
};
