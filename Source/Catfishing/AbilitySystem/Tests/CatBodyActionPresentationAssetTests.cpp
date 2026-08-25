// 这个自动化会通过 AssetTools/Factory 写入真实 Montage 资产，只能留在 Editor-only 测试编译分支；运行时和 Shipping 不应该携带资产生成路径。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/AnimMontageFactory.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatBodyActionFormalMontageAssetCreationTest,
	"Catfishing.Editor.AbilitySystem.CreateBodyActionMontageAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** BodyAction Montage 资产生成的一行映射；它把项目正式动作语义落到现有猫动画序列，生成的 Montage 再由 DefaultGame 的表现设置引用。 */
	struct FCatBodyActionMontageAssetRow
	{
		/** 生成后的 Montage 资产名；配置文件用同名对象路径读取它，重复运行时也用它识别同一份正式资源。 */
		const TCHAR* AssetName;

		/** 作为 Montage 内容来源的现有猫动画序列对象路径；这里只接受当前 Content 中可加载的 AnimSequence，不从测试临时包造动作。 */
		const TCHAR* SourceSequencePath;
	};

	/** BodyAction Montage 的统一保存目录；所有非 Fishing 长动作共用这一层，避免资源散进各领域目录后难以审计。 */
	const TCHAR* BodyActionMontagePackageRoot = TEXT("/Game/Catfishing/Animation/BodyAction");

	/** 返回本轮要维护的 BodyAction Montage 清单；每行都必须对应 DefaultGame 里的一个动作配置。 */
	TConstArrayView<FCatBodyActionMontageAssetRow> GetBodyActionMontageAssetRows()
	{
		static const FCatBodyActionMontageAssetRow Rows[] = {
			{ TEXT("AM_BodyAction_RequestSacrifice"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Sitting_02-IP.Sitting_02-IP") },
			{ TEXT("AM_BodyAction_CampRest"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Lying_00-IP.Lying_00-IP") },
			{ TEXT("AM_BodyAction_CampfirePlayback"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Sitting_01-IP.Sitting_01-IP") },
			{ TEXT("AM_BodyAction_TransferFishToTank"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Stand_00-IP.Stand_00-IP") },
			{ TEXT("AM_BodyAction_RescueCharacterToCamp"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Trans_Sitting_To_Stand-IP.Trans_Sitting_To_Stand-IP") },
			{ TEXT("AM_BodyAction_RepairRodAtCamp"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Action_Scratching-IP.Action_Scratching-IP") },
			{ TEXT("AM_BodyAction_UseHerbOnCharacter"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Stand_Drinking_01-IP.Stand_Drinking_01-IP") },
			{ TEXT("AM_BodyAction_ConsumeFish"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Eating_01-IP.Eating_01-IP") },
			{ TEXT("AM_BodyAction_BeginTheft"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Loco_Sneak-IP.Loco_Sneak-IP") },
			{ TEXT("AM_BodyAction_CatchTheft"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Attack_Left-IP.Attack_Left-IP") },
			{ TEXT("AM_BodyAction_RequestManualHelp"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Agressive_01-IP.Agressive_01-IP") },
			{ TEXT("AM_BodyAction_RequestMischief"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Attack_Right-IP.Attack_Right-IP") },
			{ TEXT("AM_BodyAction_PlaceProtectionSign"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Action_Scratching-IP.Action_Scratching-IP") },
			{ TEXT("AM_BodyAction_CompleteShakeDry"), TEXT("/Game/Animalia/Cat/Animations/InPlace/Stand_03_LookAround-IP.Stand_03_LookAround-IP") },
		};
		return Rows;
	}

	/**
	 * BodyAction Montage 资产生成测试的核心夹具。
	 * 流程：先加载正式动画序列，再按包文件是否存在决定读取或创建 Montage；缺 Montage 时用引擎 Factory 从序列生成，随后注册资产并保存包；已有 Montage 则只做来源校验，不覆盖人工后续微调。
	 */
	UAnimMontage* LoadOrCreateBodyActionMontage(FAutomationTestBase& Test,
		const FCatBodyActionMontageAssetRow& Row)
	{
		UAnimSequence* SourceAnimation = LoadObject<UAnimSequence>(nullptr, Row.SourceSequencePath);
		if (!Test.TestNotNull(FString::Printf(TEXT("BodyAction source animation loads: %s"), Row.SourceSequencePath),
			SourceAnimation))
		{
			return nullptr;
		}

		const FString PackageName = FString::Printf(TEXT("%s/%s"), BodyActionMontagePackageRoot, Row.AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, Row.AssetName);
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());
		UAnimMontage* Montage = FindObject<UAnimMontage>(nullptr, *ObjectPath);
		if (!Montage && FPaths::FileExists(PackageFilename))
		{
			Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
		}
		bool bCreated = false;
		if (!Montage)
		{
			UPackage* Package = CreatePackage(*PackageName);
			if (!Test.TestNotNull(FString::Printf(TEXT("创建 BodyAction Montage 包: %s"), *PackageName), Package))
			{
				return nullptr;
			}
			UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
			if (!Test.TestNotNull(TEXT("创建 AnimMontageFactory"), Factory))
			{
				return nullptr;
			}
			Factory->SourceAnimation = SourceAnimation;
			UObject* CreatedAsset = Factory->FactoryCreateNew(
				UAnimMontage::StaticClass(),
				Package,
				FName(Row.AssetName),
				RF_Public | RF_Standalone,
				nullptr,
				GWarn);
			Montage = Cast<UAnimMontage>(CreatedAsset);
			if (!Test.TestNotNull(FString::Printf(TEXT("创建 BodyAction Montage 资产: %s"), *ObjectPath), Montage))
			{
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Montage);
			bCreated = true;
		}

		const bool bHasExpectedSource =
			Montage->SlotAnimTracks.Num() > 0
			&& Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0
			&& Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == SourceAnimation;
		if (!Test.TestTrue(FString::Printf(TEXT("BodyAction Montage 使用预期动画序列: %s"), *ObjectPath),
			bHasExpectedSource))
		{
			return nullptr;
		}

		UAnimMontageFactory::EnsureStartingSection(Montage);
		Montage->MarkPackageDirty();
		UPackage* Package = Montage->GetOutermost();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Montage, *PackageFilename, SaveArgs);
		Test.TestTrue(FString::Printf(TEXT("保存 BodyAction Montage 资产: %s"), *ObjectPath), bSaved);
		UE_LOG(LogTemp, Display, TEXT("BODY_ACTION_MONTAGE_ASSET_READY Asset=%s Source=%s Created=%s"),
			*ObjectPath,
			Row.SourceSequencePath,
			bCreated ? TEXT("true") : TEXT("false"));
		return bSaved ? Montage : nullptr;
	}
}

bool FCatBodyActionFormalMontageAssetCreationTest::RunTest(const FString& Parameters)
{
	// BodyAction Montage 资产生成流程：
	// 1. 从现有 Animalia 猫动画序列创建可被 Character::PlayAnimMontage 播放的正式 Montage 资产。
	// 2. 对已存在资产只校验首段动画来源，不主动重写，避免后续美术微调被自动化覆盖。
	// 3. 每个资产保存到同一个 BodyAction 目录，并输出稳定 PASS 标记供 CharacterGrowthCondition / Delivery 证据引用。
	(void)Parameters;

	int32 ReadyCount = 0;
	for (const FCatBodyActionMontageAssetRow& Row : GetBodyActionMontageAssetRows())
	{
		if (LoadOrCreateBodyActionMontage(*this, Row))
		{
			++ReadyCount;
		}
	}
	TestEqual(TEXT("BodyAction formal Montage asset count"), ReadyCount, GetBodyActionMontageAssetRows().Num());
	if (ReadyCount == GetBodyActionMontageAssetRows().Num())
	{
		UE_LOG(LogTemp, Display, TEXT("CREATE_BODY_ACTION_MONTAGE_ASSETS_PASS AssetCount=%d Directory=%s"),
			ReadyCount,
			BodyActionMontagePackageRoot);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
