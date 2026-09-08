#include "Items/CatContainerAccessRules.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Items/CatItemTypes.h"

double CatContainerAccessRules::ResolveReachRadiusCentimeters(const AActor* Host,
	const UCatCampSettings* Settings)
{
	// 半径解析流程：
	// 1. 宿主实现交互接口时读取 Actor 自己声明的半径，使鱼护箱、共享鱼缸和公共仓库共享同一触达口径。
	// 2. 接口缺失或返回非法值时才使用营地默认范围，避免每个服务器 RPC 留一份不同的兜底规则。
	if (Host && Host->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
	{
		const double Radius = ICatInteractable::Execute_GetInteractionRadius(const_cast<AActor*>(Host));
		if (FMath::IsFinite(Radius) && Radius > 0.0)
		{
			return Radius;
		}
	}
	return Settings && Settings->IsRuntimeReady() ? Settings->InteractionRadiusCentimeters : 0.0;
}

bool CatContainerAccessRules::IsHostReachable(const AActor* Host, const ACatCharacter* Character,
	const UCatCampSettings* Settings)
{
	// 触达判断流程：先取得统一半径，再用服务器当前角色位置和宿主位置比较；容器所有权和实例权限继续由 Items 服务裁决。
	const double Radius = ResolveReachRadiusCentimeters(Host, Settings);
	return Host && Character && Radius > 0.0
		&& FVector::DistSquared(Character->GetActorLocation(), Host->GetActorLocation()) <= FMath::Square(Radius);
}

int32 CatContainerAccessRules::FindFirstFreeSlot(const FCatContainerSnapshot& Snapshot)
{
	// 空槽查找流程：只在正式容量范围内扫描通用物体投影；空洞数组、鱼数组占位和后续物体种类都由 Items 快照函数统一解释。
	for (int32 SlotIndex = 0; SlotIndex < Snapshot.Capacity; ++SlotIndex)
	{
		FCatContainedObjectInstance ExistingObject;
		if (!CatItems::TryGetContainedObjectAt(Snapshot, SlotIndex, ExistingObject))
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}
