// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenericPlatformMisc.cpp: Generic implementations of misc platform functions
=============================================================================*/

#include "Core.h"
#include "LinuxPlatformMisc.h"
#include "LinuxApplication.h"

#include <sys/sysinfo.h>
#include <sched.h>
#include <fcntl.h>
#include <signal.h>

// these are not actually system headers, but a TPS library (see ThirdParty/elftoolchain)
#include <libelf.h>
#include <_libelf.h>
#include <libdwarf.h>
#include <dwarf.h>

#include "ModuleManager.h"

#include "../../Launch/Resources/Version.h"
#include <sys/utsname.h>	// for uname()

// define for glibc 2.12.2 and lower (which is shipped with CentOS 6.x and which we target by default)
#define __secure_getenv getenv

namespace PlatformMiscLimits
{
	enum
	{
		MaxUserHomeDirLength= MAX_PATH + 1
	};
};

// commandline parameter to suppress DWARF parsing (greatly speeds up callstack generation)
#define CMDARG_SUPPRESS_DWARF_PARSING			"nodwarf"

namespace
{
	/**
	 * Empty handler so some signals are just not ignored
	 */
	void EmptyChildHandler(int32 Signal, siginfo_t* Info, void* Context)
	{
	}

	/**
	 * Installs SIGCHLD signal handler so we can wait for our children (otherwise they are reaped automatically)
	 */
	void InstallChildExitedSignalHanlder()
	{
		struct sigaction Action;
		FMemory::Memzero(&Action, sizeof(struct sigaction));
		Action.sa_sigaction = EmptyChildHandler;
		sigemptyset(&Action.sa_mask);
		Action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
		sigaction(SIGCHLD, &Action, NULL);
	}
}

bool FLinuxPlatformMisc::ControlScreensaver(EScreenSaverAction Action)
{
	if (Action == FGenericPlatformMisc::EScreenSaverAction::Disable)
	{
		SDL_DisableScreenSaver();
	}
	else
	{
		SDL_EnableScreenSaver();
	}

	return true;
}

const TCHAR* FLinuxPlatformMisc::RootDir()
{
	const TCHAR * TrueRootDir = FGenericPlatformMisc::RootDir();
	return TrueRootDir;
}

void FLinuxPlatformMisc::NormalizePath(FString& InPath)
{
	if (InPath.Contains(TEXT("~"), ESearchCase::CaseSensitive))	// case sensitive is quicker, and our substring doesn't care
	{
		static bool bHaveHome = false;
		static TCHAR CachedResult[PlatformMiscLimits::MaxUserHomeDirLength];

		if (!bHaveHome)
		{
			CachedResult[0] = TEXT('~');	// init with a default value that changes nothing
			CachedResult[1] = TEXT('\0');

			//  get user $HOME var first
			const char * VarValue = secure_getenv("HOME");
			if (NULL != VarValue)
			{
				FCString::Strcpy(CachedResult, ARRAY_COUNT(CachedResult) - 1, ANSI_TO_TCHAR(VarValue));
				bHaveHome = true;
			}

			// if var failed
			if (!bHaveHome)
			{
				struct passwd * UserInfo = getpwuid(getuid());
				if (NULL != UserInfo && NULL != UserInfo->pw_dir)
				{
					FCString::Strcpy(CachedResult, ARRAY_COUNT(CachedResult) - 1, ANSI_TO_TCHAR(UserInfo->pw_dir));
					bHaveHome = true;
				}
				else
				{
					// fail for realz
					UE_LOG(LogInit, Fatal, TEXT("Could not get determine user home directory."));
				}
			}
		}

		InPath = InPath.Replace(TEXT("~"), CachedResult, ESearchCase::CaseSensitive);
	}
}

namespace
{
	bool GInitializedSDL = false;
}

