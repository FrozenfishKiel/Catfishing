#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Variable.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingForceAssetReferencesTest,
	"Catfishing.Unit.Fishing.Assets.ForceMigrationHasNoLegacyBlueprintConsumers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingForceAssetReferencesTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	const TArray<FName> LegacyNames = {TEXT("AccelerationPerStrength"), TEXT("DriveResponseSeconds"), TEXT("TensionResponseRangeCentimeters"),
		TEXT("MinimumCarrierAwaySpeedMultiplier"), TEXT("MaximumAwaySpeedMultiplier"), TEXT("CarrierAwaySpeedMultiplier")};
	int32 GraphCount = 0;
	int32 UnresolvedAssets = 0;
	for (const FAssetData& Asset : Assets)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		if (!TestNotNull(*FString::Printf(TEXT("load blueprint for reference audit: %s"), *Asset.PackageName.ToString()), Blueprint)) continue;
		if (!Blueprint->ParentClass)
		{
			++UnresolvedAssets;
			AddWarning(FString::Printf(TEXT("AssetReferenceAuditIncomplete Asset=%s Reason=MissingParentClass Action=RestoreOrMigrateBeforeRemovingSerializedFields"), *Asset.PackageName.ToString()));
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			++GraphCount;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
				{
					if (LegacyNames.Contains(Variable->GetVarName())) AddError(FString::Printf(
						TEXT("LegacyFieldConsumer Asset=%s Graph=%s Field=%s"), *Asset.PackageName.ToString(), *Graph->GetName(), *Variable->GetVarName().ToString()));
				}
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin) continue;
					for (const FName Legacy : LegacyNames)
					{
						if (Pin->DefaultValue.Contains(Legacy.ToString())) AddError(FString::Printf(
							TEXT("LegacyStringConsumer Asset=%s Graph=%s Pin=%s Field=%s"), *Asset.PackageName.ToString(), *Graph->GetName(), *Pin->PinName.ToString(), *Legacy.ToString()));
					}
				}
			}
		}
	}
	TestTrue(TEXT("project blueprint audit must not be vacuous"), Assets.Num() > 0 && GraphCount > 0);
	UClass* CharacterClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	const ACatCharacter* CharacterDefaults = CharacterClass ? Cast<ACatCharacter>(CharacterClass->GetDefaultObject()) : nullptr;
	TestNotNull(TEXT("formal character Blueprint uses the new movement subobject"), CharacterDefaults
		? Cast<UCatCharacterMovementComponent>(CharacterDefaults->GetCharacterMovement()) : nullptr);
	AddInfo(FString::Printf(TEXT("Event=fishing_force_asset_reference_audit Blueprints=%d Graphs=%d LegacyFields=%d UnresolvedAssets=%d GraphResult=%s"),
		Assets.Num(), GraphCount, LegacyNames.Num(), UnresolvedAssets, HasAnyErrors() ? TEXT("Failed") : TEXT("NoReferencesFoundInLoadedGraphs")));
	return !HasAnyErrors();
}

#endif
