#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatGA_BodyActionPlaceProtectionSign.generated.h"

class APlayerState;

/** 放置保护牌请求载荷；它把玩家期望位置带入 Social，由 Social 保证每人唯一保护牌。 */
USTRUCT()
struct CATFISHING_API FCatBodyActionRequestPlaceProtectionSignTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

public:
	/** 本次放牌命令的幂等键；Social 用它提交或重放同一个玩家意图。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 玩家期望的保护牌位置；服务器会按当前 Pawn 和配置范围裁决最终是否允许。 */
	UPROPERTY(Transient)
	FVector SignLocation = FVector::ZeroVector;
	/** 事件接收方据此确认本动作的目标数据类型。 */
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	/** 传输服务器已接受的请求参数，让拥有者客户端启动同一表现任务。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FCatBodyActionRequestPlaceProtectionSignTargetData> : TStructOpsTypeTraitsBase2<FCatBodyActionRequestPlaceProtectionSignTargetData>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

/** 放置保护牌身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionPlaceProtectionSign : public UCatGameplayAbility
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

	/** 当前放牌动作冻结的请求参数；由激活阶段写入，提交或取消收尾时重置。 */
	UPROPERTY(Transient)
	FCatBodyActionRequestPlaceProtectionSignTargetData ActiveRequest;

	/** 动画中断时取消能力，提交前取消不会进入领域写口。 */
	UFUNCTION()
	void CancelAction();
};