void FLinuxPlatformMisc::PlatformInit()
{
	// install a platform-specific signal handler
	InstallChildExitedSignalHanlder();

	UE_LOG(LogInit, Log, TEXT("Linux hardware info:"));
	UE_LOG(LogInit, Log, TEXT(" - this process' id (pid) is %d, parent process' id (ppid) is %d"), static_cast< int32 >(getpid()), static_cast< int32 >(getppid()));
	UE_LOG(LogInit, Log, TEXT(" - we are %srunning under debugger"), IsDebuggerPresent() ? TEXT("") : TEXT("not "));
	UE_LOG(LogInit, Log, TEXT(" - machine network name is '%s'"), FPlatformProcess::ComputerName());
	UE_LOG(LogInit, Log, TEXT(" - Number of physical cores available for the process: %d"), FPlatformMisc::NumberOfCores());
	UE_LOG(LogInit, Log, TEXT(" - Number of logical cores available for the process: %d"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	UE_LOG(LogInit, Log, TEXT(" - Memory allocator used: %s"), GMalloc->GetDescriptiveName());

	UE_LOG(LogInit, Log, TEXT("Linux-specific commandline switches:"));
	UE_LOG(LogInit, Log, TEXT(" -%s (currently %s): suppress parsing of DWARF debug info (callstacks will be generated faster, but won't have line numbers)"), 
		TEXT(CMDARG_SUPPRESS_DWARF_PARSING), FParse::Param( FCommandLine::Get(), TEXT(CMDARG_SUPPRESS_DWARF_PARSING)) ? TEXT("ON") : TEXT("OFF"));
	UE_LOG(LogInit, Log, TEXT(" -ansimalloc - use malloc()/free() from libc (useful for tools like valgrind and electric fence)"));
	UE_LOG(LogInit, Log, TEXT(" -jemalloc - use jemalloc for all memory allocation"));
	UE_LOG(LogInit, Log, TEXT(" -binnedmalloc - use binned malloc  for all memory allocation"));

	// [RCL] FIXME: this should be printed in specific modules, if at all
	UE_LOG(LogInit, Log, TEXT(" -httpproxy=ADDRESS:PORT - redirects HTTP requests to a proxy (only supported if compiled with libcurl)"));
	UE_LOG(LogInit, Log, TEXT(" -reuseconn - allow libcurl to reuse HTTP connections (only matters if compiled with libcurl)"));
	UE_LOG(LogInit, Log, TEXT(" -virtmemkb=NUMBER - sets process virtual memory (address space) limit (overrides VirtualMemoryLimitInKB value from .ini)"));

	// skip for servers and programs, unless they request later
	if (!UE_SERVER && !IS_PROGRAM)
	{
		PlatformInitMultimedia();
	}
}

bool FLinuxPlatformMisc::PlatformInitMultimedia()
{
	if (!GInitializedSDL)
	{
		UE_LOG(LogInit, Log, TEXT("Initializing SDL."));
		if (SDL_Init(SDL_INIT_EVERYTHING | SDL_INIT_NOPARACHUTE) != 0)
		{
			const char * SDLError = SDL_GetError();

			// do not fail at this point, allow caller handle failure
			UE_LOG(LogInit, Warning, TEXT("Could not initialize SDL: %s"), SDLError);
			return false;
		}

		GInitializedSDL = true;

		// needs to come after GInitializedSDL, otherwise it will recurse here
		// @TODO [RCL] 2014-09-30 - move to FDisplayMetrics itself sometime after 4.5
		if (!UE_BUILD_SHIPPING)
		{
			// dump information about screens for debug
			FDisplayMetrics DisplayMetrics;
			FDisplayMetrics::GetDisplayMetrics(DisplayMetrics);
			
			UE_LOG(LogInit, Log, TEXT("Display metrics:"));
			UE_LOG(LogInit, Log, TEXT("  PrimaryDisplayWidth: %d"), DisplayMetrics.PrimaryDisplayWidth);
			UE_LOG(LogInit, Log, TEXT("  PrimaryDisplayHeight: %d"), DisplayMetrics.PrimaryDisplayHeight);
			UE_LOG(LogInit, Log, TEXT("  PrimaryDisplayWorkAreaRect:"));
			UE_LOG(LogInit, Log, TEXT("    Left=%d, Top=%d, Right=%d, Bottom=%d"), 
				DisplayMetrics.PrimaryDisplayWorkAreaRect.Left, DisplayMetrics.PrimaryDisplayWorkAreaRect.Top, 
				DisplayMetrics.PrimaryDisplayWorkAreaRect.Right, DisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom);
			UE_LOG(LogInit, Log, TEXT("  VirtualDisplayRect:"));
			UE_LOG(LogInit, Log, TEXT("    Left=%d, Top=%d, Right=%d, Bottom=%d"), 
				DisplayMetrics.VirtualDisplayRect.Left, DisplayMetrics.VirtualDisplayRect.Top, 
				DisplayMetrics.VirtualDisplayRect.Right, DisplayMetrics.VirtualDisplayRect.Bottom);
			UE_LOG(LogInit, Log, TEXT("  TitleSafePaddingSize: %s"), *DisplayMetrics.TitleSafePaddingSize.ToString());
			UE_LOG(LogInit, Log, TEXT("  ActionSafePaddingSize: %s"), *DisplayMetrics.ActionSafePaddingSize.ToString());

			const int NumMonitors = DisplayMetrics.MonitorInfo.Num();
			UE_LOG(LogInit, Log, TEXT("  Number of monitors: %d"), NumMonitors);
			for (int MonitorIdx = 0; MonitorIdx < NumMonitors; ++MonitorIdx)
			{
				const FMonitorInfo & MonitorInfo = DisplayMetrics.MonitorInfo[MonitorIdx];
				UE_LOG(LogInit, Log, TEXT("    Monitor %d"), MonitorIdx);
				UE_LOG(LogInit, Log, TEXT("      Name: %s"), *MonitorInfo.Name);
				UE_LOG(LogInit, Log, TEXT("      ID: %s"), *MonitorInfo.ID);
				UE_LOG(LogInit, Log, TEXT("      NativeWidth: %d"), MonitorInfo.NativeWidth);
				UE_LOG(LogInit, Log, TEXT("      NativeHeight: %d"), MonitorInfo.NativeHeight);
				UE_LOG(LogInit, Log, TEXT("      bIsPrimary: %s"), MonitorInfo.bIsPrimary ? TEXT("true") : TEXT("false"));
			}
		}
	}

	return true;
}

void FLinuxPlatformMisc::PlatformTearDown()
{
	if (GInitializedSDL)
	{
		UE_LOG(LogInit, Log, TEXT("Tearing down SDL."));
		SDL_Quit();
		GInitializedSDL = false;
	}
}

GenericApplication* FLinuxPlatformMisc::CreateApplication()
{
	return FLinuxApplication::CreateLinuxApplication();
}

void FLinuxPlatformMisc::PumpMessages( bool bFromMainLoop )
{
	if( bFromMainLoop )
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			if( LinuxApplication )
			{
				LinuxApplication->AddPendingEvent( event );
			}
		}
	}
}

uint32 FLinuxPlatformMisc::GetCharKeyMap(uint16* KeyCodes, FString* KeyNames, uint32 MaxMappings)
{
	return FGenericPlatformMisc::GetStandardPrintableKeyMap(KeyCodes, KeyNames, MaxMappings, false, true);
}

void FLinuxPlatformMisc::LowLevelOutputDebugString(const TCHAR *Message)
{
	static_assert(PLATFORM_USE_LS_SPEC_FOR_WIDECHAR, "Check printf format");
	fprintf(stderr, "%ls", Message);	// there's no good way to implement that really
}

uint32 FLinuxPlatformMisc::GetKeyMap( uint16* KeyCodes, FString* KeyNames, uint32 MaxMappings )
{
#define ADDKEYMAP(KeyCode, KeyName)		if (NumMappings<MaxMappings) { KeyCodes[NumMappings]=KeyCode; KeyNames[NumMappings]=KeyName; ++NumMappings; };

	uint32 NumMappings = 0;

	if (KeyCodes && KeyNames && (MaxMappings > 0))
	{
		ADDKEYMAP(SDL_SCANCODE_BACKSPACE, TEXT("BackSpace"));
		ADDKEYMAP(SDL_SCANCODE_TAB, TEXT("Tab"));
		ADDKEYMAP(SDL_SCANCODE_RETURN, TEXT("Enter"));
		ADDKEYMAP(SDL_SCANCODE_RETURN2, TEXT("Enter"));
		ADDKEYMAP(SDL_SCANCODE_KP_ENTER, TEXT("Enter"));
		ADDKEYMAP(SDL_SCANCODE_PAUSE, TEXT("Pause"));

		ADDKEYMAP(SDL_SCANCODE_ESCAPE, TEXT("Escape"));
		ADDKEYMAP(SDL_SCANCODE_SPACE, TEXT("SpaceBar"));
		ADDKEYMAP(SDL_SCANCODE_PAGEUP, TEXT("PageUp"));
		ADDKEYMAP(SDL_SCANCODE_PAGEDOWN, TEXT("PageDown"));
		ADDKEYMAP(SDL_SCANCODE_END, TEXT("End"));
		ADDKEYMAP(SDL_SCANCODE_HOME, TEXT("Home"));

		ADDKEYMAP(SDL_SCANCODE_LEFT, TEXT("Left"));
		ADDKEYMAP(SDL_SCANCODE_UP, TEXT("Up"));
		ADDKEYMAP(SDL_SCANCODE_RIGHT, TEXT("Right"));
		ADDKEYMAP(SDL_SCANCODE_DOWN, TEXT("Down"));

		ADDKEYMAP(SDL_SCANCODE_INSERT, TEXT("Insert"));
		ADDKEYMAP(SDL_SCANCODE_DELETE, TEXT("Delete"));

		ADDKEYMAP(SDL_SCANCODE_F1, TEXT("F1"));
		ADDKEYMAP(SDL_SCANCODE_F2, TEXT("F2"));
		ADDKEYMAP(SDL_SCANCODE_F3, TEXT("F3"));
		ADDKEYMAP(SDL_SCANCODE_F4, TEXT("F4"));
		ADDKEYMAP(SDL_SCANCODE_F5, TEXT("F5"));
		ADDKEYMAP(SDL_SCANCODE_F6, TEXT("F6"));
		ADDKEYMAP(SDL_SCANCODE_F7, TEXT("F7"));
		ADDKEYMAP(SDL_SCANCODE_F8, TEXT("F8"));
		ADDKEYMAP(SDL_SCANCODE_F9, TEXT("F9"));
		ADDKEYMAP(SDL_SCANCODE_F10, TEXT("F10"));
		ADDKEYMAP(SDL_SCANCODE_F11, TEXT("F11"));
		ADDKEYMAP(SDL_SCANCODE_F12, TEXT("F12"));

        ADDKEYMAP(SDL_SCANCODE_CAPSLOCK, TEXT("CapsLock"));
        ADDKEYMAP(SDL_SCANCODE_LCTRL, TEXT("LeftControl"));
        ADDKEYMAP(SDL_SCANCODE_LSHIFT, TEXT("LeftShift"));
        ADDKEYMAP(SDL_SCANCODE_LALT, TEXT("LeftAlt"));
        ADDKEYMAP(SDL_SCANCODE_RCTRL, TEXT("RightControl"));
        ADDKEYMAP(SDL_SCANCODE_RSHIFT, TEXT("RightShift"));
        ADDKEYMAP(SDL_SCANCODE_RALT, TEXT("RightAlt"));
	}

	check(NumMappings < MaxMappings);
	return NumMappings;
}

