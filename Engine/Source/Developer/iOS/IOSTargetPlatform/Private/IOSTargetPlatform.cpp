// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IOSTargetPlatform.cpp: Implements the FIOSTargetPlatform class.
=============================================================================*/

#include "IOSTargetPlatformPrivatePCH.h"
#include "IProjectManager.h"

/* FIOSTargetPlatform structors
 *****************************************************************************/

FIOSTargetPlatform::FIOSTargetPlatform()
{
#if WITH_ENGINE
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName());
	TextureLODSettings.Initialize(EngineSettings, TEXT("SystemSettings"));
	StaticMeshLODSettings.Initialize(EngineSettings);
#endif // #if WITH_ENGINE

	// Initialize Ticker for device discovery
	TickDelegate = FTickerDelegate::CreateRaw(this, &FIOSTargetPlatform::HandleTicker);
	FTicker::GetCoreTicker().AddTicker(TickDelegate, 10.0f);
	
	// initialize the connected device detector
	DeviceHelper.OnDeviceConnected().AddRaw(this, &FIOSTargetPlatform::HandleDeviceConnected);
	DeviceHelper.OnDeviceDisconnected().AddRaw(this, &FIOSTargetPlatform::HandleDeviceDisconnected);
	DeviceHelper.Initialize();
}


FIOSTargetPlatform::~FIOSTargetPlatform()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegate);
}


/* ITargetPlatform interface
 *****************************************************************************/

void FIOSTargetPlatform::EnableDeviceCheck(bool OnOff)
{
	FIOSDeviceHelper::EnableDeviceCheck(OnOff);
}

void FIOSTargetPlatform::GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const
{
	OutDevices.Reset();

	for (auto Iter = Devices.CreateConstIterator(); Iter; ++Iter)
	{
		OutDevices.Add(Iter.Value());
	}
}


ITargetDevicePtr FIOSTargetPlatform::GetDefaultDevice() const
{
	if (Devices.Num() > 0)
	{
		// first device is the default
		auto Iter = Devices.CreateConstIterator();
		if(Iter)
		{
			return Iter.Value();
		}
	}

	return NULL;
}


ITargetDevicePtr FIOSTargetPlatform::GetDevice( const FTargetDeviceId& DeviceId )
{
	return Devices.FindRef(DeviceId);
}


bool FIOSTargetPlatform::IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const
{
	bool biOSSDKInstalled = true; // @todo How do we check that the iOS SDK is installed when building from Windows? Is that even possible?
#if PLATFORM_MAC
	OutDocumentationPath = FString("Shared/Tutorials/InstallingXCodeTutorial");
	biOSSDKInstalled = IFileManager::Get().DirectoryExists(TEXT("/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform"));
#else
	{
		HKEY hKey;
		LRESULT lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared"), 0, KEY_READ, &hKey);
		TCHAR dllPath[256];
		unsigned long pathSize = 256;
		if (lRes != ERROR_SUCCESS || RegQueryValueEx(hKey, TEXT("iTunesMobileDeviceDLL"), 0, NULL, (BYTE*)dllPath, &pathSize) != ERROR_SUCCESS || IFileManager::Get().FileSize(*FString(dllPath)) == INDEX_NONE)
		{
			OutDocumentationPath = FString("Shared/Tutorials/InstallingiTunesTutorial");
			biOSSDKInstalled = false;
		}
	}

#endif
	return biOSSDKInstalled;
}

