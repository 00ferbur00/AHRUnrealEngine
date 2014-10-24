// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "IOSPlatformEditorPrivatePCH.h"
#include "IOSTargetSettingsCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyEditing.h"

#include "ScopedTransaction.h"
#include "SExternalImageReference.h"
#include "SHyperlinkLaunchURL.h"
#include "SPlatformSetupMessage.h"
#include "PlatformIconInfo.h"
#include "SourceControlHelpers.h"
#include "ManifestUpdateHelper.h"
#include "SNotificationList.h"
#include "NotificationManager.h"

#define LOCTEXT_NAMESPACE "IOSTargetSettings"

//////////////////////////////////////////////////////////////////////////
// FIOSTargetSettingsCustomization

TSharedRef<IDetailCustomization> FIOSTargetSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FIOSTargetSettingsCustomization);
}

FIOSTargetSettingsCustomization::FIOSTargetSettingsCustomization()
	: EngineInfoPath(FString::Printf(TEXT("%sBuild/IOS/UE4Game-Info.plist"), *FPaths::EngineDir()))
	, GameInfoPath(FString::Printf(TEXT("%sBuild/IOS/Info.plist"), *FPaths::GameDir()))
	, EngineGraphicsPath(FString::Printf(TEXT("%sBuild/IOS/Resources/Graphics"), *FPaths::EngineDir()))
	, GameGraphicsPath(FString::Printf(TEXT("%sBuild/IOS/Resources/Graphics"), *FPaths::GameDir()))
{
	new (IconNames) FPlatformIconInfo(TEXT("Icon29.png"), LOCTEXT("SettingsIcon_iPhone", "iPhone Settings Icon"), FText::GetEmpty(), 29, 29, FPlatformIconInfo::Optional);// also iOS6 spotlight search
	new (IconNames) FPlatformIconInfo(TEXT("Icon29@2x.png"), LOCTEXT("SettingsIcon_iPhoneRetina", "iPhone Retina Settings Icon"), FText::GetEmpty(), 58, 58, FPlatformIconInfo::Optional); // also iOS6 spotlight search
	new (IconNames) FPlatformIconInfo(TEXT("Icon40.png"), LOCTEXT("SpotlightIcon_iOS7", "iOS7 Spotlight Icon"), FText::GetEmpty(), 40, 40, FPlatformIconInfo::Optional);
	new (IconNames) FPlatformIconInfo(TEXT("Icon40@2x.png"), LOCTEXT("SpotlightIcon_Retina_iOS7", "Retina iOS7 Spotlight Icon"), FText::GetEmpty(), 80, 80, FPlatformIconInfo::Optional);
	new (IconNames) FPlatformIconInfo(TEXT("Icon50.png"), LOCTEXT("SpotlightIcon_iPad_iOS6", "iPad iOS6 Spotlight Icon"), FText::GetEmpty(), 50, 50, FPlatformIconInfo::Optional);
	new (IconNames) FPlatformIconInfo(TEXT("Icon50@2x.png"), LOCTEXT("SpotlightIcon_iPadRetina_iOS6", "iPad Retina iOS6 Spotlight Icon"), FText::GetEmpty(), 100, 100, FPlatformIconInfo::Optional);
	new (IconNames) FPlatformIconInfo(TEXT("Icon57.png"), LOCTEXT("AppIcon_iPhone_iOS6", "iPhone iOS6 App Icon"), FText::GetEmpty(), 57, 57, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon57@2x.png"), LOCTEXT("AppIcon_iPhoneRetina_iOS6", "iPhone Retina iOS6 App Icon"), FText::GetEmpty(), 114, 114, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon60@2x.png"), LOCTEXT("AppIcon_iPhoneRetina_iOS7", "iPhone Retina iOS7 App Icon"), FText::GetEmpty(), 120, 120, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon72.png"), LOCTEXT("AppIcon_iPad_iOS6", "iPad iOS6 App Icon"), FText::GetEmpty(), 72, 72, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon72@2x.png"), LOCTEXT("AppIcon_iPadRetina_iOS6", "iPad Retina iOS6 App Icon"), FText::GetEmpty(), 144, 144, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon76.png"), LOCTEXT("AppIcon_iPad_iOS7", "iPad iOS7 App Icon"), FText::GetEmpty(), 76, 76, FPlatformIconInfo::Required);
	new (IconNames) FPlatformIconInfo(TEXT("Icon76@2x.png"), LOCTEXT("AppIcon_iPadRetina_iOS7", "iPad Retina iOS7 App Icon"), FText::GetEmpty(), 152, 152, FPlatformIconInfo::Required);

	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default.png"), LOCTEXT("LaunchImage_iPhone", "Launch iPhone 4/4S"), FText::GetEmpty(), 320, 480, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default@2x.png"), LOCTEXT("LaunchImage_iPhoneRetina", "Launch iPhone 4/4S Retina"), FText::GetEmpty(), 640, 960, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-568h@2x.png"), LOCTEXT("LaunchImage_iPhone5", "Launch iPhone 5/5S Retina"), FText::GetEmpty(), 640, 1136, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-Landscape.png"), LOCTEXT("LaunchImage_iPad_Landscape", "Launch iPad in Landscape"), FText::GetEmpty(), 1024, 768, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-Landscape@2x.png"), LOCTEXT("LaunchImage_iPadRetina_Landscape", "Launch iPad Retina in Landscape"), FText::GetEmpty(), 2048, 1536, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-Portrait.png"), LOCTEXT("LaunchImage_iPad_Portrait", "Launch iPad in Portrait"), FText::GetEmpty(), 768, 1024, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-Portrait@2x.png"), LOCTEXT("LaunchImage_iPadRetina_Portrait", "Launch iPad Retina in Portrait"), FText::GetEmpty(), 1536, 2048, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-IPhone6.png"), LOCTEXT("LaunchImage_iPhone6", "Launch iPhone 6"), FText::GetEmpty(), 750, 1334, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-IPhone6Plus-Landscape.png"), LOCTEXT("LaunchImage_iPhone6Plus_Landscape", "Launch iPhone 6 Plus in Landscape"), FText::GetEmpty(), 2208, 1242, FPlatformIconInfo::Required);
	new (LaunchImageNames) FPlatformIconInfo(TEXT("Default-IPhone6Plus-Portrait.png"), LOCTEXT("LaunchImage_iPhone6Plus_Portrait", "Launch iPhone 6 Plus in Portrait"), FText::GetEmpty(), 1242, 2208, FPlatformIconInfo::Required);
}

void FIOSTargetSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	SavedLayoutBuilder = &DetailLayout;

	BuildPListSection(DetailLayout);

	BuildIconSection(DetailLayout);
}

void FIOSTargetSettingsCustomization::BuildPListSection(IDetailLayoutBuilder& DetailLayout)
{
	// Info.plist category
	IDetailCategoryBuilder& AppManifestCategory = DetailLayout.EditCategory(TEXT("Info.plist"));
	IDetailCategoryBuilder& BundleCategory = DetailLayout.EditCategory(TEXT("Bundle Information"));
	IDetailCategoryBuilder& OrientationCategory = DetailLayout.EditCategory(TEXT("Orientation"));
	IDetailCategoryBuilder& RenderCategory = DetailLayout.EditCategory(TEXT("Rendering"));
	IDetailCategoryBuilder& OSInfoCategory = DetailLayout.EditCategory(TEXT("OS Info"));
	IDetailCategoryBuilder& DeviceCategory = DetailLayout.EditCategory(TEXT("Devices"));
	IDetailCategoryBuilder& CookCategory = DetailLayout.EditCategory(TEXT("Cook Settings"));

	TSharedRef<SPlatformSetupMessage> PlatformSetupMessage = SNew(SPlatformSetupMessage, GameInfoPath)
		.PlatformName(LOCTEXT("iOSPlatformName", "iOS"))
		.OnSetupClicked(this, &FIOSTargetSettingsCustomization::CopySetupFilesIntoProject);

	SetupForPlatformAttribute = PlatformSetupMessage->GetReadyToGoAttribute();

	AppManifestCategory.AddCustomRow(TEXT("Warning"), false)
		.WholeRowWidget
		[
			PlatformSetupMessage
		];

	AppManifestCategory.AddCustomRow(TEXT("Info.plist Hyperlink"), false)
		.WholeRowWidget
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			[
				SNew(SHyperlinkLaunchURL, TEXT("https://developer.apple.com/library/ios/documentation/general/Reference/InfoPlistKeyReference/Articles/AboutInformationPropertyListFiles.html"))
				.Text(LOCTEXT("ApplePlistPage", "About Information Property List Files"))
				.ToolTipText(LOCTEXT("ApplePlistPageTooltip", "Opens a page that discusses Info.plist"))
			]
		];


	AppManifestCategory.AddCustomRow(TEXT("Info.plist"), false)
		.IsEnabled(SetupForPlatformAttribute)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlistLabel", "Info.plist"))
				.Font(DetailLayout.GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenPlistFolderButton", "Open PList Folder"))
				.ToolTipText(LOCTEXT("OpenPlistFolderButton_Tooltip", "Opens the folder containing the plist for the current project in Explorer or Finder"))
				.OnClicked(this, &FIOSTargetSettingsCustomization::OpenPlistFolder)
			]
		];

	// Show properties that are gated by the plist being present and writable
	FSimpleDelegate PlistModifiedDelegate = FSimpleDelegate::CreateRaw(this, &FIOSTargetSettingsCustomization::OnPlistPropertyModified);

