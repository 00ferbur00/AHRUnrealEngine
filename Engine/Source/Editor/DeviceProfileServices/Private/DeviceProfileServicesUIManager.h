// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FDeviceProfileServicesUIManager.h: Declares the FDeviceProfileServicesUIManager class.
=============================================================================*/

#pragma once


/**
 * Implements the device profile services manager for UI
 */
class FDeviceProfileServicesUIManager
	: public IDeviceProfileServicesUIManager
{
public:

	/**
	 * Default constructor
	 */
	FDeviceProfileServicesUIManager( );

	virtual ~FDeviceProfileServicesUIManager() {}

public:

	// Begin IDeviceProfileServicesUIManager Interface

	virtual const FName GetDeviceIconName( const FString& DeviceName ) const override;
	virtual const TArray<TSharedPtr<FString> > GetPlatformList( ) override;
	virtual void GetProfilesByType( TArray<UDeviceProfile*>& OutDeviceProfiles, const FString& InType ) override;
	virtual const FName GetPlatformIconName( const FString& DeviceName ) const override;
	virtual void SetProfile( const FString& DeviceProfileName ) override;

	// End IDeviceProfileServicesUIManager Interface

protected:

	/**
	 * Generates the UI icon list.
	 */
	void CreatePlatformMap();

	/**
	 * Refresh the UI list - rebuild lists.
	 */
	void HandleRefreshUIData();

private:

	// Map of profiles to platform types
	TMap<const UClass*, FString> PickerTypeMap;

	// Map of profiles to platform types
	TMap<const FString, FString> DeviceToPlatformMap;

	// Map of profiles to platform types
	TMap<const FString, FName> DeviceTypeToIconMap;

	// Holds the list of known platforms.
	TArray<TSharedPtr<FString> > PlatformList;
};
