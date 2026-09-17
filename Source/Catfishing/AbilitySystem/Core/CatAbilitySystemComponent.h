#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "CatAbilitySystemComponent.generated.h"

/** 角色持有的 GAS 入口；管理输入边沿、默认授予及按来源回收的持续状态，权威事实保存在 Spec 和活动 GE 中。 */
/** Character 持有的公共 GAS 入口；管理输入、默认授予及状态来源句柄，资源数值仍由效果和属性集保存。 */
UCLASS()
class CATFISHING_API UCatAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	/** 服务器按来源键替换整组状态；键在同一 ASC 内唯一。空集合撤销该来源；失败返回 false 并保留旧效果，其他来源不受影响。 */
	bool SetStateTagsFromAuthority(FName Source, const FGameplayTagContainer& Tags);
	/** 撤销本 ASC 管理的全部状态来源；最终销毁时调用，不影响其他系统拥有的 GE。 */
	void ClearStateSourcesFromAuthority();
	/** 组件退出时清除来源效果，然后交由 GAS 完成剩余回收。 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** 从任意 Actor 解析项目 ASC；调用方只拿到 Cat ASC 能力面，不需要知道当前身体类如何实现 AbilitySystemInterface。 */
	static UCatAbilitySystemComponent* FindCatAbilitySystemFromActor(AActor* Actor);

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


	/** authority 清空当天黄色体力；清晨流程调用，不改变绿色体力。 */
	bool ClearYellowFightStaminaFromAuthority();

	/** 读取当前黄色储备；HUD 与搏斗结算共用属性快照。 */
	float GetYellowFightStamina() const;

	/** 搏斗支付预检使用的可用余额读取口；绿段与黄段都能支付，因此合并两者当前值，不把绿色上限当成余额。 */
	double GetTotalFightStamina() const;

	/** HUD 总容量读取口；黄色储备没有独立上限，以当前储备加绿色上限表达此刻容量，避免展示不存在的黄段恢复空间。 */
	double GetTotalFightStaminaCapacity() const;

	/** 清理 ActorInfo 前先清输入状态；防止无占有期间失效输入句柄继续激活 Ability。 */
	virtual void ClearActorInfo() override;

protected:
	/** Ability 移除前清除其输入边沿；随后再交给 GAS 父类处理 Spec 生命周期。 */
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

private:
	/** 状态来源到独立 GE 的唯一所有权记录；标签内容读取效果 Spec，不再缓存一份状态。 */
	TMap<FName, FActiveGameplayEffectHandle> StateEffects;
	/** 倒地状态监听只在首次 ActorInfo 初始化时绑定，重占有不重复注册。 */
	FDelegateHandle DownedTagHandle;
	/** 首次进入倒地时取消明确带倒地中断标签的能力；求助不受影响，角色物理及钓鱼退出由既有消费者处理。 */
	void HandleDownedTagChanged(FGameplayTag Tag, int32 Count);

	/** 本帧刚按下的 Ability Spec；帧末清空，不跨帧保存边沿。 */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** 本帧刚释放的 Ability Spec；帧末清空，不参与持续激活判断。 */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	/** 当前仍被按住的 Ability Spec；WhileInputActive Ability 依赖它在后续帧保持激活。 */
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	/** 配置默认 AbilitySet 的授予句柄集合；ASC authority 写入，最终销毁时用它整组撤销输入 Ability 和初始效果。 */
	FCatGrantedAbilitySetHandles ConfiguredDefaultAbilitySetHandles;


	/** 初始身体属性是否已经由本 ASC 成功播种；authority 重占有保持 true，重连新 Character 的新 ASC 重新开始。 */
	bool bInitialCharacterAttributesApplied = false;
};
