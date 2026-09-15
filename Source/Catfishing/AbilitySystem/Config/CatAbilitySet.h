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

/** AbilitySet 中的一条能力授予配置；定义类型、等级和输入策略，属性效果由独立 GrantedEffects 配置。 */
USTRUCT(BlueprintType)
struct FCatAbilitySetAbility
{
	GENERATED_BODY()

	/** 这条授予配置代表的运行时 Ability 类型；AbilitySet 读取它创建 GameplayAbilitySpec，空值会使运行时门禁 fail-closed。 */
	UPROPERTY(EditAnywhere, Category="Ability")
	TSubclassOf<UGameplayAbility> Ability;

	/** 这条配置绑定的稳定输入 GameplayTag；ASC 用它建立输入路由，空值表示该 Ability 只靠事件、被动或授予时机触发。 */
	UPROPERTY(EditAnywhere, Category="Ability")
	FGameplayTag InputTag;

	/** 授予 Ability 时写入 Spec 的等级；设计资产写入，AbilitySet 授予时读取，低于 1 不允许进入运行时。 */
	UPROPERTY(EditAnywhere, Category="Ability", meta=(ClampMin="1"))
	int32 Level = 1;

	/** 该 Ability 对输入或授予时机的响应方式；AbilitySet 用它写入策略 Tag，并校验正式 Fishing 输入的按住/离散边界。 */
	UPROPERTY(EditAnywhere, Category="Ability")
	ECatAbilityActivationPolicy ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;


};

/** AbilitySet 的独立初始效果配置；效果不依附某条 Ability，便于装备来源按自身句柄完整回收。 */
USTRUCT(BlueprintType)
struct FCatAbilitySetGameplayEffect
{
	GENERATED_BODY()
	/** 授予集合时施加给同一 ASC 的效果类型；为空或无法生成 Spec 时整组授予回滚。 */
	UPROPERTY(EditAnywhere, Category="Effect") TSubclassOf<UGameplayEffect> GameplayEffect;
	/** 效果等级；设计资产维护，运行期拒绝小于一的配置。 */
	UPROPERTY(EditAnywhere, Category="Effect", meta=(ClampMin="1")) int32 Level = 1;
};

/** 一次 AbilitySet 授予后产生的可回收句柄集合；拥有者在同一个 ASC 生命周期内用它整组撤销授予内容。 */
USTRUCT(BlueprintType)
struct FCatGrantedAbilitySetHandles
{
	GENERATED_BODY()

public:
	/** 返回本次授予得到的 AbilitySpec 句柄只读视图；调用方只观察数量和可寻址性，不能直接改内部数组。 */
	const TArray<FGameplayAbilitySpecHandle>& GetAbilitySpecHandles() const { return AbilitySpecHandles; }
	/** 判断本批句柄是否含 Ability 或 Effect；effects-only 装备集合也必须被外层当成成功授予。 */
	bool HasAnyGrantedHandle() const { return !AbilitySpecHandles.IsEmpty() || !GameplayEffectHandles.IsEmpty(); }
	/** 合并另一独立授予批次；调用方只在该批完整成功后调用，失败批次必须自行回滚。 */
	void Append(FCatGrantedAbilitySetHandles&& Other);

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

/** 可组合的来源能力资产；角色或装备在服务器授予能力和可回收效果，调用方拥有对应 ASC 的撤销句柄。 */
UCLASS(BlueprintType, Const)
class CATFISHING_API UCatAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 检查本集合的能力与效果是否可授予；不要求装备包含角色默认能力，效果必须能按来源独立回收。 */
	bool IsRuntimeReady() const;
	/** 判断角色默认能力集是否只保留 BodyAction 长期入口；鱼竿操作能力由部署实例另行授予。 */
	bool IsDefaultCharacterAbilitySetReady() const;

	/** 在服务器 ASC 上整组授予能力与初始效果；任一授予失败会回滚本次已授予内容并返回 false。 */
	bool GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
		FCatGrantedAbilitySetHandles& OutGrantedHandles, UObject* SourceObject = nullptr) const;

	/** 本来源授予的能力条目；定义或角色默认资产维护，运行时校验类型与输入无重复，空数组允许纯效果集合。 */
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<FCatAbilitySetAbility> GrantedAbilities;

	/** 与 Ability 条目独立的初始效果；装备 AbilitySet 能只提供状态效果，不必伪造无意义 Ability。 */
	UPROPERTY(EditDefaultsOnly, Category="Effects") TArray<FCatAbilitySetGameplayEffect> GrantedEffects;
};
