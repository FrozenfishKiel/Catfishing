#include "UI/WorldInfo/CatWorldInfoController.h"
#include "UI/WorldInfo/CatWorldInfoComponent.h"
#include "UI/WorldInfo/CatWorldInfoRegistry.h"
#include "UI/WorldInfo/CatWorldInfoWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "Logging/CatLog.h"

// 绑定流程：先解绑旧玩家并清空视图与观察缓存；空值或远端 Controller 到此结束，保持未绑定状态。
// 本地 Controller 写入弱引用后记录所属世界、网络角色、玩家对象名与固定屏幕布局模式；视图留待首次 Tick 创建。
// WorldInfoScreenLayoutBound 每次成功绑定都会记录，只证明本地绑定已建立，不代表 WBP 已加载或面板已显示。
void UCatWorldInfoController::Bind(APlayerController* Controller)
{
	Unbind();
	if (Controller && Controller->IsLocalController())
	{
		BoundController = Controller;
		UE_LOG(LogCatUI, Log, TEXT("Event=WorldInfoScreenLayoutBound World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Layout=FixedScreenColumn"),
			*GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(),
			static_cast<int32>(Controller->GetLocalRole()), *Controller->GetName());
	}
}

// 解绑流程：先将 Targeting 的额外观察距离归零，再移除每张正式 WBP 并清弱引用和刷新计时；不写入源组件或玩家输入状态。
void UCatWorldInfoController::Unbind()
{
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundController.Get()))
	{
		if (UCatInteractionTargetingComponent* Targeting = Controller->GetInteractionTargetingComponent()) Targeting->SetObservationDistanceCentimeters(0.0);
	}
	for (FCatWorldInfoDisplay& Entry : Displays)
		if (Entry.Widget) Entry.Widget->RemoveFromParent();
	Displays.Reset();
	BoundController.Reset();
	RefreshRemainingSeconds = 0;
}

// 销毁兜底：复用正常解绑，随后由 UObject 释放自身；重复调用不会恢复已失效的视图。
void UCatWorldInfoController::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

// Tick 资格：只允许有效本地观察者；CDO 不创建世界或 UI。
bool UCatWorldInfoController::IsTickable() const
{
	return !IsTemplate() && BoundController.IsValid();
}

// 世界读取：从当前观察者取得所属世界，未绑定返回空，不借用编辑器的全局 World。
UWorld* UCatWorldInfoController::GetTickableGameObjectWorld() const
{
	return BoundController.IsValid() ? BoundController->GetWorld() : nullptr;
}

// 性能归属：返回本类的统计标识，不创建额外 Tick 对象。
TStatId UCatWorldInfoController::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCatWorldInfoController, STATGROUP_Tickables);
}

// 可见计数：只统计已经挂接且当前未收起的视图，便于运行验证观察真实显示结果。
int32 UCatWorldInfoController::GetVisibleInfoCount() const
{
	int32 Count = 0;
	for (const FCatWorldInfoDisplay& Entry : Displays)
		if (Entry.Widget && Entry.Widget->IsInViewport() && Entry.Widget->GetVisibility() == ESlateVisibility::HitTestInvisible) ++Count;
	return Count;
}

