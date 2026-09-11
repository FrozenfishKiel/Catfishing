#include "Inventory/CatFishGuardInventoryItemInstance.h"

#include "FishContainers/CatFishGuardActor.h"

// 载体关联流程：只记录有效的权威鱼护，不搬运其中的鱼；落地入口仍会复核载体与请求角色属于同一World。
void UCatFishGuardInventoryItemInstance::SetGuardFromAuthority(ACatFishGuardActor* InGuard)
{
	if (InGuard && InGuard->HasAuthority())
	{
		Guard = InGuard;
	}
}

// 世界载体读取流程：返回仍存活的鱼护，销毁中的 Actor 不允许再次作为落地目标。
AActor* UCatFishGuardInventoryItemInstance::GetWorldActor() const
{
	return IsValid(Guard) && !Guard->IsActorBeingDestroyed() ? Guard.Get() : nullptr;
}

// 归属同步流程：先更新库存实例的运行宿主，再通知同一鱼护切换地面/库存状态；客户端只消费 Actor 的归属复制。
void UCatFishGuardInventoryItemInstance::SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor)
{
	Super::SetRuntimeOwnerActor(InRuntimeOwnerActor);
	if (IsValid(Guard) && Guard->HasAuthority())
	{
		Guard->SetInventoryOwnerFromAuthority(InRuntimeOwnerActor == Guard ? nullptr : InRuntimeOwnerActor);
	}
}
