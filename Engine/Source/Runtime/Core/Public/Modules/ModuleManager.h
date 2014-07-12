// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModuleInterface.h"
#include "Delegate.h"
#include "Boilerplate/ModuleBoilerplate.h"


#if !IS_MONOLITHIC
	/** If true, we are reloading a class for HotReload */
	extern CORE_API bool GIsHotReload;
#endif


/**
 * Enumerates reasons for failed module loads.
 */
enum class EModuleLoadResult
{
	/** Module loaded successfully. */
	Success,

	/** The specified module file could not be found. */
	FileNotFound,

	/** The specified module file is incompatible with the module system. */
	FileIncompatible,

	/** The operating system failed to load the module file. */
	CouldNotBeLoadedByOS,

	/** Module initialization failed. */
	FailedToInitialize
};


/**
 * Enumerates possible results of a compilation operation.
 *
 * This enum has to be compatible with the one defined in the
 * UE4\Engine\Source\Programs\UnrealBuildTool\System\ExternalExecution.cs file
 * to keep communication between UHT, UBT and Editor compiling processes valid.
 */
namespace ECompilationResult
{
	enum Type
	{
		Succeeded = 0,
		FailedDueToHeaderChange = 1,
		OtherCompilationError = 2
	};
}


/**
 * Enumerates compilation methods for modules.
 */
enum class EModuleCompileMethod
{
	Runtime,
	External,
	Unknown
};


/**
 * Enumerates reasons for modules to change.
 *
 * Values of this type will be passed into OnModuleChanged() delegates.
 */
enum class EModuleChangeReason
{
	/** A module has been loaded and is ready to be used. */
	ModuleLoaded,

	/* A module has been unloaded and should no longer be used. */
	ModuleUnloaded,

	/** The paths controlling which plug-ins are loaded have been changed and the given module has been found, but not yet loaded. */
	PluginDirectoryChanged
};


/**
 * Structure for reporting module statuses.
 */
struct FModuleStatus
{
	/** Default constructor. */
	FModuleStatus()
		: bIsLoaded(false)
		, bIsGameModule(false)
	{ }

	/** Short name for this module. */
	FString Name;

	/** Full path to this module file on disk. */
	FString FilePath;

	/** Whether the module is currently loaded or not. */
	bool bIsLoaded;

	/** Whether this module contains game play code. */
	bool bIsGameModule;

	/** The compilation method of this module. */
	FString CompilationMethod;
};


/**
 * Implements the module manager.
 *
 * The module manager is used to load and unload modules, as well as to keep track of all of the
 * modules that are currently loaded. You can access this singleton using FModuleManager::Get().
 */
