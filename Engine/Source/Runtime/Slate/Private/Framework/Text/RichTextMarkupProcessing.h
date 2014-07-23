// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_FANCY_TEXT

#include "IRichTextMarkupParser.h"

class FRichTextMarkupProcessing : public IRichTextMarkupParser
{
public:
	static TSharedRef< FRichTextMarkupProcessing > Create();

public:
	virtual void Process(TArray<FTextLineParseResults>& Results, const FString& Input, FString& Output) override;

private:
	FRichTextMarkupProcessing() {}

	void CalculateLineRanges(const FString& Input, TArray<FTextRange>& LineRanges) const;
	void ParseLineRanges(const FString& Input, const TArray<FTextRange>& LineRanges, TArray<FTextLineParseResults>& LineParseResultsArray) const;
	void HandleEscapeSequences(const FString& Input, TArray<FTextLineParseResults>& LineParseResultsArray, FString& ConcatenatedUnescapedLines) const;

};


#endif //WITH_FANCY_TEXT