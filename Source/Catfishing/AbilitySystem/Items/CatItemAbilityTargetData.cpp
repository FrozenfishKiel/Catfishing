#include "AbilitySystem/Items/CatItemAbilityTargetData.h"
#include "Inventory/CatInventoryComponent.h"
#include "Engine/PackageMapClient.h"
#include "Items/Fish/CatFishPickupActor.h"

// 网络序列化流程：传输库存、世界鱼引用和稳定身份，再传输视线、持续输入标记及目标 Actor；三个对象引用均解析成功才返回成功。
bool FCatItemAbilityTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	UObject* Object = Inventory.Get();
	const bool bMapped = Map->SerializeObject(Ar, UCatInventoryComponent::StaticClass(), Object);
	if (Ar.IsLoading()) Inventory = Cast<UCatInventoryComponent>(Object);
	UObject* Fish = WorldFish.Get();
	const bool bFishMapped = Map->SerializeObject(Ar, ACatFishPickupActor::StaticClass(), Fish);
	if (Ar.IsLoading()) WorldFish = Cast<ACatFishPickupActor>(Fish);
	Ar << ItemId << RequestId;
	Ar << Aim.bHasViewRay << Aim.ViewOrigin << Aim.ViewDirection << bContinuousInput;
	UObject* AimActor = Aim.Actor.Get();
	const bool bAimMapped = Map->SerializeObject(Ar, AActor::StaticClass(), AimActor);
	if (Ar.IsLoading()) Aim.Actor = Cast<AActor>(AimActor);
	bOutSuccess = !Ar.IsError();
	return bMapped && bFishMapped && bAimMapped;
}