class CORE_API FModuleManager
	: private FSelfRegisteringExec
{
public:

	/**
	 * Destructor.
	 */
	~FModuleManager();

	/**
	 * Gets the singleton instance of the module manager.
	 *
	 * @return The module manager instance.
	 */
	static FModuleManager& Get( );

public:

	/**
	 * Abandons a loaded module, leaving it loaded in memory but no longer tracking it in the module manager.
	 *
	 * @param InModuleName The name of the module to abandon.  Should not include path, extension or platform/configuration info.  This is just the "module name" part of the module file name.
	 * @see IsModuleLoaded, LoadModule, LoadModuleWithFailureReason, UnloadModule
	 */
	void AbandonModule( const FName InModuleName );

	/**
	 * Adds a module to our list of modules, unless it's already known.
	 *
	 * This method is used by the plug-in manager to register a plug-in module.
	 *
	 * @param InModuleName The base name of the module file.  Should not include path, extension or platform/configuration info.  This is just the "name" part of the module file name.  Names should be globally unique.
	 * @param InBinariesDirectory The directory where to find this file, or an empty string to search in the default locations.  This parameter is used by the plugin system to locate plugin binaries.
	 */
	void AddModule( const FName InModuleName );

	/**
	 * Gets the specified module.
	 *
	 * @param InModuleName Name of the module to return.
	 * @return 	The module, or nullptr if the module is not loaded.
	 * @see GetModuleChecked, GetModulePtr
	 */
	TSharedPtr<IModuleInterface> GetModule( const FName InModuleName );

	/**
	 * Checks whether the specified module is currently loaded.
	 *
	 * This is an O(1) operation.
	 *
	 * @param InModuleName The base name of the module file.  Should not include path, extension or platform/configuration info.  This is just the "module name" part of the module file name.  Names should be globally unique.
	 * @return true if module is currently loaded, false otherwise.
	 * @see AbandonModule, LoadModule, LoadModuleWithFailureReason, UnloadModule
	 */
	bool IsModuleLoaded( const FName InModuleName ) const;

	/**
	 * Loads the specified module.
	 *
	 * @param InModuleName The base name of the module file.  Should not include path, extension or platform/configuration info.  This is just the "module name" part of the module file name.  Names should be globally unique.
	 * @param bWasReloaded Indicates that the module has been reloaded (default = false).
	 * @return The loaded module, or nullptr if the load operation failed.
	 * @see AbandonModule, IsModuleLoaded, LoadModuleChecked, LoadModulePtr, LoadModuleWithFailureReason, UnloadModule
	 */
	TSharedPtr<IModuleInterface> LoadModule( const FName InModuleName, const bool bWasReloaded = false );

	/**
	 * Loads a module in memory then calls PostLoad.
	 *
	 * @param InModuleName The name of the module to load.
	 * @param Ar The archive to receive error messages, if any.
	 * @return true on success, false otherwise.
	 * @see UnloadOrAbandonModuleWithCallback
	 */
	bool LoadModuleWithCallback( const FName InModuleName, FOutputDevice &Ar );

	/**
	 * Loads the specified module and returns a result.
	 *
	 * @param InModuleName The base name of the module file.  Should not include path, extension or platform/configuration info.  This is just the "module name" part of the module file name.  Names should be globally unique.
	 * @param OutFailureReason Will contain the result.
	 * @param bWasReloaded Indicates that the module has been reloaded (default = false).
	 * @return The loaded module (null if the load operation failed).
	 * @see AbandonModule, IsModuleLoaded, LoadModule, LoadModuleChecked, LoadModulePtr, UnloadModule
	 */
	TSharedPtr<IModuleInterface> LoadModuleWithFailureReason( const FName InModuleName, EModuleLoadResult& OutFailureReason, const bool bWasReloaded = false );

	/**
	 * Queries information about a specific module name.
	 *
	 * @param InModuleName Module to query status for.
	 * @param OutModuleStatus Status of the specified module.
	 * @return true if the module was found and the OutModuleStatus is valid, false otherwise.
	 * @see QueryModules
	 */
	bool QueryModule( const FName InModuleName, FModuleStatus& OutModuleStatus );

	/**
	 * Queries information about all of the currently known modules.
	 *
	 * @param OutModuleStatuses Status of all modules.
	 * @see QueryModule
	 */
	void QueryModules( TArray<FModuleStatus>& OutModuleStatuses );

	/**
	 * Unloads a specific module
	 *
	 * @param InModuleName The name of the module to unload.  Should not include path, extension or platform/configuration info.  This is just the "module name" part of the module file name.
	 * @param bIsShutdown Is this unload module call occurring at shutdown (default = false).
	 * @return true if module was unloaded successfully, false otherwise.
	 * @see AbandonModule, IsModuleLoaded, LoadModule, LoadModuleWithFailureReason
	 */
	bool UnloadModule( const FName InModuleName, bool bIsShutdown = false );

	/**
	 * Calls PreUnload then either unloads or abandons a module in memory, depending on whether the module supports unloading.
	 *
	 * @param InModuleName The name of the module to unload.
	 * @param Ar The archive to receive error messages, if any.
	 * @see LoadModuleWithCallback
	 */
	void UnloadOrAbandonModuleWithCallback( const FName InModuleName, FOutputDevice &Ar );

public:

	/**
	  * Gets a module by name, checking to ensure it exists.
	  *
	  * This method checks whether the module actually exists. If the module does not exist, an assertion will be triggered.
	  *
	  * @param ModuleName The module to get.
	  * @return The interface to the module.
	  * @see GetModulePtr, LoadModulePtr, LoadModuleChecked
	  */
	template<typename TModuleInterface>
	static TModuleInterface& GetModuleChecked( const FName ModuleName )
	{
		FModuleManager& ModuleManager = FModuleManager::Get();

		checkf(ModuleManager.IsModuleLoaded(ModuleName), TEXT("Tried to get module interface for unloaded module: '%s'"), *(ModuleName.ToString()));
		return (TModuleInterface&)(*ModuleManager.GetModule(ModuleName));
	}

	/**
	  * Gets a module by name.
	  *
	  * @param ModuleName The module to get.
	  * @return The interface to the module, or nullptr if the module was not found.
	  * @see GetModuleChecked, LoadModulePtr, LoadModuleChecked
	  */
	template<typename TModuleInterface>
	static TModuleInterface* GetModulePtr( const FName ModuleName )
	{
		FModuleManager& ModuleManager = FModuleManager::Get();

		if (!ModuleManager.IsModuleLoaded(ModuleName))
		{
			return nullptr;
		}

		return (TModuleInterface*)(ModuleManager.GetModule(ModuleName).Get());
	}

	/**
	  * Loads a module by name, checking to ensure it exists.
	  *
	  * This method checks whether the module actually exists. If the module does not exist, an assertion will be triggered.
	  * If the module was already loaded previously, the existing instance will be returned.
	  *
	  * @param ModuleName The module to find and load
	  * @return	Returns the module interface, casted to the specified typename
	  * @see GetModulePtr, LoadModulePtr, LoadModuleChecked
	  */
	template<typename TModuleInterface>
	static TModuleInterface& LoadModuleChecked( const FName ModuleName )
	{
		FModuleManager& ModuleManager = FModuleManager::Get();

		if (!ModuleManager.IsModuleLoaded(ModuleName))
		{
			ModuleManager.LoadModule(ModuleName);
		}

		return GetModuleChecked<TModuleInterface>(ModuleName);
	}

	/**
	  * Loads a module by name.
	  *
	  * @param ModuleName The module to find and load.
	  * @return The interface to the module, or nullptr if the module was not found.
	  * @see GetModulePtr, GetModuleChecked, LoadModuleChecked
	  */
	template<typename TModuleInterface>
	static TModuleInterface* LoadModulePtr( const FName ModuleName )
	{
		FModuleManager& ModuleManager = FModuleManager::Get();

		if (!ModuleManager.IsModuleLoaded(ModuleName))
		{
			ModuleManager.LoadModule(ModuleName);
		}

		return GetModulePtr<TModuleInterface>(ModuleName);
	}

public:

	/**
	 * Finds module files on the disk for loadable modules matching the specified wildcard.
	 *
	 * @param WildcardWithoutExtension Filename part (no path, no extension, no build config info) to search for.
	 * @param OutModules List of modules found.
	 */
	void FindModules( const TCHAR* WildcardWithoutExtension, TArray<FName>& OutModules );

	/**
	 * Gets the number of loaded modules.
	 *
	 * @return The number of modules.
	 */
	int32 GetModuleCount( ) const
	{
		return Modules.Num();
	}

	/**
	 * Module manager ticking is only used to check for asynchronously compiled modules that may need to be reloaded
	 */
	void Tick( );

	/**
	 * Unloads modules during the shutdown process.
	 *
	 * This method is Usually called at various points while exiting an application.
	 */
	void UnloadModulesAtShutdown( );

	/**
	 * Checks for the solution file using the hard-coded location on disk
	 * Used to determine whether source code is potentially available for recompiles
	 * 
	 * @return	True if the solution file is found (source code MAY BE available)
	 */
	bool IsSolutionFilePresent();

	/**
	 * Returns the full path of the solution file
	 * 
	 * @return	SolutionFilepath
	 */
	FString GetSolutionFilepath();

	/**
	 * Tries to recompile the specified module.  If the module is loaded, it will first be unloaded (then reloaded after,
	 * if the recompile was successful.)
	 *
	 * @param InModuleName Name of the module to recompile.
	 * @param bReloadAfterRecompile If true, the module will automatically be reloaded after a successful compile.  Otherwise, you'll need to load it yourself after.
	 * @param Ar Output device for logging compilation status.
	 * @return	Returns true if the module was successfully recompiled (and reloaded, if it was previously loaded).
	 */
	bool RecompileModule( const FName InModuleName, const bool bReloadAfterRecompile, FOutputDevice &Ar );

	/** @return	Returns true if an asynchronous compile is currently in progress */
	bool IsCurrentlyCompiling() const;

	/**
	 * Declares a type of delegates that is executed after a module recompile has finished.
	 *
	 * The first argument signals whether compilation has finished.
	 * The second argument shows whether compilation was successful or not.
	 */
	DECLARE_DELEGATE_TwoParams( FRecompileModulesCallback, bool, bool );

	/**
	 * Tries to recompile the specified modules in the background.  When recompiling finishes, the specified callback
	 * delegate will be triggered, passing along a bool that tells you whether the compile action succeeded.  This
	 * function never tries to unload modules or to reload the modules after they finish compiling.  You should do
	 * that yourself in the recompile completion callback!
	 *
	 * @param ModuleNames Names of the modules to recompile
	 * @param RecompileModulesCallback Callback function to execute after compilation finishes (whether successful or not.)
	 * @param bWaitForCompletion True if the function should not return until recompilation attempt has finished and callbacks have fired
	 * @param Ar Output device for logging compilation status
	 * @return	True if the recompile action was kicked off successfully.  If this returns false, then the recompile callback will never fire.  In the case where bWaitForCompletion=false, this will also return false if the compilation failed for any reason.
	 */
	bool RecompileModulesAsync( const TArray< FName > ModuleNames, const FRecompileModulesCallback& RecompileModulesCallback, const bool bWaitForCompletion, FOutputDevice &Ar );

	/** Request that any current compilation operation be abandoned. */
	void RequestStopCompilation()
	{
		bRequestCancelCompilation = true;
	}

	/**
	 * Tries to compile the specified game project. Not used for recompiling modules that are already loaded.
	 *
	 * @param GameProjectFilename The filename (including path) of the game project to compile.
	 * @param Ar Output device for logging compilation status.
	 * @return Returns true if the project was successfully compiled.
	 */
	bool CompileGameProject( const FString& GameProjectFilename, FOutputDevice &Ar );

	/**
	 * Tries to compile the specified game projects editor. Not used for recompiling modules that are already loaded.
	 *
	 * @param GameProjectFilename The filename (including path) of the game project to compile.
	 * @param Ar Output device for logging compilation status.
	 * @return	Returns true if the project was successfully compiled
	 */
	bool CompileGameProjectEditor( const FString& GameProjectFilename, FOutputDevice &Ar );

	/**
	 * Tries to compile the specified game project. Not used for recompiling modules that are already loaded.
	 *
	 * @param GameProjectFilename The filename (including path) of the game project for which to generate code projects.
	 * @param Ar Output device for logging compilation status.
	 * @return	Returns true if the project was successfully compiled.
	 */
	bool GenerateCodeProjectFiles( const FString& GameProjectFilename, FOutputDevice &Ar );

	/**
	 * @return true if the UBT executable exists (in Rocket) or source code is available to compile it (in non-Rocket)
	 */
	bool IsUnrealBuildToolAvailable();

	/** Delegate that's used by the module manager to initialize a registered module that we statically linked with (monolithic only) */
	DECLARE_DELEGATE_RetVal( IModuleInterface*, FInitializeStaticallyLinkedModule )

	/**
	 * Registers an initializer for a module that is statically linked.
	 *
	 * @param InModuleName The name of this module.
	 * @param InInitializerDelegate The delegate that will be called to initialize an instance of this module.
	 */
	void RegisterStaticallyLinkedModule( const FName InModuleName, const FInitializeStaticallyLinkedModule& InInitializerDelegate )
	{
		StaticallyLinkedModuleInitializers.Add( InModuleName, InInitializerDelegate );
	}

	/**
	 * Called by the engine at startup to let the Module Manager know that it's now
	 * safe to process new UObjects discovered by loading C++ modules.
	 */
	void StartProcessingNewlyLoadedObjects();

	/** Adds an engine binaries directory. */
	void AddBinariesDirectory(const TCHAR *InDirectory, bool bIsGameDirectory);

	/**
	 *	Set the game binaries directory
	 *
	 *	@param InDirectory The game binaries directory.
	 */
	void SetGameBinariesDirectory(const TCHAR* InDirectory);

	/**
	 * Checks to see if the specified module exists and is compatible with the current engine version. 
	 *
	 * @param InModuleName The base name of the module file.
	 * @return true if module exists and is up to date, false otherwise.
	 */
	bool IsModuleUpToDate( const FName InModuleName ) const;

	/**
	 * Determines whether the specified module contains UObjects.  The module must already be loaded into
	 * memory before calling this function.
	 *
	 * @param ModuleName Name of the loaded module to check.
	 * @return True if the module was found to contain UObjects, or false if it did not (or wasn't loaded.)
	 */
	bool DoesLoadedModuleHaveUObjects( const FName ModuleName );

	/**
	 * Gets the build configuration for compiling modules, as required by UBT.
	 *
	 * @return	Configuration name for UBT.
	 */
	static const TCHAR *GetUBTConfiguration( );

public:

	/**
	 * Gets an event delegate that is executed when the set of known modules changed, i.e. upon module load or unload.
	 *
	 * The first parameter is the name of the module that changed.
	 * The second parameter is the reason for the change.
	 *
	 * @return The event delegate.
	 */
	DECLARE_EVENT_TwoParams(FModuleManager, FModulesChangedEvent, FName, EModuleChangeReason);
	FModulesChangedEvent& OnModulesChanged( )
	{
		return ModulesChangedEvent;
	}

	/**
	 * Gets an event delegate that is executed when compilation of a module has started.
	 *
	 * @return The event delegate.
	 */
	DECLARE_EVENT(FModuleManager, FModuleCompilerStartedEvent);
	FModuleCompilerStartedEvent& OnModuleCompilerStarted( )
	{
		return ModuleCompilerStartedEvent;
	}

	/**
	 * Gets an event delegate that is executed when compilation of a module has finished.
	 *
	 * The first parameter is the result of the compilation operation.
	 * The second parameter determines whether the log should be shown.
	 *
	 * @return The event delegate.
	 */
	DECLARE_EVENT_ThreeParams(FModuleManager, FModuleCompilerFinishedEvent, const FString&, ECompilationResult::Type, bool);
	FModuleCompilerFinishedEvent& OnModuleCompilerFinished()
	{
		return ModuleCompilerFinishedEvent;
	}

	/**
	 * Gets a multicast delegate that is executed when any UObjects need processing after a module was loaded.
	 *
	 * @return The delegate.
	 */
	FSimpleMulticastDelegate& OnProcessLoadedObjectsCallback()
	{
		return ProcessLoadedObjectsCallback;
	}

	/**
	 * Gets a delegate that is executed when a module containing UObjects has been loaded.
	 *
	 * The first parameter is the name of the loaded module.
	 *
	 * @return The event delegate.
	 */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FIsPackageLoadedCallback, FName);
	FIsPackageLoadedCallback& IsPackageLoadedCallback()
	{
		return IsPackageLoaded;
	}

