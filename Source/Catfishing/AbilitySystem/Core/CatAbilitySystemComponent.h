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
	/** 食用提交后的限时效果接收端：同鱼种刷新同一 GE，异鱼并存。 */
	bool ApplyFishTimedEffectFromAuthority(const class UCatFishDefinition* Fish, FGuid RequestId);
	/** 只有食用调用方使用成长倍率；祝福保留其独立入口与时长。 */
	double ResolveEatingEffectDuration(double BaseSeconds) const;

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

	/**
	 * 身体属性播种时把 FightStamina 拉到当前 MaxFightStamina；上限未播种时 fail-closed。
	 * 只服务角色初始播种与测试夹具：2026-09-11 裁决④删掉了「每次进搏斗把主控体力补满」，
	 * 体力是跨竿资源，搏斗入口和任何终局路径都不得再调用它回满。
	 */
	bool SeedFightStaminaToMaximumFromAuthority();

	/** authority 通过正式 GameplayEffect 给力量加一次三选一增量；调用方只提交增量，不直接写属性基值。 */
	bool ApplyFishingStrengthDelta(float Delta);

	/** authority 提高（或降低）搏斗体力上限；提升时当场按同样的差值补当前体力，见升级效果页 §2。 */
	bool ApplyMaxFightStaminaDelta(float Delta);

	/** authority 增减黄色体力护盾段；负向扣盾夹到 0，正向无上限（数值成长页 §4）。 */
	bool ApplyYellowFightStaminaDelta(float Delta);

	/** 翻天时把黄色体力整段清零；过夜清空是这段护盾的唯一自然终点。 */
	bool ClearYellowFightStaminaFromAuthority();

	/** 读取当前黄色体力存量；主动查看面板与体力条黄段渲染都读这一份。 */
	float GetYellowFightStamina() const;

	/** 可出力、可扣费的总体力（点）＝绿段＋黄段；不改变两段的属性或写入契约。 */
	double GetTotalFightStamina() const;
	/** 当前总容量（点）＝绿段恢复上限＋当前黄段；黄色不会自然恢复，不是新的持久化上限。 */
	double GetTotalFightStaminaCapacity() const;

	/*
	 * 墓碑（2026-09-12）：这里原有 ApplyPoisonDelta / IsPoisonAtLeast 两个写读口，
	 * 服务的是「跨鱼累加 Poison、到阈值倒地、休息/草药按点数清毒」的渐进中毒模型。
	 * 09-12 裁决把中毒改成按鱼各配、无渐进升级（最重一档＝吃下即倒地），倒地与解除都变成布尔事实，
	 * 由 CatConditionComponent 直接裁决，这两个口连同 Poison 属性一起删除。
	 */

	/** 清理 ActorInfo 前先清输入状态；防止无占有期间失效输入句柄继续激活 Ability。 */
	virtual void ClearActorInfo() override;

protected:
	/** Ability 授予后从 SourceTags 中抽取输入标签；让 AbilitySet 成为输入绑定的唯一配置源。 */
	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;

	/** Ability 移除前同步撤销输入索引；随后再交给 GAS 父类处理 Spec 生命周期。 */
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

private:
	// 仅 authority 的鱼种到活跃 GE 索引；时长、复制、到期由 GAS 唯一持有，不跨局保存。
	TMap<FName, FActiveGameplayEffectHandle> FishTimedEffectHandles;

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

	/** 配置默认 AbilitySet 的授予句柄集合；ASC authority 写入，最终销毁时用它整组撤销输入 Ability 和初始效果。 */
	FCatGrantedAbilitySetHandles ConfiguredDefaultAbilitySetHandles;

	/** 默认 AbilitySet 是否已经由本 ASC 授予；authority 重占有时读取它避免重复 GiveAbility。 */
	bool bConfiguredDefaultAbilitySetGranted = false;

	/** 初始身体属性是否已经由本 ASC 成功播种；authority 重占有保持 true，重连新 Character 的新 ASC 重新开始。 */
	bool bInitialCharacterAttributesApplied = false;
};
