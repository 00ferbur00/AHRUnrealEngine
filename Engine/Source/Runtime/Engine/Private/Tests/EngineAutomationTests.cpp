// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "Tests/AutomationTestSettings.h"

#if WITH_EDITOR
#include "FileHelpers.h"
#endif

#include "AutomationCommon.h"
#include "AutomationTestCommon.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogEngineAutomationTests, Log, All);

namespace
{
	UWorld* GetSimpleEngineAutomationTestGameWorld(const int32 TestFlags)
	{
		// Accessing the game world is only valid for game-only 
		check((TestFlags & EAutomationTestFlags::ATF_ApplicationMask) == EAutomationTestFlags::ATF_Game);
		check(GEngine->GetWorldContexts().Num() == 1);
		check(GEngine->GetWorldContexts()[0].WorldType == EWorldType::Game);

		return GEngine->GetWorldContexts()[0].World();
	}

	/**
	* Populates the test names and commands for complex tests that are ran on all available maps
	*
	* @param OutBeautifiedNames - The list of map names
	* @param OutTestCommands - The list of commands for each test (The file names in this case)
	*/
	void PopulateTestsForAllAvailableMaps(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands)
	{
		TArray<FString> FileList;
#if WITH_EDITOR
		FEditorFileUtils::FindAllPackageFiles(FileList);
#else
		// Look directly on disk. Very slow!
		FPackageName::FindPackagesInDirectory(FileList, *FPaths::GameContentDir());
#endif

		// Iterate over all files, adding the ones with the map extension..
		for (int32 FileIndex = 0; FileIndex < FileList.Num(); FileIndex++)
		{
			const FString& Filename = FileList[FileIndex];

			// Disregard filenames that don't have the map extension if we're in MAPSONLY mode.
			if (FPaths::GetExtension(Filename, true) == FPackageName::GetMapPackageExtension())
			{
				if (FAutomationTestFramework::GetInstance().ShouldTestContent(Filename))
				{
					OutBeautifiedNames.Add(FPaths::GetBaseFilename(Filename));
					OutTestCommands.Add(Filename);
				}
			}
		}
	}
}

/**
 * SetRes Verification - Verify changing resolution works
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST( FSetResTest, "Windows.Set Resolution", EAutomationTestFlags::ATF_Game )

/** 
 * Change resolutions, wait, and change back
 *
 * @param Parameters - Unused for this test
 * @return	TRUE if the test was successful, FALSE otherwise
 */
bool FSetResTest::RunTest(const FString& Parameters)
{
	UAutomationTestSettings const* AutomationTestSettings = GetDefault<UAutomationTestSettings>();
	check(AutomationTestSettings);
	FString MapName = AutomationTestSettings->AutomationTestmap.FilePath;
	GEngine->Exec(GetSimpleEngineAutomationTestGameWorld(GetTestFlags()), *FString::Printf(TEXT("Open %s"), *MapName));

	int32 ResX = GSystemResolution.ResX;
	int32 ResY = GSystemResolution.ResY;
	FString RestoreResolutionString = FString::Printf(TEXT("setres %dx%d"), ResX, ResY);

	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("setres 640x480")));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(RestoreResolutionString));

	return true;
}

/**
 * Stats verification - Toggle various "stats" commands
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST( FStatsVerificationMapTest, "Maps.Stats Verification", EAutomationTestFlags::ATF_Game )

/** 
 * Execute the loading of one map to verify screen captures and performance captures work
 *
 * @param Parameters - Unused for this test
 * @return	TRUE if the test was successful, FALSE otherwise
 */
bool FStatsVerificationMapTest::RunTest(const FString& Parameters)
{
	UAutomationTestSettings const* AutomationTestSettings = GetDefault<UAutomationTestSettings>();
	check(AutomationTestSettings);
	FString MapName = AutomationTestSettings->AutomationTestmap.FilePath;

	GEngine->Exec(GetSimpleEngineAutomationTestGameWorld(GetTestFlags()), *FString::Printf(TEXT("Open %s"), *MapName));

	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat game")));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat game")));

	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat scenerendering")));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat scenerendering")));

	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat memory")));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat memory")));

	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat slate")));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat slate")));

	return true;
}

