#include "AbilitySystem/Attributes/CatGrowthAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Growth/CatGrowthComponent.h"
#include "GameFramework/Actor.h"

// 成长输入流程：仅消费本属性的服务器 GE；先清元值避免委托重入重复读取，再交给唯一经验槽持有者。
void UCatGrowthAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	if (Data.EvaluatedData.Attribute != GetIncomingExperienceAttribute()) return;
	const float Amount = GetIncomingExperience();
	SetIncomingExperience(0.f);
	AActor* Avatar = Data.Target.GetAvatarActor();
	if (!Avatar || !Avatar->HasAuthority() || !FMath::IsFinite(Amount) || Amount < 0.f || double(Amount) > MAX_int32) return;
	if (auto* Growth = Avatar->FindComponentByClass<UCatGrowthComponent>()) Growth->GrantExperienceFromEffect(FMath::FloorToInt(Amount));
}
