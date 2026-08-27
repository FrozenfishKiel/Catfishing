#include "Items/CatFishTankInteractionComponent.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatFishTankActor.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"

// 构造流程：给鱼缸设置稳定中文提示和默认交互距离；实际距离与权限仍由交互扫描和服务器 Camp gate 共同裁决。
UCatFishTankInteractionComponent::UCatFishTankInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// 外部容器打开流程：
// 1. 解析本地 PlayerController、LocalPlayer、鱼缸 Actor、鱼缸复制组件和本地 UI Subsystem；缺任一依赖就只记录拒绝。
// 2. 把鱼缸复制组件作为一份外部容器上下文传给背包，组件本身仍只是只读复制出口。
// 3. 本交互不挑具体物体、不移动容器内容、不提交 Items；真实跨容器移动只能由背包 Drop 后的服务器事务完成。
bool UCatFishTankInteractionComponent::OpenInventoryForPlayer(APlayerController* PlayerController)
{
	ACatFishTankActor* Tank = GetOwningFishTank();
	UCatContainerReplicationComponent* TankReplication =
		Tank ? Tank->FindComponentByClass<UCatContainerReplicationComponent>() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (!PlayerController || !Tank || !TankReplication || !UISubsystem)
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=fish_tank_interaction_rejected Reason=DependencyUnavailable Controller=%s Tank=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(Tank));
		return false;
	}
	TArray<UCatContainerReplicationComponent*> ExternalContainers;
	ExternalContainers.Add(TankReplication);
	UISubsystem->OpenInventoryWithExternalContainerContexts(ExternalContainers);
	const FCatContainerSnapshot& Snapshot = TankReplication->GetSnapshot();
	UE_LOG(LogCatItems, Log, TEXT("Event=fish_tank_interaction_inventory_opened Tank=%s Container=%s Revision=%lld"),
		*GetNameSafe(Tank),
		*Snapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		Snapshot.Revision);
	return true;
}

// Owner 解析流程：只承认真正的 ACatFishTankActor；组件被蓝图误挂到其它 Actor 时保持 fail-closed。
ACatFishTankActor* UCatFishTankInteractionComponent::GetOwningFishTank() const
{
	return Cast<ACatFishTankActor>(GetOwner());
}