/**
 * LoadAutomationMap
 * Verification automation test to make sure features of map loading work (load, screen capture, performance capture)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST( FPerformanceCaptureTest, "Maps.Performance Capture", EAutomationTestFlags::ATF_Game )

/** 
 * Execute the loading of one map to verify screen captures and performance captures work
 *
 * @param Parameters - Unused for this test
 * @return	TRUE if the test was successful, FALSE otherwise
 */
bool FPerformanceCaptureTest::RunTest(const FString& Parameters)
{
	UAutomationTestSettings const* AutomationTestSettings = GetDefault<UAutomationTestSettings>();
	check(AutomationTestSettings);
	FString MapName = AutomationTestSettings->AutomationTestmap.FilePath;

	GEngine->Exec(GetSimpleEngineAutomationTestGameWorld(GetTestFlags()), *FString::Printf(TEXT("Open %s"), *MapName));
	ADD_LATENT_AUTOMATION_COMMAND(FEnqueuePerformanceCaptureCommands());

	return true;
}

/**
 * Latent command to take a screenshot of the viewport
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FTakeViewportScreenshotCommand, FString, ScreenshotFileName);

bool FTakeViewportScreenshotCommand::Update()
{
	
	FScreenshotRequest::RequestScreenshot( ScreenshotFileName, false );
	return true;
}

/**
 * LoadAllMapsInGame
 * Verification automation test to make sure loading all maps succeed without crashing AND does performance captures
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST( FLoadAllMapsInGameTest, "Maps.Load All In Game", EAutomationTestFlags::ATF_Game )

/** 
 * Requests a enumeration of all maps to be loaded
 */
void FLoadAllMapsInGameTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
{
	PopulateTestsForAllAvailableMaps(OutBeautifiedNames, OutTestCommands);
}

/** 
 * Execute the loading of each map and performance captures
 *
 * @param Parameters - Should specify which map name to load
 * @return	TRUE if the test was successful, FALSE otherwise
 */
bool FLoadAllMapsInGameTest::RunTest(const FString& Parameters)
{
	FString MapName = Parameters;

	//Open the map
	GEngine->Exec(GetSimpleEngineAutomationTestGameWorld(GetTestFlags()), *FString::Printf(TEXT("Open %s"), *MapName));

	if( FAutomationTestFramework::GetInstance().IsScreenshotAllowed() )
	{
		//Generate the screen shot name and path
		FString ScreenshotFileName;
		const FString TestName = FString::Printf(TEXT("LoadAllMaps_Game/%s"), *FPaths::GetBaseFilename(MapName));
		AutomationCommon::GetScreenshotPath(TestName, ScreenshotFileName, true);

		//Give the map some time to load
		ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.5f));
		//Take the screen shot
		ADD_LATENT_AUTOMATION_COMMAND(FTakeViewportScreenshotCommand(ScreenshotFileName));
		//Give the screen shot a chance to capture the scene
		ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
	}

	//Kick off any Automation matinees that are in this map
	ADD_LATENT_AUTOMATION_COMMAND(FEnqueuePerformanceCaptureCommands());

	return true;
}

/**
 * SaveGameTest
 * Test makes sure a save game (without UI) saves and loads correctly
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST( FSaveGameTest, "Engine.Game.Noninteractive Save", EAutomationTestFlags::ATF_Game )

/** 
 * Saves and loads a savegame file
 *
 * @param Parameters - Unused for this test
 * @return	TRUE if the test was successful, FALSE otherwise
 */
