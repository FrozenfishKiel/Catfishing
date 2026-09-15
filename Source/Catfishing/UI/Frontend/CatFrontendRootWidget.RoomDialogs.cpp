#include "UI/Frontend/CatFrontendRootWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Logging/CatLog.h"
#include "UI/Frontend/CatFrontendRoomModel.h"

namespace CatRoomDialogs
{
	template<class T> T* Find(UUserWidget* Page, const TCHAR* Name)
	{
		return Page ? Cast<T>(Page->GetWidgetFromName(Name)) : nullptr;
	}
	void Text(UUserWidget* Page, const TCHAR* Name, const FString& Value)
	{
		if (auto* Label = Find<UTextBlock>(Page, Name)) { Label->SetText(FText::FromString(Value)); }
	}
	void SelectTab(UUserWidget* Page, bool bFriends)
	{
		for (const TCHAR* Name : {TEXT("RoomInviteFriendsTabButton"), TEXT("RoomInviteIdTabButton")})
		{
			if (auto* Button = Find<UButton>(Page, Name))
			{
				const bool bSelected = (FCString::Strcmp(Name, TEXT("RoomInviteFriendsTabButton")) == 0) == bFriends;
				FButtonStyle Style = Button->GetStyle();
				Style.Normal.TintColor = FSlateColor(bSelected ? FLinearColor(.04f, .23f, .20f, .95f) : FLinearColor(.01f, .05f, .04f, .85f));
				Button->SetStyle(Style);
			}
		}
	}
}