// 候选刷新：
// 1. 仅遍历注册锚点，移除退出者并补入新组件；不逐帧枚举全场 Actor。
// 2. 用同一准星观察结果、身体距离和镜头遮挡求层级；对象可覆盖内置策略，按键 gate 完全独立。
// 3. 只有可读对象才加载配置的正式 WBP；缺失按对象记录一次并保持隐藏，绝不生成原生替身。
// 软类路径改变时先移除旧视图并清加载失败记忆；已有实例若被移出视口则重新挂接，不重复创建。
void UCatWorldInfoController::RefreshCandidates()
{
	APlayerController* Controller = BoundController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	UCatWorldInfoRegistry* Registry = World ? World->GetSubsystem<UCatWorldInfoRegistry>() : nullptr;
	if (!Registry) return;
	for (int32 Index = Displays.Num() - 1; Index >= 0; --Index)
	{
		if (!Displays[Index].Source.IsValid() || !Registry->GetSources().Contains(Displays[Index].Source))
		{
			if (Displays[Index].Widget) Displays[Index].Widget->RemoveFromParent();
			Displays.RemoveAt(Index);
		}
	}
	for (const auto& Source : Registry->GetSources())
	{
		if (Source.IsValid() && !Displays.ContainsByPredicate([&](const auto& Entry) { return Entry.Source == Source; }))
			Displays.AddDefaulted_GetRef().Source = Source;
	}
	const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
	UCatInteractionTargetingComponent* Targeting = CatController ? CatController->GetInteractionTargetingComponent() : nullptr;
	const APawn* Pawn = Controller->GetPawn();
	FVector CameraLocation;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
	// 由信息层声明观察预算，交互层无需引用 UI 类型；第三人称相机与身体的偏移计入射线长度，但不放宽 E 键。
	double ObservationDistance = 0.0;
	for (const auto& Source : Registry->GetSources())
	{
		if (Source.IsValid() && Source->bInfoEnabled && FMath::IsFinite(Source->DisplayDistanceCentimeters))
			ObservationDistance = FMath::Max(ObservationDistance, static_cast<double>(Source->DisplayDistanceCentimeters));
	}
	if (Pawn) ObservationDistance += FVector::Distance(Pawn->GetActorLocation(), CameraLocation);
	if (Targeting) Targeting->SetObservationDistanceCentimeters(ObservationDistance);
	for (FCatWorldInfoDisplay& Entry : Displays)
	{
		UCatWorldInfoComponent* Source = Entry.Source.Get();
		AActor* Owner = Source ? Source->GetOwner() : nullptr;
		if (Source && Entry.RequestedWidgetClass != Source->WidgetClass.ToSoftObjectPath())
		{
			if (Entry.Widget) Entry.Widget->RemoveFromParent();
			Entry.Widget = nullptr;
			Entry.RequestedWidgetClass = Source->WidgetClass.ToSoftObjectPath();
			Entry.bClassUnavailable = false;
			Entry.RenderedSerial = 0;
		}
		const ECatWorldInfoDetail PreviousDetail = Entry.Detail;
		Entry.Detail = ECatWorldInfoDetail::Hidden;
		Entry.bFocused = Owner && Targeting && Targeting->GetObservedTarget() == Owner;
		if (!Owner || !Pawn || Owner->IsHidden() || !Source->bInfoEnabled) continue;
		Entry.Distance = FVector::Distance(Pawn->GetActorLocation(), Owner->GetActorLocation());
		Entry.Detail = Source->EvaluateDisplay(Controller, Entry.bFocused, Entry.Distance);
		if (Entry.Detail != ECatWorldInfoDetail::Hidden && Source->bHideWhenOccluded)
		{
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CatWorldInfoOcclusion), false, Pawn);
			Query.AddIgnoredActor(Owner);
			if (World->LineTraceSingleByChannel(Hit, CameraLocation, Source->GetComponentLocation(),
				GetDefault<UCatInteractionSettings>()->TargetingTraceChannel, Query)) Entry.Detail = ECatWorldInfoDetail::Hidden;
		}
		if (PreviousDetail != Entry.Detail) Entry.RenderedSerial = 0;
		if (Entry.Detail == ECatWorldInfoDetail::Hidden || Entry.bClassUnavailable) continue;
		if (Entry.Widget)
		{
			if (!Entry.Widget->IsInViewport()) Entry.Widget->AddToPlayerScreen(100);
			continue;
		}
		UClass* ViewClass = Source->WidgetClass.LoadSynchronous();
		if (ViewClass && ViewClass->IsChildOf(UCatWorldInfoWidget::StaticClass()) && ViewClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			Entry.Widget = CreateWidget<UCatWorldInfoWidget>(Controller, ViewClass);
			if (Entry.Widget) Entry.Widget->AddToPlayerScreen(100);
		}
		if (!Entry.Widget)
		{
			Entry.bClassUnavailable = true;
			UE_LOG(LogCatUI, Warning, TEXT("Event=WorldInfoViewUnavailable World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Object=%s Class=%s"),
				*World->GetName(), static_cast<int32>(World->GetNetMode()), Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()),
				*Controller->GetName(), *Owner->GetName(), *Source->WidgetClass.ToSoftObjectPath().ToString());
		}
	}
}

