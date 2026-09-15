#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Logging/CatLog.h"

namespace
{
 template<class T> T* Control(UUserWidget* Page, const TCHAR* Name) { return Page ? Cast<T>(Page->GetWidgetFromName(Name)) : nullptr; }
}
void UCatFrontendRootWidget::RequestRefreshPublicRooms() { if (RoomModel) { RoomModel->RefreshPublicRooms(); } }
void UCatFrontendRootWidget::RequestJoinPublicRoom(FCatSessionSearchHandle Handle) { if (RoomModel) { RoomModel->JoinPublicRoom(Handle); } }
void UCatFrontendRootWidget::RequestSubmitRoomPassword()
{
 if (auto* Input = Control<UEditableTextBox>(JoinPage, TEXT("JoinPasswordInput")))
 {
   const FString Password = Input->GetText().ToString();
   Input->SetText(FText::GetEmpty());
   if (RoomModel) { RoomModel->SubmitPassword(Password); }
 }
}
void UCatFrontendRootWidget::RequestCancelRoomPassword()
{
 if (auto* Input = Control<UEditableTextBox>(JoinPage, TEXT("JoinPasswordInput"))) { Input->SetText(FText::GetEmpty()); }
 if (RoomModel) { RoomModel->CancelPassword(); }
 RefreshJoinPresentation();
}
void UCatFrontendRootWidget::RequestCopyShortCode()
{
 const FString Code = RoomModel ? RoomModel->GetSnapshot().InviteCode : FString();
 if (!Code.IsEmpty())
 {
   FPlatformApplicationMisc::ClipboardCopy(*Code);
   ShowRoomNotice(FText::FromString(TEXT("邀请码已复制")), FText::FromString(TEXT("将这 6 位邀请码发给朋友，即可免密码加入。")), Code);
 }
}
void UCatFrontendRootWidget::BindPublicRoomControls(bool bBind)
{
#define ROOM_BUTTON(Page, Name, Method) if (auto* Button = Control<UButton>(Page, TEXT(Name))) \
 { if (bBind) { Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Method); } else { Button->OnClicked.RemoveDynamic(this, &ThisClass::Method); } }
 ROOM_BUTTON(JoinPage, "RefreshPublicRoomsButton", RequestRefreshPublicRooms)
 ROOM_BUTTON(JoinPage, "SubmitRoomPasswordButton", RequestSubmitRoomPassword)
 ROOM_BUTTON(JoinPage, "CancelRoomPasswordButton", RequestCancelRoomPassword)
 ROOM_BUTTON(RoomPage, "CopyRoomShortCodeButton", RequestCopyShortCode)
#undef ROOM_BUTTON
}
void UCatFrontendRootWidget::RefreshPublicRoomPresentation(const FCatOnlineSnapshot& Snapshot)
{
 const bool bIdle = Snapshot.ActiveOperation == ECatOnlineOperation::None && Snapshot.SessionState == ECatOnlineSessionState::NoSession;
 if (auto* Button = Control<UButton>(JoinPage, TEXT("RefreshPublicRoomsButton"))) { Button->SetIsEnabled(bIdle); }
 if (auto* Panel = Control<UWidget>(JoinPage, TEXT("JoinPasswordPanel")))
 { Panel->SetVisibility(Snapshot.bPasswordRequested ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); Panel->SetIsEnabled(bIdle); }
 if (!Snapshot.bPasswordRequested)
 { if (auto* Input = Control<UEditableTextBox>(JoinPage, TEXT("JoinPasswordInput"))) { Input->SetText(FText::GetEmpty()); } }
 if (auto* Empty = Control<UTextBlock>(JoinPage, TEXT("PublicRoomsEmptyText")))
 {
   Empty->SetVisibility(Snapshot.SearchResults.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
   Empty->SetText(FText::FromString(Snapshot.ActiveOperation == ECatOnlineOperation::Find
     ? RoomModel && RoomModel->IsFindingPublicRooms() ? TEXT("正在搜索房间…\n可先输入邀请码，稍后加入。") : TEXT("正在查找邀请码对应的房间…")
     : TEXT("暂无可发现的房间，点击刷新重试。")));
 }
 if (auto* Rows = Control<UScrollBox>(JoinPage, TEXT("PublicRoomsScrollBox")))
 {
   Rows->ClearChildren(); Rows->SetIsEnabled(bIdle);
   const auto RowClass = LoadClass<UCatFrontendJoinFriendRowWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatJoinFriendRow.WBP_CatJoinFriendRow_C"));
   if (!RowClass) { return; }
   for (const FCatSessionSearchSummary& Room : Snapshot.SearchResults)
   {
     if (auto* Row = CreateWidget<UCatFrontendJoinFriendRowWidget>(this, RowClass)) { Row->ConfigurePublicRoom(this, Room); Rows->AddChild(Row); }
   }
 }
}
void UCatFrontendJoinFriendRowWidget::ConfigurePublicRoom(UCatFrontendRootWidget* Root, const FCatSessionSearchSummary& Room)
{
 RootWidget = Root; PublicRoomHandle = Room.Handle;
 if (FriendNameText) { FriendNameText->SetText(FText::FromString(Room.RoomName.IsEmpty() ? Room.OwnerDisplayName : Room.RoomName)); }
 if (FriendStatusText)
 {
   FriendStatusText->SetText(FText::FromString(FString::Printf(TEXT("%s · %d/%d 人\n%s · %s"), *Room.OwnerDisplayName, Room.CurrentPlayers, Room.MaxPlayers,
     Room.bHasPassword ? TEXT("需要密码") : TEXT("无密码"), !Room.bCanJoin ? TEXT("暂不可加入") : Room.bInProgress ? TEXT("游戏中 · 可加入") : TEXT("等待开始"))));
 }
 if (JoinFriendButton) { JoinFriendButton->SetIsEnabled(Room.bCanJoin); }
}
