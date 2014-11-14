// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "FriendsAndChatPrivatePCH.h"
#include "ChatViewModel.h"
#include "ChatItemViewModel.h"
#include "SChatWindow.h"
#include "SChatItem.h"

#define LOCTEXT_NAMESPACE "SChatWindow"

class SChatWindowImpl : public SChatWindow
{
public:

	void Construct(const FArguments& InArgs, const TSharedRef<FChatViewModel>& InViewModel)
	{
		FriendStyle = *InArgs._FriendStyle;
		MenuMethod = InArgs._Method;
		this->ViewModel = InViewModel;
		FChatViewModel* ViewModelPtr = ViewModel.Get();
		ViewModel->OnChatListUpdated().AddSP(this, &SChatWindowImpl::RefreshChatList);
		ViewModel->OnChatListSetFocus().AddSP(this, &SChatWindowImpl::SetFocus);

		TimeTransparency = 0.0f;

		TSharedRef<SScrollBar> ExternalScrollbar =
		SNew(SScrollBar)
		.AlwaysShowScrollbar(true);

		SUserWidget::Construct(SUserWidget::FArguments()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(FMargin(0,5))
			.VAlign(VAlign_Bottom)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
			 	.AutoWidth()
				[
					SNew( SBorder )
					.Visibility(ViewModelPtr, &FChatViewModel::GetSScrollBarVisibility)
					[
						ExternalScrollbar
					]
				]
				+SHorizontalBox::Slot()
			 	[
					SAssignNew(ChatList, SListView<TSharedRef<FChatItemViewModel>>)
					.ListItemsSource(&ViewModel->GetFilteredChatList())
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SChatWindowImpl::MakeChatWidget)
					.ExternalScrollbar(ExternalScrollbar)
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0,5))
			.VAlign(VAlign_Bottom)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(ChatBox, SHorizontalBox)
				.Visibility(ViewModelPtr, &FChatViewModel::GetEntryBarVisibility)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5)
				[
					SAssignNew(ActionMenu, SMenuAnchor)
					.Placement(EMenuPlacement::MenuPlacement_AboveAnchor)
					.Method(SMenuAnchor::UseCurrentWindow)
					.OnGetMenuContent(this, &SChatWindowImpl::GetMenuContent)
					[
						SNew(SButton)
						.ButtonStyle(&FriendStyle.FriendListActionButtonStyle)
						.ContentPadding(FMargin(5, 0))
						.OnClicked(this, &SChatWindowImpl::HandleActionDropDownClicked)
						.Cursor(EMouseCursor::Hand)
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Center)
							[
								SNew(SImage)
								.Image(&FriendStyle.FriendsCalloutBrush)
							]
							+SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Center)
							[
								SNew(SImage)
								.Image(this, &SChatWindowImpl::GetChatChannelIcon)
							]
						]
					]
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(5)
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SChatWindowImpl::GetFriendNameVisibility)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FriendStyle.FriendsFontStyleSmallBold)
						.Text(ViewModelPtr, &FChatViewModel::GetChatGroupText)
					]
					+SHorizontalBox::Slot()
					.Padding(5)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&FriendStyle.FriendListActionButtonStyle)
						.OnClicked(this, &SChatWindowImpl::HandleFriendActionDropDownClicked)
						[
							SAssignNew(ChatItemActionMenu, SMenuAnchor)
							.Placement(EMenuPlacement::MenuPlacement_AboveAnchor)
							.Method(SMenuAnchor::UseCurrentWindow)
							.OnGetMenuContent(this, &SChatWindowImpl::GetFriendActionMenu)
							[
								SNew(SImage)
								.Image(&FriendStyle.FriendsCalloutBrush)
							]
						]
					]
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(5)
				[
					SAssignNew(ChatTextBox, SEditableTextBox)
					.ClearKeyboardFocusOnCommit(false)
					.OnTextCommitted(this, &SChatWindowImpl::HandleChatEntered)
					.HintText(LOCTEXT("FriendsListSearch", "Enter to chat"))
					.Font(FriendStyle.FriendsFontStyle)
				]
			]
		]);

		RefreshChatList();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SUserWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		static const float BlendSpeed = 2.0f;

		float DesiredBlendSpeed = BlendSpeed * InDeltaTime;

		if(ViewModel.IsValid())
		{
			if(IsHovered())
			{
				TimeTransparency = FMath::Min<float>(TimeTransparency + DesiredBlendSpeed, 1.f);
			}
			else
			{
				TimeTransparency = FMath::Max<float>(TimeTransparency - DesiredBlendSpeed, 0.f);
			}
			ViewModel->SetTimeDisplayTransparency(TimeTransparency);
		}
	}