bool FSaveGameTest::RunTest(const FString& Parameters)
{
	// automation save name
	const TCHAR* SaveName = TEXT("AutomationSaveTest");
	uint32 SavedData = 99;

	// the blob we are going to write out
	TArray<uint8> Blob;
	FMemoryWriter WriteAr(Blob);
	WriteAr << SavedData;

	// get the platform's save system
	ISaveGameSystem* Save = IPlatformFeaturesModule::Get().GetSaveGameSystem();

	// write it out
	if (Save->SaveGame(false, SaveName, 0, Blob) == false)
	{
		return false;
	}

	// make sure it was written
	if (Save->DoesSaveGameExist(SaveName, 0) == false)
	{
		return false;
	}

	// read it back in
	Blob.Empty();
	if (Save->LoadGame(false, SaveName, 0, Blob) == false)
	{
		return false;
	}

	// make sure it's the same data
	FMemoryReader ReadAr(Blob);
	uint32 LoadedData;
	ReadAr << LoadedData;

	return LoadedData == SavedData;
}

/**
 * Latent command to load a map in game
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FLoadGameMapCommand, FString, MapName);

bool FLoadGameMapCommand::Update()
{
	check(GEngine->GetWorldContexts().Num() == 1);
	check(GEngine->GetWorldContexts()[0].WorldType == EWorldType::Game);

	GEngine->Exec(GEngine->GetWorldContexts()[0].World(), *FString::Printf(TEXT("Open %s"), *MapName));
	return true;
}

/**
 * Latent command to run an exec command that also requires a UWorld.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FExecWorldStringLatentCommand, FString, ExecCommand);

bool FExecWorldStringLatentCommand::Update()
{
	check(GEngine->GetWorldContexts().Num() == 1);
	check(GEngine->GetWorldContexts()[0].WorldType == EWorldType::Game);

	GEngine->Exec(GEngine->GetWorldContexts()[0].World(), *ExecCommand);
	return true;
}

/**
 * Automation test to load a map and capture FPS performance charts
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCinematicFPSPerfTest, "Engine.Cinematic FPS Perf Capture", EAutomationTestFlags::ATF_Game);

void FCinematicFPSPerfTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
{
	PopulateTestsForAllAvailableMaps(OutBeautifiedNames, OutTestCommands);
}

bool FCinematicFPSPerfTest::RunTest(const FString& Parameters)
{
	//Check we are running from commandline
	const FString CommandLine(FCommandLine::Get());
	if( CommandLine.Contains(TEXT("AutomationTests")) )
	{
		//Get the name of the console event to trigger the cinematic
		FString CinematicEventCommand;
		if( !FParse::Value(*CommandLine, TEXT("CE="), CinematicEventCommand) )
		{
			CinematicEventCommand = TEXT("CE Start");
		}

		//Get the length of time to let the cinematic run
		float RunTime;
		if( !FParse::Value(*CommandLine, TEXT("RunTime="), RunTime) )
		{
			RunTime=5.f;
		}

		//Load map
		const FString MapName = Parameters;
		ADD_LATENT_AUTOMATION_COMMAND(FLoadGameMapCommand(MapName));
		ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0f));

		//Start the matinee and perform the FPS Chart
		ADD_LATENT_AUTOMATION_COMMAND(FExecWorldStringLatentCommand(CinematicEventCommand));
		ADD_LATENT_AUTOMATION_COMMAND(FExecWorldStringLatentCommand(TEXT("StartFPSChart")));
		ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(RunTime));
		ADD_LATENT_AUTOMATION_COMMAND(FExecWorldStringLatentCommand(TEXT("StopFPSChart")));
	}
	else
	{
		UE_LOG(LogEngineAutomationTests, Warning, TEXT("FCinematicFPSPerfTest is a Commandline test.  Please use -AutomationTests=\"Engine.Cinematic FPS Perf Capture\""));
		return false;
	}

	return true;
}


/* UAutomationTestSettings interface
 *****************************************************************************/

UAutomationTestSettings::UAutomationTestSettings( const class FPostConstructInitializeProperties& PCIP )
	: Super(PCIP)
{
}
