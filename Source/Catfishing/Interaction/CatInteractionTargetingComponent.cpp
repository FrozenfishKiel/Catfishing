#include "Interaction/CatInteractionTargetingComponent.h"

#include "Interaction/CatInteractable.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Logging/CatLog.h"
#include "Components/PrimitiveComponent.h"
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
// 3. 忽略自身 Pawn，射线覆盖相机到身体的距离加交互半径，并保留更远的观察需求。
// 4. 以身体到命中点的距离筛选交互目标；相机拉远不消耗玩法半径，远处对象仍只供信息牌观察。
AActor* UCatInteractionTargetingComponent::TraceInteractableFromCrosshair(const bool bLogDecision)
{
	ObservedTarget.Reset();
	APlayerController* PlayerController = GetOwningPlayerController();
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	FHitResult Hit;
	const double InteractionRadius = Settings ? Settings->GetInteractionRadiusCentimeters() : 0.0;
	double ReachDistance = -1.0;
	const auto Finish = [&](AActor* Target, const TCHAR* Reason) -> AActor*
	{
		// 仅按键诊断落盘；20 Hz 扫描不刷日志。未命中用 -1，不能把默认 Hit.Distance=0 当作距离证据。
		if (bLogDecision)
		{
			const FString Message = FString::Printf(TEXT("Event=interaction_target_decision World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Pawn=%s HitActor=%s HitComponent=%s RayDistanceCm=%.3f ReachDistanceCm=%.3f RadiusCm=%.3f Result=%s"),
				*GetNameSafe(World), World ? int32(World->GetNetMode()) : -1,
				PlayerController && PlayerController->HasAuthority(), PlayerController ? int32(PlayerController->GetLocalRole()) : -1,
				*GetNameSafe(PlayerController), *GetNameSafe(Pawn), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
				Hit.bBlockingHit ? Hit.Distance : -1.0, ReachDistance, InteractionRadius, Reason);
			if (Target)
			{
				UE_LOG(LogCatfishing, Log, TEXT("%s"), *Message);
			}
			else
			{
				UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Message);
			}
		}
		return Target;
	};
	if (!PlayerController || !PlayerController->IsLocalController() || !Pawn || !Settings || !World)
	{
		return Finish(nullptr, TEXT("MissingLocalContext"));
	}

	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport
		|| !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
	{
		return Finish(nullptr, TEXT("NoViewportProjection"));
	}
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0) return Finish(nullptr, TEXT("EmptyViewRect"));

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!PlayerController->DeprojectScreenPositionToWorld(
		(ViewRect.Min.X + ViewRect.Max.X) * 0.5f, (ViewRect.Min.Y + ViewRect.Max.Y) * 0.5f, RayOrigin, RayDirection))
	{
		return Finish(nullptr, TEXT("DeprojectionFailed"));
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatInteractionTargeting), Settings->bTraceComplex);
	QueryParams.AddIgnoredActor(Pawn);
	// 三角不等式保证身体半径内的命中点都在射程内；唯一射线仍取第一个遮挡，不穿墙寻找目标。
	const double CameraToPawnDistance = FVector::Dist(RayOrigin, Pawn->GetActorLocation());
	const FVector TraceEnd = RayOrigin + RayDirection.GetSafeNormal()
		* FMath::Max(CameraToPawnDistance + InteractionRadius, ObservationDistanceCentimeters);
	if (!World->LineTraceSingleByChannel(Hit, RayOrigin, TraceEnd, Settings->TargetingTraceChannel, QueryParams))
	{
		return Finish(nullptr, TEXT("NoBlockingHit"));
	}
	AActor* HitActor = Hit.GetActor();
	ObservedTarget = HitActor;
	ReachDistance = FVector::Dist(Pawn->GetActorLocation(), Hit.ImpactPoint);
	if (InteractionRadius <= 0.0) return Finish(nullptr, TEXT("InvalidRadius"));
	if (ReachDistance > InteractionRadius) return Finish(nullptr, TEXT("OutOfReach"));
	if (!HitActor || !HitActor->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
		return Finish(nullptr, TEXT("NotInteractable"));
	if (!ICatInteractable::Execute_CanInteract(HitActor, PlayerController)) return Finish(nullptr, TEXT("TargetDenied"));
	return Finish(HitActor, TEXT("TargetReady"));
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
	ApplyTarget(TraceInteractableFromCrosshair(true));
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
		const FGuid RequestId = FGuid::NewGuid();
		UE_LOG(LogCatfishing, Log, TEXT("Event=interaction_request_submitted World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Target=%s RequestId=%s"),
			*GetNameSafe(GetWorld()), int32(PlayerController->GetNetMode()), PlayerController->HasAuthority(), int32(PlayerController->GetLocalRole()),
			*GetNameSafe(PlayerController), *GetNameSafe(Target), *RequestId.ToString());
		ICatInteractable::Execute_Interact(Target, PlayerController, RequestId);
	}
}
