#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Social/CatSocialTypes.h"
#include "CatSocialBodyActionAbilities.generated.h"

class APlayerState;

/** 手动求助请求载荷；它只描述普通玩家信号，不允许通过 BodyAction 伪造系统全局提示。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestManualHelp : public UObject
{
	GENERATED_BODY()

public:
	/** 本次求助信号的幂等键；Social 用它发布或拒绝同一次玩家意图。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 玩家请求的普通求助类型；Social 会拒绝不属于手动入口的系统提示。 */
	UPROPERTY(Transient)
	ECatHelpSignalKind HelpKind = ECatHelpSignalKind::Unknown;
};

/** 恶作剧请求载荷；它把目标玩家和交互位置带入 Social 裁决，不在 Controller 里写冷却。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestMischief : public UObject
{
	GENERATED_BODY()

public:
	/** 被恶作剧的目标玩家状态；提交时会在当前 World 重新定位对应 Controller。 */
	UPROPERTY(Transient)
	TObjectPtr<APlayerState> TargetPlayerState;

	/** 本次恶作剧命令的幂等键；Social 用它关联冷却、保护牌和结果。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 发起交互的服务器世界位置候选；Social 会结合当前角色位置和保护牌策略复核。 */
	UPROPERTY(Transient)
	FVector InteractionLocation = FVector::ZeroVector;
};

/** 放置保护牌请求载荷；它把玩家期望位置带入 Social，由 Social 保证每人唯一保护牌。 */
UCLASS()
class CATFISHING_API UCatBodyActionRequestPlaceProtectionSign : public UObject
{
	GENERATED_BODY()

public:
	/** 本次放牌命令的幂等键；Social 用它提交或重放同一个玩家意图。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 玩家期望的保护牌位置；服务器会按当前 Pawn 和配置范围裁决最终是否允许。 */
	UPROPERTY(Transient)
	FVector SignLocation = FVector::ZeroVector;
};

/** 手动求助身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionRequestManualHelp : public UGameplayAbility
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
	/** 前摇结束后提交手动求助请求；它只调用 Social 求助入口，并根据领域结果决定正常结束还是取消。 */
	UFUNCTION()
	void CommitManualHelpAfterWindow();

	/** 当前求助动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestManualHelp> ActiveRequest;

	/** 当前求助动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};

/** 恶作剧身体动作 Ability；它自己拥有事件校验、目标解析、前摇窗口、提交和取消收尾。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionRequestMischief : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立恶作剧 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应恶作剧事件。 */
	UCatGA_BodyActionRequestMischief();

protected:
	/** 激活恶作剧动作：校验恶作剧事件与载荷，启动角色表现，并在可取消前摇后提交 Social 恶作剧请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束恶作剧动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交恶作剧请求；它只解析目标 Controller 并调用 Social，冷却和保护牌仍由 Social 裁决。 */
	UFUNCTION()
	void CommitMischiefAfterWindow();

	/** 当前恶作剧动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestMischief> ActiveRequest;

	/** 当前恶作剧动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};

/** 放置保护牌身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionPlaceProtectionSign : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 建立放牌 Ability 的网络策略、资产标签和 GameplayEvent 触发器；这条能力只响应放置保护牌事件。 */
	UCatGA_BodyActionPlaceProtectionSign();

protected:
	/** 激活放牌动作：校验保护牌事件与载荷，启动角色表现，并在可取消前摇后提交 Social 放牌请求。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 结束放牌动作：取消时停止已启动的角色表现，然后清除本 Ability 冻结的请求状态。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
		const bool bWasCancelled) override;

private:
	/** 前摇结束后提交放置保护牌请求；它只调用 Social 放牌入口，并根据领域结果决定正常结束还是取消。 */
	UFUNCTION()
	void CommitPlaceProtectionSignAfterWindow();

	/** 当前放牌动作冻结的请求对象；由激活阶段写入，提交或取消收尾时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBodyActionRequestPlaceProtectionSign> ActiveRequest;

	/** 当前放牌动作冻结的表现事件标签；开始和取消停止表现都读取同一标签，避免配置变化造成错停。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};