// 显示帧流程：
// 1. 未绑定则返回；否则累计刷新倒计时，每 0.2 秒更新候选，随后按焦点、优先级、距离和源路径排序。
// 2. 无 Pawn、显示鼠标光标或日切输入被阻断时收起整层；每张牌先收起，再排除离开视口、失效或策略隐藏的源。
// 3. 通知序号变化时重读快照并更新已消费序号；读取失败保持隐藏。可显示的 WBP 先恢复布局参与，再预排版测量尺寸。
// 4. 按玩家视口与面板尺寸排入屏幕列，不读取物体投影；尺寸无效或放不下的牌收起，继续尝试后面的牌。
// 5. 成功放置后写入左上角对齐、尺寸与位置，再推进下一张牌的纵向起点；此处不缩放面板或裁切正文。
void UCatWorldInfoController::Tick(const float DeltaTime)
{
	APlayerController* Controller = BoundController.Get();
	if (!Controller) return;
	RefreshRemainingSeconds -= DeltaTime;
	if (RefreshRemainingSeconds <= 0)
	{
		RefreshCandidates();
		RefreshRemainingSeconds = 0.2f;
	}
	const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
	const bool bSuppressed = !Controller->GetPawn() || Controller->bShowMouseCursor || (CatController && CatController->IsDayTransitionInputBlocked());
	Displays.StableSort([](const FCatWorldInfoDisplay& A, const FCatWorldInfoDisplay& B)
	{
		if (A.bFocused != B.bFocused) return A.bFocused;
		const int32 PA = A.Source.IsValid() ? A.Source->DisplayPriority : MIN_int32;
		const int32 PB = B.Source.IsValid() ? B.Source->DisplayPriority : MIN_int32;
		if (PA != PB) return PA > PB;
		if (!FMath::IsNearlyEqual(A.Distance, B.Distance)) return A.Distance < B.Distance;
		return GetPathNameSafe(A.Source.Get()) < GetPathNameSafe(B.Source.Get());
	});
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(Controller).GetLocalSize();
	// 视口局部尺寸、控件期望尺寸及以下偏移均使用本地玩家屏幕的 UMG 布局单位，不是物理像素。
	// 列的默认左边位于视口水平中心右侧 48 单位，顶部位于高度的 20%；屏幕边距为 16，牌间距为 8。
	const double ScreenMargin = 16.0;
	double NextTop = ViewportSize.Y * 0.2;
	bool bColumnStarted = false;
	for (FCatWorldInfoDisplay& Entry : Displays)
	{
		UCatWorldInfoWidget* Widget = Entry.Widget;
		UCatWorldInfoComponent* Source = Entry.Source.Get();
		if (!Widget) continue;
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		if (bSuppressed || !Widget->IsInViewport() || !Source || !Source->GetOwner() || Source->GetOwner()->IsHidden()
			|| !Source->bInfoEnabled || Entry.Detail == ECatWorldInfoDetail::Hidden) continue;
		if (Entry.RenderedSerial != Source->GetInfoSerial())
		{
			FCatWorldInfoViewData Data;
			if (!Source->BuildInfo(Controller, Entry.Detail, Data)) continue;
			Widget->RenderInfo(Data, Entry.Detail);
			Entry.RenderedSerial = Source->GetInfoSerial();
		}
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		Widget->ForceLayoutPrepass();
		const FVector2D Size = Widget->GetDesiredSize();
		if (Size.ContainsNaN() || ViewportSize.ContainsNaN() || Size.X <= 0 || Size.Y <= 0
			|| Size.X > ViewportSize.X - ScreenMargin * 2 || Size.Y > ViewportSize.Y - ScreenMargin * 2)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		// 只有本帧首张可放置牌调整纵向起点以完整容纳自身；后续牌沿用累积高度，放不下时不占空间。
		if (!bColumnStarted) NextTop = FMath::Clamp(NextTop, ScreenMargin, ViewportSize.Y - Size.Y - ScreenMargin);
		if (NextTop + Size.Y > ViewportSize.Y - ScreenMargin)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		// 横向对每张牌分别夹限；小窗口或较宽面板会向左收边，因此不保证左边始终在准心右侧或各牌完全对齐。
		const FVector2D TopLeft(FMath::Clamp(ViewportSize.X * 0.5 + 48.0, ScreenMargin, ViewportSize.X - Size.X - ScreenMargin), NextTop);
		Widget->SetAlignmentInViewport(FVector2D::ZeroVector);
		Widget->SetDesiredSizeInViewport(Size);
		// TopLeft 已是 UMG 布局坐标，关闭此接口的 DPI 移除步骤，避免再次换算导致位置偏移。
		Widget->SetPositionInViewport(TopLeft, false);
		NextTop += Size.Y + 8.0;
		bColumnStarted = true;
	}
}
