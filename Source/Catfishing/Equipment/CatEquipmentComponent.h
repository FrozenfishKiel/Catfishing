#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentComponent.generated.h"

/** Equipment 完整装配快照发生提交或复制变化的本机通知；UI 只把它当重读信号。 */
DECLARE_MULTICAST_DELEGATE(FCatEquipmentSnapshotChanged);

/** Character 的一局功能型装配聚合；复制装配/耗材/耐久，不持有永久解锁且不提供任何偷取接口。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启默认复制并关闭 Tick；所有写入由 authority 命令提交。 */
	UCatEquipmentComponent();

	/** 注册单一 Loadout Snapshot；终态缓存不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终装配或客户端复制读模型；调用方只能据此显示/校验 Revision，不能通过引用补耐久或改库存。 */
	const FCatEquipmentLoadoutSnapshot& GetSnapshot() const;

	/** 根据服务器目录与可信解锁证明验证三个稳定 ID 后首次原子装配；客户端 Profile 选择不授予权限，也不能借重复请求修复耐久。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName RodDefinitionId, FName BaitDefinitionId, FName FloatDefinitionId);

	/** 尝试为刚入局的真实玩家装配项目 starter 三件套；已有装配时只返回 AlreadyResolved，不修耐久也不换装。 */
	FCatDomainCommandResult ConfigureStarterLoadoutFromAuthority(FGuid RequestId);

	/**
	 * 声明：只读地回答"这一份耗材现在授得进去吗"，返回 None 表示授得进去，其余值就是 GrantRunConsumableFromAuthority
	 *       遇到同样情况会给出的错误码。它不写任何状态，也不推进 Revision。
	 * 用途：商店订单那条链要在扣钱之前先知道东西送不送得出去——公款和角色耗材栈是两个聚合，钱一旦划走商店没有反向写口，
	 *       所以那条链靠前置 gate 而不是事后退款。随身携带上限这条判据只写在这里一份，授予写口自己也调它，
	 *       否则协调器迟早会和这里的上限规则走散。
	 */
	ECatDomainCommandError ValidateRunConsumableGrant(FGuid RequestId, FName DefinitionId, int32 Quantity) const;

	/** 一局拾取/奖励/商店耗材订单上层提交耗材；只接受定义为 run consumable 的正式条目，且不让同一栈超过配置的随身携带上限。 */
	FCatDomainCommandResult GrantRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/**
	 * 把一件从团队装备库取出的实物装到对应槽位上（Rod/Bait/Float 三槽之一），替换该槽原来的定义。
	 * 这是"买到的竿真的能装上"那条链在 Equipment 这一侧的唯一写口：它只认调用方从装备库取到的实例，不重新走 RequiredUnlockId 校验——
	 * 团队装备库里的实例是服务器自己按已付款订单造出来的，它的存在就是"这局队伍已经取得这件东西"的服务器证明；
	 * 首次装配走的是客户端 Profile 选择，所以那条路才需要解锁证明。
	 * 只在已经有完整三件套之后可用；被替换下来的旧定义不回库（回库要凭空造实例，没有订单来源，飞书的取用/归还规则也未裁）。
	 */
	FCatDomainCommandResult EquipFromTeamLibraryFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FCatTeamEquipmentInstance& Instance);

	/** 消费一份指定一局耗材；草药、窝料和道具上层必须先成功提交本结果再产生领域效果。 */
	FCatDomainCommandResult ConsumeRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 为跨域效果预留一份一局耗材；预留只占住库存、不推进公开 Revision，调用方必须随后提交或释放同一 RequestId。 */
	FCatDomainCommandResult ReserveRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 把同一 RequestId 的耗材预留转成真实消耗；只有预留成功后的领域效果成功提交时才能调用。 */
	FCatDomainCommandResult CommitReservedRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 释放同一 RequestId 的耗材预留；领域效果在预留后拒绝时调用，避免占住库存到角色销毁。 */
	FCatDomainCommandResult ReleaseRunConsumableReservationFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 提交一次钓鱼失败预算；DamageRod 只保留兼容语义，正式耐久消耗必须走 Fight Resource Cursor。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	/**
	 * 提交 FishingSession 搏斗产生的鱼竿耐久消耗（僵持逐秒磨损按秒批量、强度断竿把剩余耐久磨光、抄上鱼固定 -1）；只接
	 * 受正有限成本，耐久到 0 即置 RodBroken，调用方用 Snapshot 读结果。
	 *
	 * bRecordTerminalResult 决定这次提交要不要进重放缓存。默认 true，给"带真实客户端请求身份、可能被同 RequestId 重试"
	 * 的调用方用。FishingSession 那两条路必须传 false：它们的 RequestId 是提交前一刻当场铸出来的，没有任何人手里有这个
	 * 号、也就不会拿它再来一次；记进去的条目永远命不中，却会让两张终态表按提交次数一直涨——僵持磨损是按秒提交的，
	 * 一场搏斗就能涨出上百条，直到角色销毁才释放。传 false 时连键和签名的字符串拼接也一并省掉。
	 */
	FCatDomainCommandResult CommitFightRodDurabilityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		double DurabilityCost, bool bRecordTerminalResult = true);

	/** 本机完整装配变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整装配事实后只供 UI/玩法只读消费；不反向请求自动装备。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 查找或创建一局耗材栈；调用方必须先验证定义，创建时数量为 0。 */
	FCatRunConsumableStack& FindOrAddConsumable(FName DefinitionId);

	/** 按定义 ID 查现有耗材栈；未找到返回空。查找判据只有这一份，可写重载解除它结果的 const 交给写口。 */
	const FCatRunConsumableStack* FindConsumable(FName DefinitionId) const;

	/** 按定义 ID 查现有耗材栈的可写形式；给需要改数量的写口用，未找到同样返回空。 */
	FCatRunConsumableStack* FindConsumable(FName DefinitionId);

	/** 当前跨域效果占住的一份耗材；它还没有减少公开 Snapshot，只用于阻止并发请求重复占用同一库存。 */
	struct FRunConsumableReservation
	{
		/** 被占住的一局耗材稳定 ID；提交或释放时必须与首次预留一致。 */
		FName DefinitionId = NAME_None;

		/** 首次预留看到的 Equipment Revision；提交或释放不接受调用方改写前提。 */
		int64 ExpectedRevision = 0;

		/** 首次预留的业务载荷签名；同 RequestId 改 Definition 或 Revision 会被视为载荷漂移。 */
		FString PayloadSignature;
	};

	/** 统计当前仍未提交/释放的同定义预留数量；可用库存必须扣除这些占位。 */
	int32 CountActiveConsumableReservations(FName DefinitionId) const;

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；所有 HUD 刷新因此走同一完整快照信号。 */
	void PublishSnapshot();

	/** 装配、耗材与耐久的唯一复制读模型。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 普通装备/耗材命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 普通装备/耗材命令首次受理时的业务载荷签名；同 RequestId 不能换定义、数量或 Revision 前提。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 尚未提交或释放的一局耗材预留；Key 是 RequestId，值记录被占住的定义和 Revision 前提。 */
	TMap<FGuid, FRunConsumableReservation> ActiveConsumableReservations;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;

	/** 失败预算首次受理时的业务载荷签名；同 RequestId 不能把 None 重放成扣罚或反向漂移。 */
	TMap<FGuid, FString> FailurePayloadByRequestId;
};
