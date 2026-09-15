#include "Data/CatFishDefinition.h"

#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Inventory/CatFishInventoryItemInstance.h"

// 鱼定义构造流程：在通用落地动作之外声明食用、叼起与单鱼出售；是否可执行仍按实例、容器和买家当前状态判断。
UCatFishDefinition::UCatFishDefinition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InventoryActions.Insert({CatInventoryActionTags::Use, NSLOCTEXT("CatInventory", "EatFish", "食用"), ECatInventoryActionQuantityMode::Single}, 0);
	InventoryActions.Add({CatInventoryActionTags::Carry, NSLOCTEXT("CatInventory", "CarryFish", "叼起"), ECatInventoryActionQuantityMode::Single});
	InventoryActions.Add({CatInventoryActionTags::Sell, NSLOCTEXT("CatInventory", "SellFish", "出售"), ECatInventoryActionQuantityMode::Single});
}

// 定义可用性检查流程：验证显式 gate、身份、独立稀有/体型轴、基础水域分布、权重/重量、协作人数、性格与食用成长数值；
// 时段/天气数组由可独立启用的候选过滤门消费，测试期为空不会阻断基础选鱼链。可选成像事件不属于实物鱼可用性的前置条件。
bool UCatFishDefinition::IsRuntimeDefinitionReady() const
{
	const bool bChumPreferenceValid = FMath::IsFinite(ChumPreference.Fishy)
		&& FMath::IsFinite(ChumPreference.Fragrant) && FMath::IsFinite(ChumPreference.Fermented)
		&& ChumPreference.Fishy >= 0.0 && ChumPreference.Fragrant >= 0.0
		&& ChumPreference.Fermented >= 0.0;
	TSet<FName> SeenBaitIds;
	bool bBaitMultipliersValid = true;
	for (const FCatBaitWeightMultiplier& Entry : BaitWeightMultipliers)
	{
		if (Entry.BaitDefinitionId.IsNone() || !FMath::IsFinite(Entry.Multiplier) || Entry.Multiplier <= 0.0
			|| SeenBaitIds.Contains(Entry.BaitDefinitionId))
		{
			bBaitMultipliersValid = false;
			break;
		}
		SeenBaitIds.Add(Entry.BaitDefinitionId);
	}
	const bool bFoodReady = FMath::IsFinite(EatingExperience) && EatingExperience > 0.0;
	return bEnableRuntimeDefinition && !FishDefinitionId.IsNone() && !RarityTierId.IsNone()
		&& LoadRuntimePresentationDefinition() != nullptr
		&& BodyClass != ECatFishBodyClass::Unknown
		&& RegionIds.Num() > 0
		&& FMath::IsFinite(SpawnWeight) && SpawnWeight > 0.0
		&& FMath::IsFinite(MinimumWeightKilograms) && MinimumWeightKilograms > 0.0
		&& FMath::IsFinite(MaximumWeightKilograms) && MaximumWeightKilograms >= MinimumWeightKilograms
		&& MinimumFightParticipants >= 1 && MinimumFightParticipants <= 8
		&& FMath::IsFinite(FishFightStamina) && FishFightStamina > 0.0
		&& !BitePersonalityId.IsNone() && !FightPersonalityId.IsNone() && bFoodReady
		&& bChumPreferenceValid && bBaitMultipliersValid;
}

UCatFishPresentationDefinition* UCatFishDefinition::LoadRuntimePresentationDefinition() const
{
	UCatFishPresentationDefinition* Presentation = PresentationDefinition.LoadSynchronous();
	return Presentation && Presentation->IsRuntimeDefinitionReady() ? Presentation : nullptr;
}

double UCatFishDefinition::FindBaitMultiplierOrNeutral(const FName BaitDefinitionId) const
{
	for (const FCatBaitWeightMultiplier& Entry : BaitWeightMultipliers)
	{
		if (Entry.BaitDefinitionId == BaitDefinitionId)
		{
			return Entry.Multiplier;
		}
	}
	return 1.0;
}

// 鱼库存 ID 读取流程：鱼种稳定 ID 就是库存稳定 ID，避免同一实物鱼在 Fishing 和 Inventory 之间出现双身份。
FName UCatFishDefinition::GetInventoryDefinitionId() const
{
	return FishDefinitionId;
}

// 鱼展示名读取流程：直接复用鱼表展示名；空文本交给 UI 回退到稳定 ID。
FText UCatFishDefinition::GetInventoryDisplayName() const
{
	return DisplayName;
}

// 鱼说明读取流程：鱼物品和图鉴共享鱼表描述，不再复制一份库存文案。
FText UCatFishDefinition::GetInventoryDescription() const
{
	return Description;
}

// 鱼缩略图读取流程：实物鱼格子只通过鱼定义取表现资源，不在运行实例里复制贴图引用。
TSoftObjectPtr<UTexture2D> UCatFishDefinition::GetInventoryThumbnail() const
{
	return Thumbnail;
}

// 鱼库存运行校验流程：先要求鱼表能进入 Fishing 运行时，再要求父类库存入口能解析稳定 ID 和实例类型。
bool UCatFishDefinition::IsInventoryRuntimeDefinitionReady() const
{
	return IsRuntimeDefinitionReady() && Super::IsInventoryRuntimeDefinitionReady();
}

// 鱼实例类型解析流程：显式配置只能收窄为鱼实例子类，错误配置直接拒绝，保证重量和来源会话不会落到普通实例。
TSubclassOf<UCatInventoryItemInstance> UCatFishDefinition::GetPreferredInstanceType() const
{
	if (PreferredInstanceType != nullptr)
	{
		return PreferredInstanceType->IsChildOf(UCatFishInventoryItemInstance::StaticClass())
			? PreferredInstanceType : nullptr;
	}
	return UCatFishInventoryItemInstance::StaticClass();
}

// 鱼堆叠上限读取流程：每条鱼是独立实物，单格只允许一条。
int32 UCatFishDefinition::GetMaxStackCount() const
{
	return 1;
}

// 鱼堆叠判断流程：实物鱼不按定义合并；同一种鱼的两次捕获仍然是两个独立物品实例。
bool UCatFishDefinition::CanStackWith(const UCatInventoryItemDefinition& Other) const
{
	(void)Other;
	return false;
}
