#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatFishingAbilities.generated.h"

class UCatFishingCommandComponent;

UCLASS(Abstract)
class CATFISHING_API UCatFishingGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCatFishingGameplayAbility();
	static bool ShouldWaitForRemoteClient(bool bIsNetAuthority, bool bIsLocallyControlled);

protected:
	UCatFishingCommandComponent* ResolveCommandComponent(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const;
	void FinishOneShot(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bSubmitted);

	/**
	 * 输入按下的本地表现钩子（挥网等语义唯一、成败都要播的即时动作）。
	 * 蓝图子类实现它播 Montage/音效；只在本地控制端、命令提交前调用，绝不携带任何玩法结果。
	 * Primary 因同一输入存在瞄准/提竿/收线三种语义，不调用此钩子；抛竿由 Hook CastFlight 复制状态驱动。
	 * 结果驱动的动画（拖拽循环、完美中鱼、断线）不属于这里——用 Snapshot/表现事件驱动。
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_OnLocalInputActivated();

	/** 输入松开的本地表现钩子（收势/松爪）；按住型 Ability（收线/松线/蓄力）松开时调用。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Presentation")
	void BP_OnLocalInputReleased();
};

UCLASS()
class CATFISHING_API UCatGA_FishingRodInteract : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingRodInteract();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingPrimaryAction : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingPrimaryAction();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};

/** 右键松线：按住期间解除线端限制，让鱼在 L_max 内自由带线；仅 HookedFight 有效。 */
UCLASS()
class CATFISHING_API UCatGA_FishingSlack : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingSlack();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingCancel : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingCancel();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingScoop : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingScoop();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) const override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingChum : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingChum();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};

UCLASS(Abstract)
class CATFISHING_API UCatGA_FishingOutcomeBase : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingOutcomeBase();
};
