// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITextInputMethodSystem.h"
#include "TextStoreACP.h"

#include "AllowWindowsPlatformTypes.h"
#include <msctf.h>
#include "COMPointer.h"

class FTextStoreACP;
class FWindowsTextInputMethodSystem;

class FTSFActivationProxy : public ITfInputProcessorProfileActivationSink, public ITfActiveLanguageProfileNotifySink
{
public:
	FTSFActivationProxy(FWindowsTextInputMethodSystem* InOwner) : Owner(InOwner), ReferenceCount(1)
	{}

	// IUnknown Interface Begin
	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;
	// IUnknown Interface End

	// ITfInputProcessorProfileActivationSink Interface Begin
	STDMETHODIMP OnActivated(DWORD dwProfileType, LANGID langid, __RPC__in REFCLSID clsid, __RPC__in REFGUID catid, __RPC__in REFGUID guidProfile, HKL hkl, DWORD dwFlags) override;
	// ITfInputProcessorProfileActivationSink Interface End

	// ITfActiveLanguageProfileNotifySink Interface Begin
	STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) override;
	// ITfActiveLanguageProfileNotifySink Interface End

public:
	DWORD TSFProfileCookie;
	DWORD TSFLanguageCookie;

private:
	FWindowsTextInputMethodSystem* Owner;

	// Reference count for IUnknown Implementation
	ULONG ReferenceCount;
};

class FWindowsTextInputMethodSystem : public ITextInputMethodSystem
{
	friend class FTSFActivationProxy;

public:
	bool Initialize();
	void Terminate();

	// ITextInputMethodSystem Interface Begin
	virtual TSharedPtr<ITextInputMethodChangeNotifier> RegisterContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	virtual void UnregisterContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	virtual void ActivateContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	virtual void DeactivateContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	// ITextInputMethodSystem Interface End

	int32 ProcessMessage(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam);

private:
	// IMM Implementation
	bool InitializeIMM();
	void UpdateIMMProperty(HKL KeyboardLatoutHandle);
	bool ShouldDrawIMMCompositionString() const;
	void UpdateIMMWindowPositions(HIMC IMMContext);
	void BeginIMMComposition();
	void EndIMMComposition();

	// TSF Implementation
	bool InitializeTSF();

private:
	TSharedPtr<ITextInputMethodContext> ActiveContext;
	enum class EAPI
	{
		Unknown,
		IMM,
		TSF
	} CurrentAPI;

	// TSF Implementation
	TComPtr<ITfInputProcessorProfiles> TSFInputProcessorProfiles;
	TComPtr<ITfInputProcessorProfileMgr> TSFInputProcessorProfileManager;
	TComPtr<ITfThreadMgr> TSFThreadManager;
	TfClientId TSFClientId;
	TComPtr<ITfDocumentMgr> TSFDisabledDocumentManager;
	TComPtr<FTSFActivationProxy> TSFActivationProxy;

	struct FInternalContext
	{
		TComPtr<FTextStoreACP> TSFContext;
		struct
		{
			bool IsComposing;
			bool IsDeactivating;
			int32 CompositionBeginIndex;
			uint32 CompositionLength;
		} IMMContext;
	};
	TMap< TWeakPtr<ITextInputMethodContext>, FInternalContext > ContextToInternalContextMap;

	// IMM Implementation
	DWORD IMEProperties;
};

#include "HideWindowsPlatformTypes.h"