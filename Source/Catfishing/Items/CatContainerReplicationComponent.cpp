#include "Items/CatContainerReplicationComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// 构造流程：声明默认复制并禁用 Tick；服务只在 Revision 变化后显式发布，不制造第二条定时更新链。
UCatContainerReplicationComponent::UCatContainerReplicationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	ReplicatedFish.Owner = this;
}

// 复制注册流程：先保留父类字段，再分别注册 ID、类型、Revision 元数据和 FastArray；UE 不保证两类通知的先后，所以它们各自重建同一 Snapshot 以最终收敛。
void UCatContainerReplicationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ReplicatedContainerId);
	DOREPLIFETIME(ThisClass, ReplicatedKind);
	DOREPLIFETIME(ThisClass, ReplicatedRevision);
	DOREPLIFETIME(ThisClass, ReplicatedFish);
}

// authority 发布流程：验证 Owner 权威后替换元数据，再按 FishInstanceId 删除旧项并标记数组变脏，对新项/变更项分别 MarkItemDirty；最后 ForceNetUpdate，预留锁等私有事实不进入复制。
void UCatContainerReplicationComponent::SetSnapshotFromAuthority(const FCatContainerSnapshot& NewSnapshot)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	Snapshot = NewSnapshot;
	ReplicatedContainerId = NewSnapshot.ContainerId;
	ReplicatedKind = NewSnapshot.Kind;
	ReplicatedRevision = NewSnapshot.Revision;
	bool bRemovedEntry = false;
	for (int32 Index = ReplicatedFish.Entries.Num() - 1; Index >= 0; --Index)
	{
		if (!NewSnapshot.Fish.ContainsByPredicate([&Entry = ReplicatedFish.Entries[Index]](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == Entry.Fish.FishInstanceId;
		}))
		{
			ReplicatedFish.Entries.RemoveAt(Index);
			bRemovedEntry = true;
		}
	}
	if (bRemovedEntry)
	{
		ReplicatedFish.MarkArrayDirty();
	}
	for (const FCatFishInstance& Fish : NewSnapshot.Fish)
	{
		FCatReplicatedFishEntry* Existing = ReplicatedFish.Entries.FindByPredicate([&Fish](const FCatReplicatedFishEntry& Entry)
		{
			return Entry.Fish.FishInstanceId == Fish.FishInstanceId;
		});
		if (!Existing)
		{
			FCatReplicatedFishEntry& Added = ReplicatedFish.Entries.AddDefaulted_GetRef();
			Added.Fish = Fish;
			ReplicatedFish.MarkItemDirty(Added);
		}
		else if (Existing->Fish.FishDefinitionId != Fish.FishDefinitionId
			|| Existing->Fish.SourceFishingSessionId != Fish.SourceFishingSessionId
			|| Existing->Fish.SacrificeContribution != Fish.SacrificeContribution
			|| Existing->Fish.WeightKilograms != Fish.WeightKilograms)
		{
			Existing->Fish = Fish;
			ReplicatedFish.MarkItemDirty(*Existing);
		}
	}
	Owner->ForceNetUpdate();
}

// 快照读取流程：返回组件持有的最终复制 DTO；调用方只能以 Revision 判断新旧，不能把数组当写入口。
const FCatContainerSnapshot& UCatContainerReplicationComponent::GetSnapshot() const
{
	return Snapshot;
}

// 元数据复制流程：使用当前 FastArray 条目重建只读快照；同包条目稍后到达时 FastArray 回调会再次重建，不会生成中间领域提交。
void UCatContainerReplicationComponent::OnRep_ContainerMetadata()
{
	RebuildSnapshotFromReplication();
}

// 快照重建流程：覆盖容器元数据并按当前 FastArray 顺序复制公开鱼字段；OwnerStableNetId 未复制且自然保持空。
void UCatContainerReplicationComponent::RebuildSnapshotFromReplication()
{
	Snapshot.ContainerId = ReplicatedContainerId;
	Snapshot.Kind = ReplicatedKind;
	Snapshot.Revision = ReplicatedRevision;
	Snapshot.Fish.Reset(ReplicatedFish.Entries.Num());
	for (const FCatReplicatedFishEntry& Entry : ReplicatedFish.Entries)
	{
		Snapshot.Fish.Add(Entry.Fish);
	}
}

// FastArray 收包完成流程：等待 UE 把本次新增、修改、删除全部应用后再重建一次 DTO；参数只描述映射状态，不参与领域身份或 Revision 裁决。
void FCatReplicatedFishList::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	(void)Parameters;
	if (Owner)
	{
		Owner->RebuildSnapshotFromReplication();
	}
}
