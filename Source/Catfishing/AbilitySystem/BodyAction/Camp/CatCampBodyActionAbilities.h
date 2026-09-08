#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "CatCampBodyActionAbilities.generated.h"

class ACatCampHubActor;
class ACatCharacter;

/** 营地休息请求载荷；它只携带目标营地和请求键，恢复规则仍由 Camp/Condition 判断。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestCampRest : public UObject
{
	GENERATED_BODY()

public:
	/** 玩家想使用的固定营地；Ability 提交时只作为候选对象，真实距离和状态由营地服务重读。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCampHubActor> Camp;

	/** 本次休息命令的幂等键；结果回送用它关联 UI pending 状态，不作为恢复权限。 */
	UPROPERTY(Transient)
	FGuid RequestId;
};

/** 篝火回看请求载荷；它只把目标营地和请求键带过 GAS 前摇，CapturePlan 与表现仍归 Camp。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestCampfirePlayback : public UObject
{
	GENERATED_BODY()

public:
	/** 负责本次回看的固定营地；提交阶段会验证 World 和营地内部 CapturePlan 条件。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCampHubActor> Camp;

	/** 本次回看命令的幂等键；Camp 结果和 owning-client 回执都使用同一个键。 */
	UPROPERTY(Transient)
	FGuid RequestId;
};

/** 搬运倒地伙伴回营地的请求载荷；它只描述救援意图，不直接改变目标 Character 状态。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestRescueCharacterToCamp : public UObject
{
	GENERATED_BODY()

public:
	/** 接收救援的固定营地；最终落点和距离约束由 Camp 在服务器侧裁决。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCampHubActor> Camp;

	/** 被救援的目标角色；Ability 只保存引用，倒地事实和 World 归属在提交阶段重读。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCharacter> TargetCharacter;

	/** 本次救援命令的幂等键；公共领域结果用它回送给发起者。 */
	UPROPERTY(Transient)
	FGuid RequestId;
};

/** 营地休息身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionCampRest : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立休息 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应营地休息事件。 */
	UCatGA_BodyActionCampRest();

protected:
	/** 激活休息动作：校验休息事件与载荷，启动角色表现，并在可取消前摇后提交 Camp 休息请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束休息动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交休息请求；它只调用 Camp 休息入口，并根据领域结果决定正常结束还是取消。 */
	UFUNCTION()
	void CommitCampRestAfterWindow();

	/** 当前休息动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestCampRest> ActiveRequest;

	/** 当前休息动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};

/** 篝火回看身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionCampfirePlayback : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立回看 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应篝火回看事件。 */
	UCatGA_BodyActionCampfirePlayback();

protected:
	/** 激活回看动作：校验回看事件与载荷，启动角色表现，并在可取消前摇后提交 Camp 回看请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束回看动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交篝火回看请求；它只调用 Camp 回看入口，并根据领域结果决定正常结束还是取消。 */
	UFUNCTION()
	void CommitCampfirePlaybackAfterWindow();

	/** 当前回看动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestCampfirePlayback> ActiveRequest;

	/** 当前回看动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};

/** 搬运救援身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionRescueCharacterToCamp : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立救援 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应搬运救援事件。 */
	UCatGA_BodyActionRescueCharacterToCamp();

protected:
	/** 激活救援动作：校验救援事件与载荷，启动角色表现，并在可取消前摇后提交 Camp 救援请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束救援动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交搬运救援请求；它只调用 Camp 救援入口，并根据领域结果决定正常结束还是取消。 */
	UFUNCTION()
	void CommitRescueCharacterToCampAfterWindow();

	/** 当前救援动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestRescueCharacterToCamp> ActiveRequest;

	/** 当前救援动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};