void FLinuxPlatformMisc::ClipboardCopy(const TCHAR* Str)
{
	if (SDL_HasClipboardText() == SDL_TRUE)
	{
		if (SDL_SetClipboardText(TCHAR_TO_UTF8(Str)))
		{
			UE_LOG(LogInit, Fatal, TEXT("Error copying clipboard contents: %s\n"), ANSI_TO_TCHAR(SDL_GetError()));
		}
	}
}
void FLinuxPlatformMisc::ClipboardPaste(class FString& Result)
{
	char* ClipContent;
	ClipContent = SDL_GetClipboardText();

	if (!ClipContent)
	{
		UE_LOG(LogInit, Fatal, TEXT("Error pasting clipboard contents: %s\n"), ANSI_TO_TCHAR(SDL_GetError()));
		// unreachable
		Result = TEXT("");
	}
	else
	{
		Result = FString(UTF8_TO_TCHAR(ClipContent));
	}
	SDL_free(ClipContent);
}

EAppReturnType::Type FLinuxPlatformMisc::MessageBoxExt(EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption)
{
	int NumberOfButtons = 0;

	// if multimedia cannot be initialized for messagebox, just fall back to default implementation
	if (!FPlatformMisc::PlatformInitMultimedia()) //	will not initialize more than once
	{
		return FGenericPlatformMisc::MessageBoxExt(MsgType, Text, Caption);
	}

#if DO_CHECK
	uint32 InitializedSubsystems = SDL_WasInit(SDL_INIT_EVERYTHING);
	check(InitializedSubsystems & SDL_INIT_VIDEO);
#endif // DO_CHECK

	SDL_MessageBoxButtonData *Buttons = nullptr;

	switch (MsgType)
	{
		case EAppMsgType::Ok:
			NumberOfButtons = 1;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
			Buttons[0].text = "Ok";
			Buttons[0].buttonid = EAppReturnType::Ok;
			break;

		case EAppMsgType::YesNo:
			NumberOfButtons = 2;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Yes";
			Buttons[0].buttonid = EAppReturnType::Yes;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "No";
			Buttons[1].buttonid = EAppReturnType::No;
			break;

		case EAppMsgType::OkCancel:
			NumberOfButtons = 2;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Ok";
			Buttons[0].buttonid = EAppReturnType::Ok;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "Cancel";
			Buttons[1].buttonid = EAppReturnType::Cancel;
			break;

		case EAppMsgType::YesNoCancel:
			NumberOfButtons = 3;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Yes";
			Buttons[0].buttonid = EAppReturnType::Yes;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "No";
			Buttons[1].buttonid = EAppReturnType::No;
			Buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[2].text = "Cancel";
			Buttons[2].buttonid = EAppReturnType::Cancel;
			break;

		case EAppMsgType::CancelRetryContinue:
			NumberOfButtons = 3;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Continue";
			Buttons[0].buttonid = EAppReturnType::Continue;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "Retry";
			Buttons[1].buttonid = EAppReturnType::Retry;
			Buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[2].text = "Cancel";
			Buttons[2].buttonid = EAppReturnType::Cancel;
			break;

		case EAppMsgType::YesNoYesAllNoAll:
			NumberOfButtons = 4;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Yes";
			Buttons[0].buttonid = EAppReturnType::Yes;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "No";
			Buttons[1].buttonid = EAppReturnType::No;
			Buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[2].text = "Yes to all";
			Buttons[2].buttonid = EAppReturnType::YesAll;
			Buttons[3].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[3].text = "No to all";
			Buttons[3].buttonid = EAppReturnType::NoAll;
			break;

		case EAppMsgType::YesNoYesAllNoAllCancel:
			NumberOfButtons = 5;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Yes";
			Buttons[0].buttonid = EAppReturnType::Yes;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "No";
			Buttons[1].buttonid = EAppReturnType::No;
			Buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[2].text = "Yes to all";
			Buttons[2].buttonid = EAppReturnType::YesAll;
			Buttons[3].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[3].text = "No to all";
			Buttons[3].buttonid = EAppReturnType::NoAll;
			Buttons[4].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[4].text = "Cancel";
			Buttons[4].buttonid = EAppReturnType::Cancel;
			break;

		case EAppMsgType::YesNoYesAll:
			NumberOfButtons = 3;
			Buttons = new SDL_MessageBoxButtonData[NumberOfButtons];
			Buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[0].text = "Yes";
			Buttons[0].buttonid = EAppReturnType::Yes;
			Buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[1].text = "No";
			Buttons[1].buttonid = EAppReturnType::No;
			Buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			Buttons[2].text = "Yes to all";
			Buttons[2].buttonid = EAppReturnType::YesAll;
			break;
	}

	SDL_MessageBoxData MessageBoxData = 
	{
		SDL_MESSAGEBOX_INFORMATION,
		NULL, // No parent window
		TCHAR_TO_UTF8(Caption),
		TCHAR_TO_UTF8(Text),
		NumberOfButtons,
		Buttons,
		NULL // Default color scheme
	};

	int ButtonPressed = -1;
	if (SDL_ShowMessageBox(&MessageBoxData, &ButtonPressed) == -1) 
	{
		UE_LOG(LogInit, Fatal, TEXT("Error Presenting MessageBox: %s\n"), ANSI_TO_TCHAR(SDL_GetError()));
		// unreachable
		return EAppReturnType::Cancel;
	}

	delete[] Buttons;

	return ButtonPressed == -1 ? EAppReturnType::Cancel : static_cast<EAppReturnType::Type>(ButtonPressed);
}

