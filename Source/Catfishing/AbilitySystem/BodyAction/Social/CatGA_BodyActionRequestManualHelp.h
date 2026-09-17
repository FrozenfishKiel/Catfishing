#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatGA_BodyActionRequestManualHelp.generated.h"

class APlayerState;

/** 手动求助请求载荷；它只描述普通玩家信号，不允许通过 BodyAction 伪造系统全局提示。 */
USTRUCT()
struct CATFISHING_API FCatBodyActionRequestManualHelpTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

public:
	/** 本次求助信号的幂等键；Social 用它发布或拒绝同一次玩家意图。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 玩家请求的普通求助类型；Social 会拒绝不属于手动入口的系统提示。 */
	UPROPERTY(Transient)
	ECatHelpSignalKind HelpKind = ECatHelpSignalKind::Unknown;
	/** 事件接收方据此确认本动作的目标数据类型。 */
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	/** 随 GAS 预测激活传输本地请求参数；此处仅序列化，服务器能力与领域服务继续验证请求。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FCatBodyActionRequestManualHelpTargetData> : TStructOpsTypeTraitsBase2<FCatBodyActionRequestManualHelpTargetData>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

/** 手动求助身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionRequestManualHelp : public UCatGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立求助 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应手动求助事件。 */
	UCatGA_BodyActionRequestManualHelp();

protected:
	/** 激活求助动作：校验求助事件与载荷，启动角色表现，并在可取消前摇后提交 Social 求助请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束求助动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交手动求助请求；服务器先提交 GAS 成本与冷却，再调用 Social 求助入口，按领域结果正常结束或取消。 */
	UFUNCTION()
	void CommitManualHelpAfterWindow();

	/** 当前求助动作冻结的请求参数；由激活阶段写入，提交或取消收尾时重置。 */
	UPROPERTY(Transient)
	FCatBodyActionRequestManualHelpTargetData ActiveRequest;

	/** 动画中断时取消能力，提交前取消不会进入领域写口。 */
	UFUNCTION()
	void CancelAction();
};
