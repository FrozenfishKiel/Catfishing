#include "UI/ItemTooltip/CatItemTooltipModel.h"

#include "Engine/Texture2D.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryItemDefinition.h"

// 先清空输出并核对实例定义，再读取通用展示字段；最后仅为鱼与鱼竿追加其真实实例值。
// 数值不回写领域对象；非有限值不显示为合法重量或耐久，复制未就绪时由下一次投影恢复。
bool UCatItemTooltipModel::BuildViewData(const UCatInventoryItemInstance* Instance, FCatItemTooltipViewData& OutData) const
{
	OutData = FCatItemTooltipViewData();
	const UCatInventoryItemDefinition* Definition = IsValid(Instance) ? Instance->GetItemDefinition() : nullptr;
	if (!IsValid(Definition))
	{
		return false;
	}
	OutData.Name = Definition->GetInventoryDisplayName();
	OutData.Description = Definition->GetInventoryDescription();
	OutData.Icon = Definition->GetInventoryThumbnail().LoadSynchronous();
	if (const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Instance))
	{
		const double Weight = Fish->GetFishWeightKilograms();
		if (FMath::IsFinite(Weight) && Weight > 0.0)
		{
			FNumberFormattingOptions Format;
			Format.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
			OutData.InstanceDetails = FText::Format(NSLOCTEXT("CatItemTooltip", "FishWeight", "重量：{0} kg"), FText::AsNumber(Weight, &Format));
		}
	}
	else if (const UCatEquipmentInventoryItemInstance* Equipment = Cast<UCatEquipmentInventoryItemInstance>(Instance))
	{
		if (const UCatEquipmentFragment_Rod* Rod = Definition->FindFragment<UCatEquipmentFragment_Rod>())
		{
			const double Current = Equipment->GetRodDurability();
			const double Maximum = Rod->MaximumRodDurability;
			if (FMath::IsFinite(Current) && FMath::IsFinite(Maximum) && Maximum > 0.0)
			{
				FNumberFormattingOptions Format;
				Format.SetMaximumFractionalDigits(2);
				OutData.InstanceDetails = FText::Format(
					Equipment->IsRodBroken() ? NSLOCTEXT("CatItemTooltip", "BrokenRod", "耐久：{0} / {1}（已断裂）")
						: NSLOCTEXT("CatItemTooltip", "RodDurability", "耐久：{0} / {1}"),
					FText::AsNumber(Current, &Format), FText::AsNumber(Maximum, &Format));
			}
		}
	}
	return true;
}
