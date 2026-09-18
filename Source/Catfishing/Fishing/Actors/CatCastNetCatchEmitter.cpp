#include "Fishing/Actors/CatCastNetCatchEmitter.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatWorldDropProtectionComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

// 无 Tick、无复制；根组件保存最后出鱼位置，角色离开后仍能把已扣网的鱼发完。
ACatCastNetCatchEmitter::ACatCastNetCatchEmitter()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("EmissionOrigin"));
}

// 仅服务器接收非空鱼获；追加已结算鱼并更新跟随对象和间隔，有计时任务时保留原预约，否则安排首条。
// 后续逐次预约，低帧率也不会在一帧补发整批。
void ACatCastNetCatchEmitter::StartFromAuthority(AActor* Executor, const TArray<ACatFishPickupActor*>& Fish, const float IntervalSeconds)
{
	if (!HasAuthority() || Fish.IsEmpty()) return;
	Source = Executor;
	PendingFish.Append(Fish);
	Interval = FMath::Max(0.05f, IntervalSeconds);
	if (!GetWorldTimerManager().IsTimerActive(EmissionTimer))
		GetWorldTimerManager().SetTimer(EmissionTimer, this, &ThisClass::EmitNextFish, Interval, false);
}

// 跟随对象仍在时更新喷出位置，否则沿最后位置继续；每次取一条，失效鱼跳过，合法鱼公开复制、启用物理并保护落地。
// 查到有效岸线时改朝岸外方向抛出；先试角色附近位置，球形碰撞范围仍触水时再移到最近岸线外并留出余量。
// 岸线查询失败时保留角色前方出口；抛出速度沿现有丢弃配置，最终位置仍交给已有防落水组件监控。
// 不再次抽鱼、扣网或发奖励。剩余鱼预约下一次，队空才销毁宿主。
void ACatCastNetCatchEmitter::EmitNextFish()
{
	if (Source.IsValid()) SetActorTransform(Source->GetActorTransform());
	if (!PendingFish.IsEmpty())
	{
		auto* Fish = PendingFish[0].Get(); PendingFish.RemoveAt(0);
		if (IsValid(Fish))
		{
			FVector Forward = GetActorForwardVector().GetSafeNormal2D();
			auto* Body = Cast<UPrimitiveComponent>(Fish->GetRootComponent());
			FVector Origin = GetActorLocation() + Forward * 100.0 + FVector(0, 0, 40);
			if (const auto* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>())
			{
				const auto Shore = Water->QueryNearestShoreForPreview(GetActorLocation());
				if (Shore.bSucceeded && !Shore.WaterwardDirection.IsNearlyZero())
				{
					Forward = -Shore.WaterwardDirection.GetSafeNormal2D();
					const double Clearance = (Body ? Body->Bounds.SphereRadius : 0.0) + 20.0;
					// 水域检查使用碰撞包围球中心而非 Actor 原点；修正落点时保留两者偏移，20 厘米是球面之外的岸线余量。
					const FVector CenterOffset = Body ? Body->Bounds.Origin - Fish->GetActorLocation() : FVector::ZeroVector;
					Origin = GetActorLocation() + Forward * FMath::Max(100.0, Clearance) + FVector(0, 0, 40);
					// 防落水按平面上的整段球体扫掠判定，抬高 Z 不能绕过；水侧的执行者也必须把出生中心移到岸内。
					if (Water->DoesWorldDropSweepTouchWater(Origin + CenterOffset, Origin + CenterOffset, Clearance - 20.0))
					{
						const FVector DryCenter = Shore.NearestShoreWorldPoint + Forward * Clearance;
						Origin.X = DryCenter.X - CenterOffset.X;
						Origin.Y = DryCenter.Y - CenterOffset.Y;
						Origin.Z = FMath::Max(Origin.Z, DryCenter.Z + Clearance - CenterOffset.Z);
					}
				}
			}
			Fish->SetActorLocation(Origin, false, nullptr, ETeleportType::TeleportPhysics);
			Fish->SetActorHiddenInGame(false); Fish->SetActorEnableCollision(true); Fish->SetReplicates(true);
			if (Body)
			{
				const auto* Settings = GetDefault<UCatInventorySettings>();
				Body->SetSimulatePhysics(true);
				Body->SetPhysicsLinearVelocity(Forward * Settings->DropForwardSpeed + FVector(0, 0, Settings->DropUpwardSpeed));
			}
			UCatWorldDropProtectionComponent::ArmFromAuthority(Fish);
			Fish->ForceNetUpdate();
			UE_LOG(LogCatFishing, Log, TEXT("Event=cast_net_fish_emitted Emitter=%s Fish=%s Remaining=%d Origin=%s Direction=%s World=%s NetMode=%d Authority=1"),
				*GetName(), *GetNameSafe(Fish), PendingFish.Num(), *Origin.ToCompactString(), *Forward.ToCompactString(), *GetNameSafe(GetWorld()), GetNetMode());
		}
	}
	if (PendingFish.IsEmpty()) Destroy();
	else GetWorldTimerManager().SetTimer(EmissionTimer, this, &ThisClass::EmitNextFish, Interval, false);
}

// 仅回收还没交给世界的隐藏候选；已出队鱼的生命周期与正常世界鱼一致。
void ACatCastNetCatchEmitter::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(EmissionTimer);
	for (const auto& Fish : PendingFish) if (IsValid(Fish)) Fish->Destroy();
	PendingFish.Reset();
	Super::EndPlay(Reason);
}