public:

	// FSelfRegisteringExec interface.

	virtual bool Exec( UWorld* Inworld, const TCHAR* Cmd, FOutputDevice& Ar ) override;

public:

	/** @returns Static: Returns arguments to pass to UnrealBuildTool when compiling modules */
	static FString MakeUBTArgumentsForModuleCompiling();

protected:

	/**
	 * Hidden constructor.
	 *
	 * Use the static Get function to return the singleton instance.
	 */
	FModuleManager( )
		: bCanProcessNewlyLoadedObjects(false)
//		,  ModuleCompileProcessHandle(nullptr)
		, ModuleCompileReadPipe(nullptr)
		, bRequestCancelCompilation(false)
	{ }

protected:

	/**
	 * Helper structure to hold on to module state while asynchronously recompiling DLLs
	 */
	struct FModuleToRecompile
	{
		/** Name of the module */
		FString ModuleName;

		/** Desired module file name suffix, or empty string if not needed */
		FString ModuleFileSuffix;

		/** The module file name to use after a compilation succeeds, or an empty string if not changing */
		FString NewModuleFilename;
	};

	/**
	 * Helper structure to store the compile time and method for a module
	 */
	struct FModuleCompilationData
	{
		/** A flag set when the data it updated - loaded modules don't update this info until they are compiled or just before they unload */
		bool bIsValid;

		/** Has a timestamp been set for the .dll file */
		bool bHasFileTimeStamp;

		/** Last known timestamp for the .dll file */
		FDateTime FileTimeStamp;

		/** Last known compilation method of the .dll file */
		EModuleCompileMethod CompileMethod;

		FModuleCompilationData()
			: bIsValid(false)
			, bHasFileTimeStamp(false)
			, CompileMethod(EModuleCompileMethod::Unknown)
		{ }
	};

	/**
	 * Information about a single module (may or may not be loaded.)
	 */
	class FModuleInfo
	{
	public:

		/** The original file name of the module, without any suffixes added */
		FString OriginalFilename;

		/** File name of this module (.dll file name) */
		FString Filename;

		/** Handle to this module (DLL handle), if it's currently loaded */
		void* Handle;

		/** The module object for this module.  We actually *own* this module, so it's lifetime is controlled by the scope of this shared pointer. */
		TSharedPtr< IModuleInterface > Module;

		/** True if this module was unloaded at shutdown time, and we never want it to be loaded again */
		bool bWasUnloadedAtShutdown;

		/** Arbitrary number that encodes the load order of this module, so we can shut them down in reverse order. */
		int32 LoadOrder;

		/** Last know compilation data for this module - undefined if CompileData.bIsValid is false */
		FModuleCompilationData CompileData;

		/** static that tracks the current load number. Incremented whenever we add a new module*/
		static int32 CurrentLoadOrder;

	public:

		/** Constructor */
		FModuleInfo()
			: Handle( nullptr ),
			  bWasUnloadedAtShutdown( false ),
			  LoadOrder(CurrentLoadOrder++)
		{ }
	};

	/** Type definition for maps of module names to module infos. */
	typedef TMap<FName, TSharedRef<FModuleInfo>> FModuleMap;

	/**
	 * Tries to recompile the specified DLL using UBT. Does not interact with modules. This is a low level routine.
	 *
	 * @param ModuleNames List of modules to recompile, including the module name and optional file suffix.
	 * @param Ar Output device for logging compilation status.
	 */
	bool RecompileModuleDLLs( const TArray< FModuleToRecompile >& ModuleNames, FOutputDevice &Ar );

	/**
	 * Generates a unique file name for the specified module name by adding a random suffix and checking for file collisions.
	 */
	void MakeUniqueModuleFilename( const FName InModuleName, FString& UniqueSuffix, FString& UniqueModuleFileName );

	/** 
	 *	Starts compiling DLL files for one or more modules.
	 *
	 *	@param GameName The name of the game.
	 *	@param ModuleNames The list of modules to compile.
	 *	@param InRecompileModulesCallback Callback function to make when module recompiles.
	 *	@param Ar
	 *	@param bInFailIfGeneratedCodeChanges If true, fail the compilation if generated headers change.
	 *	@param InAdditionalCmdLineArgs Additional arguments to pass to UBT.
	 *	@return true if successful, false otherwise.
	 */
	bool StartCompilingModuleDLLs( const FString& GameName, const TArray< FModuleToRecompile >& ModuleNames, 
		const FRecompileModulesCallback& InRecompileModulesCallback, FOutputDevice& Ar, bool bInFailIfGeneratedCodeChanges, 
		const FString& InAdditionalCmdLineArgs = FString() );

	/** Returns the path to the unreal build tool source code */
	FString GetUnrealBuildToolSourceCodePath();

	/** Returns the filename for UBT including the path */
	FString GetUnrealBuildToolExecutableFilename();

	/** Builds unreal build tool using a compiler specific to the currently running platform */
	bool BuildUnrealBuildTool(FOutputDevice &Ar);

	/** Launches UnrealBuildTool with the specified command line parameters */
	bool InvokeUnrealBuildTool(const FString& InCmdLineParams, FOutputDevice &Ar);

	/** Checks to see if a pending compilation action has completed and optionally waits for it to finish.  If completed, fires any appropriate callbacks and reports status provided bFireEvents is true. */
	void CheckForFinishedModuleDLLCompile( const bool bWaitForCompletion, bool& bCompileStillInProgress, bool& bCompileSucceeded, FOutputDevice& Ar, const FText& SlowTaskOverrideTest = FText::GetEmpty(), bool bFireEvents = true );

	/** Compares file versions between the current executing engine version and the specified dll */
	static bool CheckModuleCompatibility(const TCHAR *Filename);

	/** Called during CheckForFinishedModuleDLLCompile() for each successfully recomplied module */
	void OnModuleCompileSucceeded(FName ModuleName, TSharedRef<FModuleInfo> ModuleInfo);

	/** Called when the compile data for a module need to be update in memory and written to config */
	void UpdateModuleCompileData(FName ModuleName, TSharedRef<FModuleInfo> ModuleInfo);

	/** Called when a new module is added to the manager to get the saved compile data from config */
	static void ReadModuleCompilationInfoFromConfig(FName ModuleName, TSharedRef<FModuleInfo> ModuleInfo);

	/** Saves the module's compile data to config */
	static void WriteModuleCompilationInfoToConfig(FName ModuleName, TSharedRef<FModuleInfo> ModuleInfo);

	/** Access the module's file and read the timestamp from the file system. Returns true if the timestamp was read successfully. */
	static bool GetModuleFileTimeStamp(TSharedRef<const FModuleInfo> ModuleInfo, FDateTime& OutFileTimeStamp);

	/** Finds modules matching a given name wildcard. */
	void FindModulePaths(const TCHAR *NamePattern, TMap<FName, FString> &OutModulePaths) const;

	/** Finds modules matching a given name wildcard within a given directory. */
	void FindModulePathsInDirectory(const FString &DirectoryName, bool bIsGameDirectory, const TCHAR *NamePattern, TMap<FName, FString> &OutModulePaths) const;

