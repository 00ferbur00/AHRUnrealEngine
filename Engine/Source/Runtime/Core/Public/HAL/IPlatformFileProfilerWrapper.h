// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once


#if !UE_BUILD_SHIPPING

/**
 * Wrapper to log the low level file system
**/
DECLARE_LOG_CATEGORY_EXTERN(LogProfiledFile, Log, All);

extern bool bSuppressProfiledFileLog;

#define PROFILERFILE_LOG(CategoryName, Verbosity, Format, ...) \
	if (!bSuppressProfiledFileLog) \
	{ \
		bSuppressProfiledFileLog = true; \
		UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
		bSuppressProfiledFileLog = false; \
	}

struct FProfiledFileStatsBase
{
	/** Start time (ms) */
	double StartTime;
	/** Duration (ms) */
	double Duration;

	FProfiledFileStatsBase()
		: StartTime( 0.0 )
		, Duration( 0.0 )
	{
	}
};

struct FProfiledFileStatsOp : public FProfiledFileStatsBase
{
	enum OpType
	{
		Unknown = 0,
		Tell = 1,
		Seek,
		Read,
		Write,
		Size,
		OpenRead,		
		OpenWrite,
		Exists,
		Delete,
		Move,
		IsReadOnly,
		SetReadOnly,
		GetTimeStamp,
		SetTimeStamp,
		Create,
		Copy,
		Iterate,

		Count
	};

	/** Operation type */
	uint8 Type;

	/** Number of bytes processed */
	int64 Bytes;

	/** The last time this operation was executed */
	double LastOpTime;

	FProfiledFileStatsOp( uint8 InType )
		: Type( InType )
		, Bytes( 0 )
		, LastOpTime( 0.0 )
	{}
};

struct FProfiledFileStatsFileBase : public FProfiledFileStatsBase
{
	/** File name */
	FString	Name;
	/** Child stats */
	TArray< TSharedPtr< FProfiledFileStatsOp > > Children;
	FCriticalSection SynchronizationObject;

	FProfiledFileStatsFileBase( const TCHAR* Filename )
	: Name( Filename )
	{}
};

struct FProfiledFileStatsFileDetailed : public FProfiledFileStatsFileBase
{
	FProfiledFileStatsFileDetailed( const TCHAR* Filename )
	: FProfiledFileStatsFileBase( Filename )
	{}

	FORCEINLINE FProfiledFileStatsOp* CreateOpStat( uint8 Type )
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		TSharedPtr< FProfiledFileStatsOp > Stat( new FProfiledFileStatsOp( Type ) );
		Children.Add( Stat );
		Stat->StartTime = FPlatformTime::Seconds() * 1000.0;
		Stat->LastOpTime = Stat->StartTime;
		return Stat.Get();
	}
};

struct FProfiledFileStatsFileSimple : public FProfiledFileStatsFileBase
{
	FProfiledFileStatsFileSimple( const TCHAR* Filename )
	: FProfiledFileStatsFileBase( Filename )
	{
		for( uint8 TypeIndex = 0; TypeIndex < FProfiledFileStatsOp::Count; TypeIndex++ )
		{
			TSharedPtr< FProfiledFileStatsOp > Stat( new FProfiledFileStatsOp( TypeIndex ) );
			Children.Add( Stat );
		}
	}

	FORCEINLINE FProfiledFileStatsOp* CreateOpStat( uint8 Type )
	{
		TSharedPtr< FProfiledFileStatsOp > Stat = Children[ Type ];
		Stat->LastOpTime = FPlatformTime::Seconds() * 1000.0;
		if( Stat->StartTime == 0.0 )
		{
			Stat->StartTime = Stat->LastOpTime;
		}
		return Stat.Get();
	}
};

template< typename StatType >
class TProfiledFileHandle : public IFileHandle
{
	TAutoPtr<IFileHandle> FileHandle;
	FString Filename;
	StatType* FileStats;

public:

	TProfiledFileHandle(IFileHandle* InFileHandle, const TCHAR* InFilename, StatType* InStats)
		: FileHandle(InFileHandle)
		, Filename(InFilename)
		, FileStats(InStats)
	{
	}

	virtual int64		Tell() override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Tell ) );
		int64 Result = FileHandle->Tell();
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		return Result;
	}
	virtual bool		Seek(int64 NewPosition) override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Seek ) );
		bool Result = FileHandle->Seek(NewPosition);
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		return Result;
	}
	virtual bool		SeekFromEnd(int64 NewPositionRelativeToEnd) override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Seek ) );
		bool Result = FileHandle->SeekFromEnd(NewPositionRelativeToEnd);
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		return Result;
	}
	virtual bool		Read(uint8* Destination, int64 BytesToRead) override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Read ) );
		bool Result = FileHandle->Read(Destination, BytesToRead);
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		Stat->Bytes += BytesToRead;
		return Result;
	}
	virtual bool		Write(const uint8* Source, int64 BytesToWrite) override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Write ) );
		bool Result = FileHandle->Write(Source, BytesToWrite);
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		Stat->Bytes += BytesToWrite;
		return Result;
	}
	virtual int64		Size() override
	{
		FProfiledFileStatsOp* Stat( FileStats->CreateOpStat( FProfiledFileStatsOp::Size ) );
		int64 Result = FileHandle->Size();
		Stat->Duration += FPlatformTime::Seconds() * 1000.0 - Stat->LastOpTime;
		return Result;
	}
};

class CORE_API FProfiledPlatformFile : public IPlatformFile
{
protected:

	IPlatformFile* LowerLevel;
	TMap< FString, TSharedPtr< FProfiledFileStatsFileBase > > Stats;
	double StartTime;
	FCriticalSection SynchronizationObject;

	FProfiledPlatformFile()
		: LowerLevel(nullptr)
		, StartTime(0.0)
	{
	}

public:

	virtual ~FProfiledPlatformFile()
	{
	}

	virtual bool ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const override
	{
		return false;
	}

	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CommandLineParam) override
	{
		// Inner is required.
		check(Inner != nullptr);
		LowerLevel = Inner;
		StartTime = FPlatformTime::Seconds() * 1000.0;
		return !!LowerLevel;
	}

	virtual IPlatformFile* GetLowerLevel() override
	{
		return LowerLevel;
	}

	double GetStartTime() const
	{
		return StartTime;
	}

	const TMap< FString, TSharedPtr< FProfiledFileStatsFileBase > >& GetStats() const
	{
		return Stats;
	}
};

template <class StatsType>
class TProfiledPlatformFile : public FProfiledPlatformFile
{
	FORCEINLINE StatsType* CreateStat( const TCHAR* Filename )
	{
		FString Path( Filename );
		FScopeLock ScopeLock(&SynchronizationObject);

		TSharedPtr< FProfiledFileStatsFileBase >* ExistingStat = Stats.Find( Path );
		if( ExistingStat != nullptr )
		{
			return (StatsType*)(ExistingStat->Get());
		}
		else
		{
			TSharedPtr< StatsType > Stat( new StatsType( *Path ) );
			Stats.Add( Path, Stat );
			Stat->StartTime = FPlatformTime::Seconds() * 1000.0;
			
			return Stat.Get();
		}		
	}

public:

	TProfiledPlatformFile()
	{
	}
	static const TCHAR* GetTypeName()
	{
		return nullptr;
	}
	virtual const TCHAR* GetName() const override
	{
		return TProfiledPlatformFile::GetTypeName();
	}
	virtual bool		FileExists(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Exists );
		bool Result = LowerLevel->FileExists(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual int64		FileSize(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Size );
		int64 Result = LowerLevel->FileSize(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		DeleteFile(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Delete );
		bool Result = LowerLevel->DeleteFile(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		IsReadOnly(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::IsReadOnly );
		bool Result = LowerLevel->IsReadOnly(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		MoveFile(const TCHAR* To, const TCHAR* From) override
	{
		StatsType* FileStat = CreateStat( From );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Move );
		bool Result = LowerLevel->MoveFile(To, From);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::SetReadOnly );
		bool Result = LowerLevel->SetReadOnly(Filename, bNewReadOnlyValue);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual FDateTime	GetTimeStamp(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::GetTimeStamp );
		double StartTime = FPlatformTime::Seconds();
		FDateTime Result = LowerLevel->GetTimeStamp(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000 - OpStat->LastOpTime;
		return Result;
	}
	virtual void		SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::SetTimeStamp );
		double StartTime = FPlatformTime::Seconds();
		LowerLevel->SetTimeStamp(Filename, DateTime);
		OpStat->Duration += FPlatformTime::Seconds() * 1000 - OpStat->LastOpTime;
	}
	virtual FDateTime	GetAccessTimeStamp(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::GetTimeStamp );
		double StartTime = FPlatformTime::Seconds();
		FDateTime Result = LowerLevel->GetAccessTimeStamp(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000 - OpStat->LastOpTime;
		return Result;
	}
	virtual IFileHandle*	OpenRead(const TCHAR* Filename) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::OpenRead );
		IFileHandle* Result = LowerLevel->OpenRead(Filename);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result ? (new TProfiledFileHandle< StatsType >( Result, Filename, FileStat )) : Result;
	}
	virtual IFileHandle*	OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
	{
		StatsType* FileStat = CreateStat( Filename );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::OpenWrite );
		IFileHandle* Result = LowerLevel->OpenWrite(Filename, bAppend, bAllowRead);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result ? (new TProfiledFileHandle< StatsType >( Result, Filename, FileStat )) : Result;
	}

	virtual bool		DirectoryExists(const TCHAR* Directory) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Exists );
		bool Result = LowerLevel->DirectoryExists(Directory);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		CreateDirectory(const TCHAR* Directory) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Create );
		bool Result = LowerLevel->CreateDirectory(Directory);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		DeleteDirectory(const TCHAR* Directory) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Delete );
		bool Result = LowerLevel->DeleteDirectory(Directory);
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}

	virtual bool		IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Iterate );
		bool Result = LowerLevel->IterateDirectory( Directory, Visitor );
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Iterate );
		bool Result = LowerLevel->IterateDirectoryRecursively( Directory, Visitor );
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		DeleteDirectoryRecursively(const TCHAR* Directory) override
	{
		StatsType* FileStat = CreateStat( Directory );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Delete );
		bool Result = LowerLevel->DeleteDirectoryRecursively( Directory );
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}
	virtual bool		CopyFile(const TCHAR* To, const TCHAR* From) override
	{
		StatsType* FileStat = CreateStat( From );
		FProfiledFileStatsOp* OpStat = FileStat->CreateOpStat( FProfiledFileStatsOp::Copy );
		bool Result = LowerLevel->CopyFile( To, From );
		OpStat->Duration += FPlatformTime::Seconds() * 1000.0 - OpStat->LastOpTime;
		return Result;
	}

	//static void CreateProfileVisualizer
};

template<>
inline const TCHAR* TProfiledPlatformFile<FProfiledFileStatsFileDetailed>::GetTypeName()
{
	return TEXT("ProfileFile");
}

template<>
inline const TCHAR* TProfiledPlatformFile<FProfiledFileStatsFileSimple>::GetTypeName()
{
	return TEXT("SimpleProfileFile");
}

class FPlatformFileReadStatsHandle : public IFileHandle
{
	TAutoPtr<IFileHandle> FileHandle;
	FString Filename;
	volatile int32* BytesPerSecCounter;
	volatile int32* BytesReadCounter;
	volatile int32* ReadsCounter;

public:

	FPlatformFileReadStatsHandle(IFileHandle* InFileHandle, const TCHAR* InFilename, volatile int32* InBytesPerSec, volatile int32* InByteRead, volatile int32* InReads)
		: FileHandle(InFileHandle)
		, Filename(InFilename)
		, BytesPerSecCounter(InBytesPerSec)
		, BytesReadCounter(InByteRead)
		, ReadsCounter(InReads)
	{
	}

	virtual int64		Tell() override
	{
		return FileHandle->Tell();
	}
	virtual bool		Seek(int64 NewPosition) override
	{
		return FileHandle->Seek(NewPosition);
	}
	virtual bool		SeekFromEnd(int64 NewPositionRelativeToEnd) override
	{
		return FileHandle->SeekFromEnd(NewPositionRelativeToEnd);
	}
	virtual bool		Read(uint8* Destination, int64 BytesToRead) override;
	virtual bool		Write(const uint8* Source, int64 BytesToWrite) override
	{
		return FileHandle->Write(Source, BytesToWrite);
	}
	virtual int64		Size() override
	{
		return FileHandle->Size();
	}
};

class CORE_API FPlatformFileReadStats : public IPlatformFile
{
protected:

	IPlatformFile*	LowerLevel;
	double			LifetimeReadSpeed;	// Total maintained over lifetime of runtime, in KB per sec
	double			LifetimeReadSize;	// Total maintained over lifetime of runtime, in bytes
	int64			LifetimeReadCalls;	// Total maintained over lifetime of runtime
	double			Timer;
	volatile int32	BytePerSecThisTick;
	volatile int32	BytesReadThisTick;
	volatile int32	ReadsThisTick;

public:

	FPlatformFileReadStats()
		: LowerLevel(nullptr)
		, Timer(0.f)
	{
	}

	virtual ~FPlatformFileReadStats()
	{
	}

	virtual bool ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const override
	{
#if STATS
		bool bResult = FParse::Param(CmdLine, TEXT("FileReadStats"));
		return bResult;
#else
		return false;
#endif
	}

	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CommandLineParam) override;
	bool Tick(float Delta);
	virtual IPlatformFile* GetLowerLevel() override
	{
		return LowerLevel;
	}
	static const TCHAR* GetTypeName()
	{
		return TEXT("FileReadStats");
	}
	virtual const TCHAR* GetName() const override
	{
		return GetTypeName();
	}
	virtual bool		FileExists(const TCHAR* Filename) override
	{
		return LowerLevel->FileExists(Filename);
	}
	virtual int64		FileSize(const TCHAR* Filename) override
	{
		return LowerLevel->FileSize(Filename);
	}
	virtual bool		DeleteFile(const TCHAR* Filename) override
	{
		return LowerLevel->DeleteFile(Filename);
	}
	virtual bool		IsReadOnly(const TCHAR* Filename) override
	{
		return LowerLevel->IsReadOnly(Filename);
	}
	virtual bool		MoveFile(const TCHAR* To, const TCHAR* From) override
	{
		return LowerLevel->MoveFile(To, From);
	}
	virtual bool		SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override
	{
		return LowerLevel->SetReadOnly(Filename, bNewReadOnlyValue);
	}
	virtual FDateTime	GetTimeStamp(const TCHAR* Filename) override
	{
		return LowerLevel->GetTimeStamp(Filename);
	}
	virtual void		SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override
	{
		LowerLevel->SetTimeStamp(Filename, DateTime);
	}
	virtual FDateTime	GetAccessTimeStamp(const TCHAR* Filename) override
	{
		return LowerLevel->GetAccessTimeStamp(Filename);
	}
	virtual IFileHandle*	OpenRead(const TCHAR* Filename) override
	{
		IFileHandle* Result = LowerLevel->OpenRead(Filename);
		return Result ? (new FPlatformFileReadStatsHandle(Result, Filename, &BytePerSecThisTick, &BytesReadThisTick, &ReadsThisTick)) : Result;
	}
	virtual IFileHandle*	OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
	{
		IFileHandle* Result = LowerLevel->OpenWrite(Filename, bAppend, bAllowRead);
		return Result ? (new FPlatformFileReadStatsHandle(Result, Filename, &BytePerSecThisTick, &BytesReadThisTick, &ReadsThisTick)) : Result;
	}

	virtual bool		DirectoryExists(const TCHAR* Directory) override
	{
		return LowerLevel->DirectoryExists(Directory);
	}
	virtual bool		CreateDirectory(const TCHAR* Directory) override
	{
		return LowerLevel->CreateDirectory(Directory);
	}
	virtual bool		DeleteDirectory(const TCHAR* Directory) override
	{
		return LowerLevel->DeleteDirectory(Directory);
	}

	virtual bool		IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectory( Directory, Visitor );
	}
	virtual bool		IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectoryRecursively( Directory, Visitor );
	}
	virtual bool		DeleteDirectoryRecursively(const TCHAR* Directory) override
	{
		return LowerLevel->DeleteDirectoryRecursively( Directory );
	}
	virtual bool		CopyFile(const TCHAR* To, const TCHAR* From) override
	{
		return LowerLevel->CopyFile( To, From );
	}
};


#endif // !UE_BUILD_SHIPPING
