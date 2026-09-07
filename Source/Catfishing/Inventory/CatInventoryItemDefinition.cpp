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

	return bExactMatch ? SemanticTags.HasTagExact(Tag) : SemanticTags.HasTag(Tag);
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

// 实例类型解析流程：调用方覆盖类用于少数运行来源；普通来源按定义类默认对象读取 PreferredInstanceType。
TSubclassOf<UCatInventoryItemInstance> UCatInventoryItemDefinition::ResolveItemInstanceClass(
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceOverrideClass)
{
	if (ItemInstanceOverrideClass != nullptr)
	{
		return ItemInstanceOverrideClass;
	}

	if (ItemDefinitionClass == nullptr)
	{
		return nullptr;
	}

	const UCatInventoryItemDefinition* ItemDefinition = GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass);
	return ItemDefinition != nullptr ? ItemDefinition->GetPreferredInstanceType() : nullptr;
}

// 堆叠上限读取流程：把异常配置收束到 1，避免容量预演和正式入库分支各自处理 0 或负数。
int32 UCatInventoryItemDefinition::GetMaxStackCount() const
{
	return FMath::Max(1, MaxStackCount);
}

// 堆叠兼容判断流程：迁移期先看稳定 ID，再看定义类；没有稳定 ID 的蓝图定义仍可按同类合并。
bool UCatInventoryItemDefinition::CanStackWith(const UCatInventoryItemDefinition& Other) const
{
	if (!ItemDefinitionId.IsNone() && ItemDefinitionId == Other.ItemDefinitionId)
	{
		return true;
	}

	return GetClass() == Other.GetClass();
}

// 网络支持声明：定义可被实例引用并在必要时作为子对象参与复制，但业务上仍只应保存静态配置。
bool UCatInventoryItemDefinition::IsSupportedForNetworking() const
{
	return true;
}

// 蓝图片段读取流程：只访问定义类默认对象，不创建运行实例，不写库存组件。
const UCatInventoryItemFragment* UCatInventoryBlueprintLibrary::FindFragmentByClass(
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
	const TSubclassOf<UCatInventoryItemFragment> FragmentClass)
{
	if (ItemDefinitionClass == nullptr || FragmentClass == nullptr)
	{
		return nullptr;
	}

	const UCatInventoryItemDefinition* ItemDefinition = GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass);
	return ItemDefinition != nullptr ? ItemDefinition->FindFragmentByClass(FragmentClass) : nullptr;
}
