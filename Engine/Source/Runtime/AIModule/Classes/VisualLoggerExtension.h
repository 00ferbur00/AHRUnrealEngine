// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#pragma once
#if ENABLE_VISUAL_LOG
#	include "VisualLog.h"
#endif
#include "VisualLoggerExtension.generated.h"

namespace EVisLogTags
{
	const FString TAG_EQS = TEXT("EQS");
}

#if ENABLE_VISUAL_LOG
class FVisualLoggerExtension : public FVisualLogExtensionInterface
{
public:
	FVisualLoggerExtension();
	virtual void OnTimestampChange(float Timestamp, class UWorld* InWorld, class AActor* HelperActor) override;
	virtual void DrawData(class UWorld* InWorld, class UCanvas* Canvas, class AActor* HelperActor, const FName& TagName, const struct FVisualLogEntry::FDataBlock& DataBlock, float Timestamp) override;
	virtual void DisableDrawingForData(class UWorld* InWorld, class UCanvas* Canvas, class AActor* HelperActor, const FName& TagName, const FVisualLogEntry::FDataBlock& DataBlock, float Timestamp) override;
	virtual void LogEntryLineSelectionChanged(TSharedPtr<struct FLogEntryItem> SelectedItem, int64 UserData, FName TagName) override;

private:
	void DisableEQSRendering(class AActor* HelperActor);

protected:
	uint32 CachedEQSId;
	uint32 SelectedEQSId;
	float CurrentTimestamp;
};
#endif //ENABLE_VISUAL_LOG

UCLASS(Abstract, CustomConstructor)
class AIMODULE_API UVisualLoggerExtension : public UObject
{
	GENERATED_UCLASS_BODY()

	UVisualLoggerExtension(const class FPostConstructInitializeProperties& PCIP) : Super(PCIP) {}
};
