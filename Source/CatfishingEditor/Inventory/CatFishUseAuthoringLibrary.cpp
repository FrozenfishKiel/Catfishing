#include "CatFishUseAuthoringLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/Fragments/CatConsumableEffectFragment.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

// 迁移流程：枚举现有正式鱼定义，只给可食用鱼补片段；保存前核验单条消费和即时效果，不覆盖人工冲突配置。
bool UCatFishUseAuthoringLibrary::MigrateExistingFishUseEffects()
{
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	Registry.Get().SearchAllAssets(true);
	FARFilter Filter;
	Filter.ClassPaths.Add(UCatFishDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add("/Game/Catfishing/Data/Fish");
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(Filter, Assets);
	if (Assets.IsEmpty()) return false;
	for (const FAssetData& Asset : Assets)
	{
		UCatFishDefinition* Definition = Cast<UCatFishDefinition>(Asset.GetAsset());
		if (!Definition || !Definition->IsEdible()) continue;
		UCatConsumableEffectFragment* Fragment = Definition->FindFragment<UCatConsumableEffectFragment>();
		if (Fragment)
		{
			if (!Fragment->IsRuntimeReady() || Fragment->ConsumeCount != 1) return false;
			continue;
		}
		Definition->Modify();
		Fragment = NewObject<UCatConsumableEffectFragment>(Definition, NAME_None, RF_Transactional);
		Fragment->EffectClass = UCatGE_FishExperience::StaticClass();
		Fragment->ConsumeCount = 1;
		Definition->Fragments.Add(Fragment);
		Definition->MarkPackageDirty();
		FSavePackageArgs Save;
		Save.TopLevelFlags = RF_Public | RF_Standalone;
		Save.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Definition->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Definition->GetOutermost(), Definition, *Filename, Save)) return false;
	}
	return true;
}