int32 FLinuxPlatformMisc::NumberOfCores()
{
	cpu_set_t AvailableCpusMask;
	CPU_ZERO(&AvailableCpusMask);

	if (0 != sched_getaffinity(0, sizeof(AvailableCpusMask), &AvailableCpusMask))
	{
		return 1;	// we are running on something, right?
	}

	char FileNameBuffer[1024];
	unsigned char PossibleCores[CPU_SETSIZE] = { 0 };

	for(int32 CpuIdx = 0; CpuIdx < CPU_SETSIZE; ++CpuIdx)
	{
		if (CPU_ISSET(CpuIdx, &AvailableCpusMask))
		{
			sprintf(FileNameBuffer, "/sys/devices/system/cpu/cpu%d/topology/core_id", CpuIdx);
			
			FILE* CoreIdFile = fopen(FileNameBuffer, "r");
			unsigned int CoreId = 0;
			if (CoreIdFile)
			{
				if (1 != fscanf(CoreIdFile, "%d", &CoreId))
				{
					CoreId = 0;
				}
				fclose(CoreIdFile);
			}

			if (CoreId >= ARRAY_COUNT(PossibleCores))
			{
				CoreId = 0;
			}
			
			PossibleCores[ CoreId ] = 1;
		}
	}

	int32 NumCoreIds = 0;
	for(int32 Idx = 0; Idx < ARRAY_COUNT(PossibleCores); ++Idx)
	{
		NumCoreIds += PossibleCores[Idx];
	}

	return NumCoreIds;
}

int32 FLinuxPlatformMisc::NumberOfCoresIncludingHyperthreads()
{
	cpu_set_t AvailableCpusMask;
	CPU_ZERO(&AvailableCpusMask);

	if (0 != sched_getaffinity(0, sizeof(AvailableCpusMask), &AvailableCpusMask))
	{
		return 1;	// we are running on something, right?
	}

	return CPU_COUNT(&AvailableCpusMask);
}

void FLinuxPlatformMisc::LoadPreInitModules()
{
#if WITH_EDITOR
	FModuleManager::Get().LoadModule(TEXT("OpenGLDrv"));
#endif // WITH_EDITOR
}

void FLinuxPlatformMisc::LoadStartupModules()
{
#if !IS_PROGRAM && !UE_SERVER
	FModuleManager::Get().LoadModule(TEXT("ALAudio"));	// added in Launch.Build.cs for non-server targets
	FModuleManager::Get().LoadModule(TEXT("HeadMountedDisplay"));
#endif // !IS_PROGRAM && !UE_SERVER

#if WITH_STEAMCONTROLLER
	FModuleManager::Get().LoadModule(TEXT("SteamController"));
#endif // WITH_STEAMCONTROLLER

#if WITH_EDITOR
	FModuleManager::Get().LoadModule(TEXT("SourceCodeAccess"));
#endif	//WITH_EDITOR
}

const TCHAR* FLinuxPlatformMisc::GetNullRHIShaderFormat()
{
	return TEXT("GLSL_150");
}

#if PLATFORM_HAS_CPUID
FString FLinuxPlatformMisc::GetCPUVendor()
{
	union
	{
		char Buffer[12+1];
		struct
		{
			int dw0;
			int dw1;
			int dw2;
		} Dw;
	} VendorResult;


	int32 Args[4];
	asm( "cpuid" : "=a" (Args[0]), "=b" (Args[1]), "=c" (Args[2]), "=d" (Args[3]) : "a" (0));

	VendorResult.Dw.dw0 = Args[1];
	VendorResult.Dw.dw1 = Args[3];
	VendorResult.Dw.dw2 = Args[2];
	VendorResult.Buffer[12] = 0;

	return ANSI_TO_TCHAR(VendorResult.Buffer);
}

uint32 FLinuxPlatformMisc::GetCPUInfo()
{
	uint32 Args[4];
	asm( "cpuid" : "=a" (Args[0]), "=b" (Args[1]), "=c" (Args[2]), "=d" (Args[3]) : "a" (1));

	return Args[0];
}
#endif // PLATFORM_HAS_CPUID

FString DescribeSignal(int32 Signal, siginfo_t* Info)
{
	FString ErrorString;

#define HANDLE_CASE(a,b) case a: ErrorString += TEXT(#a ": " b); break;

	switch (Signal)
	{
	case SIGSEGV:
		ErrorString += TEXT("SIGSEGV: invalid attempt to access memory at address ");
		ErrorString += FString::Printf(TEXT("0x%08x"), (uint32*)Info->si_addr);
		break;
	case SIGBUS:
		ErrorString += TEXT("SIGBUS: invalid attempt to access memory at address ");
		ErrorString += FString::Printf(TEXT("0x%08x"), (uint32*)Info->si_addr);
		break;

		HANDLE_CASE(SIGINT, "program interrupted")
		HANDLE_CASE(SIGQUIT, "user-requested crash")
		HANDLE_CASE(SIGILL, "illegal instruction")
		HANDLE_CASE(SIGTRAP, "trace trap")
		HANDLE_CASE(SIGABRT, "abort() called")
		HANDLE_CASE(SIGFPE, "floating-point exception")
		HANDLE_CASE(SIGKILL, "program killed")
		HANDLE_CASE(SIGSYS, "non-existent system call invoked")
		HANDLE_CASE(SIGPIPE, "write on a pipe with no reader")
		HANDLE_CASE(SIGTERM, "software termination signal")
		HANDLE_CASE(SIGSTOP, "stop")

	default:
		ErrorString += FString::Printf(TEXT("Signal %d (unknown)"), Signal);
	}

	return ErrorString;
#undef HANDLE_CASE
}

FLinuxCrashContext::~FLinuxCrashContext()
{
	if (BacktraceSymbols)
	{
		// glibc uses malloc() to allocate this, and we only need to free one pointer, see http://www.gnu.org/software/libc/manual/html_node/Backtraces.html
		free(BacktraceSymbols);
		BacktraceSymbols = NULL;
	}

	if (DebugInfo)
	{
		Dwarf_Error ErrorInfo;
		dwarf_finish(DebugInfo, &ErrorInfo);
		DebugInfo = NULL;
	}

	if (ElfHdr)
	{
		elf_end(ElfHdr);
		ElfHdr = NULL;
	}

	if (ExeFd >= 0)
	{
		close(ExeFd);
		ExeFd = -1;
	}
}

void FLinuxCrashContext::InitFromSignal(int32 InSignal, siginfo_t* InInfo, void* InContext)
{
	Signal = InSignal;
	Info = InInfo;
	Context = reinterpret_cast< ucontext_t* >( InContext );

	// open ourselves for examination
	if (!FParse::Param( FCommandLine::Get(), TEXT(CMDARG_SUPPRESS_DWARF_PARSING)))
	{
		ExeFd = open("/proc/self/exe", O_RDONLY);
		if (ExeFd >= 0)
		{
			Dwarf_Error ErrorInfo;
			// allocate DWARF debug descriptor
			if (dwarf_init(ExeFd, DW_DLC_READ, NULL, NULL, &DebugInfo, &ErrorInfo) == DW_DLV_OK)
			{
				// get ELF descritor
				if (dwarf_get_elf(DebugInfo, &ElfHdr, &ErrorInfo) != DW_DLV_OK)
				{
					dwarf_finish(DebugInfo, &ErrorInfo);
					DebugInfo = NULL;

					close(ExeFd);
					ExeFd = -1;
				}
			}
			else
			{
				DebugInfo = NULL;
				close(ExeFd);
				ExeFd = -1;
			}
		}
	}

	FCString::Strcat(SignalDescription, ARRAY_COUNT( SignalDescription ) - 1, *DescribeSignal(Signal, Info));
}

