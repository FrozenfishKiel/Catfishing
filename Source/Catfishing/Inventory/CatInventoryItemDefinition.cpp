#include "Inventory/CatInventoryItemDefinition.h"

#include "Inventory/CatInventoryItemInstance.h"

// 片段创建回调流程：通用片段默认不改变实例，避免每个定义都隐式写入运行状态。
void UCatInventoryItemFragment::OnInstanceCreated(UCatInventoryItemInstance* Instance) const
{
	(void)Instance;
}

// 定义构造流程：只保留类默认配置；运行期状态由 UCatInventoryItemInstance 承载。
UCatInventoryItemDefinition::UCatInventoryItemDefinition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 片段查找流程：先拒绝空类型，再按 IsA 支持蓝图片段继承，找不到时返回空让调用方 fail closed。
UCatInventoryItemFragment* UCatInventoryItemDefinition::FindFragmentByClass(
	const TSubclassOf<UCatInventoryItemFragment> FragmentClass) const
{
	if (FragmentClass == nullptr)
	{
		return nullptr;
	}

	for (UCatInventoryItemFragment* Fragment : Fragments)
	{
		if (Fragment != nullptr && Fragment->IsA(FragmentClass))
		{
			return Fragment;
		}
	}

	return nullptr;
}

// 标签判断流程：无效标签直接失败；调用方声明精确匹配时不向父级标签放宽。
bool UCatInventoryItemDefinition::HasSemanticTag(const FGameplayTag Tag, const bool bExactMatch) const
{
	if (!Tag.IsValid())
	{
		return false;
	}

	const FGameplayTagContainer& Tags = GetInventorySemanticTags();
	return bExactMatch ? Tags.HasTagExact(Tag) : Tags.HasTag(Tag);
}

// 稳定 ID 读取流程：基础库存资产直接返回自己的目录 ID；旧装备资产通过覆盖方法返回 EquipmentDefinitionId。
FName UCatInventoryItemDefinition::GetInventoryDefinitionId() const
{
	return InventoryDefinitionId;
}

// 展示名读取流程：普通库存资产直接返回库存字段；空文本由 UI 再决定是否回退到稳定 ID。
FText UCatInventoryItemDefinition::GetInventoryDisplayName() const
{
	return InventoryDisplayName;
}

// 说明文本读取流程：这里只暴露静态描述，不把 Use、装备或商店状态混进库存定义。
FText UCatInventoryItemDefinition::GetInventoryDescription() const
{
	return InventoryDescription;
}

// 缩略图读取流程：表现资源只从静态定义取得，避免运行格子复制贴图引用。
TSoftObjectPtr<UTexture2D> UCatInventoryItemDefinition::GetInventoryThumbnail() const
{
	return InventoryThumbnail;
}

// 标签集合读取流程：返回定义自己的集合引用，调用方不得修改返回的静态配置。
const FGameplayTagContainer& UCatInventoryItemDefinition::GetInventorySemanticTags() const
{
	return InventorySemanticTags;
}

// 运行目录校验流程：库存定义必须有稳定 ID 和可生成的实例类，避免商店发出无法实例化的物品。
bool UCatInventoryItemDefinition::IsInventoryRuntimeDefinitionReady() const
{
	return !GetInventoryDefinitionId().IsNone()
		&& ResolveItemInstanceClass(this) != nullptr;
}

// 实例类型读取流程：定义显式配置优先，否则使用通用物品实例，确保收货链总能生成可复制对象。
TSubclassOf<UCatInventoryItemInstance> UCatInventoryItemDefinition::GetPreferredInstanceType() const
{
	if (PreferredInstanceType != nullptr)
	{
		return PreferredInstanceType;
	}

	return UCatInventoryItemInstance::StaticClass();
}

// 实例类型解析流程：调用方覆盖类用于少数运行来源；普通来源按定义资产读取 PreferredInstanceType。
TSubclassOf<UCatInventoryItemInstance> UCatInventoryItemDefinition::ResolveItemInstanceClass(
	const UCatInventoryItemDefinition* ItemDefinition,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceOverrideClass)
{
	if (ItemInstanceOverrideClass != nullptr)
	{
		return ItemInstanceOverrideClass;
	}

	if (ItemDefinition == nullptr)
	{
		return nullptr;
	}

	return ItemDefinition->GetPreferredInstanceType();
}

// 堆叠上限读取流程：把异常配置收束到 1，避免容量预演和正式入库分支各自处理 0 或负数。
int32 UCatInventoryItemDefinition::GetMaxStackCount() const
{
	return FMath::Max(1, InventoryMaxStackCount);
}

// 库存 Use 策略读取流程：基础定义不从显示名、标签或片段猜 Use 后果；没有子类覆盖时返回 None，让库存组件保持 fail-closed。
ECatInventoryItemUseEffect UCatInventoryItemDefinition::GetInventoryUseEffect() const
{
	return ECatInventoryItemUseEffect::None;
}

// 库存实例持有策略判断流程：只把明确声明为 Hold 的物品移入活动区；基础定义默认不让可见槽位消失。
bool UCatInventoryItemDefinition::KeepsInventoryInstanceWhileUsed() const
{
	return GetInventoryUseEffect() == ECatInventoryItemUseEffect::HoldInstanceUntilUnUse;
}

// 库存扣量策略判断流程：只有明确声明为 ConsumeQuantity 的物品由库存组件扣数量；基础定义默认不扣。
bool UCatInventoryItemDefinition::ConsumesInventoryQuantityOnUse() const
{
	return GetInventoryUseEffect() == ECatInventoryItemUseEffect::ConsumeQuantity;
}

// 堆叠兼容判断流程：稳定 ID 是跨资产主键；没有 ID 时只允许同一资产对象合并，避免同类 DataAsset 串格。
bool UCatInventoryItemDefinition::CanStackWith(const UCatInventoryItemDefinition& Other) const
{
	const FName ThisDefinitionId = GetInventoryDefinitionId();
	const FName OtherDefinitionId = Other.GetInventoryDefinitionId();
	if (!ThisDefinitionId.IsNone() && ThisDefinitionId == OtherDefinitionId)
	{
		return true;
	}

	return this == &Other;
}

// 网络支持声明：定义可被实例引用并在必要时作为子对象参与复制，但业务上仍只应保存静态配置。
bool UCatInventoryItemDefinition::IsSupportedForNetworking() const
{
	return true;
}

// 蓝图片段读取流程：只访问传入定义资产，不创建运行实例，不写库存组件。
const UCatInventoryItemFragment* UCatInventoryBlueprintLibrary::FindFragmentByClass(
	const UCatInventoryItemDefinition* ItemDefinition,
	const TSubclassOf<UCatInventoryItemFragment> FragmentClass)
{
	if (ItemDefinition == nullptr || FragmentClass == nullptr)
	{
		return nullptr;
	}

	return ItemDefinition->FindFragmentByClass(FragmentClass);
}
