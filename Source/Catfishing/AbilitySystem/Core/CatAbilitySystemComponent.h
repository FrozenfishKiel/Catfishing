#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "CatAbilitySystemComponent.generated.h"

UCLASS()
class CATFISHING_API UCatAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	/** 从任意 Actor 解析项目 ASC；调用方只拿到 Cat ASC 能力面，不需要知道当前身体类如何实现 AbilitySystemInterface。 */
	static UCatAbilitySystemComponent* FindCatAbilitySystemFromActor(AActor* Actor);

	/** 记录某个 Ability Spec 对应的输入标签与激活策略；AbilitySet 授予时写入，输入帧处理时读取。 */
	void RegisterAbilityInput(FGameplayAbilitySpecHandle Handle, FGameplayTag InputTag,
		ECatAbilityActivationPolicy ActivationPolicy);

	/** 移除某个 Ability Spec 的输入索引；Ability 被撤销或生命周期结束时调用，防止失效句柄继续响应输入。 */
	void UnregisterAbilityInput(FGameplayAbilitySpecHandle Handle);

	/** 记录当前帧按下的输入标签；真正激活在 ProcessAbilityInput 中统一发生，便于预测事件成对发送。 */
	void AbilityInputTagPressed(FGameplayTag InputTag);

	/** 记录当前帧松开的输入标签；持续激活 Ability 会在帧处理里收到标准 Released 事件。 */
	void AbilityInputTagReleased(FGameplayTag InputTag);

	/** 消费本帧输入边沿并按 Ability 激活策略触发或转发事件；暂停时保留边沿，避免 UI 暂停吃掉玩家输入。 */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);

	/** 清空所有输入边沿和持续按住集合；占有丢失、ActorInfo 清理和重置失败时使用。 */
	void ResetAbilityInput();

	/** authority 取消当前仍在提交窗口内的 BodyAction Ability；Fishing Cancel 服务器入口用它中止未落地的身体动作。 */
	bool CancelBodyActionAbilitiesFromAuthority();

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试读取本帧 Press 边沿数量；只在开发测试构建暴露，正式运行不依赖该观察口。 */
	int32 GetPressedInputCount() const { return InputPressedSpecHandles.Num(); }

	/** 自动化测试读取本帧 Release 边沿数量；只用于验证输入生命周期是否成对清理。 */
	int32 GetReleasedInputCount() const { return InputReleasedSpecHandles.Num(); }

	/** 自动化测试读取持续按住句柄数量；只用于覆盖重复占有和释放路径。 */
	int32 GetHeldInputCount() const { return InputHeldSpecHandles.Num(); }
#endif

	/** authority 通过正式 GameplayEffect 修改搏斗体力；调用方只提交增减量，不直接写 AttributeSet。 */
	bool ApplyFishingStaminaDelta(float Delta);

	/** Character-owned ASC 的运行 gate 与 Owner/Avatar 建立入口；配置未启用时会清除 ActorInfo，成功后后续 Ability 和属性写入才允许继续。 */
	bool InitializeCharacterOwnerAvatar(AActor* CharacterOwnerAvatar);

	/** authority 在 ActorInfo 就绪后授予一次正式默认 AbilitySet；缺资产或未就绪时保持 fail-closed，重占有不会重复授予。 */
	bool GrantConfiguredDefaultAbilitySetFromAuthority();

	/** Character 最终销毁时撤销由配置默认授予的 AbilitySet；未授予或客户端空句柄调用保持无副作用。 */
	void RevokeConfiguredDefaultAbilitySet();

	/** Character 的 ActorInfo 就绪后按 CatDefinitionId 整体播种身体初始属性；仅 authority 写入，已成功播种后重占有不会重置局内消耗。 */
	bool InitializeCharacterAttributesFromDefinition(FName CatDefinitionId);

	/** 按 ASC 当前 MaxFightStamina 把 FightStamina 回满到本次搏斗上限；上限未播种时 fail-closed。 */
	bool InitializeFishingStaminaForSession();

	/** 请求下一次 ActorInfo 可用时重置搏斗体力；无 Avatar 时保留待处理标记。 */
	bool RequestFishingStaminaReset();

	/** 在新搏斗开始前确认 FightStamina 与 MaxFightStamina 都可用；待重置未完成或数值非正都会拒绝进入会话。 */
	bool EnsureFishingStaminaReadyForNewSession();

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试读取待重置标记；正式玩法只通过 EnsureFishingStaminaReadyForNewSession 观察结果。 */
	bool HasPendingFishingStaminaReset() const { return bPendingFishingStaminaReset; }
#endif

	/** authority 通过正式 GameplayEffect 修改 Poison；负向恢复会夹到 0，避免调用方直接写属性基值。 */
	bool ApplyPoisonDelta(float Delta);

	/** 读取 Poison 是否达到给定阈值；Condition 用它裁决 Downed，但不直接知道 AttributeSet 字段。 */
	bool IsPoisonAtLeast(float Threshold) const;

	/** 建立或刷新 GAS Owner/Avatar；若存在待处理体力重置，会在 ActorInfo 恢复后补做一次。 */
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;

	/** 清理 ActorInfo 前先清输入状态；防止无占有期间失效输入句柄继续激活 Ability。 */
	virtual void ClearActorInfo() override;

protected:
	/** Ability 授予后从 SourceTags 中抽取输入标签；让 AbilitySet 成为输入绑定的唯一配置源。 */
	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;

	/** Ability 移除前同步撤销输入索引；随后再交给 GAS 父类处理 Spec 生命周期。 */
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

private:
	/** 输入标签到 Ability Spec 的索引；PlayerController 只提交标签，具体 Ability 由此处解析。 */
	TMap<FGameplayTag, TArray<FGameplayAbilitySpecHandle>> SpecHandlesByInputTag;

	/** Ability Spec 到激活策略的索引；ProcessAbilityInput 用它区分点按、按住和授予即激活。 */
	TMap<FGameplayAbilitySpecHandle, ECatAbilityActivationPolicy> ActivationPolicyByHandle;

	/** 本帧刚按下的 Ability Spec；帧末清空，不跨帧保存边沿。 */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** 本帧刚释放的 Ability Spec；帧末清空，不参与持续激活判断。 */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	/** 当前仍被按住的 Ability Spec；WhileInputActive Ability 依赖它在后续帧保持激活。 */
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	/** FightStamina 等待 ActorInfo 恢复后按 MaxFightStamina 回满的标记；只由 authority 生命周期和会话入口读写。 */
	bool bPendingFishingStaminaReset = false;

	/** 配置默认 AbilitySet 的授予句柄集合；ASC authority 写入，最终销毁时用它整组撤销输入 Ability 和初始效果。 */
	FCatGrantedAbilitySetHandles ConfiguredDefaultAbilitySetHandles;

	/** 默认 AbilitySet 是否已经由本 ASC 授予；authority 重占有时读取它避免重复 GiveAbility。 */
	bool bConfiguredDefaultAbilitySetGranted = false;

	/** 初始身体属性是否已经由本 ASC 成功播种；authority 重占有保持 true，重连新 Character 的新 ASC 重新开始。 */
	bool bInitialCharacterAttributesApplied = false;
};