private:

	/** Map of all modules.  Maps the case-insensitive module name to information about that module, loaded or not. */
	FModuleMap Modules;

	/** Map of module names to a delegate that can initialize each respective statically linked module */
	typedef TMap< FName, FInitializeStaticallyLinkedModule > FStaticallyLinkedModuleInitializerMap;
	FStaticallyLinkedModuleInitializerMap StaticallyLinkedModuleInitializers;

	/** True if module manager should automatically register new UObjects discovered while loading C++ modules */
	bool bCanProcessNewlyLoadedObjects;

	/** Multicast delegate that will broadcast a notification when modules are loaded, unloaded, or
	    our set of known modules changes */
	FModulesChangedEvent ModulesChangedEvent;
	
	/** Multicast delegate which will broadcast a notification when the compiler starts */
	FModuleCompilerStartedEvent ModuleCompilerStartedEvent;
	
	/** Multicast delegate which will broadcast a notification when the compiler finishes */
	FModuleCompilerFinishedEvent ModuleCompilerFinishedEvent;

	/** Multicast delegate called to process any new loaded objects. */
	FSimpleMulticastDelegate ProcessLoadedObjectsCallback;

	/** When compiling a module using an external application, stores the handle to the process that is running */
	FProcHandle ModuleCompileProcessHandle;

	/** When compiling a module using an external application, this is the process read pipe handle */
	void* ModuleCompileReadPipe;

	/** When compiling a module using an external application, this is the text that was read from the read pipe handle */
	FString ModuleCompileReadPipeText;

	/** Callback to execute after an asynchronous recompile has completed (whether successful or not.) */
	FRecompileModulesCallback RecompileModulesCallback;

	/** When module manager is linked against an application that supports UObjects, this delegate will be primed
	    at startup to provide information about whether a UObject package is loaded into memory. */
	FIsPackageLoadedCallback IsPackageLoaded;

	/** Array of modules that we're currently recompiling */
	TArray< FModuleToRecompile > ModulesBeingCompiled;

	/** Array of modules that we're going to recompile */
	TArray< FModuleToRecompile > ModulesThatWereBeingRecompiled;

	/** true if we should attempt to cancel the current async compilation */
	bool bRequestCancelCompilation;

	/** Array of engine binaries directories. */
	TArray<FString> EngineBinariesDirectories;

	/** Array of game binaries directories. */
	TArray<FString> GameBinariesDirectories;
};


