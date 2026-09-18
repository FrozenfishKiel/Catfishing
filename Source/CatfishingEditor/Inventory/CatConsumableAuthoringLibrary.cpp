#include "CatConsumableAuthoringLibrary.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Inventory/Fragments/CatItemDropFragment.h"
#include "Inventory/Fragments/CatItemContainerOpenFragment.h"
#include "AbilitySystem/Items/Abilities/CatGA_ApplyItemEffects.h"
#include "AbilitySystem/Items/Abilities/CatGA_RestoreStamina.h"
#include "AbilitySystem/Items/Abilities/CatGA_WaterSpray.h"
#include "AbilitySystem/Items/Abilities/CatGA_PlaceDecoy.h"
#include "AbilitySystem/Items/Abilities/CatGA_Horn.h"
#include "AbilitySystem/Items/Abilities/CatGA_CastNet.h"
#include "AbilitySystem/Effects/CatItemEffects.h"
#include "AbilitySystem/Cues/CatGC_ItemSplash.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Data/CatFishDefinition.h"
#include "Items/CatIconItem.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Texture2D.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace CatConsumableAuthoring
{
	// 保存流程：只写指定资源所在包，不连带保存编辑器里其他未提交资产。
	static bool Save(UObject* Asset)
	{
		if (!Asset) return false;
		Asset->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset,
			*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	// 可编辑行为流程：只创建缺失的原生子类蓝图，重跑时保留策划已调整的默认值。
	static UClass* Behavior(UClass* Parent, const FString& Name)
	{
		const FString Path = TEXT("/Game/Catfishing/Items/Abilities/") + Name;
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *(Path + TEXT(".") + Name));
		if (!BP)
		{
			BP = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*Path), FName(*Name), BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (!BP) return nullptr;
			FAssetRegistryModule::AssetCreated(BP); FKismetEditorUtilities::CompileBlueprint(BP);
			if (!Save(BP)) return nullptr;
		}
		return BP->GeneratedClass && BP->GeneratedClass->IsChildOf(Parent) ? BP->GeneratedClass : nullptr;
	}
}

