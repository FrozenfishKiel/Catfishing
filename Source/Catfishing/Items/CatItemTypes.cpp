#include "Items/CatItemTypes.h"

namespace
{
	// 鱼槽投影资格判断流程：只认服务器分配过的实例 ID；默认构造的槽位占位不能参与 UI 展示或事务匹配。
	bool IsFishSlotEligibleForObjectProjection(const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId.IsValid();
	}

	// 有效对象判断流程：派生投影必须同时有领域类别和实例 ID；空槽默认对象不占用 UI 格子。
	bool IsValidContainedObjectSlot(const FCatContainedObjectInstance& Object)
	{
		return Object.ObjectKind != ECatContainedObjectKind::Unknown
			&& Object.ObjectInstanceId.IsValid();
	}
}

// 鱼到通用容器对象的投影流程：先拒绝空槽占位，再保留鱼自己的实例 ID 和定义 ID，同时只给容器层暴露 ObjectKind/ObjectInstanceId 这一组通用身份。
FCatContainedObjectInstance CatItems::MakeContainedObjectFromFish(const FCatFishInstance& Fish)
{
	FCatContainedObjectInstance Object;
	if (!IsFishSlotEligibleForObjectProjection(Fish))
	{
		return Object;
	}
	Object.ObjectInstanceId = Fish.FishInstanceId;
	Object.ObjectKind = ECatContainedObjectKind::Fish;
	Object.DefinitionId = Fish.FishDefinitionId;
	Object.StackQuantity = 1;
	Object.Fish = Fish;
	return Object;
}

// 通用投影重建流程：只从当前权威鱼槽数组派生 Objects；装备和耗材尚未进入 Items 容器槽位，不能保留旧 Objects 来伪装成后端真相。
void CatItems::RebuildContainedObjectsFromFish(FCatContainerSnapshot& Snapshot)
{
	const int32 SlotCount = Snapshot.Fish.Num();
	Snapshot.Objects.Reset(SlotCount);
	Snapshot.Objects.SetNum(SlotCount);
	for (int32 FishIndex = 0; FishIndex < Snapshot.Fish.Num(); ++FishIndex)
	{
		if (IsFishSlotEligibleForObjectProjection(Snapshot.Fish[FishIndex]))
		{
			Snapshot.Objects[FishIndex] = MakeContainedObjectFromFish(Snapshot.Fish[FishIndex]);
		}
	}
	while (!Snapshot.Objects.IsEmpty() && !IsValidContainedObjectSlot(Snapshot.Objects.Last()))
	{
		Snapshot.Objects.Pop(EAllowShrinking::No);
	}
}

// 对象数量读取流程：按权威鱼槽数组统计真实鱼；当前 Items 容器没有非鱼写口，不能把旧投影数组当容量占用。
int32 CatItems::GetContainedObjectCount(const FCatContainerSnapshot& Snapshot)
{
	int32 Count = 0;
	for (int32 SlotIndex = 0; SlotIndex < Snapshot.Fish.Num(); ++SlotIndex)
	{
		if (Snapshot.Fish.IsValidIndex(SlotIndex) && IsFishSlotEligibleForObjectProjection(Snapshot.Fish[SlotIndex]))
		{
			++Count;
		}
	}
	return Count;
}

// 对象下标读取流程：槽位下标直接读取权威鱼数组并临时派生通用对象；调用方拿到的是该位置的对象，不会把其他槽位的对象前移补位。
bool CatItems::TryGetContainedObjectAt(const FCatContainerSnapshot& Snapshot, const int32 ObjectIndex,
	FCatContainedObjectInstance& OutObject)
{
	if (ObjectIndex < 0)
	{
		OutObject = FCatContainedObjectInstance();
		return false;
	}
	if (Snapshot.Fish.IsValidIndex(ObjectIndex) && IsFishSlotEligibleForObjectProjection(Snapshot.Fish[ObjectIndex]))
	{
		OutObject = MakeContainedObjectFromFish(Snapshot.Fish[ObjectIndex]);
		return true;
	}
	OutObject = FCatContainedObjectInstance();
	return false;
}
