#include "Inventory/CatInventoryAccessRules.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"

double CatInventoryAccessRules::ResolveReachRadiusCentimeters(const AActor* Host,
	const UCatCampSettings* Settings)
{
	// 半径解析流程：
	// 1. 宿主实现交互接口时读取 Actor 自己声明的半径，使鱼护、共享鱼缸和公共仓库共享同一触达口径。
	// 2. 接口缺失或返回非法值时使用营地默认范围，避免每个服务器 RPC 自己声明触达半径。
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

bool CatInventoryAccessRules::IsHostReachable(const AActor* Host, const ACatCharacter* Character,
	const UCatCampSettings* Settings)
{
	// 触达判断流程：先取得统一半径，再用服务器当前角色位置和宿主位置比较；库存内容和权限继续由正式库存组件裁决。
	const double Radius = ResolveReachRadiusCentimeters(Host, Settings);
	return Host && Character && Radius > 0.0
		&& FVector::DistSquared(Character->GetActorLocation(), Host->GetActorLocation()) <= FMath::Square(Radius);
}
