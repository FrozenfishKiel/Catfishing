#include "UI/Run/CatAltarConfirmationWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace CatAltarConfirmationPresentation
{
	/** 顶部窗口从上方滑入的固定像素距离；布局基准仍由正式 WBP 的 Canvas/Slot 决定。 */
	constexpr float SlideInOffsetY = -48.0f;
	/** 顶部窗口滑入时长，单位秒；它只表达本地表现节奏，不参与服务器 30 秒确认时限。 */
	constexpr double SlideInDurationSeconds = 0.2;
	/** 玩家可见的确认时限上限；服务器权威仍来自 Deadline，本地服务器时间同步初帧偏慢时也不能显示超过规则的秒数。 */
	constexpr int32 MaxVisibleRemainingSeconds = 30;
}

// 渲染流程：
// 1. 直接读取公开 Participants 数组，统计确认数并找到 owning PlayerState 的当前确认态；按服务器 Initiator 区分取消整轮和撤回本人，不另建 WBP 或本地权限状态。
// 2. Waiting 请求变化时重置视觉滑入和倒计时缓存；Cancelled 保留取消原因供 UI Subsystem 的两秒停留展示，Accepted/Idle 交给子系统收起。
// 3. 窗口始终穿透鼠标且不可聚焦，F8/F9 继续由 PlayerController 与已有模态页面的预览键统一处理。
void UCatAltarConfirmationWidget::RenderConfirmation(const FCatAltarConfirmationSnapshot& Confirmation)
{
	const bool bWaiting = Confirmation.State == ECatAltarConfirmationState::Waiting;
	if (bWaiting && DisplayRequestId != Confirmation.RequestId)
	{
		DisplayRequestId = Confirmation.RequestId;
		SlideInStartedAtSeconds = FPlatformTime::Seconds();
		LastDisplayedRemainingSeconds = INDEX_NONE;
	}
	bDisplayingWaitingConfirmation = bWaiting;
	DisplayDeadlineServerTimeSeconds = bWaiting ? Confirmation.DeadlineServerTimeSeconds : 0.0;

	const APlayerState* LocalPlayerState = GetOwningPlayer() ? GetOwningPlayer()->PlayerState : nullptr;
	const bool bLocalInitiator = LocalPlayerState && Confirmation.Initiator == LocalPlayerState;
	int32 ConfirmedCount = 0;
	bool bLocalConfirmed = false;
	bool bLocalParticipant = false;
	for (const FCatAltarConfirmationParticipant& Participant : Confirmation.Participants)
	{
		if (Participant.bConfirmed)
		{
			++ConfirmedCount;
		}
		if (Participant.PlayerState == LocalPlayerState)
		{
			bLocalParticipant = true;
			bLocalConfirmed = Participant.bConfirmed;
		}
	}
	const FString InitiatorName = Confirmation.Initiator ? Confirmation.Initiator->GetPlayerName() : TEXT("队友");
	InitiatorTextBlock->SetText(FText::Format(NSLOCTEXT("CatAltarConfirmation", "Initiator", "{0} 发起献祭 · 结束今天"),
		FText::FromString(InitiatorName)));
	ConfirmationCountTextBlock->SetText(FText::Format(NSLOCTEXT("CatAltarConfirmation", "Count", "已确认 {0}/{1}"),
		FText::AsNumber(ConfirmedCount), FText::AsNumber(Confirmation.Participants.Num())));
	OwnConfirmationTextBlock->SetText(Confirmation.State == ECatAltarConfirmationState::Cancelled
		? NSLOCTEXT("CatAltarConfirmation", "Cancelled", "本次献祭已取消")
		: (!bLocalParticipant
		? NSLOCTEXT("CatAltarConfirmation", "AwaitingEligibility", "正在同步你的确认资格")
		: (bLocalInitiator
			? NSLOCTEXT("CatAltarConfirmation", "InitiatorWaiting", "你已发起献祭 · 等待队友确认")
			: (bLocalConfirmed
			? NSLOCTEXT("CatAltarConfirmation", "Confirmed", "你已确认 · 按 F9 撤回")
			: NSLOCTEXT("CatAltarConfirmation", "Unconfirmed", "你尚未确认 · 按 F8 确认")))));
	InputHintsTextBlock->SetText(bLocalInitiator
		? NSLOCTEXT("CatAltarConfirmation", "InitiatorCancelHint", "F9 取消本次献祭")
		: NSLOCTEXT("CatAltarConfirmation", "ParticipantHints", "F8 确认 · F9 撤回确认"));
	InputHintsTextBlock->SetVisibility(bWaiting ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	CancelReasonTextBlock->SetText(Confirmation.State == ECatAltarConfirmationState::Cancelled
		? Confirmation.CancelReason : FText::GetEmpty());
	if (bWaiting)
	{
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		RefreshCountdownText(GameState ? GameState->GetServerWorldTimeSeconds() : 0.0);
		RefreshSlideInPresentation();
	}
	else
	{
		LastDisplayedRemainingSeconds = INDEX_NONE;
		CountdownTextBlock->SetText(FText::GetEmpty());
		ConfirmationPanel->SetRenderTranslation(FVector2D::ZeroVector);
	}
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

// Tick 流程：父类维持 UMG 生命周期后，等待确认期间从 GameState 的同步服务器时间重新计算显示秒数；状态转换仍只由下一份公开快照驱动。
void UCatAltarConfirmationWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bDisplayingWaitingConfirmation)
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	RefreshCountdownText(GameState ? GameState->GetServerWorldTimeSeconds() : 0.0);
	RefreshSlideInPresentation();
}

// 倒计时刷新流程：按 Deadline - ServerNow 取非负向上取整秒数，并限制在玩家规则写明的 30 秒内；同一显示秒内不重复改 TextBlock，服务器超时裁决不会由这里触发。
void UCatAltarConfirmationWidget::RefreshCountdownText(const double ServerTimeSeconds)
{
	const int32 RemainingSeconds = FMath::Clamp(FMath::CeilToInt(DisplayDeadlineServerTimeSeconds - ServerTimeSeconds),
		0, CatAltarConfirmationPresentation::MaxVisibleRemainingSeconds);
	if (RemainingSeconds == LastDisplayedRemainingSeconds)
	{
		return;
	}
	LastDisplayedRemainingSeconds = RemainingSeconds;
	CountdownTextBlock->SetText(FText::Format(NSLOCTEXT("CatAltarConfirmation", "Countdown", "剩余 {0} 秒"),
		FText::AsNumber(RemainingSeconds)));
}

// 滑入刷新流程：第一次 Waiting 渲染记录单调时钟，随后把 0 到 0.2 秒映射为从上方偏移到面板原位；完成后保持零位移且不创建计时器。
void UCatAltarConfirmationWidget::RefreshSlideInPresentation()
{
	const double ElapsedSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - SlideInStartedAtSeconds);
	const float Alpha = static_cast<float>(FMath::Clamp(ElapsedSeconds / CatAltarConfirmationPresentation::SlideInDurationSeconds, 0.0, 1.0));
	ConfirmationPanel->SetRenderTranslation(FVector2D(0.0f,
		FMath::Lerp(CatAltarConfirmationPresentation::SlideInOffsetY, 0.0f, Alpha)));
}
