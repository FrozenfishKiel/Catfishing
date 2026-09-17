#include "CatFishUseAuthoringLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "Animation/AnimMontage.h"
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
		UCatItemUseFragment* Fragment = Definition->FindFragment<UCatItemUseFragment>();
		Definition->Modify();
		Definition->Fragments.RemoveAll([](const auto& Candidate) { return !IsValid(Candidate); });
		if (!Fragment)
		{
			Fragment = NewObject<UCatItemUseFragment>(Definition, NAME_None, RF_Transactional);
			Definition->Fragments.Add(Fragment);
		}
		Fragment->AbilityClass = UCatGA_ConsumeFish::StaticClass();
		Fragment->Effects = { UCatGE_FishExperience::StaticClass() };
		Fragment->ConsumeCount = 1;
		Fragment->CommitDelay = 1.0f;
		Fragment->Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_ConsumeFish.AM_BodyAction_ConsumeFish"));
		if (!Fragment->IsRuntimeReady()) return false;
		Definition->MarkPackageDirty();
		FSavePackageArgs Save;
		Save.TopLevelFlags = RF_Public | RF_Standalone;
		Save.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Definition->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Definition->GetOutermost(), Definition, *Filename, Save)) return false;
	}
	return true;
}
