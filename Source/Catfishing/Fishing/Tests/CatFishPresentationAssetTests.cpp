#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Data/CatFishDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCreateFishPresentationAssetsTest,
	"Catfishing.Editor.Fishing.CreateFishPresentationAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishPresentationAssetsContractTest,
	"Catfishing.Unit.Fishing.Assets.FormalFishPresentationsUseCatalogOwnedAnimBlueprintChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishPresentationAssetTests
{
	struct FMapping
	{
		const TCHAR* FishAssetName;
		const TCHAR* SourceKey;
		const TCHAR* StruggleAssetSuffix;
	};

	static constexpr FMapping Mappings[] =
	{
		{ TEXT("RiverPattern"), TEXT("koi"), TEXT("fastswim_anim") },
		{ TEXT("LittleSilver"), TEXT("mackerel"), TEXT("fastswim_anim") },
		{ TEXT("LittleColor"), TEXT("clownfish"), TEXT("fastswim_anim") },
		{ TEXT("ForestLongtail"), TEXT("barracuda"), TEXT("fastswim_anim") },
		{ TEXT("SilvermoonTrout"), TEXT("trout"), TEXT("fastswim_anim") },
		{ TEXT("LakeGiantShadow"), TEXT("arapaima"), TEXT("fastswim_anim") },
		{ TEXT("Petal"), TEXT("discus_v2"), TEXT("fastswim_anim") },
		{ TEXT("Windbell"), TEXT("butterfly_fish"), TEXT("fastswim_anim") },
		{ TEXT("Salted"), TEXT("atlantic_cod"), TEXT("fastswim_anim") },
		{ TEXT("Stinky"), TEXT("carp"), TEXT("fastswim_anim") },
		{ TEXT("Blackfish"), TEXT("black_redeye_fish"), TEXT("fastswim_anim") },
		{ TEXT("Loach"), TEXT("oreochromis"), TEXT("fastswim_anim") },
		{ TEXT("EstuaryBass"), TEXT("peacock_bass"), TEXT("fastswim_anin") },
		{ TEXT("Puffer"), TEXT("frontosa"), TEXT("fastswim_anim") },
		{ TEXT("ElectricEel"), TEXT("electric_catfish"), TEXT("fastswim_anim") },
		{ TEXT("Pike"), TEXT("pike"), TEXT("fastswim_anim") },
	};

	static FString FishAssetPath(const FMapping& Mapping)
	{
		return FString::Printf(TEXT("/Game/Catfishing/Data/Fish/Fish_%s.Fish_%s"),
			Mapping.FishAssetName, Mapping.FishAssetName);
	}

	static FString PresentationPackagePath(const FMapping& Mapping)
	{
		return FString::Printf(TEXT("/Game/Catfishing/Data/Fish/Presentation/FishPresentation_%s"),
			Mapping.FishAssetName);
	}

	static FString AnimBlueprintPackagePath(const FMapping& Mapping)
	{
		return FString::Printf(TEXT("/Game/Catfishing/Fishing/Animation/Fish/ABP_Fish_%s"),
			Mapping.FishAssetName);
	}

	static FString MeshPath(const FMapping& Mapping)
	{
		const FString Key(Mapping.SourceKey);
		const FString RigSuffix = Key == TEXT("great_white_shark") || Key == TEXT("octopus")
			? TEXT("rig_exp_SK") : TEXT("rig_exp20_SK");
		return FString::Printf(TEXT("/Game/Underwater_life/Mesh/Skeletal_mesh/Animals/%s_%s.%s_%s"),
			Mapping.SourceKey, *RigSuffix, Mapping.SourceKey, *RigSuffix);
	}

	static FString AnimationPath(const FMapping& Mapping, const TCHAR* Suffix)
	{
		return FString::Printf(TEXT("/Game/Underwater_life/Animations/%s_%s.%s_%s"),
			Mapping.SourceKey, Suffix, Mapping.SourceKey, Suffix);
	}

	static bool SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	enum class EBaseNodeRole : uint8
	{
		Calm,
		Struggle,
		Exhausted,
		Unknown
	};

	static EBaseNodeRole ResolveNodeRole(const UAnimGraphNode_SequencePlayer* Node)
	{
		if (!Node)
		{
			return EBaseNodeRole::Unknown;
		}
		const FString SequenceName = GetNameSafe(Node->Node.GetSequence());
		if (SequenceName.Contains(TEXT("slowswim"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Calm;
		if (SequenceName.Contains(TEXT("fastswim"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Struggle;
		if (SequenceName.Contains(TEXT("die"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Exhausted;

		FString OuterChain;
		for (const UObject* Outer = Node->GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			OuterChain += TEXT("/") + Outer->GetName();
		}
		if (OuterChain.Contains(TEXT("AutoHauling"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Exhausted;
		if (OuterChain.Contains(TEXT("Struggle"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Struggle;
		if (OuterChain.Contains(TEXT("Calm"), ESearchCase::IgnoreCase)) return EBaseNodeRole::Calm;
		return EBaseNodeRole::Unknown;
	}

	static void GetBaseNodes(UAnimBlueprint* BaseBlueprint,
		TMap<EBaseNodeRole, UAnimGraphNode_SequencePlayer*>& OutNodes)
	{
		OutNodes.Reset();
		TArray<UAnimGraphNode_SequencePlayer*> Nodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(BaseBlueprint, Nodes);
		for (UAnimGraphNode_SequencePlayer* Node : Nodes)
		{
			const EBaseNodeRole Role = ResolveNodeRole(Node);
			if (Role != EBaseNodeRole::Unknown)
			{
				OutNodes.FindOrAdd(Role) = Node;
			}
		}
	}

	static UAnimBlueprint* LoadBaseTemplate(FAutomationTestBase& Test)
	{
		constexpr TCHAR BaseObjectPath[] =
			TEXT("/Game/Catfishing/Fishing/Animation/Fish/ABPT_CatFishBase.ABPT_CatFishBase");
		UAnimBlueprint* Base = LoadObject<UAnimBlueprint>(nullptr, BaseObjectPath);
		return Test.TestNotNull(TEXT("formal skeleton-free fish AnimBP base exists"), Base) ? Base : nullptr;
	}

	static UCatFishPresentationDefinition* CreateOrLoadPresentation(const FMapping& Mapping)
	{
		const FString PackagePath = PresentationPackagePath(Mapping);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (UCatFishPresentationDefinition* Existing = LoadObject<UCatFishPresentationDefinition>(
			nullptr, *(PackagePath + TEXT(".") + AssetName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackagePath);
		UCatFishPresentationDefinition* Created = NewObject<UCatFishPresentationDefinition>(Package,
			*AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Created);
		return Created;
	}

	static UAnimBlueprint* CreateOrLoadChildAnimBlueprint(FAutomationTestBase& Test, const FMapping& Mapping,
		UAnimBlueprint* Base, USkeletalMesh* Mesh)
	{
		const FString PackagePath = AnimBlueprintPackagePath(Mapping);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		UAnimBlueprint* Child = LoadObject<UAnimBlueprint>(nullptr, *(PackagePath + TEXT(".") + AssetName));
		if (!Child)
		{
			UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
			Factory->BlueprintType = BPTYPE_Normal;
			Factory->ParentClass = Base->GeneratedClass;
			Factory->TargetSkeleton = Mesh->GetSkeleton();
			Factory->PreviewSkeletalMesh = Mesh;
			Factory->bTemplate = false;
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			Child = Cast<UAnimBlueprint>(AssetTools.CreateAsset(AssetName,
				FPackageName::GetLongPackagePath(PackagePath), UAnimBlueprint::StaticClass(), Factory));
		}
		if (!Test.TestNotNull(FString::Printf(TEXT("creates child AnimBP %s"), Mapping.FishAssetName), Child))
		{
			return nullptr;
		}
		Child->ParentClass = Base->GeneratedClass;
		Child->TargetSkeleton = Mesh->GetSkeleton();
		Child->bIsTemplate = false;
		Child->SetPreviewMesh(Mesh);
		return Child;
	}

	static bool AreAnimationAssetsCompatible(USkeletalMesh* Mesh, const TArray<UAnimSequenceBase*>& Animations)
	{
		if (!Mesh || !Mesh->GetSkeleton()) return false;
		for (const UAnimSequenceBase* Animation : Animations)
		{
			if (!Animation || !Animation->GetSkeleton()
				|| !Animation->GetSkeleton()->IsCompatibleMesh(Mesh))
			{
				return false;
			}
		}
		return true;
	}

	static bool ConfigureOne(FAutomationTestBase& Test, const FMapping& Mapping, UAnimBlueprint* Base)
	{
		UCatFishDefinition* Fish = LoadObject<UCatFishDefinition>(nullptr, *FishAssetPath(Mapping));
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath(Mapping));
		UAnimSequenceBase* Calm = LoadObject<UAnimSequenceBase>(nullptr,
			*AnimationPath(Mapping, TEXT("slowswim_anim")));
		UAnimSequenceBase* Struggle = LoadObject<UAnimSequenceBase>(nullptr,
			*AnimationPath(Mapping, Mapping.StruggleAssetSuffix));
		UAnimSequenceBase* Exhausted = LoadObject<UAnimSequenceBase>(nullptr,
			*AnimationPath(Mapping, TEXT("die_anim")));
		if (!Test.TestNotNull(FString::Printf(TEXT("loads formal fish %s"), Mapping.FishAssetName), Fish)
			|| !Test.TestNotNull(FString::Printf(TEXT("loads mapped mesh %s"), Mapping.SourceKey), Mesh)
			|| !Test.TestTrue(FString::Printf(TEXT("mapped animations match %s skeleton"), Mapping.SourceKey),
				AreAnimationAssetsCompatible(Mesh, { Calm, Struggle, Exhausted })))
		{
			return false;
		}

		TMap<EBaseNodeRole, UAnimGraphNode_SequencePlayer*> BaseNodes;
		GetBaseNodes(Base, BaseNodes);
		if (!Test.TestEqual(TEXT("base exposes exactly three override nodes"), BaseNodes.Num(), 3))
		{
			return false;
		}
		UAnimBlueprint* Child = CreateOrLoadChildAnimBlueprint(Test, Mapping, Base, Mesh);
		if (!Child) return false;
		Child->ParentAssetOverrides.Reset();
		Child->ParentAssetOverrides.Emplace(BaseNodes[EBaseNodeRole::Calm]->NodeGuid, Calm);
		Child->ParentAssetOverrides.Emplace(BaseNodes[EBaseNodeRole::Struggle]->NodeGuid, Struggle);
		Child->ParentAssetOverrides.Emplace(BaseNodes[EBaseNodeRole::Exhausted]->NodeGuid, Exhausted);
		FBlueprintEditorUtils::RefreshAllNodes(Child);
		FKismetEditorUtilities::CompileBlueprint(Child, EBlueprintCompileOptions::SkipGarbageCollection);
		if (!Test.TestTrue(FString::Printf(TEXT("child AnimBP %s compiles"), Mapping.FishAssetName),
			Child->Status == BS_UpToDate) || !SaveAsset(Child))
		{
			return false;
		}

		UCatFishPresentationDefinition* Presentation = CreateOrLoadPresentation(Mapping);
		if (!Test.TestNotNull(FString::Printf(TEXT("creates presentation %s"), Mapping.FishAssetName), Presentation))
		{
			return false;
		}
		Presentation->SkeletalMesh = Mesh;
		Presentation->AnimInstanceClass = Child->GeneratedClass;
		Presentation->CalmAnimation = Calm;
		Presentation->StruggleAnimation = Struggle;
		Presentation->ExhaustedAnimation = Exhausted;
		Presentation->LandedAnimation = Exhausted;
		Presentation->MeshReferenceWeightKilograms = FMath::Sqrt(
			Fish->MinimumWeightKilograms * Fish->MaximumWeightKilograms);
		Presentation->MinimumUniformScale = 0.8;
		Presentation->MaximumUniformScale = 1.25;
		Presentation->EncounterMeshRelativeTransform = FTransform(
			FRotator::ZeroRotator, FVector(0.0, 0.0, -5.0));
		Presentation->LandedMeshRelativeTransform = FTransform::Identity;
		Presentation->CarriedMeshRelativeTransform = FTransform::Identity;
		Presentation->ExhaustedVisualRollDegrees = 90.0;
		Presentation->LandedActorRollDegrees = 90.0;
		if (!Test.TestTrue(FString::Printf(TEXT("presentation %s is complete"), Mapping.FishAssetName),
			Presentation->IsRuntimeDefinitionReady()) || !SaveAsset(Presentation))
		{
			return false;
		}

		Fish->PresentationDefinition = Presentation;
		return SaveAsset(Fish);
	}

	static bool ValidateOne(FAutomationTestBase& Test, const FMapping& Mapping, UAnimBlueprint* Base)
	{
		UCatFishDefinition* Fish = LoadObject<UCatFishDefinition>(nullptr, *FishAssetPath(Mapping));
		UCatFishPresentationDefinition* Presentation = Fish ? Fish->LoadRuntimePresentationDefinition() : nullptr;
		USkeletalMesh* Mesh = Presentation ? Presentation->SkeletalMesh.LoadSynchronous() : nullptr;
		UClass* AnimClass = Presentation ? Presentation->AnimInstanceClass.LoadSynchronous() : nullptr;
		const IAnimClassInterface* AnimInterface = IAnimClassInterface::GetFromClass(AnimClass);
		const bool bDirectPath = Fish && Fish->PresentationDefinition.ToSoftObjectPath().GetLongPackageName()
			== PresentationPackagePath(Mapping);
		Test.TestTrue(FString::Printf(TEXT("%s uses its direct catalog presentation reference"), Mapping.FishAssetName),
			bDirectPath);
		Test.TestNotNull(FString::Printf(TEXT("%s presentation resolves"), Mapping.FishAssetName), Presentation);
		Test.TestTrue(FString::Printf(TEXT("%s AnimBP inherits native fish base"), Mapping.FishAssetName),
			AnimClass && AnimClass->IsChildOf(UCatFishAnimInstance::StaticClass()));
		Test.TestTrue(FString::Printf(TEXT("%s AnimBP skeleton matches its mesh"), Mapping.FishAssetName),
			Mesh && AnimInterface && AnimInterface->GetTargetSkeleton()
			&& AnimInterface->GetTargetSkeleton()->IsCompatibleMesh(Mesh));
		Test.TestTrue(FString::Printf(TEXT("%s source animations match its mesh"), Mapping.FishAssetName),
			Presentation && AreAnimationAssetsCompatible(Mesh,
				{ Presentation->CalmAnimation.LoadSynchronous(), Presentation->StruggleAnimation.LoadSynchronous(),
					Presentation->ExhaustedAnimation.LoadSynchronous(), Presentation->LandedAnimation.LoadSynchronous() }));

		UAnimBlueprint* Child = AnimClass ? Cast<UAnimBlueprint>(AnimClass->ClassGeneratedBy) : nullptr;
		TMap<EBaseNodeRole, UAnimGraphNode_SequencePlayer*> BaseNodes;
		GetBaseNodes(Base, BaseNodes);
		TMap<FGuid, UAnimationAsset*> OverrideByGuid;
		if (Child)
		{
			for (const FAnimParentNodeAssetOverride& Override : Child->ParentAssetOverrides)
			{
				OverrideByGuid.Add(Override.ParentNodeGuid, Override.NewAsset);
			}
		}
		Test.TestTrue(FString::Printf(TEXT("%s Calm override equals presentation DA"), Mapping.FishAssetName),
			BaseNodes.Contains(EBaseNodeRole::Calm)
			&& OverrideByGuid.FindRef(BaseNodes[EBaseNodeRole::Calm]->NodeGuid) == Presentation->CalmAnimation.Get());
		Test.TestTrue(FString::Printf(TEXT("%s Struggle override equals presentation DA"), Mapping.FishAssetName),
			BaseNodes.Contains(EBaseNodeRole::Struggle)
			&& OverrideByGuid.FindRef(BaseNodes[EBaseNodeRole::Struggle]->NodeGuid) == Presentation->StruggleAnimation.Get());
		Test.TestTrue(FString::Printf(TEXT("%s Exhausted override equals presentation DA"), Mapping.FishAssetName),
			BaseNodes.Contains(EBaseNodeRole::Exhausted)
			&& OverrideByGuid.FindRef(BaseNodes[EBaseNodeRole::Exhausted]->NodeGuid) == Presentation->ExhaustedAnimation.Get());
		return !Test.HasAnyErrors();
	}
}

bool FCatCreateFishPresentationAssetsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAnimBlueprint* Base = CatFishPresentationAssetTests::LoadBaseTemplate(*this);
	if (!Base) return false;
	for (const CatFishPresentationAssetTests::FMapping& Mapping : CatFishPresentationAssetTests::Mappings)
	{
		if (!CatFishPresentationAssetTests::ConfigureOne(*this, Mapping, Base))
		{
			return false;
		}
	}
	AddInfo(TEXT("CREATE_FISH_PRESENTATION_ASSETS_PASS Count=16 Base=ABPT_CatFishBase"));
	return !HasAnyErrors();
}

bool FCatFishPresentationAssetsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAnimBlueprint* Base = LoadObject<UAnimBlueprint>(nullptr,
		TEXT("/Game/Catfishing/Fishing/Animation/Fish/ABPT_CatFishBase.ABPT_CatFishBase"));
	if (!TestNotNull(TEXT("skeleton-free fish AnimBP base exists"), Base)) return false;
	TestTrue(TEXT("fish AnimBP base is a template"), Base->bIsTemplate);
	TestNull(TEXT("fish AnimBP base has no target skeleton"), Base->TargetSkeleton);
	TestTrue(TEXT("fish AnimBP base inherits UCatFishAnimInstance"),
		Base->ParentClass == UCatFishAnimInstance::StaticClass());
	for (const CatFishPresentationAssetTests::FMapping& Mapping : CatFishPresentationAssetTests::Mappings)
	{
		CatFishPresentationAssetTests::ValidateOne(*this, Mapping, Base);
	}
	return !HasAnyErrors();
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
