#include "UI/Frontend/CatFrontendRootWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
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
	CatRoomDialogs::Text(RoomPage, TEXT("RoomSettingsFeedbackText"), Snapshot.bIsHost ? TEXT("房间设置暂未开放，修改尚不能保存。") : TEXT("只有房主可以修改房间设置。"));
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
	CatRoomDialogs::Text(RoomPage, TEXT("RoomInviteFeedbackText"), RoomModel ? RoomModel->GetLastResultText().ToString() : TEXT(""));
	const bool bEditable = Snapshot.bIsHost && Snapshot.ActiveOperation == ECatOnlineOperation::None && !Snapshot.bIsGameplayLoadPending;
	for (const TCHAR* Name : {TEXT("RoomNameInput"), TEXT("RoomCapacityInput"), TEXT("RoomAccessInput"), TEXT("RoomPasswordInput"), TEXT("RoomClearPasswordCheckBox")})
	{
		if (auto* Field = CatRoomDialogs::Find<UWidget>(RoomPage, Name)) { Field->SetIsEnabled(bEditable); }
	}
	if (auto* Save = CatRoomDialogs::Find<UButton>(RoomPage, TEXT("SaveRoomSettingsButton"))) { Save->SetIsEnabled(false); }
	for (const TCHAR* Name : {TEXT("CopyModalRoomIdButton"), TEXT("CopySettingsRoomIdButton")})
	{
		if (auto* Copy = CatRoomDialogs::Find<UButton>(RoomPage, Name)) { Copy->SetIsEnabled(!Snapshot.LobbyId.IsEmpty()); }
	}
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
#undef ROOM_DIALOG_BUTTON
}
