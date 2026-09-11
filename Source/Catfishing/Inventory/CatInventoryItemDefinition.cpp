#include "Inventory/CatInventoryItemDefinition.h"

#include "Inventory/CatInventoryItemInstance.h"

// 基础片段就绪流程：没有领域约束时直接放行；具体能力的配置校验由派生片段实现。
bool UCatInventoryItemFragment::IsRuntimeReady() const
{
	return true;
}

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

// 稳定 ID 读取流程：基础库存资产直接返回自己的目录 ID；装备资产通过覆盖方法返回 EquipmentDefinitionId。
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
	if (GetInventoryDefinitionId().IsNone() || ResolveItemInstanceClass(this) == nullptr)
	{
		return false;
	}
	// 每个片段只校验自己拥有的配置；新能力不需要修改库存核心或增加用途枚举。
	for (const UCatInventoryItemFragment* Fragment : Fragments)
	{
		if (Fragment == nullptr || !Fragment->IsRuntimeReady())
		{
			return false;
		}
	}
	return true;
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
		return ItemInstanceOverrideClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			? nullptr : ItemInstanceOverrideClass;
	}

	if (ItemDefinition == nullptr)
	{
		return nullptr;
	}

	const TSubclassOf<UCatInventoryItemInstance> InstanceClass = ItemDefinition->GetPreferredInstanceType();
	// 抽象或已替换的蓝图类无法实例化；预检拒绝它们，避免整批收货过程中触发 NewObject 断言。
	return InstanceClass && !InstanceClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		? InstanceClass : nullptr;
}

// 堆叠上限读取流程：把异常配置收束到 1，避免容量预演和正式入库分支各自处理 0 或负数。
int32 UCatInventoryItemDefinition::GetMaxStackCount() const
{
	return FMath::Max(1, InventoryMaxStackCount);
}

// 堆叠等价判断流程：稳定 ID 是跨资产主键；没有 ID 时只允许同一资产对象合并，避免同类 DataAsset 串格。
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

// 网络支持声明：允许网络引用解析到定义资产；这里不注册子对象，也不把静态配置变成运行实例。
bool UCatInventoryItemDefinition::IsSupportedForNetworking() const
{
	return true;
}
