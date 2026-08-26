#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Character.h"
#include "CatCharacter.generated.h"

class UAbilitySystemComponent;
class UCatAbilitySystemComponent;
class UCatSurvivalAttributeSet;
class UCatContainerReplicationComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
class UCatGrowthComponent;

/**
 * Lake 的唯一玩法身体；同时宿主 Character-owned ASC、Condition、Growth、Equipment 与个人鱼护复制出口。
 * Character 同时作为 ASC Owner/Avatar；丢失占有或销毁时先收口 Fishing/Social，所有局内事实都不上移到 Profile/Online。
 */
UCLASS()
class CATFISHING_API ACatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** 构造 ASC/属性集、Condition、Growth、Equipment 与个人鱼护出口，开启组件复制但不在 CDO 写任何运行数值。 */
	ACatCharacter();

	/** 返回 Character 持有的唯一 ASC；runtime gate 关闭也返回组件，让外部只读接缝不需要第二条查找路径。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UCatAbilitySystemComponent* GetCatAbilitySystemComponent() const;

	/** 返回 authority 为本 Character 注册的一局个人鱼护 ID；未注册时为无效 GUID。 */
	FGuid GetPersonalFishGuardId() const;

	/** 返回 Character 唯一离散身体状态组件；Wet/Downed/恢复不进入 PlayerState 或 Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Survival")
	UCatConditionComponent* GetConditionComponent() const;

	/** 返回 Character 唯一本局吃鱼成长组件；经验槽和待选次数不进入 PlayerState 或 Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Growth")
	UCatGrowthComponent* GetGrowthComponent() const;

	/** 返回 Character 唯一一局装备组件；永久解锁/选择仍在 LocalPlayer Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	UCatEquipmentComponent* GetEquipmentComponent() const;

	/** 返回该角色选择的猫种类定义 ID；None 表示使用全局 CatAbilitySettings 初值。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Character")
	FName GetCatDefinitionId() const { return CatDefinitionId; }

	/**
	 * 该角色使用的猫种类定义 ID（在角色蓝图 Details 里配置，或换皮子类各选一种）。
	 * 必须与 CatAbilitySettings.CharacterDefinitions 里某个 DA 的 CatDefinitionId 一致；
	 * 留空时使用全局初始属性；填了但找不到定义时属性播种会 fail-closed 并留下 Warning 日志。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Character")
	FName CatDefinitionId = NAME_None;

	/**
	 * 权威侧广播一次性表现事件到所有客户端，唯一调用方是 UCatFishingCommandComponent 的权威处理路径。
	 *
	 * 为什么需要这条通道：挥网落空、提竿空竿这类动作**失败时不产生任何权威状态变化**，
	 * 表现层没有可读的复制事实；挥网可由 Ability 在本地预测，提竿则必须等服务器先判明 Primary 的真实语义，
	 * 所以两者都由 authority 广播，而接收端按标签决定是否跳过已经预测过的本地动作。
	 * 放竿/收竿/断竿/抛竿/打窝都有各自的复制状态与表现事件，不要走这条，否则同一动作会播两遍。
	 *
	 * Unreliable：纯装饰，丢包只是少看一次挥网，不影响任何玩法事实，不值得占用可靠通道。
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCosmeticEvent(FGameplayTag EventTag);

	/**
	 * 一次性表现事件的蓝图落点；用 Switch on Gameplay Tag 分派到各自的 Montage/音效/特效。
	 * 已由 Ability 本地预测的事件会跳过发起端；提竿这类服务器裁决后才知道语义的事件也会发给本地端。
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_PlayCosmeticEvent(FGameplayTag EventTag);

	/**
	 * 服务器 BodyAction Ability 广播的长动作表现开始事件。
	 * BodyAction 没有客户端预测实例，所以本地玩家和旁观玩家都必须收到；蓝图按 Command 或 PresentationEventTag 播 Montage、音效或特效。
	 * 该事件是 Unreliable，因为它只表达可丢弃的外观提示；正式状态仍来自服务端命令结果和复制快照，蓝图不得依赖它保存不可恢复状态。
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBodyActionPresentation(ECatBodyActionAbilityCommand Command, FGameplayTag PresentationEventTag);

	/**
	 * 服务器 BodyAction Ability 广播的长动作表现停止事件。
	 * 只有提交窗口内取消、领域入口拒绝或 Ability 异常取消时触发，用来让蓝图停掉循环 Montage 或清掉正在播的前摇特效。
	 * 该事件是 Reliable，因为收到开始表现的客户端必须收到停止信号；正式循环表现仍应保留 Montage 自身或 AnimBP 超时兜底。
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopBodyActionPresentation(ECatBodyActionAbilityCommand Command, FGameplayTag PresentationEventTag);

	/** BodyAction 表现开始的蓝图落点；C++ 只负责广播和可选 Montage，正式表现由角色蓝图决定。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_PlayBodyActionPresentation(ECatBodyActionAbilityCommand Command, FGameplayTag PresentationEventTag);

	/** BodyAction 表现停止的蓝图落点；蓝图可按同一 Command 或标签停止循环、淡出特效或重置动作层。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_StopBodyActionPresentation(ECatBodyActionAbilityCommand Command, FGameplayTag PresentationEventTag);

	/**
	 * 从 BodyAction 表现配置读取并播放可选 Montage。
	 * 返回值只说明本机是否播到了动画；没有正式 Montage 时仍会触发 BP_PlayBodyActionPresentation。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool PlayBodyActionMontageFromPresentation(ECatBodyActionAbilityCommand Command);

	/**
	 * 从 BodyAction 表现配置读取并停止可选 Montage。
	 * 返回值只说明本机是否找到了需要停止的配置；蓝图停止事件仍会被广播，用于处理非 Montage 表现。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool StopBodyActionMontageFromPresentation(ECatBodyActionAbilityCommand Command);

	/**
	 * 由已复制的 Hook CastFlight 表现状态调用，在本机这份角色 Mesh 上播放配置的抛竿 Montage。
	 * 只负责动画，不提交命令、不改会话，也不发送 RPC；每台客户端各播一次。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool PlayFishingCastMontageFromPresentation();

protected:
	/** 组件注册完成后幂等刷新 Owner/Avatar；未裁 runtime 会清除引擎自动建立的 ActorInfo 并保持 fail-closed。 */
	virtual void BeginPlay() override;

	/** 父类完成占有后刷新 Owner/Avatar 和一次初值；authority 才授正式 AbilitySet 并注册个人鱼护，客户端不 GiveAbility。 */
	virtual void PossessedBy(AController* NewController) override;

	/** Controller 复制变化后刷新拥有客户端 ActorInfo；Controller 失效时 ClearActorInfo，不保留失效 Avatar。 */
	virtual void OnRep_Controller() override;

	/** 本地 Pawn 重启后刷新 ActorInfo；正式输入由 PlayerController 的 AbilityInputConfig 负责。 */
	virtual void PawnClientRestart() override;

	/** 失去占有前先收口 Fishing/Social 并取消 Ability；父类断开 Controller 后才清 ActorInfo。 */
	virtual void UnPossessed() override;

	/** Actor 离开 World 时幂等收口 Fishing/Social、解注册鱼护，再取消 Ability/清 ActorInfo，最后交父类销毁。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 按正式 ASC gate 初始化 Character=this 的 Owner/Avatar；未裁复制策略时主动 Clear 而不是猜 Mixed。 */
	void InitializeAbilityActorInfo();

	/** authority 在 ActorInfo 就绪后授予一次正式默认 AbilitySet；缺资产或未就绪时保持 fail-closed。 */
	void GrantDefaultAbilitySetOnce();

	/** authority 新 Character 首次 ActorInfo 就绪时整体应用三项显式初值；重占有不重置已消耗的局内状态。 */
	void ApplyInitialAttributesOnce();

	/** authority 从 PlayerState::UniqueId 注册个人鱼护；StableNetId 只进入 Items 私有记录，不进入复制组件。 */
	void RegisterPersonalFishGuard();

	/** 受控 Starter 兜底入口；正式默认关闭，只在设置显式打开时为仍为空的 Loadout 走一次权威校验装配。 */
	void ApplyStarterLoadoutIfConfigured();

	/** 在失去占有或销毁前终止本 Character 参与的钓鱼与偷鱼协议，随后才允许身体和 ASC 清理。 */
	void NotifyFishingOwnerUnavailable();

	/** 猫身体唯一 AbilitySystemComponent；构造期创建、随 Character 复制，并拥有本 Actor 的 AttributeSet 与 Ability。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatAbilitySystemComponent> AbilitySystemComponent;

	/** 猫身体唯一 Survival 属性集；构造期显式交给 ASC 持有 Poison、FishingStrength 和 FightStamina 三项真相。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSurvivalAttributeSet> SurvivalAttributes;

	/** 个人鱼护的只读复制出口；数组只由 authority Items Service 提交，本 Character 不提供写口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatContainerReplicationComponent> PersonalFishGuard;

	/** 猫身体唯一离散状态组件；复制 Wet/Downed/Recovery，数值仍由 Survival AttributeSet 拥有。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatConditionComponent> ConditionComponent;

	/** 猫身体唯一吃鱼成长组件；复制经验槽与待选次数，Buff 内容未裁时不生成第二套效果状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Growth", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatGrowthComponent> GrowthComponent;

	/** 一局功能型装配、耗材与鱼竿耐久宿主；没有等级、词条、战力或偷取接口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatEquipmentComponent> EquipmentComponent;

	/** 本 Character 一局内的个人鱼护 ID；由 authority 首次注册生成，不复制成第二份容器快照。 */
	FGuid PersonalFishGuardId;

	/** authority 首次正式授予的默认集合句柄；重占有保留，Character 最终销毁时成组撤销。 */
	FCatGrantedAbilitySetHandles DefaultAbilitySetHandles;

	/** 默认 AbilitySet 是否已正式授予；只由 authority 读写，避免重占有重复 GiveAbility。 */
	bool bDefaultAbilitySetGranted = false;

	/** 本 Character 是否已经整体应用过初始属性；只在 authority 写，重占有保持 true，重连新身体重新开始。 */
	bool bInitialAttributesApplied = false;
};
