#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/BlockAbilityTagsGameplayEffectComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Fishing/CatFishingSettings.h"
#include "GameFramework/Pawn.h"
#include "Logging/CatLog.h"
#include "Engine/World.h"

UCatGE_FishingScoopCooldown::UCatGE_FishingScoopCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.0f));

	FInheritedTagContainer CooldownTags;
	CooldownTags.AddTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("ScoopCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
	UBlockAbilityTagsGameplayEffectComponent* Blocking = CreateDefaultSubobject<UBlockAbilityTagsGameplayEffectComponent>(TEXT("MissStunAbilityBlock"));
	GEComponents.Add(Blocking);
	FInheritedTagContainer Blocked;
	Blocked.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cat.Ability")));
	Blocking->SetAndApplyBlockedAbilityTagChanges(Blocked);
}

// 墓碑（2026-09-14，T16；钓鱼规则 §5.2/§5.4）：原来只禁重复 Scoop，现在同一 GE 管完整操作硬直。
bool UCatGE_FishingScoopCooldown::IsOperationBlocked(const AActor* Actor)
{
	const UCatAbilitySystemComponent* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(const_cast<AActor*>(Actor));
	return ASC && ASC->HasMatchingGameplayTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop);
}
void UCatGE_FishingScoopCooldown::ApplyMissFromAuthority(AController* Controller)
{
	if (!Controller || !Controller->HasAuthority()) return;
	UCatAbilitySystemComponent* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(Controller->GetPawn());
	if (!ASC || IsOperationBlocked(Controller->GetPawn())) return; // 硬直中按键不能续罚。
	double Seconds = 0.0;
	if (!GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(Seconds)) return;
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid()) return;
	Spec.Data->SetDuration(float(Seconds), true);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	ASC->CancelAllAbilities();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller))
		CatController->ClearPhysicalControlInput(TEXT("FishingMissStun"));
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_miss_stun Player=%s Seconds=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d Result=AllOperationsBlocked"),
		*GetNameSafe(Controller), Seconds, *GetNameSafe(Controller->GetWorld()), int32(Controller->GetNetMode()), int32(Controller->GetLocalRole()));
}
