#include "CatInventoryActionsAuthoringLibrary.h"
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "AbilitySystem/Items/Abilities/CatGA_ConsumeFish.h"
#include "AbilitySystem/Items/Abilities/CatGA_DeployFishingRod.h"
#include "AbilitySystem/Items/Abilities/CatGA_UseScoopNet.h"
#include "AbilitySystem/Items/Abilities/CatGA_SelectFishingLoadout.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/BodyAction/CatCancelBodyActionAbility.h"
#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Factories/DataAssetFactory.h"

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

	bool SaveAuthoringAsset(UObject& Asset)
	{
		Asset.MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *Filename, Args);
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
		const UCatEquipmentItemDefinition* Equipment = Cast<UCatEquipmentItemDefinition>(&Definition);
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

// 装备使用迁移流程：按既有鱼竿、饵、漂、抄网和窝料片段选择程序能力，统一写入数据型装备实例及零前摇使用配置。
// 窝料沿原规则每次支付一份，其余装配动作零成本；扫描全项目同类资产，覆盖总表引用的旧基础物品并保留稳定编号。
bool UCatInventoryActionsAuthoringLibrary::MigrateFormalEquipmentUseInstanceTypes()
{
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	Registry.Get().SearchAllAssets(true);
	FARFilter Filter;
	Filter.ClassPaths.Add(UCatEquipmentItemDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Registry.Get().GetAssets(Filter, Assets);
	bool bSucceeded = true;
	for (const FAssetData& Asset : Assets)
	{
		UCatEquipmentItemDefinition* Definition = Cast<UCatEquipmentItemDefinition>(Asset.GetAsset());
		if (!Definition) { bSucceeded = false; continue; }
		// 旧基础资产仍使用 Slot_* 名称；只迁移已存在的四个槽语义，物品编号和片段中的玩法参数保持原值。
		const TPair<FName, FName> LegacySlots[] = {
			{TEXT("Slot_Rod"), UCatEquipmentItemDefinition::FishingRodLoadoutSlotId()},
			{TEXT("Slot_Bait"), UCatEquipmentItemDefinition::FishingBaitLoadoutSlotId()},
			{TEXT("Slot_Float"), UCatEquipmentItemDefinition::FishingFloatLoadoutSlotId()},
			{TEXT("Slot_ScoopNet"), UCatEquipmentItemDefinition::ScoopNetLoadoutSlotId()}};
		for (const auto& Slot : LegacySlots)
			if (Definition->LoadoutSlotId == Slot.Key) Definition->LoadoutSlotId = Slot.Value;
		TSubclassOf<UCatInventoryItemInstance> Expected = UCatEquipmentInventoryItemInstance::StaticClass();
		TSubclassOf<UCatItemGameplayAbility> Ability;
		if (Definition->CanServeFishingRod()) Ability = UCatGA_DeployFishingRod::StaticClass();
		else if (Definition->CanServeFishingBait() || Definition->CanServeFishingFloat()) Ability = UCatGA_SelectFishingLoadout::StaticClass();
		else if (Definition->CanServeScoopNet()) Ability = UCatGA_UseScoopNet::StaticClass();
		else if (Definition->CanServeChumPlacement()) Ability = UCatGA_FishingChum::StaticClass();
		else continue;
		if (Ability)
		{
			auto* Use = Definition->FindFragment<UCatItemUseFragment>();
			if (!Use) { Use = NewObject<UCatItemUseFragment>(Definition, NAME_None, RF_Transactional); Definition->Fragments.Add(Use); }
			Use->AbilityClass = Ability; Use->ConsumeCount = Definition->CanServeChumPlacement() ? 1 : 0; Use->CommitDelay = 0.0f;
		}
		const ECatEquipmentLoadoutTargetSlot ExpectedSlot = Definition->CanServeFishingBait() ? ECatEquipmentLoadoutTargetSlot::Bait : Definition->CanServeFishingFloat() ? ECatEquipmentLoadoutTargetSlot::Float : ECatEquipmentLoadoutTargetSlot::None;
		const bool bChanged = Ability != nullptr || Definition->PreferredInstanceType != Expected || Definition->TargetSlot != ExpectedSlot;
		const bool bSaved = !bChanged || (Definition->Modify(), Definition->PreferredInstanceType = Expected, Definition->TargetSlot = ExpectedSlot, CatInventoryActionsAuthoring::SaveDefinition(*Definition));
		if (bSaved)
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Display, TEXT("Event=equipment_use_instance_migration Asset=%s Class=%s Changed=%d Saved=1"), *Definition->GetPathName(), *GetNameSafe(Expected.Get()), bChanged);
		}
		else
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Error, TEXT("Event=equipment_use_instance_migration Asset=%s Class=%s Changed=%d Saved=0"), *Definition->GetPathName(), *GetNameSafe(Expected.Get()), bChanged);
		}
		bSucceeded &= bSaved;
	}

	return bSucceeded;
}