/**
 * Finds a function name in DWARF DIE (Debug Information Entry).
 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
 *
 * @return true if we need to stop search (i.e. either found it or some error happened)
 */
bool FindFunctionNameInDIE(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName)
{
	Dwarf_Error ErrorInfo;
	Dwarf_Half Tag;
	Dwarf_Unsigned LowerPC, HigherPC;
	char *TempFuncName;
	int ReturnCode;

	if (dwarf_tag(Die, &Tag, &ErrorInfo) != DW_DLV_OK || Tag != DW_TAG_subprogram ||
		dwarf_attrval_unsigned(Die, DW_AT_low_pc, &LowerPC, &ErrorInfo) != DW_DLV_OK ||
		dwarf_attrval_unsigned(Die, DW_AT_high_pc, &HigherPC, &ErrorInfo) != DW_DLV_OK ||
		Addr < LowerPC || HigherPC <= Addr
		) 
	{
		return false;
	}
	
	// found it
	*OutFuncName = NULL;
	Dwarf_Attribute SubAt;
	ReturnCode = dwarf_attr(Die, DW_AT_name, &SubAt, &ErrorInfo);
	if (ReturnCode == DW_DLV_ERROR)
	{
		return true;	// error, but stop the search
	}
	else if (ReturnCode == DW_DLV_OK) 
	{
		if (dwarf_formstring(SubAt, &TempFuncName, &ErrorInfo))
		{
			*OutFuncName = NULL;
		}
		else
		{
			*OutFuncName = TempFuncName;
		}
		return true;
	}

	// DW_AT_Name is not present, look in DW_AT_specification
	Dwarf_Attribute SpecAt;
	if (dwarf_attr(Die, DW_AT_specification, &SpecAt, &ErrorInfo))
	{
		// not found, tough luck
		return false;
	}

	Dwarf_Off Offset;
	if (dwarf_global_formref(SpecAt, &Offset, &ErrorInfo))
	{
		return false;
	}

	Dwarf_Die SpecDie;
	if (dwarf_offdie(DebugInfo, Offset, &SpecDie, &ErrorInfo))
	{
		return false;
	}

	if (dwarf_attrval_string(SpecDie, DW_AT_name, OutFuncName, &ErrorInfo))
	{
		*OutFuncName = NULL;
	}

	return true;
}

/**
 * Finds a function name in DWARF DIE (Debug Information Entry) and its children.
 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
 * Note: that function is not exactly traversing the tree, but this "seems to work"(tm). Not sure if we need to descend properly (taking child of every sibling), this
 * takes too much time (and callstacks seem to be fine without it).
 */
void FindFunctionNameInDIEAndChildren(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName)
{
	if (OutFuncName == NULL || *OutFuncName != NULL)
	{
		return;
	}

	// search for this Die
	if (FindFunctionNameInDIE(DebugInfo, Die, Addr, OutFuncName))
	{
		return;
	}

	Dwarf_Die PrevChild = Die, Current = NULL;
	Dwarf_Error ErrorInfo;

	int32 MaxChildrenAllowed = 32 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
	for(;;)
	{
		if (--MaxChildrenAllowed <= 0)
		{
			fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many children).\n");
			return;
		}

		// Get the child
		if (dwarf_child(PrevChild, &Current, &ErrorInfo) != DW_DLV_OK)
		{
			return;	// bail out
		}

		PrevChild = Current;

		// look for in the child
		if (FindFunctionNameInDIE(DebugInfo, Current, Addr, OutFuncName))
		{
			return;	// got the function name!
		}

		// search among child's siblings
		int32 MaxSiblingsAllowed = 64 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
		for(;;)
		{
			if (--MaxSiblingsAllowed <= 0)
			{
				fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many siblings).\n");
				break;
			}

			Dwarf_Die Prev = Current;
			if (dwarf_siblingof(DebugInfo, Prev, &Current, &ErrorInfo) != DW_DLV_OK || Current == NULL)
			{
				break;
			}

			if (FindFunctionNameInDIE(DebugInfo, Current, Addr, OutFuncName))
			{
				return;	// got the function name!
			}
		}
	};
}