/**
 * Utility class for registering modules that are statically linked.
 */
template< class ModuleClass >
class FStaticallyLinkedModuleRegistrant
{
public:

	/**
	 * Explicit constructor that registers a statically linked module
	 */
	FStaticallyLinkedModuleRegistrant( const ANSICHAR* InModuleName )
	{
		// Create a delegate to our InitializeModule method
		FModuleManager::FInitializeStaticallyLinkedModule InitializerDelegate = FModuleManager::FInitializeStaticallyLinkedModule::CreateRaw(
				this, &FStaticallyLinkedModuleRegistrant<ModuleClass>::InitializeModule );

		// Register this module
		FModuleManager::Get().RegisterStaticallyLinkedModule(
			FName( InModuleName ),	// Module name
			InitializerDelegate );	// Initializer delegate
	}
	
	/**
	 * Creates and initializes this statically linked module.
	 *
	 * The module manager calls this function through the delegate that was created
	 * in the @see FStaticallyLinkedModuleRegistrant constructor.
	 *
	 * @return A pointer to a new instance of the module.
	 */
	IModuleInterface* InitializeModule( )
	{
		return new ModuleClass();
	}
};


/**
 * Function pointer type for InitializeModule().
 *
 * All modules must have an InitializeModule() function. Usually this is declared automatically using
 * the IMPLEMENT_MODULE macro below. The function must be declared using as 'extern "C"' so that the
 * name remains undecorated. The object returned will be "owned" by the caller, and will be deleted
 * by the caller before the module is unloaded.
 */