bool UCatInventoryActionsAuthoringLibrary::MigrateEquipmentAbilitySetGrants()
{
	// 能力资产迁移流程：从 Settings 解析默认角色集合，建立或复用鱼竿操作集合；角色保留四项身体动作、取消及共享进食六项能力。
	// 再扫描正式物品，通过已有装备片段更新独立装备资产的集合引用；片段缺失即失败，不补造装备定义。
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	UCatAbilitySet* DefaultSet = Settings ? Settings->DefaultAbilitySet.LoadSynchronous() : nullptr;
	if (!DefaultSet) return false;
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	auto CreateSet = [&Tools](const TCHAR* Name) -> UCatAbilitySet*
	{
		const FString PackagePath = TEXT("/Game/Data/Abilities");
		const FString ObjectPath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
		if (UCatAbilitySet* Existing = LoadObject<UCatAbilitySet>(nullptr, *ObjectPath)) return Existing;
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>(); Factory->DataAssetClass = UCatAbilitySet::StaticClass();
		return Cast<UCatAbilitySet>(Tools.CreateAsset(Name, PackagePath, UCatAbilitySet::StaticClass(), Factory));
	};
	UCatAbilitySet* RodSet = CreateSet(TEXT("DA_CatAbilitySet_RodOperations"));
	if (!RodSet) return false;
	const TSet<FGameplayTag> RodTags = { CatFishingAbilityTags::Input_Fishing_Primary, CatFishingAbilityTags::Input_Fishing_Slack, CatFishingAbilityTags::Input_Fishing_Cancel };
	const TArray<FCatAbilitySetAbility> RodEntries = DefaultSet->GrantedAbilities.FilterByPredicate([&RodTags](const FCatAbilitySetAbility& Entry) { return RodTags.Contains(Entry.InputTag) && Entry.Ability != UCatGA_CancelBodyAction::StaticClass(); });
	if (RodEntries.Num() == 3) RodSet->GrantedAbilities = RodEntries;
	else if (!RodEntries.IsEmpty()) return false;
	if (RodSet->GrantedAbilities.Num() != 3) return false;
	DefaultSet->GrantedAbilities.RemoveAll([&RodTags](const FCatAbilitySetAbility& Entry)
	{
		return RodTags.Contains(Entry.InputTag) || Entry.InputTag == CatFishingAbilityTags::Input_Fishing_RodInteract || Entry.Ability == UCatGA_FishingChum::StaticClass();
	});
	DefaultSet->GrantedAbilities.RemoveAll([](const FCatAbilitySetAbility& Entry) { return Entry.Ability == UCatGA_CancelBodyAction::StaticClass(); });
	FCatAbilitySetAbility CancelEntry; CancelEntry.Ability = UCatGA_CancelBodyAction::StaticClass(); CancelEntry.Level = 1;
	CancelEntry.InputTag = CatFishingAbilityTags::Input_Fishing_Cancel; CancelEntry.ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;
	DefaultSet->GrantedAbilities.Add(CancelEntry);
	DefaultSet->GrantedAbilities.RemoveAll([](const FCatAbilitySetAbility& Entry) { return Entry.Ability == UCatGA_ConsumeFish::StaticClass(); });
	FCatAbilitySetAbility Eat; Eat.Ability = UCatGA_ConsumeFish::StaticClass(); Eat.Level = 1;
	Eat.ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;
	DefaultSet->GrantedAbilities.Add(Eat);
	bool bSucceeded = CatInventoryActionsAuthoring::SaveAuthoringAsset(*DefaultSet) && CatInventoryActionsAuthoring::SaveAuthoringAsset(*RodSet);
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")); TArray<FAssetData> Assets;
	Registry.Get().GetAssetsByClass(UCatEquipmentItemDefinition::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		UCatEquipmentItemDefinition* Definition = Cast<UCatEquipmentItemDefinition>(Asset.GetAsset()); if (!Definition) { bSucceeded = false; continue; }
		TArray<TSoftObjectPtr<UCatAbilitySet>> Expected;
		if (Definition->CanServeFishingRod()) Expected.Add(RodSet);
		if (Expected.IsEmpty() && !Definition->CanServeChumPlacement()) continue;
		auto* Equippable = Definition->FindFragment<UCatEquippableItemFragment>();
		// 只消耗的旧窝料不需要装备实例；鱼竿的操作能力必须有明确装备来源。
		if (!Equippable) { if (!Expected.IsEmpty()) return false; continue; }
		if (!Equippable->EquipmentDefinition) return false;
		auto* Equipped = Equippable->EquipmentDefinition.Get();
		if (Equipped->AbilitySetsToGrant != Expected) { Equipped->Modify(); Equipped->AbilitySetsToGrant = Expected; bSucceeded &= CatInventoryActionsAuthoring::SaveAuthoringAsset(*Equipped); }
	}
	return bSucceeded;
}

