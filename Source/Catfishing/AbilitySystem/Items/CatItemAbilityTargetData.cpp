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
	Ar << bSecondaryInput;
	// 有界字符载荷避免在校验前按客户端声明分配任意长字符串。
	uint8 Length = static_cast<uint8>(FMath::Min(Message.Len(), 120));
	Ar << Length;
	if (Length > 120) { Ar.SetError(); bOutSuccess = false; return false; }
	if (Ar.IsLoading()) Message.Empty(Length);
	for (uint8 Index = 0; Index < Length; ++Index)
	{
		uint16 Character = Ar.IsSaving() ? static_cast<uint16>(Message[Index]) : 0;
		Ar << Character;
		if (Ar.IsLoading()) Message.AppendChar(static_cast<TCHAR>(Character));
	}
	UObject* AimActor = Aim.Actor.Get();
	const bool bAimMapped = Map->SerializeObject(Ar, AActor::StaticClass(), AimActor);
	if (Ar.IsLoading()) Aim.Actor = Cast<AActor>(AimActor);
	bOutSuccess = !Ar.IsError();
	return bMapped && bFishMapped && bAimMapped;
}