int FIOSTargetPlatform::DoesntHaveRequirements(const FString& ProjectPath, bool bProjectHasCode, FString& OutDocumentationPath) const
{
	int bReadyToBuild = ETargetPlatformReadyStatus::Ready; // @todo How do we check that the iOS SDK is installed when building from Windows? Is that even possible?
	if (!IsSdkInstalled(bProjectHasCode, OutDocumentationPath))
	{
		bReadyToBuild |= ETargetPlatformReadyStatus::SDKNotFound;
	}
#if PLATFORM_MAC
	OutDocumentationPath = FString("Shared/Tutorials/InstallingXCodeTutorial");
#else
	if (bProjectHasCode && FRocketSupport::IsRocket())
	{
		OutDocumentationPath = FString("Shared/Tutorials/iOSonPCRestrictions");
		bReadyToBuild |= ETargetPlatformReadyStatus::CodeUnsupported;
	}
	if (FRocketSupport::IsRocket() && IProjectManager::Get().IsNonDefaultPluginEnabled())
	{
		OutDocumentationPath = FString("Shared/Tutorials/iOSonPCValidPlugins");
		bReadyToBuild |= ETargetPlatformReadyStatus::PluginsUnsupported;
	}

	// shell to IPP and get the status of the provision and cert
	FString CmdExe = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNet/IOS/IPhonePackager.exe"));
	FString CommandLine = FString::Printf(TEXT("Validate Engine -project %s"), *ProjectPath);
	TSharedPtr<FMonitoredProcess> IPPProcess = MakeShareable(new FMonitoredProcess(CmdExe, CommandLine, true));
	IPPProcess->Launch();
	while(IPPProcess->IsRunning())
	{
		FPlatformProcess::Sleep(0.01f);
	}
	int RetCode = IPPProcess->GetReturnCode();
	if (RetCode == 14)
	{
		OutDocumentationPath = FString("Shared/Tutorials/CreatingInfoPlist");
		bReadyToBuild |= ETargetPlatformReadyStatus::ManifestNotFound;
	}
	else if (RetCode == 13)
	{
		OutDocumentationPath = FString("Shared/Tutorials/CreatingSigningCertAndProvision");
		bReadyToBuild |= ETargetPlatformReadyStatus::SigningKeyNotFound;
		bReadyToBuild |= ETargetPlatformReadyStatus::ProvisionNotFound;
	}
	else if (RetCode == 12)
	{
		OutDocumentationPath = FString("Shared/Tutorials/CreatingSigningCertAndProvision");
		bReadyToBuild |= ETargetPlatformReadyStatus::SigningKeyNotFound;
	}
	else if (RetCode == 11)
	{
		OutDocumentationPath = FString("Shared/Tutorials/CreatingSigningCertAndProvision");
		bReadyToBuild |= ETargetPlatformReadyStatus::ProvisionNotFound;
	}
#endif
	return bReadyToBuild;
}


/* FIOSTargetPlatform implementation
 *****************************************************************************/

void FIOSTargetPlatform::PingNetworkDevices()
{
	if (!MessageEndpoint.IsValid())
	{
		MessageEndpoint = FMessageEndpoint::Builder("FIOSTargetPlatform")
			.Handling<FIOSLaunchDaemonPong>(this, &FIOSTargetPlatform::HandlePongMessage);
	}

	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Publish(new FIOSLaunchDaemonPing(), EMessageScope::Network);
	}

	// remove disconnected & timed out devices
	FDateTime Now = FDateTime::UtcNow();

	for (auto DeviceIt = Devices.CreateIterator(); DeviceIt; ++DeviceIt)
	{
		FIOSTargetDevicePtr Device = DeviceIt->Value;

		if (Now > Device->LastPinged + FTimespan::FromSeconds(60.0))
		{
			DeviceIt.RemoveCurrent();
			DeviceLostEvent.Broadcast(Device.ToSharedRef());
		}
	}
}


/* FIOSTargetPlatform callbacks
 *****************************************************************************/