bool FLinuxCrashContext::GetInfoForAddress(void * Address, const char **OutFunctionNamePtr, const char **OutSourceFilePtr, int *OutLineNumberPtr)
{
	if (DebugInfo == NULL)
	{
		return false;
	}

	Dwarf_Die Die;
	Dwarf_Unsigned Addr = reinterpret_cast< Dwarf_Unsigned >( Address ), LineNumber = 0;
	const char * SrcFile = NULL;

	static_assert(sizeof(Dwarf_Unsigned) >= sizeof(Address), "Dwarf_Unsigned type should be long enough to represent pointers. Check libdwarf bitness.");

	int ReturnCode = DW_DLV_OK;
	Dwarf_Error ErrorInfo;
	bool bExitHeaderLoop = false;
	int32 MaxCompileUnitsAllowed = 16 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
	const int32 kMaxBufferLinesAllowed = 16 * 1024 * 1024;	// safeguard to prevent too long line loop
	for(;;)
	{
		if (--MaxCompileUnitsAllowed <= 0)
		{
			fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many compile units).\n");
			ReturnCode = DW_DLE_DIE_NO_CU_CONTEXT;	// invalidate
			break;
		}

		if (bExitHeaderLoop)
			break;

		ReturnCode = dwarf_next_cu_header(DebugInfo, NULL, NULL, NULL, NULL, NULL, &ErrorInfo);
		if (ReturnCode != DW_DLV_OK)
			break;

		Die = NULL;

		while(dwarf_siblingof(DebugInfo, Die, &Die, &ErrorInfo) == DW_DLV_OK)
		{
			Dwarf_Half Tag;
			if (dwarf_tag(Die, &Tag, &ErrorInfo) != DW_DLV_OK)
			{
				bExitHeaderLoop = true;
				break;
			}

			if (Tag == DW_TAG_compile_unit)
			{
				break;
			}
		}

		if (Die == NULL)
		{
			break;
		}

		// check if address is inside this CU
		Dwarf_Unsigned LowerPC, HigherPC;
		if (!dwarf_attrval_unsigned(Die, DW_AT_low_pc, &LowerPC, &ErrorInfo) && !dwarf_attrval_unsigned(Die, DW_AT_high_pc, &HigherPC, &ErrorInfo))
		{
			if (Addr < LowerPC || Addr >= HigherPC)
			{
				continue;
			}
		}

		Dwarf_Line * LineBuf;
		Dwarf_Signed NumLines = kMaxBufferLinesAllowed;
		if (dwarf_srclines(Die, &LineBuf, &NumLines, &ErrorInfo) != DW_DLV_OK)
		{
			// could not get line info for some reason
			break;
		}

		if (NumLines >= kMaxBufferLinesAllowed)
		{
			fprintf(stderr, "Number of lines associated with a DIE looks unreasonable (%ld), early quitting.\n", NumLines);
			ReturnCode = DW_DLE_DIE_NO_CU_CONTEXT;	// invalidate
			break;
		}

		// look which line is that
		Dwarf_Addr LineAddress, PrevLineAddress = ~0ULL;
		Dwarf_Unsigned PrevLineNumber = 0;
		const char * PrevSrcFile = NULL;
		char * SrcFileTemp = NULL;
		for (int Idx = 0; Idx < NumLines; ++Idx)
		{
			if (dwarf_lineaddr(LineBuf[Idx], &LineAddress, &ErrorInfo) != 0 ||
				dwarf_lineno(LineBuf[Idx], &LineNumber, &ErrorInfo) != 0)
			{
				bExitHeaderLoop = true;
				break;
			}

			if (!dwarf_linesrc(LineBuf[Idx], &SrcFileTemp, &ErrorInfo))
			{
				SrcFile = SrcFileTemp;
			}

			// check if we hit the exact line
			if (Addr == LineAddress)
			{
				bExitHeaderLoop = true;
				break;
			}
			else if (PrevLineAddress < Addr && Addr < LineAddress)
			{
				LineNumber = PrevLineNumber;
				SrcFile = PrevSrcFile;
				bExitHeaderLoop = true;
				break;
			}

			PrevLineAddress = LineAddress;
			PrevLineNumber = LineNumber;
			PrevSrcFile = SrcFile;
		}

	}

	const char * FunctionName = NULL;
	if (ReturnCode == DW_DLV_OK)
	{
		FindFunctionNameInDIEAndChildren(DebugInfo, Die, Addr, &FunctionName);
	}

	if (OutFunctionNamePtr != NULL && FunctionName != NULL)
	{
		*OutFunctionNamePtr = FunctionName;
	}

	if (OutSourceFilePtr != NULL && SrcFile != NULL)
	{
		*OutSourceFilePtr = SrcFile;
		
		if (OutLineNumberPtr != NULL)
		{
			*OutLineNumberPtr = LineNumber;
		}
	}

	// Resets internal CU pointer, so next time we get here it begins from the start
	while (ReturnCode != DW_DLV_NO_ENTRY) 
	{
		if (ReturnCode == DW_DLV_ERROR)
			break;
		ReturnCode = dwarf_next_cu_header(DebugInfo, NULL, NULL, NULL, NULL, NULL, &ErrorInfo);
	}

	// if we weren't able to find a function name, don't trust the source file either
	return FunctionName != NULL;
}

/**
 * Handles graceful termination. Gives time to exit gracefully, but second signal will quit immediately.
 */
void GracefulTerminationHandler(int32 Signal, siginfo_t* Info, void* Context)
{
	printf("CtrlCHandler: Signal=%d\n", Signal);

	// make sure as much data is written to disk as possible
	if (GLog)
	{
		GLog->Flush();
	}
	if (GWarn)
	{
		GWarn->Flush();
	}
	if (GError)
	{
		GError->Flush();
	}

	if( !GIsRequestingExit )
	{
		GIsRequestingExit = 1;
	}
	else
	{
		FPlatformMisc::RequestExit(true);
	}
}

void CreateExceptionInfoString(int32 Signal, siginfo_t* Info)
{
	FString ErrorString = TEXT("Unhandled Exception: ");
	ErrorString += DescribeSignal(Signal, Info);
	FCString::Strncpy(GErrorExceptionDescription, *ErrorString, FMath::Min(ErrorString.Len() + 1, (int32)ARRAY_COUNT(GErrorExceptionDescription)));
}

namespace
{
	/** 
	 * Write a line of UTF-8 to a file
	 */
	void WriteLine(FArchive* ReportFile, const ANSICHAR* Line = NULL)
	{
		if( Line != NULL )
		{
			int64 StringBytes = FCStringAnsi::Strlen(Line);
			ReportFile->Serialize(( void* )Line, StringBytes);
		}

		// use Windows line terminator
		static ANSICHAR WindowsTerminator[] = "\r\n";
		ReportFile->Serialize(WindowsTerminator, 2);
	}

	/**
	 * Serializes UTF string to UTF-16
	 */
	void WriteUTF16String(FArchive* ReportFile, const TCHAR * UTFString4BytesChar, uint32 NumChars)
	{
		check(UTFString4BytesChar != NULL || NumChars == 0);
		static_assert(sizeof(TCHAR) == 4, "Platform TCHAR is not 4 bytes. Revisit this function.");

		for (uint32 Idx = 0; Idx < NumChars; ++Idx)
		{
			ReportFile->Serialize(const_cast< TCHAR* >( &UTFString4BytesChar[Idx] ), 2);
		}
	}

	/** 
	 * Writes UTF-16 line to a file
	 */
	void WriteLine(FArchive* ReportFile, const TCHAR* Line)
	{
		if( Line != NULL )
		{
			int64 NumChars = FCString::Strlen(Line);
			WriteUTF16String(ReportFile, Line, NumChars);
		}

		// use Windows line terminator
		static TCHAR WindowsTerminator[] = TEXT("\r\n");
		WriteUTF16String(ReportFile, WindowsTerminator, 2);
	}
}

/** 
 * Write all the data mined from the minidump to a text file
 */
void FLinuxCrashContext::GenerateReport(const FString & DiagnosticsPath) const
{
	FArchive* ReportFile = IFileManager::Get().CreateFileWriter(*DiagnosticsPath);
	if (ReportFile != NULL)
	{
		FString Line;

		WriteLine(ReportFile, "Generating report for minidump");
		WriteLine(ReportFile);

		Line = FString::Printf(TEXT("Application version %d.%d.%d.%d" ), 1, 0, ENGINE_VERSION_HIWORD, ENGINE_VERSION_LOWORD);
		WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));

		Line = FString::Printf(TEXT(" ... built from changelist %d"), ENGINE_VERSION);
		WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));
		WriteLine(ReportFile);

		utsname UnixName;
		if (uname(&UnixName) == 0)
		{
			Line = FString::Printf(TEXT( "OS version %s %s (network name: %s)" ), ANSI_TO_TCHAR(UnixName.sysname), ANSI_TO_TCHAR(UnixName.release), ANSI_TO_TCHAR(UnixName.nodename));
			WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));	
			Line = FString::Printf( TEXT( "Running %d %s processors (%d logical cores)" ), FPlatformMisc::NumberOfCores(), ANSI_TO_TCHAR(UnixName.machine), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
			WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));
		}
		else
		{
			Line = FString::Printf(TEXT("OS version could not be determined (%d, %s)"), errno, ANSI_TO_TCHAR(strerror(errno)));
			WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));	
			Line = FString::Printf( TEXT( "Running %d unknown processors" ), FPlatformMisc::NumberOfCores());
			WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));
		}
		Line = FString::Printf(TEXT("Exception was \"%s\""), SignalDescription);
		WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));
		WriteLine(ReportFile);

		WriteLine(ReportFile, "<SOURCE START>");
		WriteLine(ReportFile, "<SOURCE END>");
		WriteLine(ReportFile);

		WriteLine(ReportFile, "<CALLSTACK START>");
		WriteLine(ReportFile, MinidumpCallstackInfo);
		WriteLine(ReportFile, "<CALLSTACK END>");
		WriteLine(ReportFile);

		WriteLine(ReportFile, "0 loaded modules");

		WriteLine(ReportFile);

		Line = FString::Printf(TEXT("Report end!"));
		WriteLine(ReportFile, TCHAR_TO_UTF8(*Line));

		ReportFile->Close();
		delete ReportFile;
	}
}

