#include "UI/Frontend/CatFrontendRootWidget.h"
#include "Online/CatRoomChatSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Logging/CatLog.h"

namespace CatChatView
{
template<class T> T* Find(UUserWidget* Page, const TCHAR* Name) { return Page ? Cast<T>(Page->GetWidgetFromName(Name)) : nullptr; }
void Label(UUserWidget* Page, const TCHAR* Name, const FString& Value)
{ if (auto* Text = Find<UTextBlock>(Page, Name)) { Text->SetText(FText::FromString(Value)); } }
}

void UCatFrontendRootWidget::BindRoomChatControls(bool bBind)
{
    using namespace CatChatView;
    if (RoomChat) { RoomChat->OnChanged.Remove(RoomChatHandle); }
    RoomChat = nullptr;
    for (const TCHAR* Name : {TEXT("RoomChatOpenButton"), TEXT("RoomChatCloseButton"), TEXT("RoomChatSendButton"), TEXT("RoomChatLatestButton")})
    {
        if (auto* Button = Find<UButton>(RoomPage, Name))
        {
            Button->OnClicked.RemoveAll(this);
            if (bBind)
            {
                if (FCString::Strcmp(Name, TEXT("RoomChatSendButton")) == 0) { Button->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestSendRoomChat); }
                else if (FCString::Strcmp(Name, TEXT("RoomChatLatestButton")) == 0) { Button->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestChatLatest); }
                else { Button->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestToggleRoomChat); }
            }
        }
        else if (bBind) { UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=Room Control=%s"), Name); }
    }
    if (auto* Input = Find<UEditableTextBox>(RoomPage, TEXT("RoomChatInput")))
    {
        Input->OnTextCommitted.RemoveAll(this);
        if (bBind) { Input->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::HandleChatCommitted); }
    }
    if (auto* Scroll = Find<UScrollBox>(RoomPage, TEXT("RoomChatMessages")))
    {
        Scroll->OnUserScrolled.RemoveAll(this);
        if (bBind) { Scroll->OnUserScrolled.AddUniqueDynamic(this, &ThisClass::HandleChatScrolled); }
    }
    if (bBind && GetGameInstance())
    {
        RoomChat = GetGameInstance()->GetSubsystem<UCatRoomChatSubsystem>();
        if (RoomChat) { RoomChatHandle = RoomChat->OnChanged.AddUObject(this, &ThisClass::RefreshRoomChat); }
        RefreshRoomChat();
    }
}