typedef IModuleInterface* ( *FInitializeModuleFunctionPtr )( void );


/**
 * A default minimal implementation of a module that does nothing at startup and shutdown
 */
class FDefaultModuleImpl
	: public IModuleInterface
{ };


/**
 * Default minimal module class for gameplay modules.  Does nothing at startup and shutdown.
 */
class FDefaultGameModuleImpl
	: public FDefaultModuleImpl
{
	/**
	 * Returns true if this module hosts gameplay code
	 *
	 * @return True for "gameplay modules", or false for engine code modules, plug-ins, etc.
	 */
	virtual bool IsGameModule() const override
	{
		return true;
	}
};


/**
 * Module implementation boilerplate for regular modules.
 *
 * This macro is used to expose a module's main class to the rest of the engine.
 * You must use this macro in one of your modules C++ modules, in order for the 'InitializeModule'
 * function to be declared in such a way that the engine can find it. Also, this macro will handle
 * the case where a module is statically linked with the engine instead of dynamically loaded.
 *
 * This macro is intended for modules that do NOT contain gameplay code.
 * If your module does contain game classes, use IMPLEMENT_GAME_MODULE instead.
 *
 * Usage:   IMPLEMENT_MODULE(<My Module Class>, <Module name string>)
 *
 * @see IMPLEMENT_GAME_MODULE
 */
