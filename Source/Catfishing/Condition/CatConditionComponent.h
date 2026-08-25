#pragma once

#include "CoreMinimal.h"
#include "Condition/CatConditionTypes.h"
#include "Components/ActorComponent.h"
#include "CatConditionComponent.generated.h"

class AController;
class UCatAbilitySystemComponent;
class UCatFishDefinition;

/** Condition 完整快照发生提交或复制变化的本机通知；订阅者必须重新读取 GetSnapshot，不使用增量载荷拼状态。 */
DECLARE_MULTICAST_DELEGATE(FCatConditionSnapshotChanged);

/** Character 局内离散身体状态组件；ASC 拥有数值，本组件只裁决 Wet/Downed/恢复生命周期且绝不产生死亡。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatConditionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；所有状态只由显式 authority 命令推进。 */
	UCatConditionComponent();

	/** 注册唯一 Snapshot 复制；终态缓存和 Controller/身份不会进入网络。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供身体条件的服务器最终值或客户端复制值；外部只据此判断交互资格，不能借返回值改写 Wet/Downed。 */
	const FCatConditionSnapshot& GetSnapshot() const;

	/** Environment/水体在 authority 设置纯表现 Wet；重复相同值不增加 Revision，也不修改任何 Attribute。 */
	void SetWetFromAuthority(bool bNewWet);

	/** 在实物鱼被不可逆移除前只读校验食用定义、ASC 与倒地阈值；返回 None 才允许上层提交 Items 事务。 */
	ECatDomainCommandError ValidateFishConsumption(const UCatFishDefinition* FishDefinition) const;

	/** 在草药库存被不可逆扣除前只读校验施药者距离、ASC、倒地阈值与正式恢复数值；返回 None 才允许上层提交 Equipment 事务。 */
	ECatDomainCommandError ValidateHerbRecovery(AController* HelpingController) const;

	/** 实物鱼消费提交后读取 FishDefinition 食用字段，增加可选 Poison、推进成长经验，并重新裁决倒地。 */
	FCatDomainCommandResult ConsumeCommittedFish(FGuid RequestId, const UCatFishDefinition* FishDefinition);

	/** 单人可用的野外休息入口；按显式较慢数值恢复 Poison，不要求其他玩家。 */
	FCatDomainCommandResult RequestFieldSelfRecovery(AController* RequestingController, FGuid RequestId);

	/** 固定营地休息入口；只允许 Camp actor 传入已到达事实并按营地恢复值处理。 */
	FCatDomainCommandResult RequestCampRest(AController* RequestingController, FGuid RequestId, bool bAtCamp);

	/** 草药恢复入口；允许本人或伙伴调用，但库存消费必须在上层先完成，组件只处理身体事实且缓存同 RequestId 的首次终态。 */
	FCatDomainCommandResult ApplyCommittedHerbRecovery(AController* HelpingController, FGuid RequestId);

	/** 搬运完成入口；要求目标已倒地且服务器已把 Character 放到固定营地救援点，随后记录恢复方式；同 RequestId 只提交一次。 */
	FCatDomainCommandResult CompleteCarryToCamp(AController* HelpingController, FGuid RequestId, bool bAtCampRescuePoint);

	/** 本机完整快照变化通知；LocalPlayer UI 成对订阅，领域写入者不依赖该通知推进。 */
	FCatConditionSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整 Snapshot 后只供表现读取；不会在 RepNotify 改 Attribute 或发送恢复命令。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 校验 Recovery 配置并对 Poison 应用非负减量；随后按阈值更新 Downed/RecoveryMode。 */
	FCatDomainCommandResult ApplyRecovery(FGuid RequestId, ECatRecoveryMode Mode, double PoisonRelief);

	/** 通过项目 ASC 读取 Poison 阈值结果并更新 Downed；首次倒地会终止该 Character 的 FishingSession。 */
	void EvaluateDownedFromAttributes(ECatRecoveryMode RecoveryMode);

	/** 定位 Owner Character 的项目 ASC 供阈值读取与 GE 提交；Owner 类型不匹配时返回空，避免创建平行身体属性源。 */
	UCatAbilitySystemComponent* ResolveAbilitySystem() const;

	/** 构造操作+RequestId 的局内幂等键；身份由上层 Controller 权限另行验证。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；集中保证 UI 不漏掉任何完整快照变化。 */
	void PublishSnapshot();

	/** Wet/Downed/Recovery 的唯一复制事实。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatConditionSnapshot Snapshot;

	/** 身体命令的首次完整终态；防止重复吃鱼或重复恢复。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;
};