bool UCatFrontendRootWidget::IsRoomChatOpen() const
{
    const auto* Panel = CatChatView::Find<UWidget>(RoomPage, TEXT("RoomChatExpanded"));
    return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UCatFrontendRootWidget::RequestToggleRoomChat()
{
    if (!IsShowingRoom() || IsRoomDialogOpen()) { return; }
    const bool bOpen = !IsRoomChatOpen();
    if (auto* Panel = CatChatView::Find<UWidget>(RoomPage, TEXT("RoomChatExpanded")))
    { Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
    if (bOpen)
    {
        RequestChatLatest();
        if (auto* Input = CatChatView::Find<UEditableTextBox>(RoomPage, TEXT("RoomChatInput")); Input && Input->GetIsEnabled()) { Input->SetKeyboardFocus(); }
    }
    else { SetKeyboardFocus(); }
}

bool UCatFrontendRootWidget::HandleRoomChatEscape()
{
    if (!IsRoomChatOpen()) { return false; }
    RequestToggleRoomChat(); return true;
}

void UCatFrontendRootWidget::RequestSendRoomChat()
{
    auto* Input = CatChatView::Find<UEditableTextBox>(RoomPage, TEXT("RoomChatInput"));
    if (!Input || !RoomChat || !IsShowingRoom()) { return; }
    FText Error;
    if (RoomChat->Send(Input->GetText().ToString(), Error))
    { Input->SetText(FText::GetEmpty()); RequestChatLatest(); }
    CatChatView::Label(RoomPage, TEXT("RoomChatFeedback"), Error.ToString());
    Input->SetKeyboardFocus();
}

void UCatFrontendRootWidget::HandleChatCommitted(const FText& Text, ETextCommit::Type Method)
{
    // Slate 文本控件提交负责输入法；只处理 OnEnter，失焦和 Esc 不发送。
    if (Method == ETextCommit::OnEnter) { RequestSendRoomChat(); }
}

void UCatFrontendRootWidget::RequestChatLatest()
{
    bChatAtBottom = true; ChatUnread = 0;
    if (auto* Scroll = CatChatView::Find<UScrollBox>(RoomPage, TEXT("RoomChatMessages"))) { Scroll->ScrollToEnd(); }
    if (auto* Button = CatChatView::Find<UButton>(RoomPage, TEXT("RoomChatLatestButton"))) { Button->SetVisibility(ESlateVisibility::Collapsed); }
    CatChatView::Label(RoomPage, TEXT("RoomChatOpenButtonLabel"), TEXT("房间聊天"));
}

void UCatFrontendRootWidget::HandleChatScrolled(float Offset)
{
    if (auto* Scroll = CatChatView::Find<UScrollBox>(RoomPage, TEXT("RoomChatMessages")))
    {
        bChatAtBottom = Offset >= Scroll->GetScrollOffsetOfEnd() - 6;
        if (bChatAtBottom) { RequestChatLatest(); }
    }
}

void UCatFrontendRootWidget::RefreshRoomChat()
{
    if (!RoomChat) { return; }
    if (ChatRoomKey != RoomChat->GetRoomKey())
    {
        ChatRoomKey = RoomChat->GetRoomKey(); LastChatMessage.Invalidate(); ChatUnread = 0; bChatAtBottom = true;
        if (auto* Input = CatChatView::Find<UEditableTextBox>(RoomPage, TEXT("RoomChatInput"))) { Input->SetText(FText::GetEmpty()); }
        CatChatView::Label(RoomPage, TEXT("RoomChatFeedback"), FString());
    }
    RenderRoomChat(RoomChat->GetMessages(), RoomChat->IsAvailable());
}

void UCatFrontendRootWidget::RenderRoomChat(const TArray<FCatRoomChatMessage>& Messages, bool bAvailable)
{
    using namespace CatChatView;
    auto* Scroll = Find<UScrollBox>(RoomPage, TEXT("RoomChatMessages"));
    auto* Prototype = Find<UTextBlock>(RoomPage, TEXT("RoomChatRowStyle"));
    if (!Scroll || !Prototype || !RoomPage->WidgetTree) { return; }
    if (auto* Input = Find<UEditableTextBox>(RoomPage, TEXT("RoomChatInput")))
    {
        Input->SetIsEnabled(bAvailable);
        Input->SetHintText(FText::FromString(bAvailable ? TEXT("和伙伴说点什么…") : TEXT("开启联机后可聊天")));
    }
    if (auto* Send = Find<UButton>(RoomPage, TEXT("RoomChatSendButton"))) { Send->SetIsEnabled(bAvailable); }
    Label(RoomPage, TEXT("RoomChatStatus"), bAvailable ? TEXT("仅房间成员可见") : TEXT("开启联机，与伙伴相聚"));
    const bool bNew = !Messages.IsEmpty() && Messages.Last().MessageId != LastChatMessage;
    if (bNew && (!IsRoomChatOpen() || !bChatAtBottom))
    {
        const int32 LastIndex = Messages.IndexOfByPredicate([&](const auto& Message) { return Message.MessageId == LastChatMessage; });
        ChatUnread = FMath::Min(CatRoomChatProtocol::HistoryLimit, ChatUnread + Messages.Num() - LastIndex - 1);
    }
    LastChatMessage = Messages.IsEmpty() ? FGuid() : Messages.Last().MessageId;
    Label(RoomPage, TEXT("RoomChatOpenButtonLabel"), ChatUnread ? FString::Printf(TEXT("房间聊天 · %d 条新消息"), ChatUnread) : TEXT("房间聊天"));
    Label(RoomPage, TEXT("RoomChatSummary"), Messages.IsEmpty() ? (bAvailable ? TEXT("和伙伴打个招呼吧") : TEXT("开启联机后可与伙伴聊天"))
        : (Messages.Last().bSystem ? Messages.Last().Text : Messages.Last().SenderName + TEXT("：") + Messages.Last().Text));
    const float OldOffset = Scroll->GetScrollOffset();
    // 显式换行宽度让第一次 prepass 就能算出行高；折叠时尚无 Geometry，使用面板可用内容宽度。
    const float MeasuredWidth = Scroll->GetCachedGeometry().GetLocalSize().X;
    const float TextWidth = MeasuredWidth > 20 ? MeasuredWidth - 10 : 288;
    Scroll->ClearChildren();
    for (const auto& Message : Messages)
    {
        auto* Row = RoomPage->WidgetTree->ConstructWidget<UVerticalBox>();
        Scroll->AddChild(Row);
        if (auto* RowSlot = Cast<UScrollBoxSlot>(Row->Slot)) { RowSlot->SetPadding(FMargin(0, 0, 5, 11)); }
        if (!Message.bSystem)
        {
            auto* Name = RoomPage->WidgetTree->ConstructWidget<UTextBlock>();
            FSlateFontInfo Font = Prototype->GetFont(); Font.Size = 11; Name->SetFont(Font);
            const FString State = Message.bPending ? TEXT("  ·  发送中") : Message.bFailed ? TEXT("  ·  未确认送达") : FString();
            Name->SetText(FText::FromString(Message.SenderName + State));
            Name->SetColorAndOpacity(FSlateColor(Message.bFailed ? FLinearColor(.94f,.52f,.34f) : Message.bLocal ? FLinearColor(.43f,.81f,.67f) : FLinearColor(.87f,.75f,.48f)));
            Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Row->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,0,0,3));
        }
        auto* Body = RoomPage->WidgetTree->ConstructWidget<UTextBlock>();
        Body->SetFont(Prototype->GetFont()); Body->SetAutoWrapText(false); Body->SetWrapTextAt(TextWidth);
        Body->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        Body->SetText(FText::FromString(Message.Text));
        Body->SetColorAndOpacity(FSlateColor(Message.bSystem ? FLinearColor(.43f,.61f,.54f) : FLinearColor(.86f,.89f,.80f)));
        Row->AddChildToVerticalBox(Body);
    }
    Scroll->ForceLayoutPrepass();
    if (bChatAtBottom) { Scroll->ScrollToEnd(); } else { Scroll->SetScrollOffset(OldOffset); }
    if (auto* Latest = Find<UButton>(RoomPage, TEXT("RoomChatLatestButton")))
    { Latest->SetVisibility(ChatUnread && !bChatAtBottom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
    if (auto* Empty = Find<UWidget>(RoomPage, TEXT("RoomChatEmpty"))) { Empty->SetVisibility(Messages.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
}
