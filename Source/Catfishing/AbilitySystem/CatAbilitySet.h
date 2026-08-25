#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/DataAsset.h"
#include "GameplayEffectTypes.h"
#include "CatAbilitySet.generated.h"

class UCatAbilitySystemComponent;
class UGameplayEffect;

/** Ability 输入激活策略；AbilitySet 用它决定授予后是等待离散输入、按住期间持续激活，还是授予时立即激活。 */
UENUM(BlueprintType)
enum class ECatAbilityActivationPolicy : uint8
{
	/** 离散输入策略；按下边沿触发一次 Ability，适合交互、取消、抢抄等单次意图。 */
	OnInputTriggered,

	/** 按住型输入策略；按下建立 held 状态、松开结束，适合拖拽、放线和打窝蓄力。 */
	WhileInputActive,

	/** 授予即激活策略；用于不依赖玩家输入的被动或初始化 Ability。 */
	OnGranted
};

/** AbilitySet 中的一条授予配置；把 Ability 类、输入 Tag、等级、激活策略和可选初始效果绑定成同一个授予单元。 */
USTRUCT(BlueprintType)
struct FCatAbilitySetAbility
{
	GENERATED_BODY()

	/** 要授予给 ASC 的 Ability 类；AbilitySet 读取它创建 GameplayAbilitySpec，空值会使运行时门禁 fail-closed。 */
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TSubclassOf<UGameplayAbility> Ability;

	/** 玩家输入对应的稳定 GameplayTag；ASC 用它建立输入到 AbilitySpec 的路由，重复 Tag 会被运行时门禁拒绝。 */
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	FGameplayTag InputTag;

	/** 授予 Ability 时写入 Spec 的等级；设计资产写入，AbilitySet 授予时读取，低于 1 不允许进入运行时。 */
	UPROPERTY(EditDefaultsOnly, Category="Ability", meta=(ClampMin="1"))
	int32 Level = 1;

	/** 该 Ability 对输入边沿的响应方式；AbilitySet 用它写入策略 Tag，并校验正式 Fishing 输入的按住/离散边界。 */
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	ECatAbilityActivationPolicy ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;

	/** 授予 Ability 时同步施加的初始 GameplayEffect；为空表示本条配置只授予 Ability，不改变属性或状态。 */
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TSubclassOf<UGameplayEffect> InitialEffect;
};

/** 一次 AbilitySet 授予后产生的可回收句柄集合；拥有者在同一个 ASC 生命周期内用它整组撤销授予内容。 */
USTRUCT(BlueprintType)
struct FCatGrantedAbilitySetHandles
{
	GENERATED_BODY()

public:
	/** 返回本次授予得到的 AbilitySpec 句柄只读视图；调用方只观察数量和可寻址性，不能直接改内部数组。 */
	const TArray<FGameplayAbilitySpecHandle>& GetAbilitySpecHandles() const { return AbilitySpecHandles; }

	/** 从服务器 ASC 上撤销本集合记录的 Ability 和 GameplayEffect；成功后清空句柄，重复调用保持无副作用。 */
	void TakeFromAbilitySystem(UCatAbilitySystemComponent* AbilitySystem);

private:
	friend class UCatAbilitySet;

	/** 本次授予成功的 AbilitySpec 句柄；AbilitySet 写入，撤销流程读取并逐个清除对应 Ability 输入路由。 */
	UPROPERTY(Transient)
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	/** 本次授予同步施加的 GameplayEffect 句柄；AbilitySet 写入，撤销流程读取并移除对应临时效果。 */
	UPROPERTY(Transient)
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;
};

/** 项目默认能力授予资产；Character authority 读取它给 ASC 建立 Fishing 输入能力和初始效果的唯一默认集合。 */
UCLASS(BlueprintType, Const)
class CATFISHING_API UCatAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 判断默认能力集是否足以支撑正式 Fishing 输入入口；缺少任一稳定输入 Tag、重复能力或策略不匹配都会 fail-closed。 */
	bool IsRuntimeReady() const;

	/** 在服务器 ASC 上整组授予能力与初始效果；任一授予失败会回滚本次已授予内容并返回 false。 */
	bool GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
		FCatGrantedAbilitySetHandles& OutGrantedHandles) const;

	/** 设计资产配置的授予条目列表；运行时门禁读取它验证六个正式 Fishing 输入能力齐全且无重复。 */
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<FCatAbilitySetAbility> GrantedAbilities;
};
