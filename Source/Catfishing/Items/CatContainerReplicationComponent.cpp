#include "Items/CatContainerReplicationComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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

// 结束流程：先从当前 World 的 TimerManager 取消延迟广播，再清本地 pending 与外部订阅，最后交还组件生命周期；重复 EndPlay 不会留下迟到回调。
void UCatContainerReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredSnapshotNotificationHandle);
	}
	DeferredSnapshotNotificationHandle.Invalidate();
	bSnapshotNotificationPending = false;
	OnSnapshotChanged.Clear();
	Super::EndPlay(EndPlayReason);
}

// authority 发布流程：验证 Owner 权威后替换元数据，再按 FishInstanceId 删除旧项并标记数组变脏，对新项/变更项分别 MarkItemDirty；全部字段一致后广播只读变化并 ForceNetUpdate，预留锁等私有事实不进入复制。
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
	OnSnapshotChanged.Broadcast();
	Owner->ForceNetUpdate();
}

// 快照读取流程：返回组件持有的最终复制 DTO；调用方只能以 Revision 判断新旧，不能把数组当写入口。
const FCatContainerSnapshot& UCatContainerReplicationComponent::GetSnapshot() const
{
	return Snapshot;
}

// 元数据复制流程：只安排下一帧合并，不立即暴露当前 FastArray；若同帧鱼数组稍后到达，两类事实会在同一次广播前组合完成。
void UCatContainerReplicationComponent::OnRep_ContainerMetadata()
{
	ScheduleSnapshotNotificationFromReplication();
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

// 合并安排流程：已有 pending 时直接复用；否则在有效 World 上登记下一帧回调。无 World 的异常复制入口退化为立即重建和广播，避免读模型永远停在旧值。
void UCatContainerReplicationComponent::ScheduleSnapshotNotificationFromReplication()
{
	if (bSnapshotNotificationPending)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		RebuildSnapshotFromReplication();
		OnSnapshotChanged.Broadcast();
		return;
	}
	bSnapshotNotificationPending = true;
	DeferredSnapshotNotificationHandle = World->GetTimerManager().SetTimerForNextTick(
		this, &ThisClass::FlushSnapshotNotificationFromReplication);
}

// 合并提交流程：先清计时与 pending 状态，再从最新元数据和完整 FastArray 重建 Snapshot，最后只广播“可重新读取”信号；订阅者永远看不到本方法内部的半成品。
void UCatContainerReplicationComponent::FlushSnapshotNotificationFromReplication()
{
	DeferredSnapshotNotificationHandle.Invalidate();
	bSnapshotNotificationPending = false;
	RebuildSnapshotFromReplication();
	OnSnapshotChanged.Broadcast();
}

// FastArray 收包完成流程：UE 已应用本次新增、修改与删除，但元数据通知顺序仍不保证；因此只安排与 OnRep 共用的下一帧合并，参数不参与领域身份或 Revision 裁决。
void FCatReplicatedFishList::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	(void)Parameters;
	if (Owner)
	{
		Owner->ScheduleSnapshotNotificationFromReplication();
	}
}
