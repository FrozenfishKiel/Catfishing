#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/CatAbilitySet.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameFramework/Character.h"
#include "CatCharacter.generated.h"

class UAbilitySystemComponent;
class UCatAbilitySystemComponent;
class UCatSurvivalAttributeSet;
class UCatContainerReplicationComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;

/**
 * Lake 的唯一玩法身体；同时宿主 Character-owned ASC/五属性、Condition、Equipment 与个人鱼护复制出口。
 * Character 同时作为 ASC Owner/Avatar；丢失占有或销毁时先收口 Fishing/Social，所有局内事实都不上移到 Profile/Online。
 */
UCLASS()
class CATFISHING_API ACatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** 构造 ASC/五属性集、Condition、Equipment 与个人鱼护出口，开启组件复制但不在 CDO 写任何运行数值。 */
	ACatCharacter();

	/** 返回 Character 持有的唯一 ASC；即使阶段 C gate 关闭也返回组件，让外部只读接缝不需要第二条查找路径。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UCatAbilitySystemComponent* GetCatAbilitySystemComponent() const;

	/** 返回 authority 为本 Character 注册的一局个人鱼护 ID；未注册时为无效 GUID。 */
	FGuid GetPersonalFishGuardId() const;

	/** 返回 Character 唯一离散身体状态组件；Wet/Downed/恢复不进入 PlayerState 或 Profile。 */
	UCatConditionComponent* GetConditionComponent() const;

	/** 返回 Character 唯一一局装备组件；永久解锁/选择仍在 LocalPlayer Profile。 */
	UCatEquipmentComponent* GetEquipmentComponent() const;

protected:
	/** 组件注册完成后幂等刷新 Owner/Avatar；未裁 runtime 会清除引擎自动建立的 ActorInfo 并保持 fail-closed。 */
	virtual void BeginPlay() override;

	/** 父类完成占有后刷新 Owner/Avatar 和一次初值；authority 才授诊断 Ability 并注册个人鱼护，客户不 GiveAbility。 */
	virtual void PossessedBy(AController* NewController) override;

	/** Controller 复制变化后刷新拥有客户端 ActorInfo；Controller 失效时先移除自有 MappingContext 再 ClearActorInfo。 */
	virtual void OnRep_Controller() override;

	/** 本地 Pawn 重启后刷新 ActorInfo，并以 remove-own/add-own 顺序重装唯一临时 MappingContext。 */
	virtual void PawnClientRestart() override;

	/** 只在 EnhancedInputComponent 上绑定软配置的临时 InputAction；无资产或 gate 关闭时不创建传统输入旁路。 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 失去占有前先收口 Fishing/Social，移除自有 MappingContext 并取消 Ability；父类断开 Controller 后才清 ActorInfo。 */
	virtual void UnPossessed() override;

	/** Actor 离开 World 时幂等收口 Fishing/Social、解注册鱼护，再移输入/取消 Ability/清 ActorInfo，最后交父类销毁。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 按 Stage C fail-closed 配置初始化 Character=this 的 Owner/Avatar；未裁复制策略时主动 Clear 而不是猜 Mixed。 */
	void InitializeAbilityActorInfo();

	/** authority 在 ActorInfo 就绪后授予一次诊断 Ability；有效 SpecHandle 是唯一去重事实，重占有不会重复授予。 */
	void GrantStageCTestAbility();
	void GrantDefaultAbilitySetOnce();

	/** authority 新 Character 首次 ActorInfo 就绪时整体应用五项显式初值；重占有不重置已消耗的局内状态。 */
	void ApplyInitialAttributesOnce();

	/** 为当前本地拥有者重装临时 MappingContext；只移除本 Character 自己曾添加的实例，不调用 ClearAllMappings。 */
	void RefreshProvisionalMappingContext();

	/** 从保存的 LocalPlayer 输入子系统成对移除自有 MappingContext；旧 World 或空引用下重复调用安全。 */
	void RemoveProvisionalMappingContext();

	/** 临时 InputAction 的触发入口；只调用 ASC TryActivateAbilityByClass，让 ServerOnly Ability 决定最终执行。 */
	void HandleDiagnosticAbilityInput();

	/** authority 从 PlayerState::UniqueId 注册个人鱼护；StableNetId 只进入 Items 私有记录，不进入复制组件。 */
	void RegisterPersonalFishGuard();

	/** 在失去占有或销毁前终止本 Character 参与的钓鱼与偷鱼协议，随后才允许身体和 ASC 清理。 */
	void NotifyFishingOwnerUnavailable();

	/** 猫身体唯一 AbilitySystemComponent；构造期创建、随 Character 复制，并拥有本 Actor 的 AttributeSet 与 Ability。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatAbilitySystemComponent> AbilitySystemComponent;

	/** 猫身体唯一 Survival 属性集；ASC 自动发现并持有 Hunger/Fatigue/Poison/FishingStrength/FightStamina 五项局内真相。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSurvivalAttributeSet> SurvivalAttributes;

	/** 个人鱼护的只读复制出口；数组只由 authority Items Service 提交，本 Character 不提供写口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatContainerReplicationComponent> PersonalFishGuard;

	/** 猫身体唯一离散状态组件；复制 Wet/Downed/Recovery，数值仍由 Survival AttributeSet 拥有。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatConditionComponent> ConditionComponent;

	/** 一局功能型装配、耗材与鱼竿耐久宿主；没有等级、词条、战力或偷取接口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatEquipmentComponent> EquipmentComponent;

	/** 本 Character 一局内的个人鱼护 ID；由 authority 首次注册生成，不复制成第二份容器快照。 */
	FGuid PersonalFishGuardId;

	/** 服务器已授予的阶段 C 诊断 Ability 句柄；无效表示尚未授予，Character 销毁时随 ASC 一起释放。 */
	FGameplayAbilitySpecHandle StageCTestAbilityHandle;

	/** authority 首次正式授予的默认集合句柄；重占有保留，Character 最终销毁时成组撤销。 */
	FCatGrantedAbilitySetHandles DefaultAbilitySetHandles;
	bool bDefaultAbilitySetGranted = false;

	/** 本 Character 是否已经整体应用过初始属性；只在 authority 写，重占有保持 true，重连新身体重新开始。 */
	bool bInitialAttributesApplied = false;

	/** 本 Character 最近一次成功添加的临时 MappingContext；只为成对 Remove 保留，不代表最终键位配置。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;

	/** 实际接收 AddMappingContext 的 LocalPlayer 子系统弱引用；跨 World 清理时不强持 LocalPlayer 生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;
};