// 资产流程：先准备行为与七张图标，再创建缺失定义并分配总表后续 ID；旧编号和策划已存在配置保持。
// 最后补真鱼分类及空掉落片段，商店只上架四件可购买道具；随机奖励内容留在鱼定义供策划填写。
bool UCatConsumableAuthoringLibrary::CreateMissingConsumables()
{
	using namespace CatConsumableAuthoring;
	auto* Catalog = GetDefault<UCatInventorySettings>()->ItemCatalog.LoadSynchronous();
	auto* Shop = GetDefault<UCatShopEconomySettings>()->DefaultShopCatalogTable.LoadSynchronous();
	if (!Catalog || Catalog->GetRowStruct() != FCatItemCatalogRow::StaticStruct() || !Shop || Shop->GetRowStruct() != FCatShopCatalogTableRow::StaticStruct()) return false;
	UClass* Restore = Behavior(UCatGE_RestoreStamina::StaticClass(), TEXT("GE_DriedFish"));
	UClass* Lucky = Behavior(UCatGE_LuckyCatch::StaticClass(), TEXT("GE_LuckyClover"));
	UClass* Efficient = Behavior(UCatGE_FightEfficiency::StaticClass(), TEXT("GE_PawGloves"));
	UClass* Splash = Behavior(UCatGE_ItemSplash::StaticClass(), TEXT("GE_ItemSplash"));
	if (!Restore || !Lucky || !Efficient || !Splash || !Behavior(ACatGC_ItemSplash::StaticClass(), TEXT("GC_ItemSplash"))) return false;
	const TCHAR* Keys[] = {TEXT("DriedFish"), TEXT("PawGloves"), TEXT("LuckyClover"), TEXT("WaterSprayer"), TEXT("FakeFish"), TEXT("Horn"), TEXT("CastNet")};
	const TCHAR* Names[] = {TEXT("小鱼干"), TEXT("爪爪套"), TEXT("幸运四叶草"), TEXT("湿毛器"), TEXT("恶作剧假鱼"), TEXT("响响筒"), TEXT("渔网")};
	const TCHAR* Descriptions[] = {TEXT("搏斗外使用，立即回满绿色体力。"), TEXT("下一场搏斗降低体力消耗，结束后耗尽；同类不叠加。"),
		TEXT("提高下一次选鱼中受加成鱼种的概率，不保证稀有鱼。"), TEXT("左键喷水，右键对准附近水面补水。"),
		TEXT("放入鱼护，下一次开护喷开护者一脸水；一件触发一次。"), TEXT("输入文字并确认后向全队喊话，共三次。"),
		TEXT("一次捕获5至8条非巨型鱼；落点周围30秒不产生新鱼。")};
	UClass* Abilities[] = {UCatGA_RestoreStamina::StaticClass(), UCatGA_ApplyItemEffects::StaticClass(), UCatGA_ApplyItemEffects::StaticClass(),
		UCatGA_WaterSpray::StaticClass(), UCatGA_PlaceDecoy::StaticClass(), UCatGA_Horn::StaticClass(), UCatGA_CastNet::StaticClass()};
	UClass* Effects[] = {Restore, Efficient, Lucky, Splash, nullptr, nullptr, nullptr};
	const int32 Prices[] = {10, 5, -1, 5, -1, -1, 500};
	int32 NextId = 1;
	for (const auto& Pair : Catalog->GetRowMap()) NextId = FMath::Max(NextId, reinterpret_cast<const FCatItemCatalogRow*>(Pair.Value)->ItemId + 1);
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const FString Key(Keys[Index]); const FString Name = TEXT("Item_") + Key;
		const FString Path = TEXT("/Game/Catfishing/Data/Items/") + Name;
		auto* Icon = LoadObject<UTexture2D>(nullptr, *(TEXT("/Game/Catfishing/Items/Icons/T_") + Key + TEXT(".T_") + Key));
		UClass* Ability = Behavior(Abilities[Index], TEXT("GA_") + Key);
		if (!Icon || !Ability) return false;
		auto* Definition = LoadObject<UCatInventoryItemDefinition>(nullptr, *(Path + TEXT(".") + Name));
		if (!Definition)
		{
			Definition = NewObject<UCatInventoryItemDefinition>(CreatePackage(*Path), FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
			Definition->ItemId = NextId++; Definition->InventoryDisplayName = FText::FromString(Names[Index]);
			Definition->InventoryDescription = FText::FromString(Descriptions[Index]); Definition->InventoryThumbnail = Icon;
			Definition->InventorySemanticTags.AddTag(CatItemTags::Tool); Definition->WorldActorClass = ACatIconItem::StaticClass();
			Definition->InventoryMaxStackCount = Index == 0 || Index == 1 || Index == 2 ? 5 : 1;
			Definition->InventoryCarryLimit = Index == 6 ? 1 : 0;
			Definition->TeamPurchaseLimitPerRun = Index == 6 ? 2 : 0;
			Definition->InventoryActions.Insert({CatInventoryActionTags::Use, FText::FromString(TEXT("使用")), ECatInventoryActionQuantityMode::Single}, 0);
			auto* Use = NewObject<UCatItemUseFragment>(Definition, TEXT("Use"), RF_Transactional); Definition->Fragments.Add(Use);
			Use->AbilityClass = Ability; Use->ConsumeCount = Index == 3 || Index == 4 || Index == 5 ? 0 : 1;
			Use->BlockedStateTags.AddTag(CatStateTags::FishingFight);
			if (Effects[Index]) Use->Effects.Add(Effects[Index]);
			if (Index == 1) Use->BlockedStateTags.AddTag(CatItemEffectTags::NextFight);
			if (Index == 2) Use->BlockedStateTags.AddTag(CatItemEffectTags::NextFish);
			if (Index == 3) Use->ResourceCapacity = 5;
			if (Index == 5) { Use->ResourceCapacity = 3; Use->bConsumeWhenEmpty = true; }
			if (Index == 4)
			{
				Definition->InventorySemanticTags.AddTag(CatItemTags::Fish);
				auto* Open = NewObject<UCatItemContainerOpenFragment>(Definition, TEXT("ContainerOpen"), RF_Transactional);
				Open->Effect = Splash; Definition->Fragments.Add(Open);
			}
			FAssetRegistryModule::AssetCreated(Definition);
			if (!Definition->IsInventoryRuntimeDefinitionReady() || !Save(Definition)) return false;
		}
		const FName RowName(*FString::FromInt(Definition->ItemId));
		if (const auto* Existing = Catalog->FindRow<FCatItemCatalogRow>(RowName, TEXT("Consumables")); Existing && Existing->ItemDefinition.ToSoftObjectPath() != FSoftObjectPath(Definition)) return false;
		FCatItemCatalogRow Row; Row.ItemId = Definition->ItemId; Row.ItemDefinition = Definition; Catalog->AddRow(RowName, Row);
		if (Prices[Index] >= 0 && !Shop->FindRow<FCatShopCatalogTableRow>(FName(*Key), TEXT("Consumables"), false))
		{
			FCatShopCatalogTableRow Product; Product.ItemId = Definition->ItemId; Product.UnitPrice = Prices[Index];
			Product.PurchaseQuantity = 1; Product.InitialStock = Index == 6 ? 2 : 5; Product.bAlwaysStocked = true;
			Product.DisplayCategoryId = TEXT("Items"); Product.DisplayCategoryNameOverride = FText::FromString(TEXT("道具")); Product.SortOrder = 200 + Index;
			Product.bDailyRestock = Index != 6; Product.DailyRestockQuantity = Index == 6 ? 0 : 5;
			Shop->AddRow(FName(*Key), Product);
		}
	}
	for (const auto& Pair : Catalog->GetRowMap())
		if (auto* Fish = Cast<UCatFishDefinition>(reinterpret_cast<const FCatItemCatalogRow*>(Pair.Value)->ItemDefinition.LoadSynchronous()))
		{
			Fish->InventorySemanticTags.AddTag(CatItemTags::Fish);
			if (!Fish->FindFragment<UCatItemDropFragment>())
			{
				Fish->Fragments.Add(NewObject<UCatItemDropFragment>(Fish, TEXT("CaptureDrops"), RF_Transactional));
				Fish->bReceivesRarityBonus = Fish->RarityTierId == TEXT("Rare") || Fish->RarityTierId == TEXT("Event");
			}
			if (!Save(Fish)) return false;
		}
	return Save(Catalog) && Save(Shop);
}
