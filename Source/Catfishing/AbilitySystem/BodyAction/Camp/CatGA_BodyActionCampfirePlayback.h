#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "CatGA_BodyActionCampfirePlayback.generated.h"

class ACatCampHubActor;

/** 篝火回看请求载荷；它只把目标营地和请求键带过 GAS 前摇，CapturePlan 与表现仍归 Camp。 */
USTRUCT()
struct CATFISHING_API FCatBodyActionRequestCampfirePlaybackTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

public:
	/** 负责本次回看的固定营地；提交阶段会验证 World 和营地内部 CapturePlan 条件。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCampHubActor> Camp;

	/** 本次回看命令的幂等键；Camp 结果和 owning-client 回执都使用同一个键。 */
	UPROPERTY(Transient)
	FGuid RequestId;
	/** 事件接收方据此确认本动作的目标数据类型。 */
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	/** 传输服务器已接受的请求参数，让拥有者客户端启动同一表现任务。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FCatBodyActionRequestCampfirePlaybackTargetData> : TStructOpsTypeTraitsBase2<FCatBodyActionRequestCampfirePlaybackTargetData>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

/** 篝火回看身体动作 Ability；它自己拥有事件校验、前摇窗口、提交和取消收尾，不把流程交给共享父类。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionCampfirePlayback : public UCatGameplayAbility
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

	/** 当前回看动作冻结的请求参数；由激活阶段写入，提交或取消收尾时重置。 */
	UPROPERTY(Transient)
	FCatBodyActionRequestCampfirePlaybackTargetData ActiveRequest;

	/** 动画中断时取消能力，提交前取消不会进入领域写口。 */
	UFUNCTION()
	void CancelAction();
};