void FIOSTargetPlatform::HandlePongMessage( const FIOSLaunchDaemonPong& Message, const IMessageContextRef& Context )
{
	FTargetDeviceId DeviceId;
	FTargetDeviceId::Parse(Message.DeviceID, DeviceId);

	FIOSTargetDevicePtr& Device = Devices.FindOrAdd(DeviceId);

	if (!Device.IsValid())
	{
		Device = MakeShareable(new FIOSTargetDevice(*this));

		Device->SetFeature(ETargetDeviceFeatures::Reboot, Message.bCanReboot);
		Device->SetFeature(ETargetDeviceFeatures::PowerOn, Message.bCanPowerOn);
		Device->SetFeature(ETargetDeviceFeatures::PowerOff, Message.bCanPowerOff);
		Device->SetDeviceId(DeviceId);
		Device->SetDeviceName(Message.DeviceName);
		Device->SetDeviceType(Message.DeviceType);
		Device->SetDeviceEndpoint(Context->GetSender());
		Device->SetIsSimulated(Message.DeviceID.Contains(TEXT("Simulator")));

		DeviceDiscoveredEvent.Broadcast(Device.ToSharedRef());
	}

	Device->LastPinged = FDateTime::UtcNow();
}

void FIOSTargetPlatform::HandleDeviceConnected(const FIOSLaunchDaemonPong& Message)
{
	FTargetDeviceId DeviceId;
	FTargetDeviceId::Parse(Message.DeviceID, DeviceId);
	
	FIOSTargetDevicePtr& Device = Devices.FindOrAdd(DeviceId);
	
	if (!Device.IsValid())
	{
		Device = MakeShareable(new FIOSTargetDevice(*this));
		
		Device->SetFeature(ETargetDeviceFeatures::Reboot, Message.bCanReboot);
		Device->SetFeature(ETargetDeviceFeatures::PowerOn, Message.bCanPowerOn);
		Device->SetFeature(ETargetDeviceFeatures::PowerOff, Message.bCanPowerOff);
		Device->SetDeviceId(DeviceId);
		Device->SetDeviceName(Message.DeviceName);
		Device->SetDeviceType(Message.DeviceType);
		Device->SetIsSimulated(Message.DeviceID.Contains(TEXT("Simulator")));
		
		DeviceDiscoveredEvent.Broadcast(Device.ToSharedRef());
	}
	
	// Add a very long time period to prevent the devices from getting disconnected due to a lack of pong messages
	Device->LastPinged = FDateTime::UtcNow() + FTimespan(100, 0, 0, 0, 0);
}


void FIOSTargetPlatform::HandleDeviceDisconnected(const FIOSLaunchDaemonPong& Message)
{
	FTargetDeviceId DeviceId;
	FTargetDeviceId::Parse(Message.DeviceID, DeviceId);
	
	FIOSTargetDevicePtr& Device = Devices.FindOrAdd(DeviceId);
	
	if (Device.IsValid())
	{
		DeviceLostEvent.Broadcast(Device.ToSharedRef());
		Devices.Remove(DeviceId);
	}
}

bool FIOSTargetPlatform::HandleTicker(float DeltaTime )
{
	PingNetworkDevices();

	return true;
}


/* ITargetPlatform interface
 *****************************************************************************/

#if WITH_ENGINE

void FIOSTargetPlatform::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_OPENGL_ES2_IOS(TEXT("GLSL_ES2_IOS"));
	static FName NAME_SF_METAL(TEXT("SF_METAL"));

	// default to supporting ES2
	bool bSupportOpenGLES2 = true;
	GConfig->GetBool(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("bSupportOpenGLES2"), bSupportOpenGLES2, GEngineIni);
	if (bSupportOpenGLES2)
	{
		OutFormats.AddUnique(NAME_OPENGL_ES2_IOS);
	}

	// default to NOT supporting metal
	bool bSupportMetal = false;
	GConfig->GetBool(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("bSupportMetal"), bSupportMetal, GEngineIni);
	if (bSupportMetal)
	{
		OutFormats.AddUnique(NAME_SF_METAL);
	}
}


void FIOSTargetPlatform::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}


