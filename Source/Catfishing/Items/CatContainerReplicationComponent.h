#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/CatItemTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "CatContainerReplicationComponent.generated.h"

class UCatContainerReplicationComponent;

/** FastArray 的单条实物鱼复制项；ReplicationID/Key 只服务网络增量，不成为领域身份。 */
USTRUCT()
struct FCatReplicatedFishEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 已提交的实物鱼公开字段；OwnerStableNetId 无 UPROPERTY，仍不会发送客户端。 */
	UPROPERTY()
	FCatFishInstance Fish;
};

/** 容器鱼数组的 FastArray 复制适配；服务端 Items 仍拥有完整数组与 Revision，适配器只发送增删改。 */
USTRUCT()
struct FCatReplicatedFishList : public FFastArraySerializer
{
	GENERATED_BODY()

	/** 当前网络增量项；只有组件 SetSnapshotFromAuthority 可以同步它。 */
	UPROPERTY()
	TArray<FCatReplicatedFishEntry> Entries;

	/** 本机拥有该列表的组件；构造时设置，不复制、不跨 World 持有。 */
	UCatContainerReplicationComponent* Owner = nullptr;

	/** 把标准 FastArray delta 交给 UE NetCore；Item 类型不拥有领域写逻辑。 */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FCatReplicatedFishEntry, FCatReplicatedFishList>(
			Entries, DeltaParams, *this);
	}

	/** 客户端完成一次 FastArray 增量反序列化后重建最终只读 Snapshot；此时新增、修改和删除均已落入数组，不会暴露删除前的旧条目。 */
	void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);
};

template<>
/** 告诉 UE 该列表使用自定义 FastArray delta 序列化；缺少此 Trait 会退回普通结构复制，破坏增量回调与 Snapshot 重建配对。 */
struct TStructOpsTypeTraits<FCatReplicatedFishList> : TStructOpsTypeTraitsBase2<FCatReplicatedFishList>
{
	enum
	{
		/** 告诉 UScriptStruct 调用上方 NetDeltaSerialize；它只是编译期 Trait 开关，不是容器运行状态。 */
		WithNetDeltaSerializer = true
	};
};

/** Items Aggregate 的只读复制出口；组件不提供数组写口，所有变更必须先由 UCatItemsService 提交。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatContainerReplicationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；容器真相只在服务提交后推送，不做每帧同步。 */
	UCatContainerReplicationComponent();

	/** 注册复制字段；个人组件沿 Character owner 相关性发送，共享组件沿 FishTank Actor 相关性发送。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** authority 服务发布完整快照；拒绝客户端调用，并用 ForceNetUpdate 加速事务终态收敛。 */
	void SetSnapshotFromAuthority(const FCatContainerSnapshot& NewSnapshot);

	/** 提供 Items 已发布的服务器事实或客户端重建结果；只读引用禁止表现绕过聚合写口修改鱼数组。 */
	const FCatContainerSnapshot& GetSnapshot() const;

private:
	friend struct FCatReplicatedFishList;

	/** 元数据或 FastArray 回调发生后重建外部只读 Snapshot；不产生写命令、Revision 或终态缓存。 */
	void RebuildSnapshotFromReplication();

	/** 容器 ID、类型或 Revision 到达客户端时重建完整读模型；FastArray 同包顺序差异由两类回调共同收敛。 */
	UFUNCTION()
	void OnRep_ContainerMetadata();

	/** 由复制元数据与 FastArray 组合的本机只读事实；组件自身不持有预留或终态缓存。 */
	UPROPERTY(Transient)
	FCatContainerSnapshot Snapshot;

	/** 容器稳定 ID 的独立复制字段；Items 发布时写入，客户与 Kind/Revision/FastArray 重建同一只读 Snapshot。 */
	UPROPERTY(ReplicatedUsing = OnRep_ContainerMetadata)
	FGuid ReplicatedContainerId;

	/** 容器类别的独立复制字段；Items 决定并随元数据到达，客户只组合 Snapshot 而不据此授权。 */
	UPROPERTY(ReplicatedUsing = OnRep_ContainerMetadata)
	ECatContainerKind ReplicatedKind = ECatContainerKind::Unknown;

	/** Items 聚合提交后的版本；服务器随整份快照写入，客户用它识别元数据与数组是否收敛到新事务。 */
	UPROPERTY(ReplicatedUsing = OnRep_ContainerMetadata)
	int64 ReplicatedRevision = 0;

	/** 实物鱼增删改的 FastArray 网络适配；Items 是唯一写者，客户 delta 回调与元数据 OnRep 可任意先后重建并最终收敛。 */
	UPROPERTY(Replicated)
	FCatReplicatedFishList ReplicatedFish;
};