private:

	TSharedRef<ITableRow> MakeChatWidget(TSharedRef<FChatItemViewModel> ChatMessage, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(STableRow< TSharedPtr<SWidget> >, OwnerTable)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.OnClicked(ViewModel.Get(), &FChatViewModel::HandleSelectionChanged, ChatMessage)
			[
				SNew(SChatItem, ChatMessage)
				.FriendStyle(&FriendStyle)
				.Method(MenuMethod)
			]
		];
	}

	FReply HandleActionDropDownClicked() const
	{
		ActionMenu->SetIsOpen(true);
		return FReply::Handled();
	}

	FReply HandleFriendActionDropDownClicked() const
	{
		ChatItemActionMenu->SetIsOpen(true);
		return FReply::Handled();
	}

	/**
	 * Generate the action menu.
	 * @return the action menu widget
	 */
	TSharedRef<SWidget> GetMenuContent()
	{
		TSharedPtr<SVerticalBox> ChannelSelection;

		TSharedRef<SWidget> Contents =
		SNew(SUniformGridPanel)
		+SUniformGridPanel::Slot(0,0)
		[
			SNew(SBorder)
			.BorderImage(&FriendStyle.Background)
			[
				SAssignNew(ChannelSelection, SVerticalBox)
			]
		]
		+SUniformGridPanel::Slot(1,0)
		[
			SNew(SBorder)
			.BorderImage(&FriendStyle.TitleBarBrush)
			.ColorAndOpacity(FLinearColor::Gray)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged(this, &SChatWindowImpl::OnGlocalOptionChanged)
				.IsChecked(this, &SChatWindowImpl::GetGlobalOptionState)
				[
					SNew(STextBlock)
					.Text(FText::FromString("Global Chatter"))
					.Font(FriendStyle.FriendsFontStyleSmallBold)
					.ColorAndOpacity(FriendStyle.DefaultFontColor)
				]
			]
		];

		for( const auto& RecentFriend : ViewModel->GetRecentOptions())
		{
			ChannelSelection->AddSlot()
			[
				SNew(SButton)
				.ButtonStyle(&FriendStyle.FriendListItemButtonStyle)
				.OnClicked(this, &SChatWindowImpl::HandleChannelWhisperChanged, RecentFriend)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(RecentFriend->FriendName)
					.Font(FriendStyle.FriendsFontStyleSmallBold)
					.ColorAndOpacity(FriendStyle.DefaultFontColor)
				]
			];
		}

		TArray<EChatMessageType::Type> ChatMessageOptions;
		ViewModel->EnumerateChatChannelOptionsList(ChatMessageOptions);
		for( const auto& Option : ChatMessageOptions)
		{
			FSlateBrush* ChatImage = nullptr;

			switch(Option)
			{
				case EChatMessageType::Global: ChatImage =  &FriendStyle.ChatGlobalBrush; break;
				case EChatMessageType::Whisper: ChatImage = &FriendStyle.ChatWhisperBrush; break;
				case EChatMessageType::Party: ChatImage = &FriendStyle.ChatPartyBrush; break;
			}

			FLinearColor ChannelColor = FLinearColor::Gray;
			switch(Option)
			{
				case EChatMessageType::Global: ChannelColor = FriendStyle.DefaultChatColor; break;
				case EChatMessageType::Whisper: ChannelColor = FriendStyle.WhisplerChatColor; break;
				case EChatMessageType::Party: ChannelColor = FriendStyle.PartyChatColor; break;
			}

			ChannelSelection->AddSlot()
			[
				SNew(SButton)
				.ButtonStyle(&FriendStyle.FriendListItemButtonStyle)
				.OnClicked(this, &SChatWindowImpl::HandleChannelChanged, Option)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(5)
					[
						SNew(SImage)
						.Image(ChatImage)
					]
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(5)
					[
						SNew(STextBlock)
						.Text(EChatMessageType::ToText(Option))
						.Font(FriendStyle.FriendsFontStyleSmallBold)
						.ColorAndOpacity(ChannelColor)
					]
				]
			];
		};

		return Contents;
	}

	TSharedRef<SWidget> GetFriendActionMenu()
	{
		TSharedPtr<SVerticalBox> FriendActionBox;

		TSharedRef<SWidget> Contents =
			SNew(SBorder)
			.BorderImage(&FriendStyle.Background)
			[
				SAssignNew(FriendActionBox, SVerticalBox)
			];
	
		FriendActionBox->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(&FriendStyle.FriendListItemButtonStyle)
			.OnClicked(this, &SChatWindowImpl::HandleFriendActionClicked)
			[
				SNew(STextBlock)
				.Font(FriendStyle.FriendsFontStyleSmallBold)
				.ColorAndOpacity(FriendStyle.DefaultFontColor)
				.Text(FText::FromString("Some Action"))
			]
		];

		return Contents;
	}

	FReply HandleChannelChanged(const EChatMessageType::Type NewOption)
	{
		ViewModel->SetChatChannel(NewOption);
		ActionMenu->SetIsOpen(false);
		return FReply::Handled();
	}

	FReply HandleChannelWhisperChanged(const TSharedPtr<FSelectedFriend> Friend)
	{
		ViewModel->SetWhisperChannel(Friend);
		ActionMenu->SetIsOpen(false);
		return FReply::Handled();
	}

	FReply HandleFriendActionClicked()
	{
		ChatItemActionMenu->SetIsOpen(false);
		return FReply::Handled();
	}

	void CreateChatList()
	{
		if(ViewModel->GetFilteredChatList().Num())
		{
			ChatList->RequestListRefresh();
			ChatList->RequestScrollIntoView(ViewModel->GetFilteredChatList().Last());
		}
	}

	void HandleChatEntered(const FText& CommentText, ETextCommit::Type CommitInfo)
	{
		if (CommitInfo == ETextCommit::OnEnter)
		{
			SendChatMessage();
		}
	}

	FReply HandleSendClicked()
	{
		SendChatMessage();
		return FReply::Handled();
	}

	void SendChatMessage()
	{
		ViewModel->SendMessage(ChatTextBox->GetText());
		ChatTextBox->SetText(FText::GetEmpty());
	}

	void RefreshChatList()
	{
		CreateChatList();
	}

	void SetFocus()
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
		if (ChatTextBox.IsValid())
		{
			FWidgetPath WidgetToFocusPath;

			bool bFoundPath = FSlateApplication::Get().FindPathToWidget(ChatTextBox.ToSharedRef(), WidgetToFocusPath);
			if (bFoundPath && WidgetToFocusPath.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(WidgetToFocusPath, EKeyboardFocusCause::SetDirectly);
			}
		}
	}

	EVisibility GetFriendNameVisibility() const
	{
		return ViewModel->GetChatChannelType() == EChatMessageType::Whisper ? EVisibility::Visible : EVisibility::Collapsed;
	}

	const FSlateBrush* GetChatChannelIcon() const
	{
		switch(ViewModel->GetChatChannelType())
		{
			case EChatMessageType::Global: return &FriendStyle.ChatGlobalBrush; break;
			case EChatMessageType::Whisper: return &FriendStyle.ChatWhisperBrush; break;
			case EChatMessageType::Party: return &FriendStyle.ChatPartyBrush; break;
			default:
			return nullptr;
		}
	}

	void OnGlocalOptionChanged(ESlateCheckBoxState::Type NewState)
	{
		ViewModel->SetAllowGlobalChat(NewState == ESlateCheckBoxState::Unchecked ? false : true);
	}

	ESlateCheckBoxState::Type GetGlobalOptionState() const
	{
		return ViewModel->IsGlobalChatEnabled() ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual FReply OnFocusReceived( const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent ) override
	{
		return FReply::Handled().ReleaseMouseCapture().LockMouseToWidget( SharedThis( this ) );
	}

private:

	// Holds the chat list
	TSharedPtr<SListView<TSharedRef<FChatItemViewModel> > > ChatList;

	// Holds the chat box
	TSharedPtr<SHorizontalBox> ChatBox;

	// Holds the chat list display
	TSharedPtr<SEditableTextBox> ChatTextBox;

	// Holds the menu anchor
	TSharedPtr<SMenuAnchor> ActionMenu;

	// Holds the Friend action button
	TSharedPtr<SMenuAnchor> ChatItemActionMenu;

	// Holds the Friends List view model
	TSharedPtr<FChatViewModel> ViewModel;

	/** Holds the style to use when making the widget. */
	FFriendsAndChatStyle FriendStyle;

	// Holds the menu method - Full screen requires use owning window or crashes.
	SMenuAnchor::EMethod MenuMethod;

	// Holds the time transparency.
	float TimeTransparency;
};

TSharedRef<SChatWindow> SChatWindow::New()
{
	return MakeShareable(new SChatWindowImpl());
}

#undef LOCTEXT_NAMESPACE