void FIOSTargetPlatform::GetTextureFormats( const UTexture* Texture, TArray<FName>& OutFormats ) const
{
	check(Texture);

	FName TextureFormatName = NAME_None;

	//// Supported texture format names.

	// Compressed Texture Formats
	static FName NamePVRTC2(TEXT("PVRTC2"));
	static FName NamePVRTC4(TEXT("PVRTC4"));

	// Same as PVRTC4, but derives Z from X and Y
	static FName NamePVRTCN(TEXT("PVRTCN"));
	static FName NameAutoPVRTC(TEXT("AutoPVRTC"));

	// Uncompressed Texture Formats
	static FName NameBGRA8(TEXT("BGRA8"));
	static FName NameG8(TEXT("G8"));
	static FName NameVU8(TEXT("VU8"));
	static FName NameRGBA16F(TEXT("RGBA16F"));
	bool bIsCubemap = Texture->IsA(UTextureCube::StaticClass());

	bool bNoCompression = Texture->CompressionNone				// Code wants the texture uncompressed.
		|| (Texture->LODGroup == TEXTUREGROUP_ColorLookupTable)	// Textures in certain LOD groups should remain uncompressed.
		|| (Texture->LODGroup == TEXTUREGROUP_Bokeh)
		|| (Texture->CompressionSettings == TC_EditorIcon)
		|| (Texture->Source.GetSizeX() < 4)						// Don't compress textures smaller than the DXT block size.
		|| (Texture->Source.GetSizeY() < 4)
		|| (Texture->Source.GetSizeX() % 4 != 0)
		|| (Texture->Source.GetSizeY() % 4 != 0);

	ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();

	// Determine the pixel format of the compressed texture.
	if (bNoCompression)
	{
		if (Texture->HasHDRSource())
		{
			TextureFormatName = NameRGBA16F;
		}
		else if (SourceFormat == TSF_G8 || Texture->CompressionSettings == TC_Grayscale)
		{
			TextureFormatName = NameG8;
		}
		else if (Texture->LODGroup == TEXTUREGROUP_Shadowmap)
		{
			TextureFormatName = NameG8;
		}
		else
		{
			TextureFormatName = NameBGRA8;
		}
	}
	else if (Texture->CompressionSettings == TC_HDR)
	{
		TextureFormatName = NameRGBA16F;
	}
	else if (Texture->CompressionSettings == TC_Normalmap)
	{
		TextureFormatName = NamePVRTCN;
	}
	else if (Texture->CompressionSettings == TC_Displacementmap)
	{
		TextureFormatName = NameG8;
	}
	else if (Texture->CompressionSettings == TC_VectorDisplacementmap)
	{
		TextureFormatName = NameBGRA8;
	}
	else if (Texture->CompressionSettings == TC_Grayscale)
	{
		TextureFormatName = NameG8;
	}
	else if (Texture->CompressionSettings == TC_Alpha)
	{
		TextureFormatName = NameG8;
	}
	else if (Texture->CompressionSettings == TC_DistanceFieldFont)
	{
		TextureFormatName = NameG8;
	}
	else if (Texture->bForcePVRTC4)
	{
		TextureFormatName = NamePVRTC4;
	}
	else if (Texture->CompressionNoAlpha)
	{
		TextureFormatName = NamePVRTC2;
	}
	else if (Texture->bDitherMipMapAlpha)
	{
		TextureFormatName = NamePVRTC4;
	}
	else
	{
		TextureFormatName = NameAutoPVRTC;
	}

	// Some PC GPUs don't support sRGB read from G8 textures (e.g. AMD DX10 cards on ShaderModel3.0)
	// This solution requires 4x more memory but a lot of PC HW emulate the format anyway
	if ((TextureFormatName == NameG8) && Texture->SRGB)
	{
		TextureFormatName = NameG8;
	}

	OutFormats.Add(TextureFormatName);
}


const FTextureLODSettings& FIOSTargetPlatform::GetTextureLODSettings() const
{
	return TextureLODSettings;
}


FName FIOSTargetPlatform::GetWaveFormat( class USoundWave* Wave ) const
{
	static FName NAME_ADPCM(TEXT("ADPCM"));
	return NAME_ADPCM;
}

#endif // WITH_ENGINE
