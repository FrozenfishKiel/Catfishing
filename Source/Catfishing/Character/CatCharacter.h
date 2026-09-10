#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Character.h"
#include "CatCharacter.generated.h"

class UAbilitySystemComponent;
class UCatAbilitySystemComponent;
class UCatSurvivalAttributeSet;
class UCatConditionComponent;
class UCatEquipmentComponent;
class UCatInventoryComponent;
class UCatGrowthComponent;
class UCatFishingCameraComponent;

/**
 * Lake 的唯一玩法身体；同时宿主 Character-owned ASC、Condition、Growth、Inventory 与 Equipment。
 * Character 同时作为 ASC Owner/Avatar；丢失占有或销毁只处理身体和组件生命周期，跨系统会话由 GameMode 等 authority 协调者收口。
 * 鱼护是独立箱子式库存对象，不由 Character 创建、注册或复制；鱼以外的随身物品由 Inventory 承载，Equipment 只保存钓鱼选择读模型。
 */
UCLASS()
class CATFISHING_API ACatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** 构造 ASC/属性集、Condition、Growth、Inventory 与 Equipment，开启组件复制但不在 CDO 写任何运行数值。 */
	ACatCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	/** 上鱼时由 Fishing 表现提供持杆第一人称；其余时间保留角色蓝图的相机。 */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	/** 返回 Character 持有的唯一 ASC；runtime gate 关闭也返回组件，让外部只读接缝不需要第二条查找路径。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UCatAbilitySystemComponent* GetCatAbilitySystemComponent() const;

	/** 返回 Character 唯一离散身体状态组件；Wet/Downed/恢复不进入 PlayerState 或 Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Survival")
	UCatConditionComponent* GetConditionComponent() const;

	/** 返回 Character 唯一本局吃鱼成长组件；经验槽和待选次数不进入 PlayerState 或 Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Growth")
	UCatGrowthComponent* GetGrowthComponent() const;

	/** 返回 Character 唯一一局装备组件；永久解锁/选择仍在 LocalPlayer Profile。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	UCatEquipmentComponent* GetEquipmentComponent() const;

	/** 返回 Character 正式随身库存组件；拾取、商店、营地发货、保存和加载都围绕它提交。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	UCatInventoryComponent* GetInventoryComponent() const;

	/** 猫种类定义 ID 是角色蓝图选择身体数值模板的稳定键；为空时角色类不猜默认值，而由能力系统配置在播种属性时解析。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Character")
	FName GetCatDefinitionId() const { return CatDefinitionId; }

	/**
	 * 该角色使用的猫种类定义 ID（在角色蓝图 Details 里配置，或换皮子类各选一种）。
	 * 必须与能力系统配置里的猫种类清单一致；
	 * 留空时使用能力系统配置的正式默认猫种；显式 ID 或默认 ID 找不到定义时属性播种会 fail-closed。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Character")
	FName CatDefinitionId = NAME_None;

	/**
	 * 权威侧广播一次性表现事件到所有客户端。输入动作由 UCatFishingCommandComponent 发出；
	 * 切线/落水由 ACatFishingSession 在首次写入终局后发出。
	 *
	 * 为什么需要这条通道：挥网落空、提竿空竿这类动作**失败时不产生任何权威状态变化**，
	 * 表现层没有可读的复制事实；挥网可由 Ability 在本地预测，提竿则必须等服务器先判明 Primary 的真实语义，
	 * 所以两者都由 authority 广播，而接收端按标签决定是否跳过已经预测过的本地动作。
	 * 放竿/收竿/抛竿/打窝都有各自的复制状态与表现事件，不要走这条，否则同一动作会播两遍。
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
	 * BodyAction 没有客户端预测实例，所以本地玩家和旁观玩家都必须收到；蓝图按动作标签与表现标签播 Montage、音效或特效。
	 * 该事件是 Unreliable，因为它只表达可丢弃的外观提示；正式状态仍来自服务端命令结果和复制快照，蓝图不得依赖它保存不可恢复状态。
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBodyActionPresentation(FGameplayTag BodyActionEventTag, FGameplayTag PresentationEventTag);

	/**
	 * 服务器 BodyAction Ability 广播的长动作表现停止事件。
	 * 只有提交窗口内取消、领域入口拒绝或 Ability 异常取消时触发，用来让蓝图停掉循环 Montage 或清掉正在播的前摇特效。
	 * 该事件是 Reliable，因为收到开始表现的客户端必须收到停止信号；正式循环表现仍应保留 Montage 自身或 AnimBP 超时保护。
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopBodyActionPresentation(FGameplayTag BodyActionEventTag, FGameplayTag PresentationEventTag);

	/** BodyAction 表现开始的蓝图落点；动作标签标识本次生命周期，表现标签标识美术分派键。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_PlayBodyActionPresentation(FGameplayTag BodyActionEventTag, FGameplayTag PresentationEventTag);

	/** BodyAction 表现停止的蓝图落点；蓝图以同一对标签停止循环、淡出特效或重置动作层。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_StopBodyActionPresentation(FGameplayTag BodyActionEventTag, FGameplayTag PresentationEventTag);

	/**
	 * 从 BodyAction 表现配置读取并播放可选 Montage。
	 * 返回值只说明本机是否播到了动画；没有正式 Montage 时仍会触发 BP_PlayBodyActionPresentation。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool PlayBodyActionMontageFromPresentation(FGameplayTag BodyActionEventTag);

	/**
	 * 从 BodyAction 表现配置读取并停止可选 Montage。
	 * 返回值只说明本机是否找到了需要停止的配置；蓝图停止事件仍会被广播，用于处理非 Montage 表现。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool StopBodyActionMontageFromPresentation(FGameplayTag BodyActionEventTag);

	/**
	 * 由已复制的 Hook CastFlight 表现状态调用，在本机这份角色 Mesh 上播放配置的抛竿 Montage。
	 * 只负责动画，不提交命令、不改会话，也不发送 RPC；每台客户端各播一次。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool PlayFishingCastMontageFromPresentation();

	/**
	 * 按服务器确认的切线/落水 Cosmetic Tag，从 Fishing 表现设置读取并播放对应 Montage。
	 * 只接受这两个终局标签；其他一次性表现仍交给 BP_PlayCosmeticEvent。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Catfishing|Presentation")
	bool PlayFishingOutcomeMontageFromPresentation(FGameplayTag OutcomeEventTag);

protected:
	/** 组件注册完成后幂等刷新 Owner/Avatar；未裁 runtime 会清除引擎自动建立的 ActorInfo 并保持 fail-closed。 */
	virtual void BeginPlay() override;

	/** 父类完成占有后只触发 ASC 与 Equipment 的边界入口；能力授予、属性播种和 starter 细节分别留在所属组件内。 */
	virtual void PossessedBy(AController* NewController) override;

	/** Controller 复制变化后刷新拥有客户端 ActorInfo；Controller 失效时 ClearActorInfo，不保留失效 Avatar。 */
	virtual void OnRep_Controller() override;

	/** 本地 Pawn 重启后刷新 ActorInfo；正式输入由 PlayerController 的 AbilityInputConfig 负责。 */
	virtual void PawnClientRestart() override;

	/** 失去占有时只取消身体 Ability；父类断开 Controller 后才清 ActorInfo，跨系统会话清理由 GameMode 的 Pawn 通知统一负责。 */
	virtual void UnPossessed() override;

	/** Actor 离开 World 时请求 ASC 撤销自身配置授予并清理身体 Ability；Fishing/Social 会话不由身体生命周期直接操作。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 钓鱼专用第一人称相机组件；只在上鱼表现可提供有效视角时接管 CalcCamera，平时让角色蓝图相机继续生效。 */
	UPROPERTY(VisibleAnywhere, Category = "Catfishing|Fishing")
	TObjectPtr<UCatFishingCameraComponent> FishingCameraComponent;

	/** 按正式 ASC gate 初始化 Character=this 的 Owner/Avatar；未裁复制策略时主动 Clear 而不是猜 Mixed。 */
	void InitializeAbilityActorInfo();

	/** 猫身体唯一 AbilitySystemComponent；构造期创建、随 Character 复制，并拥有本 Actor 的 AttributeSet 与 Ability。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatAbilitySystemComponent> AbilitySystemComponent;

	/** 猫身体唯一 Survival 属性集；构造期显式交给 ASC 持有 Poison、FishingStrength、FightStamina 和 MaxFightStamina。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSurvivalAttributeSet> SurvivalAttributes;

	/** 猫身体唯一离散状态组件；复制 Wet/Downed/Recovery，数值仍由 Survival AttributeSet 拥有。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatConditionComponent> ConditionComponent;

	/** 猫身体唯一吃鱼成长组件；复制经验槽与待选次数，Buff 内容未裁时不会生成额外效果状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Growth", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatGrowthComponent> GrowthComponent;

	/** Character 的正式随身库存组件；它承载物品实例和堆叠，是随身物品唯一事实源。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatInventoryComponent> InventoryComponent;

	/** 钓具选择读模型与 Fishing 使用协调宿主；正式物品实例和数量归库存系统。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatEquipmentComponent> EquipmentComponent;
};