FReply UCatFrontendRootWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && IsRoomDialogOpen())
	{
		RequestCloseRoomDialog();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

bool UCatFrontendRootWidget::IsRoomDialogOpen() const
{
	const auto* Layer = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomDialogLayer"));
	return Layer && Layer->GetVisibility() != ESlateVisibility::Collapsed;
}

void UCatFrontendRootWidget::RequestCloseRoomDialog()
{
	bConfirmRoomDismiss = false;
	if (auto* Notice = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomNoticeDialog"))) { Notice->SetVisibility(ESlateVisibility::Collapsed); }
	if (auto* Layer = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomDialogLayer"))) { Layer->SetVisibility(ESlateVisibility::Collapsed); }
	if (auto* Password = CatRoomDialogs::Find<UEditableTextBox>(RoomPage, TEXT("RoomPasswordInput"))) { Password->SetText(FText::GetEmpty()); }
	if (auto* Open = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("OpenRoomInviteButton")); IsShowingRoom() && Open) { Open->SetKeyboardFocus(); }
}

void UCatFrontendRootWidget::RequestOpenRoomInvite()
{
	if (!IsShowingRoom()) { return; }
	RequestCloseRoomDialog();
	if (auto* Layer = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomDialogLayer"))) { Layer->SetVisibility(ESlateVisibility::Visible); }
	if (auto* Invite = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomInviteDialog"))) { Invite->SetVisibility(ESlateVisibility::Visible); }
	if (auto* Settings = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomSettingsDialog"))) { Settings->SetVisibility(ESlateVisibility::Collapsed); }
	RequestRoomInviteFriendsTab();
	// A disconnected room preview has no platform session to refresh.
	if (RoomModel && RoomModel->GetSnapshot().SessionState != ECatOnlineSessionState::NoSession) { RequestRefreshFriends(); }
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_room_invite_opened World=%s NetMode=%d"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
}

void UCatFrontendRootWidget::RequestRoomInviteFriendsTab()
{
	if (auto* Tabs = CatRoomDialogs::Find<UWidgetSwitcher>(RoomPage, TEXT("RoomInviteTabs"))) { Tabs->SetActiveWidgetIndex(0); }
	CatRoomDialogs::SelectTab(RoomPage, true);
	if (FriendSearchTextBox) { FriendSearchTextBox->SetKeyboardFocus(); }
}

void UCatFrontendRootWidget::RequestRoomInviteIdTab()
{
	if (auto* Tabs = CatRoomDialogs::Find<UWidgetSwitcher>(RoomPage, TEXT("RoomInviteTabs"))) { Tabs->SetActiveWidgetIndex(1); }
	CatRoomDialogs::SelectTab(RoomPage, false);
	if (auto* Copy = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("CopyModalRoomIdButton"))) { Copy->SetKeyboardFocus(); }
}

void UCatFrontendRootWidget::RequestOpenRoomSettings()
{
	if (!IsShowingRoom()) { return; }
	RequestCloseRoomDialog();
	const auto Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	if (auto* Layer = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomDialogLayer"))) { Layer->SetVisibility(ESlateVisibility::Visible); }
	if (auto* Invite = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomInviteDialog"))) { Invite->SetVisibility(ESlateVisibility::Collapsed); }
	if (auto* Settings = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomSettingsDialog"))) { Settings->SetVisibility(ESlateVisibility::Visible); }
	if (auto* Name = CatRoomDialogs::Find<UEditableTextBox>(RoomPage, TEXT("RoomNameInput"))) { Name->SetText(FText::FromString(Snapshot.RoomName)); Name->SetKeyboardFocus(); }
	if (auto* Capacity = CatRoomDialogs::Find<UComboBoxString>(RoomPage, TEXT("RoomCapacityInput"))) { Capacity->SetSelectedOption(FString::FromInt(Snapshot.MaxPlayers)); }
	if (auto* Access = CatRoomDialogs::Find<UComboBoxString>(RoomPage, TEXT("RoomAccessInput")))
	{
		Access->SetSelectedIndex(Snapshot.SessionAccess == ECatSessionAccessPolicy::Public ? 0 : Snapshot.SessionAccess == ECatSessionAccessPolicy::FriendsOnly ? 1 : 2);
	}
	if (auto* Clear = CatRoomDialogs::Find<UCheckBox>(RoomPage, TEXT("RoomClearPasswordCheckBox"))) { Clear->SetIsChecked(false); }
	RefreshRoomDialogPresentation(Snapshot);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_room_settings_opened World=%s NetMode=%d Host=%d"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Snapshot.bIsHost);
}

void UCatFrontendRootWidget::RequestSaveRoomSettings()
{
	// The save action is enabled only once the room settings service is connected.
	CatRoomDialogs::Text(RoomPage, TEXT("RoomSettingsFeedbackText"), TEXT("房间设置服务暂不可用，修改尚未保存。"));
}

void UCatFrontendRootWidget::RefreshRoomDialogPresentation(const FCatOnlineSnapshot& Snapshot)
{
	CatRoomDialogs::Text(RoomPage, TEXT("RoomNameText"), Snapshot.RoomName.IsEmpty() ? TEXT("房间") : Snapshot.RoomName);
	CatRoomDialogs::Text(RoomPage, TEXT("RoomMemberCountText"), FString::Printf(TEXT("%d / %d 位伙伴"), Snapshot.CurrentPlayers, Snapshot.MaxPlayers));
	CatRoomDialogs::Text(RoomPage, TEXT("RoomModalIdText"), Snapshot.LobbyId.IsEmpty() ? TEXT("等待房间 ID") : Snapshot.LobbyId);
	if (auto* Feedback = CatRoomDialogs::Find<UTextBlock>(RoomPage, TEXT("RoomInviteFeedbackText")))
	{
		const FText Result = RoomModel ? RoomModel->GetLastResultText() : FText::GetEmpty();
		Feedback->SetText(Result);
		Feedback->SetVisibility(Result.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	const bool bEditable = Snapshot.bIsHost && Snapshot.ActiveOperation == ECatOnlineOperation::None && !Snapshot.bIsGameplayLoadPending;
	if (const auto* Settings = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomSettingsDialog")); Settings && Settings->GetVisibility() == ESlateVisibility::Visible)
	{
		CatRoomDialogs::Text(RoomPage, TEXT("RoomSettingsFeedbackText"), Snapshot.bIsHost ? TEXT("房间设置暂未开放，修改尚不能保存。") : TEXT(""));
	}
	for (const TCHAR* Name : {TEXT("RoomNameInput"), TEXT("RoomCapacityInput"), TEXT("RoomAccessInput"), TEXT("RoomPasswordInput"), TEXT("RoomClearPasswordCheckBox")})
	{
		if (auto* Field = CatRoomDialogs::Find<UWidget>(RoomPage, Name)) { Field->SetIsEnabled(bEditable); }
	}
	if (auto* Save = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("SaveRoomSettingsButton"))) { Save->SetIsEnabled(false); }
	for (const TCHAR* Name : {TEXT("CopyModalRoomIdButton"), TEXT("CopySettingsRoomIdButton")})
	{
		if (auto* Copy = CatRoomDialogs::Find<UButton>(RoomPage, Name)) { Copy->SetIsEnabled(!Snapshot.LobbyId.IsEmpty()); }
	}
	if (auto* Copy = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("CopyRoomLinkButton"))) { Copy->SetIsEnabled(!Snapshot.JoinLobbyUri.IsEmpty()); }
	if (auto* Dismiss = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("DismissRoomButton"))) { Dismiss->SetVisibility(Snapshot.bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); Dismiss->SetIsEnabled(bEditable); }
	if (auto* Lock = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomSettingsReadonlyText"))) { Lock->SetVisibility(Snapshot.bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
}

void UCatFrontendRootWidget::BindRoomDialogControls(bool bBind)
{
#define ROOM_DIALOG_BUTTON(Name, Method) \
	if (auto* Button = CatRoomDialogs::Find<UButton>(RoomPage, TEXT(Name))) \
	{ \
		if (bBind) { Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Method); } \
		else { Button->OnClicked.RemoveDynamic(this, &ThisClass::Method); } \
	}
	ROOM_DIALOG_BUTTON("OpenRoomInviteButton", RequestOpenRoomInvite)
	ROOM_DIALOG_BUTTON("OpenRoomSettingsButton", RequestOpenRoomSettings)
	ROOM_DIALOG_BUTTON("RoomDialogBackdrop", RequestCloseRoomDialog)
	ROOM_DIALOG_BUTTON("CloseRoomInviteButton", RequestCloseRoomDialog)
	ROOM_DIALOG_BUTTON("CloseRoomSettingsButton", RequestCloseRoomDialog)
	ROOM_DIALOG_BUTTON("RoomInviteFriendsTabButton", RequestRoomInviteFriendsTab)
	ROOM_DIALOG_BUTTON("RoomInviteIdTabButton", RequestRoomInviteIdTab)
	ROOM_DIALOG_BUTTON("CopyModalRoomIdButton", RequestCopyRoomInviteCode)
	ROOM_DIALOG_BUTTON("CopySettingsRoomIdButton", RequestCopyRoomInviteCode)
	ROOM_DIALOG_BUTTON("SaveRoomSettingsButton", RequestSaveRoomSettings)
	ROOM_DIALOG_BUTTON("CopyRoomLinkButton", RequestCopyRoomLink)
	ROOM_DIALOG_BUTTON("ConfirmRoomNoticeButton", RequestConfirmRoomNotice)
	ROOM_DIALOG_BUTTON("CloseRoomNoticeButton", RequestCloseRoomDialog)
	ROOM_DIALOG_BUTTON("DismissRoomButton", RequestDismissRoomConfirmation)
#undef ROOM_DIALOG_BUTTON
}

void UCatFrontendRootWidget::ShowRoomNotice(const FText& Title, const FText& Message, const FString& Value, bool bSuccess)
{
	if (!IsShowingRoom()) { return; }
	RequestCloseRoomDialog();
	for (const TCHAR* Name : {TEXT("RoomInviteDialog"), TEXT("RoomSettingsDialog")})
	{
		if (auto* Panel = CatRoomDialogs::Find<UWidget>(RoomPage, Name)) { Panel->SetVisibility(ESlateVisibility::Collapsed); }
	}
	for (const TCHAR* Name : {TEXT("RoomDialogLayer"), TEXT("RoomNoticeDialog")})
	{
		if (auto* Panel = CatRoomDialogs::Find<UWidget>(RoomPage, Name)) { Panel->SetVisibility(ESlateVisibility::Visible); }
	}
	CatRoomDialogs::Text(RoomPage, TEXT("RoomNoticeDialogTitle"), Title.ToString());
	CatRoomDialogs::Text(RoomPage, TEXT("RoomNoticeMessageText"), Message.ToString());
	CatRoomDialogs::Text(RoomPage, TEXT("RoomNoticeValueText"), Value);
	if (auto* Mark = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomNoticeSuccessMark"))) { Mark->SetVisibility(bSuccess ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (auto* Label = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomNoticeValueText"))) { Label->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (auto* Confirm = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("ConfirmRoomNoticeButton"))) { Confirm->SetKeyboardFocus(); }
}

void UCatFrontendRootWidget::RequestCopyRoomLink()
{
	const auto Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	if (Snapshot.JoinLobbyUri.IsEmpty())
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_room_link_copy_rejected World=%s Reason=LinkUnavailable"), *GetNameSafe(GetWorld()));
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Snapshot.JoinLobbyUri);
	ShowRoomNotice(FText::FromString(TEXT("复制房间链接")), FText::FromString(TEXT("已复制到剪贴板\n快去分享给你的朋友吧！")), Snapshot.JoinLobbyUri);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_room_link_copied World=%s NetMode=%d"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
}

void UCatFrontendRootWidget::RequestDismissRoomConfirmation()
{
	if (!RoomModel || !RoomModel->GetSnapshot().bIsHost) { return; }
	ShowRoomNotice(FText::FromString(TEXT("解散房间？")), FText::FromString(TEXT("所有队员将离开当前大厅。")), FString());
	bConfirmRoomDismiss = true;
}

void UCatFrontendRootWidget::RequestConfirmRoomNotice()
{
	const bool bDismiss = bConfirmRoomDismiss;
	RequestCloseRoomDialog();
	if (bDismiss && RoomModel && RoomModel->GetSnapshot().bIsHost) { RequestLeaveRoom(); }
}

void UCatFrontendRootWidget::ResetRoomScene()
{
	PresentedLobbyId.Reset(); PresentedMembers.Reset(); RoomJoinNoticeUntil = 0;
	if (auto* Toast = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomJoinedToast"))) { Toast->SetVisibility(ESlateVisibility::Collapsed); }
	if (auto* Background = Cast<UImage>(GetWidgetFromName(TEXT("StaticBackgroundImage"))))
	{
		Background->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Texture/Frontend/T_UI_Frontend_LakeNight.T_UI_Frontend_LakeNight")));
	}
}

void UCatFrontendRootWidget::RefreshRoomScene(const FCatOnlineSnapshot& Snapshot)
{
	if (!IsShowingRoom()) { return; }
	if (auto* Source = CatRoomDialogs::Find<UImage>(RoomPage, TEXT("RoomBackgroundSource")))
	{
		if (auto* Background = Cast<UImage>(GetWidgetFromName(TEXT("StaticBackgroundImage"))))
		{
			if (auto* Texture = Cast<UTexture2D>(Source->GetBrush().GetResourceObject()); Texture && Background->GetBrush().GetResourceObject() != Texture) { Background->SetBrushFromTexture(Texture); }
		}
	}
	FString SaveTitle = Snapshot.bIsHost ? TEXT("当前存档") : TEXT("房主的存档");
	FString SaveMeta = Snapshot.bIsHost ? TEXT("准备出发") : TEXT("跟随房主一起出发");
	if (Snapshot.bIsHost && SaveModel)
	{
		const FName ActiveId = SaveModel->GetActiveSlotId();
		if (const auto* Save = SaveModel->GetSlotSummaries().FindByPredicate([&](const auto& Item) { return Item.SlotId == ActiveId; }))
		{
			SaveTitle = Save->DisplayName;
			SaveMeta = Save->DayIndex > 0 ? FString::Printf(TEXT("第 %d 天 · %d 分钟"), Save->DayIndex, FMath::FloorToInt(Save->PlayedDurationSeconds / 60.0)) : TEXT("新的旅程");
		}
	}
	CatRoomDialogs::Text(RoomPage, TEXT("RoomSaveTitleText"), SaveTitle);
	CatRoomDialogs::Text(RoomPage, TEXT("RoomSaveMetaText"), SaveMeta);
	if (!Snapshot.LobbyId.IsEmpty() && PresentedLobbyId == Snapshot.LobbyId)
	{
		for (const auto& Member : Snapshot.RoomMembers)
		{
			if (Member.MemberId.IsValid() && !PresentedMembers.Contains(Member.MemberId) && !Member.bIsLocalPlayer)
			{
				CatRoomDialogs::Text(RoomPage, TEXT("RoomJoinedNameText"), Member.DisplayName + TEXT(" 加入了房间！"));
				if (auto* Toast = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomJoinedToast"))) { Toast->SetVisibility(ESlateVisibility::HitTestInvisible); Toast->SetRenderOpacity(1); }
				RoomJoinNoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.5;
				UE_LOG(LogCatUI, Log, TEXT("Event=ui_room_member_arrived World=%s NetMode=%d Member=%s"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), *Member.MemberId.ToString());
			}
		}
	}
	PresentedLobbyId = Snapshot.LobbyId;
	PresentedMembers.Reset();
	for (const auto& Member : Snapshot.RoomMembers) { if (Member.MemberId.IsValid()) { PresentedMembers.Add(Member.MemberId); } }
}

void UCatFrontendRootWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (RoomJoinNoticeUntil <= 0 || !GetWorld()) { return; }
	if (auto* Toast = CatRoomDialogs::Find<UWidget>(RoomPage, TEXT("RoomJoinedToast")))
	{
		const double Remaining = RoomJoinNoticeUntil - GetWorld()->GetRealTimeSeconds();
		Toast->SetRenderOpacity(float(FMath::Clamp(Remaining / .5, 0.0, 1.0)));
		if (Remaining <= 0) { Toast->SetVisibility(ESlateVisibility::Collapsed); RoomJoinNoticeUntil = 0; }
	}
}