/** 
 * Mimics Windows WER format
 */
void GenerateWindowsErrorReport(const FString & WERPath)
{
	FArchive* ReportFile = IFileManager::Get().CreateFileWriter(*WERPath);
	if (ReportFile != NULL)
	{
		// write BOM
		static uint16 ByteOrderMarker = 0xFEFF;
		ReportFile->Serialize(&ByteOrderMarker, sizeof(ByteOrderMarker));

		WriteLine(ReportFile, TEXT("<?xml version=\"1.0\" encoding=\"UTF-16\"?>"));
		WriteLine(ReportFile, TEXT("<WERReportMetadata>"));
		
		WriteLine(ReportFile, TEXT("\t<OSVersionInformation>"));
		WriteLine(ReportFile, TEXT("\t\t<WindowsNTVersion>0.0</WindowsNTVersion>"));
		WriteLine(ReportFile, TEXT("\t\t<Build>No Build</Build>"));
		WriteLine(ReportFile, TEXT("\t\t<Product>Linux</Product>"));
		WriteLine(ReportFile, TEXT("\t\t<Edition>No Edition</Edition>"));
		WriteLine(ReportFile, TEXT("\t\t<BuildString>No BuildString</BuildString>"));
		WriteLine(ReportFile, TEXT("\t\t<Revision>0</Revision>"));
		WriteLine(ReportFile, TEXT("\t\t<Flavor>No Flavor</Flavor>"));
		WriteLine(ReportFile, TEXT("\t\t<Architecture>Unknown Architecture</Architecture>"));
		WriteLine(ReportFile, TEXT("\t\t<LCID>0</LCID>"));
		WriteLine(ReportFile, TEXT("\t</OSVersionInformation>"));
		
		WriteLine(ReportFile, TEXT("\t<ParentProcessInformation>"));
		WriteLine(ReportFile, *FString::Printf(TEXT("\t\t<ParentProcessId>%d</ParentProcessId>"), getppid()));
		WriteLine(ReportFile, TEXT("\t\t<ParentProcessPath>C:\\Windows\\explorer.exe</ParentProcessPath>"));			// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<ParentProcessCmdLine>C:\\Windows\\Explorer.EXE</ParentProcessCmdLine>"));	// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t</ParentProcessInformation>"));

		WriteLine(ReportFile, TEXT("\t<ProblemSignatures>"));
		WriteLine(ReportFile, TEXT("\t\t<EventType>APPCRASH</EventType>"));
		WriteLine(ReportFile, *FString::Printf(TEXT("\t\t<Parameter0>UE4-%s</Parameter0>"), FApp::GetGameName()));
		WriteLine(ReportFile, *FString::Printf(TEXT("\t\t<Parameter1>1.0.%d.%d</Parameter1>"), ENGINE_VERSION_HIWORD, ENGINE_VERSION_LOWORD));
		WriteLine(ReportFile, TEXT("\t\t<Parameter2>0</Parameter2>"));													// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<Parameter3>Unknown Fault Module</Parameter3>"));										// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<Parameter4>0.0.0.0</Parameter4>"));													// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<Parameter5>00000000</Parameter5>"));													// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<Parameter6>00000000</Parameter6>"));													// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<Parameter7>0000000000000000</Parameter7>"));											// FIXME: supply valid?
		WriteLine(ReportFile, *FString::Printf(TEXT("\t\t<Parameter8>!%s!</Parameter8>"), FCommandLine::Get()));				// FIXME: supply valid? Only partially valid
		WriteLine(ReportFile, *FString::Printf(TEXT("\t\t<Parameter9>%s!%s!%s!%d</Parameter9>"), TEXT( BRANCH_NAME ), FPlatformProcess::BaseDir(), FPlatformMisc::GetEngineMode(), BUILT_FROM_CHANGELIST));
		WriteLine(ReportFile, TEXT("\t</ProblemSignatures>"));

		WriteLine(ReportFile, TEXT("\t<DynamicSignatures>"));
		WriteLine(ReportFile, TEXT("\t\t<Parameter1>6.1.7601.2.1.0.256.48</Parameter1>"));
		WriteLine(ReportFile, TEXT("\t\t<Parameter2>1033</Parameter2>"));
		WriteLine(ReportFile, TEXT("\t</DynamicSignatures>"));

		WriteLine(ReportFile, TEXT("\t<SystemInformation>"));
		WriteLine(ReportFile, TEXT("\t\t<MID>11111111-2222-3333-4444-555555555555</MID>"));							// FIXME: supply valid?
		
		WriteLine(ReportFile, TEXT("\t\t<SystemManufacturer>Unknown.</SystemManufacturer>"));						// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<SystemProductName>Linux machine</SystemProductName>"));					// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t\t<BIOSVersion>A02</BIOSVersion>"));											// FIXME: supply valid?
		WriteLine(ReportFile, TEXT("\t</SystemInformation>"));

		WriteLine(ReportFile, TEXT("</WERReportMetadata>"));

		ReportFile->Close();
		delete ReportFile;
	}
}

/** 
 * Creates (fake so far) minidump
 */
void GenerateMinidump(const FString & Path)
{
	FArchive* ReportFile = IFileManager::Get().CreateFileWriter(*Path);
	if (ReportFile != NULL)
	{
		// write BOM
		static uint32 Garbage = 0xDEADBEEF;
		ReportFile->Serialize(&Garbage, sizeof(Garbage));

		ReportFile->Close();
		delete ReportFile;
	}
}