#if IS_MONOLITHIC

	// If we're linking monolithically we assume all modules are linked in with the main binary.
	#define IMPLEMENT_MODULE( ModuleImplClass, ModuleName ) \
		/** Global registrant object for this module when linked statically */ \
		static FStaticallyLinkedModuleRegistrant< ModuleImplClass > ModuleRegistrant##ModuleName( #ModuleName ); \
		/** Implement an empty function so that if this module is built as a statically linked lib, */ \
		/** static initialization for this lib can be forced by referencing this symbol */ \
		void EmptyLinkFunctionForStaticInitialization##ModuleName(){} \
		PER_MODULE_BOILERPLATE_ANYLINK(ModuleImplClass, ModuleName)

#else

	#define IMPLEMENT_MODULE( ModuleImplClass, ModuleName ) \
		\
		/**/ \
		/* InitializeModule function, called by module manager after this module's DLL has been loaded */ \
		/**/ \
		/* @return	Returns an instance of this module */ \
		/**/ \
		extern "C" DLLEXPORT IModuleInterface* InitializeModule() \
		{ \
			return new ModuleImplClass(); \
		} \
		PER_MODULE_BOILERPLATE \
		PER_MODULE_BOILERPLATE_ANYLINK(ModuleImplClass, ModuleName)

#endif //IS_MONOLITHIC


/**
 * Module implementation boilerplate for game play code modules.
 *
 * This macro works like IMPLEMENT_MODULE but is specifically used for modules that contain game play code.
 * If your module does not contain game classes, use IMPLEMENT_MODULE instead.
 *
 * Usage:   IMPLEMENT_GAME_MODULE(<My Game Module Class>, <Game Module name string>)
 *
 * @see IMPLEMENT_MODULE
 */
