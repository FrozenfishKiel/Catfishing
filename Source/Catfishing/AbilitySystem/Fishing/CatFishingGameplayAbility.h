#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatFishingGameplayAbility.generated.h"

class UCatFishingCommandComponent;

/** Fishing 玩家输入 Ability 的共享基类；它统一处理本地预测、远端镜像防重入和命令组件解析。 */
UCLASS(Abstract)
class CATFISHING_API UCatFishingGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 配置 Fishing 输入 Ability 的预测和实例化策略，完成后具体输入类只需要提交自己的命令语义。 */
	UCatFishingGameplayAbility();

	/** 判断当前实例是否是服务器上的远程客户端镜像；调用方用它避免客户端 RPC 和服务器镜像重复提交同一输入。 */
	static bool ShouldWaitForRemoteClient(bool bIsNetAuthority, bool bIsLocallyControlled);

protected:
	/** 从 Ability ActorInfo 找到玩家控制器上的 Fishing 命令组件；缺少控制器或组件时返回空指针并让输入 fail-closed。 */
	UCatFishingCommandComponent* ResolveCommandComponent(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 判断当前 Ability 实例能否从本机提交 Fishing 命令；只有本地控制端且命令组件存在时才返回 true。 */
	bool CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 判断当前 Ability 是否只是服务器上的远程玩家镜像；输入 Ability 用它跳过重复提交。 */
	bool IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 结束一次性 Fishing 输入 Ability；命令已提交时正常结束，提交失败时取消本次激活。 */
	void FinishOneShot(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bSubmitted);

	/**
	 * 输入按下的本地表现钩子（挥网等语义唯一、成败都要播的即时动作）。
	 * 蓝图子类实现它播 Montage/音效；只在本地控制端、命令提交前调用，绝不携带任何玩法结果。
	 * Primary 因同一输入存在瞄准/提竿/收线三种语义，不调用此钩子；抛竿由 Hook CastFlight 复制状态驱动。
	 * 结果驱动的动画（拖拽循环、完美中鱼、断线）不属于这里，用 Snapshot/表现事件驱动。
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_OnLocalInputActivated();

	/** 输入松开的本地表现钩子（收势/松爪）；按住型 Ability 松开时调用，玩法结果仍由服务器命令回包表达。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_OnLocalInputReleased();
};
