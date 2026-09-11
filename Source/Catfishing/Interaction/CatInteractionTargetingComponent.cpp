#include "Interaction/CatInteractionTargetingComponent.h"

#include "Interaction/CatInteractable.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

UCatInteractionTargetingComponent::UCatInteractionTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UCatInteractionTargetingComponent::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PlayerController = GetOwningPlayerController();
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	if (!PlayerController || !PlayerController->IsLocalController() || !Settings || !GetWorld())
	{
		return;
	}
	const float Interval = static_cast<float>(FMath::Max(0.016, Settings->TargetingIntervalSeconds));
	GetWorld()->GetTimerManager().SetTimer(TargetingTimer, this,
		&ThisClass::RefreshTargetFromCrosshair, Interval, true, 0.0f);
}

void UCatInteractionTargetingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TargetingTimer);
	}
	ClearTarget();
	Super::EndPlay(EndPlayReason);
}

APlayerController* UCatInteractionTargetingComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

// 观察范围写入：非有限值或负值收口为零；只影响下一次只读射线长度，不触发刷新或发送交互请求。
void UCatInteractionTargetingComponent::SetObservationDistanceCentimeters(const double Distance)
{
	ObservationDistanceCentimeters = FMath::IsFinite(Distance) ? FMath::Max(0.0, Distance) : 0.0;
}

// 观察与执行分流：
// 1. 清空上次观察命中；缺 Controller、设置、World、本地视口或投影数据时返回空，不能继续使用旧世界焦点。
// 2. 以所属玩家受限视图矩形的中心反投影；这样分屏与宽高比留黑边时仍对准实际画面，空矩形或反投影失败时结束。
// 3. 忽略自身 Pawn，以交互上限和观察需求中的较大距离发出唯一射线；未命中返回空，命中则保存原始观察对象。
// 4. 只有命中距离仍在原交互上限内、实现交互接口且 CanInteract 通过时才返回执行目标；远处或不可交互对象仍可供信息牌观察。
AActor* UCatInteractionTargetingComponent::TraceInteractableFromCrosshair()
{
	ObservedTarget.Reset();
	APlayerController* PlayerController = GetOwningPlayerController();
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	if (!PlayerController || !PlayerController->IsLocalController() || !Settings || !World)
	{
		return nullptr;
	}

	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport
		|| !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
	{
		return nullptr;
	}
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0) return nullptr;

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!PlayerController->DeprojectScreenPositionToWorld(
		(ViewRect.Min.X + ViewRect.Max.X) * 0.5f, (ViewRect.Min.Y + ViewRect.Max.Y) * 0.5f, RayOrigin, RayDirection))
	{
		return nullptr;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatInteractionTargeting), Settings->bTraceComplex);
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}
	FHitResult Hit;
	const FVector TraceEnd = RayOrigin + RayDirection.GetSafeNormal()
		* FMath::Max(Settings->MaximumTargetingDistanceCentimeters, ObservationDistanceCentimeters);
	if (!World->LineTraceSingleByChannel(Hit, RayOrigin, TraceEnd, Settings->TargetingTraceChannel, QueryParams))
	{
		return nullptr;
	}
	AActor* HitActor = Hit.GetActor();
	ObservedTarget = HitActor;
	return HitActor && Hit.Distance <= Settings->MaximumTargetingDistanceCentimeters
		&& HitActor->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		&& ICatInteractable::Execute_CanInteract(HitActor, PlayerController)
		? HitActor : nullptr;
}

void UCatInteractionTargetingComponent::RefreshTargetFromCrosshair()
{
	ApplyTarget(TraceInteractableFromCrosshair());
}

// 目标应用：相同有效目标只刷新提示；对象变化或销毁才保存上一目标、结束旧高亮并开启新高亮，最后发布本机通知。
void UCatInteractionTargetingComponent::ApplyTarget(AActor* NewTarget)
{
	const bool bPreviousTargetWasDestroyed = CurrentTarget.IsStale();
	if (!bPreviousTargetWasDestroyed && CurrentTarget.Get() == NewTarget)
	{
		if (NewTarget) OnTargetRefreshed.Broadcast(NewTarget, NewTarget);
		return;
	}
	AActor* PreviousTarget = CurrentTarget.Get();
	LastTarget = CurrentTarget;
	CurrentTarget = NewTarget;
	if (IsValid(PreviousTarget) && PreviousTarget->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
	{
		ICatInteractable::Execute_EndLocalFocus(PreviousTarget);
	}
	if (IsValid(NewTarget) && NewTarget->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
	{
		ICatInteractable::Execute_BeginLocalFocus(NewTarget);
	}
	OnTargetRefreshed.Broadcast(PreviousTarget, NewTarget);
}

// 退出清理：先释放观察焦点，再取消长按并结束旧目标高亮，最后丢弃上一目标；不改变下次装配的距离配置。
void UCatInteractionTargetingComponent::ClearTarget()
{
	// 页面拆除或旅行时同时清观察命中，避免信息牌继续把旧世界对象当作焦点。
	ObservedTarget.Reset();
	EndInteractionInput(true);
	ApplyTarget(nullptr);
	LastTarget.Reset();
}

// 按下流程：丢弃上次候选后刷新准星；只有鱼护开启一次性计时，其他目标继续原有即时交互。
void UCatInteractionTargetingComponent::BeginInteractionInput()
{
	EndInteractionInput(true);
	RefreshTargetFromCrosshair();
	if (Cast<ACatFishGuardActor>(CurrentTarget.Get()) && GetWorld())
	{
		PendingGuardTarget = CurrentTarget;
		GetWorld()->GetTimerManager().SetTimer(GuardHoldTimer, this,
			&ThisClass::CompleteGuardHold, FMath::Max(0.1f, GuardHoldSeconds), false);
	}
	else TryInteract();
}

// 松开流程：先清除计时和候选，再复核原目标；已完成长按或被取消时不触发短按，避免一次输入执行两种动作。
void UCatInteractionTargetingComponent::EndInteractionInput(const bool bCanceled)
{
	AActor* PressedTarget = PendingGuardTarget.Get();
	PendingGuardTarget.Reset();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(GuardHoldTimer);
	if (!bCanceled && PressedTarget)
	{
		RefreshTargetFromCrosshair();
		if (CurrentTarget.Get() == PressedTarget) TryInteract();
	}
}

// 阈值流程：先取走本次候选防止重复提交；目标移开、输入被锁或鱼护已失效都只结束本次按键。
void UCatInteractionTargetingComponent::CompleteGuardHold()
{
	ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(PendingGuardTarget.Get());
	PendingGuardTarget.Reset();
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	RefreshTargetFromCrosshair();
	if (Guard && Controller && !Controller->IsDayTransitionInputBlocked()
		&& CurrentTarget.Get() == Guard && ICatInteractable::Execute_CanInteract(Guard, Controller))
		Controller->ServerPickUpFishGuard(Guard, FGuid::NewGuid());
}

void UCatInteractionTargetingComponent::TryInteract()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	AActor* Target = CurrentTarget.Get();
	if (PlayerController && PlayerController->IsLocalController() && IsValid(Target)
		&& Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		&& ICatInteractable::Execute_CanInteract(Target, PlayerController))
	{
		ICatInteractable::Execute_Interact(Target, PlayerController, FGuid::NewGuid());
	}
}
