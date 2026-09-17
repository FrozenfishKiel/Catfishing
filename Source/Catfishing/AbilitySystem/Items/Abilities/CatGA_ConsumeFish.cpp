#include "AbilitySystem/Items/Abilities/CatGA_ConsumeFish.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Collection/CatFishCollectionLayers.h"
#include "GameFramework/PlayerState.h"
#include "Items/Fish/CatFishPickupActor.h"

// 单鱼配置流程：固定单条消费与成长效果类型，不解析 GE 内部修饰器；实际经验仍由本条鱼的重量和鱼种计算。
bool UCatGA_ConsumeFish::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
	if (!Super::ValidateUseConfiguration(Configuration, OutError)) return false;
	if (Configuration.ConsumeCount == 1 && Configuration.Effects.ContainsByPredicate([](const TSubclassOf<UGameplayEffect>& Effect)
		{ return Effect && Effect->IsChildOf(UCatGE_FishExperience::StaticClass()); })) return true;
	OutError = NSLOCTEXT("CatItem", "FishUseCount", "吃鱼的消耗数量必须为 1，且使用效果须包含 CatGE_FishExperience 或其子类；每次按这一条鱼的实际重量结算。");
	return false;
}
// 食用预检流程：公共规则通过后读取鱼实例与可食用标记，服务器再检查成长配置和本条鱼的实际经验。
bool UCatGA_ConsumeFish::ValidateUse() const
{
	if (!Super::ValidateUse()) return false;
	const auto* Fish = Cast<UCatFishInventoryItemInstance>(ResolveSourceItem());
	const auto* Character = Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
	const auto* Definition = Fish ? Fish->GetFishDefinition() : UseTarget.WorldFish
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(UseTarget.WorldFish->GetPresentationState().ItemId) : nullptr;
	const double Weight = Fish ? Fish->GetFishWeightKilograms() : UseTarget.WorldFish ? UseTarget.WorldFish->GetPresentationState().WeightKilograms : 0.0;
	return Definition && Definition->IsEdible() && Character->GetGrowthComponent()
		&& (!Character->HasAuthority() || Character->GetGrowthComponent()->ValidateFishGrowth(Definition, Weight) == ECatDomainCommandError::None);
}
// 实例参数流程：按服务器保存的实际重量计算经验；覆盖配置同名值，客户端不能提交更大的重量或经验。
void UCatGA_ConsumeFish::GatherEffectParameters(TMap<FGameplayTag, float>& Parameters) const
{
	Super::GatherEffectParameters(Parameters);
	if (const auto* Fish = Cast<UCatFishInventoryItemInstance>(ResolveSourceItem()))
		Parameters.Add(UCatGE_FishExperience::GetExperienceTag(), float(FMath::FloorToInt(Fish->GetFishDefinition()->ResolveEatingExperiencePoints(Fish->GetFishWeightKilograms()))));
	else if (const auto* WorldFish = UseTarget.WorldFish.Get())
		Parameters.Add(UCatGE_FishExperience::GetExperienceTag(), float(FMath::FloorToInt(WorldFish->GetFishDefinition()->ResolveEatingExperiencePoints(WorldFish->GetPresentationState().WeightKilograms))));
}
// 食用成功流程：从库存实例或世界鱼读取鱼种，记录当前食用者的知识；库存鱼再释放隐藏载体，嘴叼鱼已由成本完成实物销毁。
void UCatGA_ConsumeFish::OnUseCommitted(UCatInventoryItemInstance* ConsumedItem)
{
	auto* Fish = Cast<UCatFishInventoryItemInstance>(ConsumedItem);
	auto* Character = Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
	const auto* Definition = Fish ? Fish->GetFishDefinition() : UseTarget.WorldFish ? UseTarget.WorldFish->GetFishDefinition() : nullptr;
	const auto* PlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (Definition && CatFishCollectionLayers::HasKnowledgeLayer(Definition) && PlayerState && PlayerState->GetUniqueId().IsValid())
		if (auto* Imprints = GetWorld()->GetSubsystem<UCatRunImprintService>()) Imprints->RecordFishKnowledge(Definition->ItemId, PlayerState->GetUniqueId()->ToString());
	if (Fish) if (auto* Actor = Fish->GetWorldActor()) Actor->Destroy();
}