#define IMPLEMENT_GAME_MODULE( ModuleImplClass, ModuleName ) \
	IMPLEMENT_MODULE( ModuleImplClass, ModuleName )


/**
 * Macro for declaring the engine directory to check for foreign or nested projects.
 */
#if PLATFORM_DESKTOP
	#ifdef UE_ENGINE_DIRECTORY
		#define IMPLEMENT_FOREIGN_ENGINE_DIR() const TCHAR *GForeignEngineDir = TEXT( PREPROCESSOR_TO_STRING(UE_ENGINE_DIRECTORY) );
	#else
		#define IMPLEMENT_FOREIGN_ENGINE_DIR() const TCHAR *GForeignEngineDir = nullptr;
	#endif
#else
	#define IMPLEMENT_FOREIGN_ENGINE_DIR() 
#endif

/**
 * Macro for declaring the project name variable in monolithic builds
 */
#if IS_MONOLITHIC
	#ifdef UE_PROJECT_NAME
		#define IMPLEMENT_PROJECT_NAME() const TCHAR *GProjectName = TEXT( PREPROCESSOR_TO_STRING(UE_PROJECT_NAME) );
	#else
		#define IMPLEMENT_PROJECT_NAME() const TCHAR *GProjectName = nullptr;
	#endif
#else
	#define IMPLEMENT_PROJECT_NAME() 
#endif



#if IS_PROGRAM

	#if IS_MONOLITHIC
		#define IMPLEMENT_APPLICATION( ModuleName, GameName ) \
			/* For monolithic builds, we must statically define the game's name string (See Core.h) */ \
			TCHAR GGameName[64] = TEXT( GameName ); \
			IMPLEMENT_FOREIGN_ENGINE_DIR() \
			IMPLEMENT_GAME_MODULE(FDefaultGameModuleImpl, ModuleName) \
			PER_MODULE_BOILERPLATE \
			FEngineLoop GEngineLoop;

	#else		

		#define IMPLEMENT_APPLICATION( ModuleName, GameName ) \
			/* For non-monolithic programs, we must set the game's name string before main starts (See Core.h) */ \
			struct FAutoSet##ModuleName \
			{ \
				FAutoSet##ModuleName() \
				{ \
					FCString::Strncpy(GGameName, TEXT( GameName ), ARRAY_COUNT(GGameName)); \
				} \
			} AutoSet##ModuleName; \
			PER_MODULE_BOILERPLATE \
			PER_MODULE_BOILERPLATE_ANYLINK(FDefaultGameModuleImpl, ModuleName) \
			FEngineLoop GEngineLoop;
	#endif

#else

/** IMPLEMENT_PRIMARY_GAME_MODULE must be used for at least one game module in your game.  It sets the "name"
    your game when compiling in monolithic mode. */
#if IS_MONOLITHIC
	#if PLATFORM_DESKTOP

		#define IMPLEMENT_PRIMARY_GAME_MODULE( ModuleImplClass, ModuleName, GameName ) \
			/* For monolithic builds, we must statically define the game's name string (See Core.h) */ \
			TCHAR GGameName[64] = TEXT( GameName ); \
			/* Implement the GIsGameAgnosticExe variable (See Core.h). */ \
			bool GIsGameAgnosticExe = false; \
			IMPLEMENT_PROJECT_NAME() \
			IMPLEMENT_FOREIGN_ENGINE_DIR() \
			IMPLEMENT_GAME_MODULE( ModuleImplClass, ModuleName ) \
			PER_MODULE_BOILERPLATE \
			void UELinkerFixupCheat() \
			{ \
				extern void UELinkerFixups(); \
				UELinkerFixups(); \
			}

	#else	//PLATFORM_DESKTOP

		#define IMPLEMENT_PRIMARY_GAME_MODULE( ModuleImplClass, ModuleName, GameName ) \
			/* For monolithic builds, we must statically define the game's name string (See Core.h) */ \
			TCHAR GGameName[64] = TEXT( GameName ); \
			PER_MODULE_BOILERPLATE \
			IMPLEMENT_PROJECT_NAME() \
			IMPLEMENT_FOREIGN_ENGINE_DIR() \
			IMPLEMENT_GAME_MODULE( ModuleImplClass, ModuleName ) \
			/* Implement the GIsGameAgnosticExe variable (See Core.h). */ \
			bool GIsGameAgnosticExe = false; \

	#endif	//PLATFORM_DESKTOP

#else	//IS_MONOLITHIC

	#define IMPLEMENT_PRIMARY_GAME_MODULE( ModuleImplClass, ModuleName, GameName ) \
		/* Nothing special to do for modular builds.  The game name will be set via the command-line */ \
		IMPLEMENT_GAME_MODULE( ModuleImplClass, ModuleName )
#endif	//IS_MONOLITHIC

#endif