// 装备资产收口流程：重存已有组合定义，移除磁盘上的旧字段；缺少装备引用时明确失败，不能用猜测的默认能力重建资产。
bool UCatInventoryActionsAuthoringLibrary::MigrateEquipmentDefinitions()
{
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	FARFilter Filter; Filter.PackagePaths.Add(TEXT("/Game")); Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UCatEquipmentItemDefinition::StaticClass()->GetClassPathName());
	TArray<FAssetData> Assets; Registry.GetAssets(Filter, Assets);
	if (Assets.IsEmpty())
	{
		UE_LOG(LogCatInventoryActionsAuthoring, Error, TEXT("Event=equipment_definition_migration_rejected Reason=NoAssets"));
		return false;
	}
	for (const auto& Asset : Assets)
	{
		auto* Item = Cast<UCatEquipmentItemDefinition>(Asset.GetAsset());
		if (!Item)
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Error, TEXT("Event=equipment_definition_migration_rejected Asset=%s Reason=LoadFailed"), *Asset.GetObjectPathString());
			return false;
		}
		auto* Fragment = Item->FindFragment<UCatEquippableItemFragment>();
		// 编号 6 等纯投放物没有拿出后的装备生命周期，只有使用能力，不为它们补空装备实例。
		if (!Fragment && Item->CanServeChumPlacement()) continue;
		if (!Fragment || !Fragment->IsRuntimeReady() || !CatInventoryActionsAuthoring::SaveDefinition(*Item))
		{
			UE_LOG(LogCatInventoryActionsAuthoring, Error, TEXT("Event=equipment_definition_migration_rejected Asset=%s Reason=MissingEquipmentOrSaveFailed"), *Item->GetPathName());
			return false;
		}
		UE_LOG(LogCatInventoryActionsAuthoring, Display, TEXT("Event=equipment_definition_migrated Item=%s ItemId=%d Equipment=%s"),
			*Item->GetPathName(), Item->ItemId, *GetPathNameSafe(Fragment->EquipmentDefinition));
	}
	return true;
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