#define SETUP_PLIST_PROP(PropName, Category, Tip) \
	{ \
		TSharedRef<IPropertyHandle> PropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UIOSRuntimeSettings, PropName)); \
		PropertyHandle->SetOnPropertyValueChanged(PlistModifiedDelegate); \
		Category.AddProperty(PropertyHandle) \
			.EditCondition(SetupForPlatformAttribute, NULL) \
			.ToolTip(Tip); \
	}

	SETUP_PLIST_PROP(BundleDisplayName, BundleCategory, TEXT("Specifies the the display name for the application. This will be displayed under the icon on the device."));
	SETUP_PLIST_PROP(BundleName, BundleCategory, TEXT("Specifies the the name of the application bundle. This is the short name for the application bundle."));
	SETUP_PLIST_PROP(BundleIdentifier, BundleCategory, TEXT("Specifies the bundle identifier for the application."));
	SETUP_PLIST_PROP(VersionInfo, BundleCategory, TEXT("Specifies the version for the application."));
	SETUP_PLIST_PROP(bSupportsPortraitOrientation, OrientationCategory, TEXT("Supports default portrait orientation. Landscape will not be supported."));
	SETUP_PLIST_PROP(bSupportsUpsideDownOrientation, OrientationCategory, TEXT("Supports upside down portrait orientation. Landscape will not be supported."));
	SETUP_PLIST_PROP(bSupportsLandscapeLeftOrientation, OrientationCategory, TEXT("Supports left landscape orientation. Protrait will not be supported."));
	SETUP_PLIST_PROP(bSupportsLandscapeRightOrientation, OrientationCategory, TEXT("Supports right landscape orientation. Protrait will not be supported."));
	
	SETUP_PLIST_PROP(bSupportsMetal, RenderCategory, TEXT("Whether or not to add support for Metal API (requires IOS8 and A7 processors)."));
	SETUP_PLIST_PROP(bSupportsOpenGLES2, RenderCategory, TEXT("Whether or not to add support for OpenGL ES2 (if this is false, then your game should specify minimum IOS8 version and use \"metal\" instead of \"opengles-2\" in UIRequiredDeviceCapabilities)"));

	SETUP_PLIST_PROP(bSupportsIPad, DeviceCategory, TEXT("Whether or not to add support for iPad devices"));
	SETUP_PLIST_PROP(bSupportsIPhone, DeviceCategory, TEXT("Whether or not to add support for iPhone devices"));

	SETUP_PLIST_PROP(MinimumiOSVersion, OSInfoCategory, TEXT("WMinimum iOS version this game supports"));

#undef SETUP_PLIST_PROP
}

void FIOSTargetSettingsCustomization::BuildIconSection(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& RequiredIconCategory = DetailLayout.EditCategory(TEXT("Required Icons"));
	IDetailCategoryBuilder& OptionalIconCategory = DetailLayout.EditCategory(TEXT("Optional Icons"));

	// Add the icons
	for (const FPlatformIconInfo& Info : IconNames)
	{
		const FVector2D IconImageMaxSize(Info.IconRequiredSize);
		IDetailCategoryBuilder& IconCategory = (Info.RequiredState == FPlatformIconInfo::Required) ? RequiredIconCategory : OptionalIconCategory;
		BuildImageRow(DetailLayout, IconCategory, Info, IconImageMaxSize);
	}

	// Add the launch images
	IDetailCategoryBuilder& LaunchImageCategory = DetailLayout.EditCategory(TEXT("Launch Images"));
	const FVector2D LaunchImageMaxSize(150.0f, 150.0f);
	for (const FPlatformIconInfo& Info : LaunchImageNames)
	{
		BuildImageRow(DetailLayout, LaunchImageCategory, Info, LaunchImageMaxSize);
	}
}


