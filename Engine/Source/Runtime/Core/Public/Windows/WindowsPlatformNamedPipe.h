// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	WindowsPlatformNamedPipe.h: Declares the FWindowsPlatformNamedPipe class
==============================================================================================*/

#pragma once
#include "HAL/Platform.h"
#include "GenericPlatform/GenericPlatformNamedPipe.h"
#include "Windows/WindowsSystemIncludes.h"

#if PLATFORM_SUPPORTS_NAMED_PIPES

class FString;

// Windows wrapper for named pipe communications
class CORE_API FWindowsPlatformNamedPipe : public FGenericPlatformNamedPipe
{
public:

	FWindowsPlatformNamedPipe();
	virtual ~FWindowsPlatformNamedPipe();


public:

	// Create a named pipe, using overlapped IO if bAsync=1
	virtual bool Create(const FString& PipeName, bool bAsServer, bool bAsync) override;

	// Destroy the pipe
	virtual bool Destroy() override;

	// Open a connection from a client
	virtual bool OpenConnection() override;

	// Blocks if there's an IO operation in progress until it's done or errors out
	virtual bool BlockForAsyncIO() override;

	// Lets the user know if the pipe is ready to send or receive data
	virtual bool IsReadyForRW() const override;

	// Updates status of async state of the current pipe
	virtual bool UpdateAsyncStatus() override;

	// Writes a buffer out
	virtual bool WriteBytes(int32 NumBytes, const void* Data) override;

	// Reads a buffer in
	virtual bool ReadBytes(int32 NumBytes, void* OutData) override;

	// Returns true if the pipe has been created and hasn't been destroyed
	virtual bool IsCreated() const override;

	// Return true if the pipe has had any communication error
	virtual bool HasFailed() const override;


private:

	void*		Pipe;
	OVERLAPPED	Overlapped;
	double		LastWaitingTime;
	bool		bUseOverlapped : 1;
	bool		bIsServer : 1;

	enum EState
	{
		State_Uninitialized,
		State_Created,
		State_Connecting,
		State_ReadyForRW,
		State_WaitingForRW,

		State_ErrorPipeClosedUnexpectedly,
	};

	EState State;

	bool UpdateAsyncStatusAfterRW();
};


typedef FWindowsPlatformNamedPipe FPlatformNamedPipe;

#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
