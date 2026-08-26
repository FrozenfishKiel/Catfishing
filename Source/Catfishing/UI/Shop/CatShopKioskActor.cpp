#include "UI/Shop/CatShopKioskActor.h"

#include "Components/SceneComponent.h"
#include "UI/Shop/CatShopInteractionComponent.h"

// 构造流程：建立一个轻量可放置 Actor，把商店打开能力放在组件上；Actor 自身不读取或修改商店经济后端。
ACatShopKioskActor::ACatShopKioskActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	ShopInteraction = CreateDefaultSubobject<UCatShopInteractionComponent>(TEXT("ShopInteraction"));
}

// 组件读取流程：返回当前 Actor 自带的交互组件；调用方不能通过 Actor 绕过组件的交互 gate。
UCatShopInteractionComponent* ACatShopKioskActor::GetShopInteraction() const
{
	return ShopInteraction;
}
