#include "Inventory/CatInventoryItemDefinition.h"

#include "Inventory/CatInventoryItemInstance.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace CatItemTags
{
	UE_DEFINE_GAMEPLAY_TAG(Fish, "Item.Category.Fish");
	UE_DEFINE_GAMEPLAY_TAG(Tool, "Item.Category.Tool");
}

namespace CatInventoryActionTags
{
	UE_DEFINE_GAMEPLAY_TAG(Use, "Inventory.Action.Use");
	UE_DEFINE_GAMEPLAY_TAG(Drop, "Inventory.Action.Drop");
	UE_DEFINE_GAMEPLAY_TAG(Place, "Inventory.Action.Place");
	UE_DEFINE_GAMEPLAY_TAG(Carry, "Inventory.Action.Carry");
	UE_DEFINE_GAMEPLAY_TAG(Sell, "Inventory.Action.Sell");
}

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

// 定义构造流程：普通物品默认声明通用丢弃和放置；派生定义及资产可改清单，实例只保存事实并查询可用性，动作执行归 GA 或世界交互。
UCatInventoryItemDefinition::UCatInventoryItemDefinition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InventoryActions = {
		{CatInventoryActionTags::Drop, NSLOCTEXT("CatInventory", "Drop", "丢弃"), ECatInventoryActionQuantityMode::Select},
		{CatInventoryActionTags::Place, NSLOCTEXT("CatInventory", "Place", "放置"), ECatInventoryActionQuantityMode::Select}};
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

// 数字身份读取流程：返回总表分配给该物品定义的 ItemId，不读取旧英文身份；0 表示尚未登记。
int32  UCatInventoryItemDefinition::GetItemId() const
{
	return ItemId;
}

// 原始展示名读取流程：普通库存资产返回策划字段；保留空值供资产审计，玩家表现通过 GetPlayerFacingName 收口。
FText UCatInventoryItemDefinition::GetInventoryDisplayName() const
{
	return InventoryDisplayName;
}

// 先区分无法解析的定义，再读取鱼、装备或普通物品自己的显示名；空名统一占位，数字 ID 留给日志和数据契约。
FText UCatInventoryItemDefinition::GetPlayerFacingName(const UCatInventoryItemDefinition* Definition)
{
	if (!Definition) return NSLOCTEXT("CatItem", "UnknownItem", "未知物品");
	const FText Name = Definition->GetInventoryDisplayName();
	return Name.IsEmptyOrWhitespace() ? NSLOCTEXT("CatItem", "UnnamedItem", "未命名物品") : Name;
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
	if ((GetItemId() == 0) || ResolveItemInstanceClass(this) == nullptr)
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
	const int32  ThisItemId = GetItemId();
	const int32  OtherItemId = Other.GetItemId();
	if (!(ThisItemId == 0) && ThisItemId == OtherItemId)
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
#if WITH_EDITOR
// 资产验证流程：先保留父类结果，再核对身份与实例，并逐个调用片段校验；空片段直接报错，不创建运行对象。
EDataValidationResult UCatInventoryItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = Super::IsDataValid(Context) != EDataValidationResult::Invalid;
	if (GetItemId() <= 0 || !ResolveItemInstanceClass(this))
	{
		Context.AddError(NSLOCTEXT("CatItem", "InvalidIdentity", "物品必须登记正整数编号，并配置可实例化的物品实例类型。"));
		bValid = false;
	}
	for (const UCatInventoryItemFragment* Fragment : Fragments)
	{
		if (!Fragment)
		{
			Context.AddError(NSLOCTEXT("CatItem", "NullFragment", "物品包含空片段，请删除空项或选择有效片段。"));
			bValid = false;
		}
		else if (Fragment->IsDataValid(Context) == EDataValidationResult::Invalid) bValid = false;
		else if (!Fragment->IsRuntimeReady())
		{
			Context.AddError(FText::Format(NSLOCTEXT("CatItem", "FragmentNotReady", "片段 {0} 的运行配置不完整，请检查其引用资产与必填参数。"), FText::FromString(Fragment->GetClass()->GetName())));
			bValid = false;
		}
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
