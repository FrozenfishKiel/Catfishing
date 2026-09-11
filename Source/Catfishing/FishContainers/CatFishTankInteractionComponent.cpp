#include "FishContainers/CatFishTankInteractionComponent.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"

#include "Engine/LocalPlayer.h"
#include "FishContainers/CatFishTankActor.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/CatInventoryComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryWidget.h"

// 构造流程：鱼缸交互组件不 Tick；它只在玩家确认交互时把鱼缸正式库存交给 UI。
UCatFishTankInteractionComponent::UCatFishTankInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// 外部库存打开流程：
// 1. 解析本地 PlayerController、LocalPlayer、鱼缸 Actor、鱼缸正式库存和本地 UI Subsystem；缺任一依赖就只记录拒绝。
// 2. 把鱼缸库存和正式库存 WBP 类交给同一打开入口，面板只绑定鱼缸自己的 Model。
// 3. 本交互不挑具体鱼、不移动库存内容、不提交领域服务；真实跨库存移动只能由背包 Drop 后的服务器库存事务完成。
bool UCatFishTankInteractionComponent::OpenInventoryForPlayer(APlayerController* PlayerController)
{
	ACatFishTankActor* Tank = GetOwningFishTank();
	UCatInventoryComponent* TankInventory = Tank ? Tank->GetFishInventoryComponent() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (!PlayerController || !Tank || !TankInventory || !UISubsystem)
	{
		UE_LOG(LogCatFishContainers, Warning, TEXT("Event=fish_tank_interaction_rejected Reason=DependencyUnavailable Controller=%s Tank=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(Tank));
		return false;
	}
	const bool bOpened = UISubsystem->OpenInventory(TankInventory, GetDefault<UCatUISettings>()->LoadInventoryWidgetClass());
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_tank_interaction_inventory_opened Tank=%s Opened=%s"),
		*GetNameSafe(Tank), bOpened ? TEXT("true") : TEXT("false"));
	return bOpened;
}

// Owner 解析流程：只承认真正的 ACatFishTankActor；组件被蓝图误挂到其它 Actor 时保持 fail-closed。
ACatFishTankActor* UCatFishTankInteractionComponent::GetOwningFishTank() const
{
	return Cast<ACatFishTankActor>(GetOwner());
}
