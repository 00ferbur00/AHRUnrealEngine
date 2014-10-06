// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ICursor.h"

#ifndef __OBJC__
class NSCursor;
#endif

class FMacCursor : public ICursor
{
public:

	FMacCursor();

	virtual ~FMacCursor();

	virtual FVector2D GetPosition() const override;

	virtual void SetPosition( const int32 X, const int32 Y ) override;

	virtual void SetType( const EMouseCursor::Type InNewCursor ) override;

	virtual void GetSize( int32& Width, int32& Height ) const override;

	virtual void Show( bool bShow ) override;

	virtual void Lock( const RECT* const Bounds ) override;


public:

	bool UpdateCursorClipping( FVector2D& CursorPosition );
	
	void WarpCursor( const int32 X, const int32 Y );
	
	FVector2D GetMouseWarpDelta( bool const bClearAccumulatedDelta );

	void AssociateMouseAndCursorPosition( bool const bEnable );
	
	void SetMouseScaling( FVector2D Scale );
	
	FVector2D GetMouseScaling( void );

private:

	void UpdateVisibility();

	/** Cursors */
	NSCursor* CursorHandles[EMouseCursor::TotalCursorCount];

	FIntRect CusorClipRect;

	bool bIsVisible;
	bool bAssociateMouseCursor;
	NSCursor* CurrentCursor;
	
	FVector2D MouseWarpDelta;
	FVector2D MouseScale;
};
