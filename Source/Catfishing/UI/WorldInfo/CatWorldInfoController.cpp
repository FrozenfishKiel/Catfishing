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
// 本地 Controller 写入弱引用后记录所属世界、网络角色、玩家对象名与物体上方锚定模式；视图留待首次 Tick 创建。
// WorldInfoLayoutBound 每次成功绑定都会记录，只证明本地绑定已建立，不代表 WBP 已加载或面板已显示。
void UCatWorldInfoController::Bind(APlayerController* Controller)
{
	Unbind();
	if (Controller && Controller->IsLocalController())
	{
		BoundController = Controller;
		UE_LOG(LogCatUI, Log, TEXT("Event=WorldInfoLayoutBound World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Layout=WorldAnchorCentered"),
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
// 3. 将物体信息组件的上方挂点投影到本玩家屏幕；投影失败或视口无效则隐藏，内容序号变化时重读快照，读取失败保持隐藏。
// 4. 可显示的正式 WBP 恢复布局参与并测量，以面板中心对齐挂点；尺寸无效、整板离屏或与排序在前且已放置的牌重叠则隐藏。
// 5. 成功放置后写入左上角对齐、尺寸与位置并记录占用矩形；保持物体锚定，不换边、不夹向屏幕固定位置，边缘部分超出交给视口自然裁切。
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
	TArray<FBox2D> Occupied;
	for (FCatWorldInfoDisplay& Entry : Displays)
	{
		UCatWorldInfoWidget* Widget = Entry.Widget;
		UCatWorldInfoComponent* Source = Entry.Source.Get();
		if (!Widget) continue;
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		if (bSuppressed || !Widget->IsInViewport() || !Source || !Source->GetOwner() || Source->GetOwner()->IsHidden()
			|| !Source->bInfoEnabled || Entry.Detail == ECatWorldInfoDetail::Hidden) continue;
		FVector2D AnchorPosition;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, Source->GetComponentLocation(), AnchorPosition, true)
			|| AnchorPosition.ContainsNaN() || ViewportSize.ContainsNaN()
			|| ViewportSize.X <= 0 || ViewportSize.Y <= 0) continue;
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
		if (Size.ContainsNaN() || Size.X <= 0 || Size.Y <= 0)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		// 组件挂点已经在物体上方，应对齐面板中心；再上移整个面板高度会让祭坛详情在普通视角下越出屏幕。
		const FVector2D TopLeft = AnchorPosition - Size * 0.5;
		const FBox2D Rect(TopLeft, TopLeft + Size);
		// 只排除完全离屏的面板；部分越界仍保留可见内容，不能因为标题或一行越界就把整块祭坛信息收起。
		if (Rect.Max.X <= 0 || Rect.Max.Y <= 0 || Rect.Min.X >= ViewportSize.X || Rect.Min.Y >= ViewportSize.Y
			|| Occupied.ContainsByPredicate([&](const FBox2D& Other) { return Rect.Intersect(Other); }))
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Widget->SetAlignmentInViewport(FVector2D::ZeroVector);
		Widget->SetDesiredSizeInViewport(Size);
		// TopLeft 已是 UMG 布局坐标，关闭此接口的 DPI 移除步骤，避免再次换算导致位置偏移。
		Widget->SetPositionInViewport(TopLeft, false);
		Occupied.Add(Rect);
	}
}
