#include "CatInventoryActionsAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

/** 库存动作资产迁移的独立日志分类；每个资产都写出推导语义、变更和保存结果，便于审核一次性脚本没有靠 UI 猜测。 */
DEFINE_LOG_CATEGORY_STATIC(LogCatInventoryActionsAuthoring, Log, All);

namespace CatInventoryActionsAuthoring
{
	/** 比较资产当前动作和迁移目标；标签、显示名、数量模式与数组顺序都属于菜单和服务器共享合同，任一不同都需要保存。 */
	bool AreActionsEqual(const TArray<FCatInventoryActionDefinition>& Current,
		const TArray<FCatInventoryActionDefinition>& Expected)
	{
		// 先比较数量，再逐项比较每个序位的完整合同；不按 Tag 集合比较，避免把顺序或标签文本错误地当作已迁移。
		if (Current.Num() != Expected.Num()) return false;
		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			if (Current[Index].Action != Expected[Index].Action || Current[Index].QuantityMode != Expected[Index].QuantityMode
				|| !Current[Index].Label.EqualTo(Expected[Index].Label)) return false;
		}
		return true;
	}

	/** 保存一个已完成动作迁移的 DataAsset；包路径来自资产自身，函数不修改 Config、目录设置或任何运行时默认值。 */
	bool SaveDefinition(UCatInventoryItemDefinition& Definition)
	{
		// 先标记所属包脏，再以现有 /Game 包路径保存顶层对象；保存失败保留 false 给批处理入口，使脚本不能把部分迁移报成成功。
		Definition.MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Definition.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Definition.GetOutermost(), &Definition, *Filename, SaveArgs);
	}

	/** 一次性迁移旧资产的操作声明；公共和鱼清单直接取各定义的默认配置，避免迁移器维护另一份动作顺序。 */
	TArray<FCatInventoryActionDefinition> DetermineActions(const UCatInventoryItemDefinition& Definition, FString& OutKind)
	{
		// 鱼与普通物品复用定义默认清单；装备只按当前实际支持的四种使用行为补 Use，不在运行期覆盖资产配置。
		if (Definition.IsA<UCatFishDefinition>())
		{
			OutKind = TEXT("FishInstance");
			return GetDefault<UCatFishDefinition>()->InventoryActions;
		}
		TArray<FCatInventoryActionDefinition> Actions = GetDefault<UCatInventoryItemDefinition>()->InventoryActions;
		const UCatEquipmentDefinition* Equipment = Cast<UCatEquipmentDefinition>(&Definition);
		const bool bSupportsUse = Equipment && (Equipment->CanServeFishingRod() || Equipment->CanServeFishingBait()
			|| Equipment->CanServeFishingFloat() || Equipment->CanServeScoopNet() || Equipment->CanServeChumPlacement());
		if (bSupportsUse)
		{
			Actions.Insert({CatInventoryActionTags::Use, NSLOCTEXT("CatInventory", "Use", "使用"), ECatInventoryActionQuantityMode::Single}, 0);
		}
		OutKind = bSupportsUse ? TEXT("EquipmentUseInstance") : TEXT("OrdinaryWorldActions");
		return Actions;
	}
}

// 正式库存动作迁移流程：
// 1. 从 Asset Registry 按 UCatInventoryItemDefinition 基类递归收集所有正式 DataAsset，不依赖 Config 或 UI 列表遗漏子类资产。
// 2. 逐个加载定义，按鱼实例、装备四项现有 Use 资格或普通实例语义生成完整有序动作数组。
// 3. 只有数组合同确有变化时才覆盖该资产并保存；迁移器不进入 PostLoad，因此后续策划编辑不会被运行时强制改回。
// 4. 每个资产都记录 Kind、Changed 和 Saved，任一加载或保存失败都会让整体返回 false，脚本据此停止后续宣称。
bool UCatInventoryActionsAuthoringLibrary::MigrateFormalInventoryDefinitionActions()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByClass(UCatInventoryItemDefinition::StaticClass()->GetClassPathName(), Assets, true);
	bool bSucceeded = true;
	int32 ChangedCount = 0;
	for (const FAssetData& Asset : Assets)
	{
		UCatInventoryItemDefinition* Definition = Cast<UCatInventoryItemDefinition>(Asset.GetAsset());
		if (!Definition)
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Error, TEXT("Event=inventory_actions_migration_load_failed Asset=%s"), *Asset.GetObjectPathString());
			bSucceeded = false;
			continue;
		}
		FString Kind;
		const TArray<FCatInventoryActionDefinition> ExpectedActions = CatInventoryActionsAuthoring::DetermineActions(*Definition, Kind);
		const bool bChanged = !CatInventoryActionsAuthoring::AreActionsEqual(Definition->InventoryActions, ExpectedActions);
		bool bSaved = true;
		if (bChanged)
		{
			Definition->Modify();
			Definition->InventoryActions = ExpectedActions;
			bSaved = CatInventoryActionsAuthoring::SaveDefinition(*Definition);
		}
		if (bChanged) ++ChangedCount;
		if (bSaved)
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Display,
				TEXT("Event=inventory_actions_migration_asset Asset=%s Kind=%s Changed=%d Saved=1 ActionCount=%d"),
				*Definition->GetPathName(), *Kind, bChanged, ExpectedActions.Num());
		}
		else
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Error,
				TEXT("Event=inventory_actions_migration_asset Asset=%s Kind=%s Changed=%d Saved=0 ActionCount=%d"),
				*Definition->GetPathName(), *Kind, bChanged, ExpectedActions.Num());
		}
		bSucceeded &= bSaved;
	}
	if (bSucceeded)
	{
		UE_LOG(LogCatInventoryActionsAuthoring, Display,
			TEXT("Event=inventory_actions_migration_completed Assets=%d Changed=%d Result=Success"), Assets.Num(), ChangedCount);
	}
	else
	{
		UE_LOG(LogCatInventoryActionsAuthoring, Error,
			TEXT("Event=inventory_actions_migration_completed Assets=%d Changed=%d Result=Failure"), Assets.Num(), ChangedCount);
	}
	return bSucceeded;
}
