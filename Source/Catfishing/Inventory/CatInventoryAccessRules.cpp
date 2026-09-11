#include "Inventory/CatInventoryAccessRules.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "FishContainers/CatFishGuardActor.h"

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
	// 触达判断流程：先排除已进入库存的鱼护，再比较同世界角色与宿主距离；搬走鱼护后，旧页面或延迟 RPC 不能继续读写其中的鱼。
	if (const ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(Host); Guard && !Guard->IsGrounded()) return false;
	const double Radius = ResolveReachRadiusCentimeters(Host, Settings);
	return Host && Character && Radius > 0.0
		&& !Host->IsActorBeingDestroyed() && Host->GetWorld() == Character->GetWorld()
		&& FVector::DistSquared(Character->GetActorLocation(), Host->GetActorLocation()) <= FMath::Square(Radius);
}