int32 ReportCrash(const FLinuxCrashContext & Context)
{
	static bool GAlreadyCreatedMinidump = false;
	// Only create a minidump the first time this function is called.
	// (Can be called the first time from the RenderThread, then a second time from the MainThread.)
	if ( GAlreadyCreatedMinidump == false )
	{
		GAlreadyCreatedMinidump = true;

		const SIZE_T StackTraceSize = 65535;
		ANSICHAR* StackTrace = (ANSICHAR*) FMemory::Malloc( StackTraceSize );
		StackTrace[0] = 0;
		// Walk the stack and dump it to the allocated memory (ignore first 2 callstack lines as those are in stack walking code)
		FPlatformStackWalk::StackWalkAndDump( StackTrace, StackTraceSize, 2, const_cast< FLinuxCrashContext* >( &Context ) );

		FCString::Strncat( GErrorHist, ANSI_TO_TCHAR(StackTrace), ARRAY_COUNT(GErrorHist) - 1 );
		CreateExceptionInfoString(Context.Signal, Context.Info);

		FMemory::Free( StackTrace );
	}

	return 0;
}

/**
 * Generates information for crash reporter
 */
void GenerateCrashInfoAndLaunchReporter(const FLinuxCrashContext & Context)
{
	// do not report crashes for tools (particularly for crash reporter itself)
#if !IS_PROGRAM

	// create a crash-specific directory
	FString CrashInfoFolder = FString::Printf(TEXT("crashinfo-%s-pid-%d-%s-%s"), FApp::GetGameName(), getpid(), 
		*FDateTime::Now().ToString(), *FGuid::NewGuid().ToString());
	FString CrashInfoAbsolute = FPaths::ConvertRelativePathToFull(CrashInfoFolder);
	if (IFileManager::Get().MakeDirectory(*CrashInfoFolder))
	{
		// generate "minidump"
		Context.GenerateReport(FPaths::Combine(*CrashInfoFolder, TEXT("diagnostics.txt")));

		// generate "WER"
		GenerateWindowsErrorReport(FPaths::Combine(*CrashInfoFolder, TEXT("wermeta.xml")));

		// generate "minidump" (just >1 byte)
		GenerateMinidump(FPaths::Combine(*CrashInfoFolder, TEXT("minidump.dmp")));

		// copy log
		FString LogSrcAbsolute = FPlatformOutputDevices::GetAbsoluteLogFilename();
		FString LogDstAbsolute = FPaths::Combine(*CrashInfoAbsolute, *FPaths::GetCleanFilename(LogSrcAbsolute));
		FPaths::NormalizeDirectoryName(LogDstAbsolute);
		static_cast<void>(IFileManager::Get().Copy(*LogDstAbsolute, *LogSrcAbsolute));	// best effort, so don't care about result: couldn't copy -> tough, no log

		// try launching the tool and wait for its exit, if at all
		const TCHAR * RelativePathToCrashReporter = TEXT("../../../engine/binaries/linux/crashreportclient");	// FIXME: painfully hard-coded
		if (!FPaths::FileExists(RelativePathToCrashReporter))
		{
			RelativePathToCrashReporter = TEXT("../../../Engine/Binaries/Linux/CrashReportClient");	// FIXME: even more painfully hard-coded
		}

		// show on the console
		printf("Starting %s\n", StringCast<ANSICHAR>(RelativePathToCrashReporter).Get());
		FProcHandle RunningProc = FPlatformProcess::CreateProc(RelativePathToCrashReporter, *(CrashInfoAbsolute + TEXT("/")), true, false, false, NULL, 0, NULL, NULL);
		if (FPlatformProcess::IsProcRunning(RunningProc))
		{
			// do not wait indefinitely
			double kTimeOut = 3 * 60.0;
			double StartSeconds = FPlatformTime::Seconds();
			for(;;)
			{
				if (!FPlatformProcess::IsProcRunning(RunningProc))
				{
					break;
				}

				if (FPlatformTime::Seconds() - StartSeconds > kTimeOut)
				{
					break;
				}

				FPlatformProcess::Sleep(1.0f);
			};
		}
	}

#endif

	FPlatformMisc::RequestExit(true);
}

/**
 * Good enough default crash reporter.
 */
void DefaultCrashHandler(const FLinuxCrashContext & Context)
{
	printf("DefaultCrashHandler: Signal=%d\n", Context.Signal);

	ReportCrash(Context);
	if (GLog)
	{
		GLog->Flush();
	}
	if (GWarn)
	{
		GWarn->Flush();
	}
	if (GError)
	{
		GError->Flush();
		GError->HandleError();
	}

	return GenerateCrashInfoAndLaunchReporter(Context);
}

/** Global pointer to crash handler */
void (* GCrashHandlerPointer)(const FGenericCrashContext & Context) = NULL;

/** True system-specific crash handler that gets called first */
void PlatformCrashHandler(int32 Signal, siginfo_t* Info, void* Context)
{
	fprintf(stderr, "Signal %d caught.\n", Signal);

	FLinuxCrashContext CrashContext;
	CrashContext.InitFromSignal(Signal, Info, Context);

	if (GCrashHandlerPointer)
	{
		GCrashHandlerPointer(CrashContext);
	}
	else
	{
		// call default one
		DefaultCrashHandler(CrashContext);
	}
}

void FLinuxPlatformMisc::SetGracefulTerminationHandler()
{
	struct sigaction Action;
	FMemory::Memzero(&Action, sizeof(struct sigaction));
	Action.sa_sigaction = GracefulTerminationHandler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
	sigaction(SIGINT, &Action, NULL);
	sigaction(SIGTERM, &Action, NULL);
	sigaction(SIGHUP, &Action, NULL);	//  this should actually cause the server to just re-read configs (restart?)
}

void FLinuxPlatformMisc::SetCrashHandler(void (* CrashHandler)(const FGenericCrashContext & Context))
{
	GCrashHandlerPointer = CrashHandler;

	struct sigaction Action;
	FMemory::Memzero(&Action, sizeof(struct sigaction));
	Action.sa_sigaction = PlatformCrashHandler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
	sigaction(SIGQUIT, &Action, NULL);	// SIGQUIT is a user-initiated "crash".
	sigaction(SIGILL, &Action, NULL);
	sigaction(SIGFPE, &Action, NULL);
	sigaction(SIGBUS, &Action, NULL);
	sigaction(SIGSEGV, &Action, NULL);
	sigaction(SIGSYS, &Action, NULL);
}

#if !UE_BUILD_SHIPPING
bool FLinuxPlatformMisc::IsDebuggerPresent()
{
	// If a process is tracing this one then TracerPid in /proc/self/status will
	// be the id of the tracing process. Use SignalHandler safe functions 

	int StatusFile = open("/proc/self/status", O_RDONLY);
	if (StatusFile == -1) 
	{
		// Failed - unknown debugger status.
		return false;
	}

	char Buffer[256];	
	ssize_t Length = read(StatusFile, Buffer, sizeof(Buffer));
	
	bool bDebugging = false;
	const char* TracerString = "TracerPid:\t";
	const ssize_t LenTracerString = strlen(TracerString);
	int i = 0;

	while((Length - i) > LenTracerString)
	{
		// TracerPid is found
		if (strncmp(&Buffer[i], TracerString, LenTracerString) == 0)
		{
			// 0 if no process is tracing.
			bDebugging = Buffer[i + LenTracerString] != '0';
			break;
		}

		++i;
	}

	close(StatusFile);
	return bDebugging;
}
#endif // !UE_BUILD_SHIPPING
