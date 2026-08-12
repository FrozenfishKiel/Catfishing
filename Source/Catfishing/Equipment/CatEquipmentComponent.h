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

	/** 一局拾取/奖励上层提交耗材；只接受定义为 run consumable 的正式条目。 */
	FCatDomainCommandResult GrantRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 消费一份指定一局耗材；草药、窝料和道具上层必须先成功提交本结果再产生领域效果。 */
	FCatDomainCommandResult ConsumeRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 提交一次钓鱼失败预算；一个 RequestId 只能选择 None/丢特殊饵/伤竿之一，绝不双罚。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	/** 固定营地修竿点提交维修；只消费一份显式浮木并恢复到当前 Rod 定义最大耐久。 */
	FCatDomainCommandResult RepairRodAtCamp(FGuid RequestId, int64 ExpectedRevision, bool bAtCamp);

	/** 本机完整装配变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整装配事实后只供 UI/玩法只读消费；不反向请求自动装备。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 查找或创建一局耗材栈；调用方必须先验证定义，创建时数量为 0。 */
	FCatRunConsumableStack& FindOrAddConsumable(FName DefinitionId);

	/** 按定义 ID 查现有耗材栈；未找到返回空。 */
	FCatRunConsumableStack* FindConsumable(FName DefinitionId);

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；所有 HUD 刷新因此走同一完整快照信号。 */
	void PublishSnapshot();

	/** 装配、耗材与耐久的唯一复制读模型。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 普通装备/耗材命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;
};
