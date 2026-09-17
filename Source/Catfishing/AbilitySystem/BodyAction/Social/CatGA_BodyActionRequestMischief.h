#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatGA_BodyActionRequestMischief.generated.h"

class APlayerState;

/** 恶作剧请求载荷；它把目标玩家和交互位置带入 Social 裁决，不在 Controller 里写冷却。 */
USTRUCT()
struct CATFISHING_API FCatBodyActionRequestMischiefTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

public:
	/** 被恶作剧的目标玩家状态；提交时会在当前 World 重新定位对应 Controller。 */
	UPROPERTY(Transient)
	TObjectPtr<APlayerState> TargetPlayerState;

	/** 本次恶作剧命令的幂等键；Social 用它关联冷却、保护牌和结果。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 发起者提供的世界坐标位置候选；Social 会结合当前角色位置和保护牌策略复核。 */
	UPROPERTY(Transient)
	FVector InteractionLocation = FVector::ZeroVector;
	/** 事件接收方据此确认本动作的目标数据类型。 */
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	/** 随 GAS 预测激活传输本地请求参数；此处仅序列化，服务器能力与领域服务继续验证请求。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FCatBodyActionRequestMischiefTargetData> : TStructOpsTypeTraitsBase2<FCatBodyActionRequestMischiefTargetData>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

/** 恶作剧身体动作 Ability；它自己拥有事件校验、目标解析、前摇窗口、提交和取消收尾。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionRequestMischief : public UCatGameplayAbility
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
	/** 前摇结束后提交恶作剧请求；服务器解析目标和服务后提交 GAS 成本与冷却，再调用 Social；恶作剧领域冷却与保护牌资格仍由 Social 裁决。 */
	UFUNCTION()
	void CommitMischiefAfterWindow();

	/** 当前恶作剧动作冻结的请求参数；由激活阶段写入，提交或取消收尾时重置。 */
	UPROPERTY(Transient)
	FCatBodyActionRequestMischiefTargetData ActiveRequest;

	/** 动画中断时取消能力，提交前取消不会进入领域写口。 */
	UFUNCTION()
	void CancelAction();
};
