#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "K2Node_Variable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishAnimationConsumerAudit,
	"Catfishing.Unit.Fishing.Assets.FishAnimationCompatibilityConsumersAreInspectable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishAnimationConsumerAudit::RunTest(const FString& Parameters)
{
	// This is deliberately read-only: native behavior changes must not silently invalidate
	// the serialized three-animation presentation contract used by the formal fish catalog.
	const TArray<FString> AssetNames = {
		TEXT("ABPT_CatFishBase"), TEXT("ABP_Fish_RiverPattern"), TEXT("ABP_Fish_LittleSilver"),
		TEXT("ABP_Fish_LittleColor"), TEXT("ABP_Fish_ForestLongtail"), TEXT("ABP_Fish_SilvermoonTrout"),
		TEXT("ABP_Fish_LakeGiantShadow"), TEXT("ABP_Fish_Petal"), TEXT("ABP_Fish_Windbell"),
		TEXT("ABP_Fish_Salted"), TEXT("ABP_Fish_Stinky"), TEXT("ABP_Fish_Blackfish"),
		TEXT("ABP_Fish_Loach"), TEXT("ABP_Fish_EstuaryBass"), TEXT("ABP_Fish_Puffer"),
		TEXT("ABP_Fish_ElectricEel"), TEXT("ABP_Fish_Pike")};
	const TArray<FName> PresentationFields = { TEXT("MotionIntent"), TEXT("SwimPlayRate"),
		TEXT("IntendedSwimSpeedCentimetersPerSecond"), TEXT("FishLineAlignment"),
		TEXT("NormalizedLineLoad"), TEXT("bStrongConfrontation") };
	int32 LoadedAssets = 0;
	int32 GraphCount = 0;
	int32 MotionIntentConsumers = 0;
	int32 SwimPlayRateConsumers = 0;
	for (const FString& AssetName : AssetNames)
	{
		const FString ObjectPath = FString::Printf(TEXT("/Game/Catfishing/Fishing/Animation/Fish/%s.%s"),
			*AssetName, *AssetName);
		UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
		if (!TestNotNull(*FString::Printf(TEXT("formal fish animation loads: %s"), *ObjectPath), Blueprint))
		{
			continue;
		}
		++LoadedAssets;
		TestTrue(*FString::Printf(TEXT("formal fish animation retains native presentation ancestry: %s"), *AssetName),
			Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UCatFishAnimInstance::StaticClass()));
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
					const FName Field = Variable->GetVarName();
					if (PresentationFields.Contains(Field))
					{
						MotionIntentConsumers += Field == TEXT("MotionIntent") ? 1 : 0;
						SwimPlayRateConsumers += Field == TEXT("SwimPlayRate") ? 1 : 0;
						AddInfo(FString::Printf(TEXT("Event=fish_animation_field_consumer Asset=%s Graph=%s Node=%s Field=%s"),
							*ObjectPath, *Graph->GetName(), *Node->GetName(), *Field.ToString()));
					}
				}
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->DefaultValue.IsEmpty()) continue;
					for (const TCHAR* Intent : { TEXT("CalmOrInward"), TEXT("StrugglingOutward"), TEXT("AutoHauling") })
					{
						if (Pin->DefaultValue.Contains(Intent))
						{
							AddInfo(FString::Printf(TEXT("Event=fish_animation_intent_consumer Asset=%s Graph=%s Node=%s Pin=%s Value=%s"),
								*ObjectPath, *Graph->GetName(), *Node->GetName(), *Pin->PinName.ToString(), *Pin->DefaultValue));
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("all formal fish animation assets are audited"), LoadedAssets, AssetNames.Num());
	TestTrue(TEXT("loaded animation graph audit is not vacuous"), GraphCount > 0);
	AddInfo(FString::Printf(TEXT("Event=fish_animation_consumer_audit ReadOnly=True Assets=%d Graphs=%d MotionIntentConsumers=%d SwimPlayRateConsumers=%d Result=%s"),
		LoadedAssets, GraphCount, MotionIntentConsumers, SwimPlayRateConsumers, HasAnyErrors() ? TEXT("Failed") : TEXT("Inspected")));
	return !HasAnyErrors();
}

#endif
