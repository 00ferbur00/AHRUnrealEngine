// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericWindow.h"
#include "SharedPointer.h"

#ifdef __OBJC__

/**
 * Custom window class used for input handling
 */
@interface FSlateCocoaWindow : NSWindow <NSWindowDelegate>
{
	EWindowMode::Type WindowMode;
	NSRect DeferFrame;
	CGFloat DeferOpacity;
	bool bAcceptsInput;
	bool bRoundedCorners;
	bool bDisplayReconfiguring;
	bool bDeferOrderFront;
	bool bDeferSetFrame;
	bool bDeferSetOrigin;
	bool bRenderInitialised;
	bool bNeedsRedraw;
@public
	bool bZoomed;
}

@property (assign) bool bForwardEvents;

/** Get the frame filled by a child OpenGL view, which may cover the window or fill the content view depending upon the window style.
 @return The NSRect for a child OpenGL view. */
- (NSRect)openGLFrame;

/** Get the view used for OpenGL rendering. @return The OpenGL view for rendering. */
- (NSView*)openGLView;

/** Perform render thread deferred order front operation. */
- (void)performDeferredOrderFront;

/** Perform deferred set frame operation */
- (void)performDeferredSetFrame;

/** Set whether the window should display with rounded corners. */
- (void)setRoundedCorners:(bool)bUseRoundedCorners;

/** Get whether the window should display with rounded corners. 
 @return True when window corners should be rounded, else false. */
- (bool)roundedCorners;

/** Lets window know if its owner (SWindow) accepts input */
- (void)setAcceptsInput:(bool)InAcceptsInput;

/** Redraws window's contents. */
- (void)redrawContents;

/** Set the initial window mode. */
- (void)setWindowMode:(EWindowMode::Type)WindowMode;

/**	@return The current mode for this Cocoa window. */
- (EWindowMode::Type)windowMode;

/** Mutator that specifies that the display arrangement is being reconfigured when bIsDisplayReconfiguring is true. */
- (void)setDisplayReconfiguring:(bool)bIsDisplayReconfiguring;

/** Order window to the front. */
- (void)orderFrontAndMakeMain:(bool)bMain andKey:(bool)bKey;

@end

/**
 * Custom window class used for mouse capture
 */
@interface FMouseCaptureWindow : NSWindow <NSWindowDelegate>
{
	FSlateCocoaWindow*	TargetWindow;
}

- (id)initWithTargetWindow: (FSlateCocoaWindow*)Window;
- (FSlateCocoaWindow*)targetWindow;
- (void)setTargetWindow: (FSlateCocoaWindow*)Window;

@end

#else // __OBJC__

class FSlateCocoaWindow;
class FMouseCaptureWindow;
class NSWindow;
class NSEvent;
class NSScreen;

#endif // __OBJC__

/**
 * A platform specific implementation of FNativeWindow.
 * Native Windows provide platform-specific backing for and are always owned by an SWindow.
 */
class CORE_API FMacWindow : public FGenericWindow, public TSharedFromThis<FMacWindow>
{
public:
	~FMacWindow();

	static TSharedRef< FMacWindow > Make();

	FSlateCocoaWindow* GetWindowHandle() const;

	void Initialize( class FMacApplication* const Application, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FMacWindow >& InParent, const bool bShowImmediately );
	
	void OnDisplayReconfiguration(CGDirectDisplayID Display, CGDisplayChangeSummaryFlags Flags);
	
	bool OnIMKKeyDown(NSEvent* Event);

public:

	virtual void ReshapeWindow( int32 X, int32 Y, int32 Width, int32 Height ) override;

	virtual bool GetFullScreenInfo( int32& X, int32& Y, int32& Width, int32& Height ) const override;

	virtual void MoveWindowTo ( int32 X, int32 Y ) override;

	virtual void BringToFront( bool bForce = false ) override;

	virtual void Destroy() override;

	virtual void Minimize() override;

	virtual void Maximize() override;

	virtual void Restore() override;

	virtual void Show() override;

	virtual void Hide() override;

	virtual void SetWindowMode( EWindowMode::Type NewWindowMode ) override;

	virtual EWindowMode::Type GetWindowMode() const override { return WindowMode; } 

	virtual bool IsMaximized() const override;

	virtual bool IsMinimized() const override;

	virtual bool IsVisible() const override;

	virtual bool GetRestoredDimensions(int32& X, int32& Y, int32& Width, int32& Height) override;

	virtual void SetWindowFocus() override;

	virtual void SetOpacity( const float InOpacity ) override;

	virtual void Enable( bool bEnable ) override;

	virtual bool IsPointInWindow( int32 X, int32 Y ) const override;

	virtual int32 GetWindowBorderSize() const override;

	virtual void* GetOSWindowHandle() const  override { return WindowHandle; }

	virtual bool IsForegroundWindow() const override;

	virtual void SetText(const TCHAR* const Text) override;

	virtual void AdjustCachedSize( FVector2D& Size ) const override;

	/**
	 * Sets the window text - usually the title but can also be text content for things like controls
	 *
	 * @param Text	The window's title or content text
	 */
	bool IsRegularWindow() const;

	int32 PositionX;
	int32 PositionY;


private:

	/**
	 * Protect the constructor; only TSharedRefs of this class can be made.
	 */
	FMacWindow();


private:

	FMacApplication* OwningApplication;

	/** Mac window handle */
	FSlateCocoaWindow* WindowHandle;
	
	/** The mode that the window is in (windowed, fullscreen, windowedfullscreen ) */
	EWindowMode::Type WindowMode;

	RECT PreFullscreenWindowRect;

	bool bIsVisible : 1;
};
