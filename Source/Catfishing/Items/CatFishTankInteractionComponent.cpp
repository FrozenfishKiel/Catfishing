#include "Items/CatFishTankInteractionComponent.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatFishTankActor.h"
#include "Logging/CatLog.h"

// 构造流程：给鱼缸设置稳定中文提示和默认交互距离；实际距离与权限仍由交互扫描和服务器 Camp gate 共同裁决。
UCatFishTankInteractionComponent::UCatFishTankInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InteractionTargetText = FText::FromString(TEXT("鱼缸"));
	InteractionRadiusCentimeters = 300.0;
}

// 可交互判断流程：读取基类的启用、Controller 和 Owner gate；只有这些条件成立且 Owner 是鱼缸 Actor 时才允许 UI 展示确认入口。
bool UCatFishTankInteractionComponent::CanInteract_Implementation(APlayerController* PlayerController) const
{
	return Super::CanInteract_Implementation(PlayerController) && GetOwningFishTank();
}

// 入缸交互流程：
// 1. 先解析本地 Controller、Pawn、鱼缸 Actor、显式关联 Camp 和两端复制组件；缺任一依赖就记录拒绝日志并返回 false。
// 2. 再读取个人鱼护和鱼缸快照，要求两边容器 ID 有效且鱼护里至少有鱼；这些失败都只终止本地请求，不写 Items。
// 3. 优先选择第一条可展示鱼；若客户端鱼表不可用或没有展示鱼，则把第一条鱼交给服务器产生正式拒绝或成功。
// 4. 最后先拷贝鱼实例 ID，再按 Authority/客户端分支提交同一个转缸入口；主机路径可能同步刷新快照，日志不能再读数组指针。
bool UCatFishTankInteractionComponent::Interact_Implementation(APlayerController* PlayerController)
{
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(PlayerController);
	ACatCharacter* Character = CatController ? Cast<ACatCharacter>(CatController->GetPawn()) : nullptr;
	ACatFishTankActor* Tank = GetOwningFishTank();
	ACatCampHubActor* Camp = ResolveLinkedCamp();
	const UCatContainerReplicationComponent* GuardReplication =
		Character ? Character->FindComponentByClass<UCatContainerReplicationComponent>() : nullptr;
	const UCatContainerReplicationComponent* TankReplication =
		Tank ? Tank->FindComponentByClass<UCatContainerReplicationComponent>() : nullptr;
	if (!CatController || !Character || !Tank || !Camp || !GuardReplication || !TankReplication)
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=fish_tank_interaction_rejected Reason=DependencyUnavailable Controller=%s Tank=%s Camp=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(Tank),
			*GetNameSafe(Camp));
		return false;
	}

	const FCatContainerSnapshot& GuardSnapshot = GuardReplication->GetSnapshot();
	const FCatContainerSnapshot& TankSnapshot = TankReplication->GetSnapshot();
	if (!GuardSnapshot.ContainerId.IsValid() || !TankSnapshot.ContainerId.IsValid() || GuardSnapshot.Fish.IsEmpty())
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=fish_tank_interaction_rejected Reason=SnapshotUnavailable Guard=%s Tank=%s FishCount=%d"),
			*GuardSnapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
			*TankSnapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
			GuardSnapshot.Fish.Num());
		return false;
	}

	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	const FCatFishInstance* FishToTransfer = GuardSnapshot.Fish.FindByPredicate([Catalog](const FCatFishInstance& Fish)
	{
		const UCatFishDefinition* Definition = Catalog ? Catalog->FindRuntimeDefinition(Fish.FishDefinitionId) : nullptr;
		return Definition && Definition->bTankDisplayEligible;
	});
	if (!FishToTransfer)
	{
		FishToTransfer = &GuardSnapshot.Fish[0];
	}
	if (!FishToTransfer->FishInstanceId.IsValid())
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=fish_tank_interaction_rejected Reason=InvalidFishInstance"));
		return false;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const FGuid FishInstanceId = FishToTransfer->FishInstanceId;
	if (CatController->HasAuthority())
	{
		CatController->ServerTransferFishToTank_Implementation(
			Camp, RequestId, FishInstanceId, GuardSnapshot.Revision, TankSnapshot.Revision);
	}
	else
	{
		CatController->ServerTransferFishToTank(
			Camp, RequestId, FishInstanceId, GuardSnapshot.Revision, TankSnapshot.Revision);
	}
	UE_LOG(LogCatItems, Log, TEXT("Event=fish_tank_interaction_transfer_requested RequestId=%s Fish=%s GuardRevision=%lld TankRevision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		GuardSnapshot.Revision,
		TankSnapshot.Revision);
	return true;
}

// 提示文本流程：不读取容器或 Camp 状态，只把构造阶段写入或蓝图覆盖的 InteractionTargetText 交给基类返回。
FText UCatFishTankInteractionComponent::GetInteractionTargetText_Implementation(APlayerController* PlayerController) const
{
	return Super::GetInteractionTargetText_Implementation(PlayerController);
}

// Owner 解析流程：只承认真正的 ACatFishTankActor；组件被蓝图误挂到其它 Actor 时保持 fail-closed。
ACatFishTankActor* UCatFishTankInteractionComponent::GetOwningFishTank() const
{
	return Cast<ACatFishTankActor>(GetOwner());
}

// 营地查找流程：遍历当前 World 的固定营地，返回那个显式 SharedFishTank 指向本鱼缸的 Hub；找不到时不猜最近营地。
ACatCampHubActor* UCatFishTankInteractionComponent::ResolveLinkedCamp() const
{
	const ACatFishTankActor* Tank = GetOwningFishTank();
	UWorld* World = Tank ? Tank->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
	{
		ACatCampHubActor* Camp = *It;
		if (Camp && Camp->IsSharedFishTank(Tank))
		{
			return Camp;
		}
	}
	return nullptr;
}