FReply FIOSTargetSettingsCustomization::OpenPlistFolder()
{
	const FString EditPlistFolder = FPaths::ConvertRelativePathToFull(FPaths::GetPath(GameInfoPath));
	FPlatformProcess::ExploreFolder(*EditPlistFolder);

	return FReply::Handled();
}

void FIOSTargetSettingsCustomization::CopySetupFilesIntoProject()
{
	// First copy the plist, it must get copied
	FText ErrorMessage;
	if (!SourceControlHelpers::CopyFileUnderSourceControl(GameInfoPath, EngineInfoPath, LOCTEXT("InfoPlist", "Info.plist"), /*out*/ ErrorMessage))
	{
		FNotificationInfo Info(ErrorMessage);
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else
	{
		// Now try to copy all of the icons, etc... (these can be ignored if the file already exists)
		TArray<FPlatformIconInfo> Graphics;
		Graphics.Empty(IconNames.Num() + LaunchImageNames.Num());
		Graphics.Append(IconNames);
		Graphics.Append(LaunchImageNames);

		for (const FPlatformIconInfo& Info : Graphics)
		{
			const FString EngineImagePath = EngineGraphicsPath / Info.IconPath;
			const FString ProjectImagePath = GameGraphicsPath / Info.IconPath;

			if (!FPaths::FileExists(ProjectImagePath))
			{
				SourceControlHelpers::CopyFileUnderSourceControl(ProjectImagePath, EngineImagePath, Info.IconName, /*out*/ ErrorMessage);
			}
		}
	}

	SavedLayoutBuilder->ForceRefreshDetails();
}

void FIOSTargetSettingsCustomization::OnPlistPropertyModified()
{
	check(SetupForPlatformAttribute.Get() == true);
	const UIOSRuntimeSettings& Settings = *GetDefault<UIOSRuntimeSettings>();

	FManifestUpdateHelper Updater(GameInfoPath);

	// The text we're trying to replace looks like this:
	// 	<key>UISupportedInterfaceOrientations</key>
	// 	<array>
	// 		<string>UIInterfaceOrientationLandscapeRight</string>
	// 		<string>UIInterfaceOrientationLandscapeLeft</string>
	// 	</array>
	const FString InterfaceOrientations(TEXT("<key>UISupportedInterfaceOrientations</key>"));
	const FString ClosingArray(TEXT("</array>"));

	// Build the replacement array
	FString OrientationArrayBody = TEXT("\n\t<array>\n");
	if (Settings.bSupportsPortraitOrientation)
	{
		OrientationArrayBody += TEXT("\t\t<string>UIInterfaceOrientationPortrait</string>\n");
	}
	if (Settings.bSupportsUpsideDownOrientation)
	{
		OrientationArrayBody += TEXT("\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n");
	}
	if (Settings.bSupportsLandscapeLeftOrientation && (!Settings.bSupportsPortraitOrientation && !Settings.bSupportsUpsideDownOrientation))
	{
		OrientationArrayBody += TEXT("\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n");
	}
	if (Settings.bSupportsLandscapeRightOrientation && (!Settings.bSupportsPortraitOrientation && !Settings.bSupportsUpsideDownOrientation))
	{
		OrientationArrayBody += TEXT("\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n");
	}
	OrientationArrayBody += TEXT("\t");
	Updater.ReplaceKey(InterfaceOrientations, ClosingArray, OrientationArrayBody);

	// build the replacement bundle display name
	const FString BundleDisplayNameKey(TEXT("<key>CFBundleDisplayName</key>"));
	const FString ClosingString(TEXT("</string>"));
	FString BundleDisplayNameBody = TEXT("\n\t<string>") + Settings.BundleDisplayName;
	Updater.ReplaceKey(BundleDisplayNameKey, ClosingString, BundleDisplayNameBody);

	// build the replacement bundle display name
	const FString BundleNameKey(TEXT("<key>CFBundleName</key>"));
	FString BundleNameBody = TEXT("\n\t<string>") + Settings.BundleName;
	Updater.ReplaceKey(BundleNameKey, ClosingString, BundleNameBody);

	// build the replacement bundle identifier
	const FString BundleIdentifierKey(TEXT("<key>CFBundleIdentifier</key>"));
	FString BundleIdentifierBody = TEXT("\n\t<string>") + Settings.BundleIdentifier;
	Updater.ReplaceKey(BundleIdentifierKey, ClosingString, BundleIdentifierBody);

	// build the replacement version info
	const FString BundleShortVersionKey(TEXT("<key>CFBundleShortVersionString</key>"));
	FString VersionInfoBody = TEXT("\n\t<string>") + Settings.VersionInfo;
	Updater.ReplaceKey(BundleShortVersionKey, ClosingString, VersionInfoBody);

	// build the replacement required device caps
	const FString RequiredDeviceCaps(TEXT("<key>UIRequiredDeviceCapabilities</key>"));
	FString DeviceCapsArrayBody = TEXT("\n\t<array>\n");
	// automatically add armv7 for now
	DeviceCapsArrayBody += TEXT("\t\t<string>armv7</string>\n");
	if (Settings.bSupportsOpenGLES2)
	{
		DeviceCapsArrayBody += TEXT("\t\t<string>opengles-2</string>\n");
	}
	else if (Settings.bSupportsMetal)
	{
		DeviceCapsArrayBody += TEXT("\t\t<string>metal</string>\n");
	}
	DeviceCapsArrayBody += TEXT("\t");
	Updater.ReplaceKey(RequiredDeviceCaps, ClosingArray, DeviceCapsArrayBody);

	// build the replacement device families
	const FString DeviceFamilyKey(TEXT("<key>UIDeviceFamily</key>"));
	FString FamilyKeyBody = TEXT("\n\t<array>\n");
	// automatically add armv7 for now
	if (Settings.bSupportsIPhone)
	{
		FamilyKeyBody += TEXT("\t\t<integer>1</integer>\n");
	}
	if (Settings.bSupportsIPad)
	{
		FamilyKeyBody += TEXT("\t\t<integer>2</integer>\n");
	}
	FamilyKeyBody += TEXT("\t");
	Updater.ReplaceKey(DeviceFamilyKey, ClosingArray, FamilyKeyBody);

	// build the replacement min iOS version
	const FString MiniOSVersionKey(TEXT("<key>MinimumOSVersion</key>"));
	FString iOSVersionBody = TEXT("\n\t<string>");
	switch (Settings.MinimumiOSVersion)
	{
	case EIOSVersion::IOS_6:
		iOSVersionBody += TEXT("6.0");
		break;
	case EIOSVersion::IOS_7:
		iOSVersionBody += TEXT("7.0");
		break;
	case EIOSVersion::IOS_8:
		iOSVersionBody += TEXT("8.0");
		break;
	}
	Updater.ReplaceKey(MiniOSVersionKey, ClosingString, iOSVersionBody);

	// Write out the updated .plist
	Updater.Finalize(GameInfoPath, true, FFileHelper::EEncodingOptions::ForceUTF8);
}

void FIOSTargetSettingsCustomization::BuildImageRow(IDetailLayoutBuilder& DetailLayout, IDetailCategoryBuilder& Category, const FPlatformIconInfo& Info, const FVector2D& MaxDisplaySize)
{
	const FString AutomaticImagePath = EngineGraphicsPath / Info.IconPath;
	const FString TargetImagePath = GameGraphicsPath / Info.IconPath;

	Category.AddCustomRow(Info.IconName.ToString())
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(Info.IconName)
				.Font(DetailLayout.GetDetailFont())
			]
		]
		.ValueContent()
		.MaxDesiredWidth(400.0f)
		.MinDesiredWidth(100.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SExternalImageReference, AutomaticImagePath, TargetImagePath)
				.FileDescription(Info.IconDescription)
				.RequiredSize(Info.IconRequiredSize)
				.MaxDisplaySize(MaxDisplaySize)
			]
		];
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE