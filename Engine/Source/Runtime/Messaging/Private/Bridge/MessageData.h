// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once


/** Type definition for shared pointers to instances of FMessageData. */
typedef TSharedPtr<class FMessageData, ESPMode::ThreadSafe> FMessageDataPtr;

/** Type definition for shared references to instances of FMessageData. */
typedef TSharedRef<class FMessageData, ESPMode::ThreadSafe> FMessageDataRef;


/**
 * Holds serialized message data.
 */
class FMessageData
	: public FMemoryWriter
	, public IMessageData
{
public:

	/** Default constructor. */
	FMessageData()
		: FMemoryWriter(Data, true)
		, State(EMessageDataState::Incomplete)
	{ }

public:

	/**
	 * Updates the state of this message data.
	 *
	 * @param InState The state to set.
	 */
	void UpdateState( EMessageDataState InState )
	{
		State = InState;
		StateChangedDelegate.ExecuteIfBound();
	}

public:

	// IMessageAttachment interface

	virtual FArchive* CreateReader() override
	{
		return new FMemoryReader(Data, true);
	}

	virtual EMessageDataState GetState() const override
	{
		return State;
	}

	virtual FSimpleDelegate& OnStateChanged() override
	{
		return StateChangedDelegate;
	}

private:

	/** Holds the data. */
	TArray<uint8> Data;

	/** Holds the message data state. */
	EMessageDataState State;

private:

	/** Holds a delegate that is invoked when the data's state changed. */
	FSimpleDelegate StateChangedDelegate;
};
