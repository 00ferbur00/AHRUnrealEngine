// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EditorFramework/AssetImportData.h"
#include "FbxAssetImportData.generated.h"

/**
 * Base class for import data and options used when importing any asset from FBX
 */
UCLASS(config=EditorUserSettings, HideCategories=Object, abstract)
class UNREALED_API UFbxAssetImportData : public UAssetImportData
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FVector ImportTranslation;

	UPROPERTY()
	FRotator ImportRotation;

	UPROPERTY()
	float ImportUniformScale;
};