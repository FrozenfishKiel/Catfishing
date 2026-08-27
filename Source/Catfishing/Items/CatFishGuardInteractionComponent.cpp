#include "Items/CatFishGuardInteractionComponent.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatFishGuardActor.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"

// 构造流程：设置玩家能读懂的默认提示和交互半径；组件不 Tick，等待本地交互扫描器调用。
UCatFishGuardInteractionComponent::UCatFishGuardInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InteractionTargetText = FText::FromString(TEXT("鱼护"));
	InteractionRadiusCentimeters = 300.0;
}

// 可交互判断流程：先沿用基类启用、Controller 和距离约束，再要求 Owner 是正式鱼护 Actor。
bool UCatFishGuardInteractionComponent::CanInteract_Implementation(APlayerController* PlayerController) const
{
	return Super::CanInteract_Implementation(PlayerController) && GetOwningFishGuard();
}

// 打开库存流程：
// 1. 解析本地 PlayerController、LocalPlayer、鱼护箱子 Actor、鱼护复制组件和 UI Subsystem。
// 2. 把这只鱼护箱子作为外部容器上下文打开；背包 Model 只读它的复制快照，真实移动仍由服务器 Items 事务裁决。
bool UCatFishGuardInteractionComponent::Interact_Implementation(APlayerController* PlayerController)
{
	ACatFishGuardActor* Guard = GetOwningFishGuard();
	UCatContainerReplicationComponent* GuardReplication =
		Guard ? Guard->GetContainerReplicationComponent() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (!PlayerController || !Guard || !GuardReplication || !UISubsystem)
	{
		UE_LOG(LogCatItems, Warning,
			TEXT("Event=fish_guard_interaction_rejected Reason=DependencyUnavailable Controller=%s Guard=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(Guard));
		return false;
	}
	TArray<UCatContainerReplicationComponent*> ExternalContainers;
	ExternalContainers.Add(GuardReplication);
	UISubsystem->OpenInventoryWithExternalContainerContexts(ExternalContainers);
	const FCatContainerSnapshot& Snapshot = GuardReplication->GetSnapshot();
	UE_LOG(LogCatItems, Log,
		TEXT("Event=fish_guard_interaction_inventory_opened Guard=%s Container=%s Revision=%lld"),
		*GetNameSafe(Guard),
		*Snapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		Snapshot.Revision);
	return true;
}

// 提示文本流程：鱼护交互不读取容器内容或玩家身份，只把组件配置的目标名交给基类返回。
FText UCatFishGuardInteractionComponent::GetInteractionTargetText_Implementation(
	APlayerController* PlayerController) const
{
	return Super::GetInteractionTargetText_Implementation(PlayerController);
}

// Owner 解析流程：只承认真正的 ACatFishGuardActor；错误挂载时不打开任何库存页面。
ACatFishGuardActor* UCatFishGuardInteractionComponent::GetOwningFishGuard() const
{
	return Cast<ACatFishGuardActor>(GetOwner());
}
