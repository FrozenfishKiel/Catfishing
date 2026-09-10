#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionAssetContractTest,
	"Catfishing.Locomotion.Contract.FormalAnimationHasSingleFootCorrectionOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatLocomotionAssetContractTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animalia/Cat/ABP_Cat.ABP_Cat"));
	UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Animalia/Cat/WalkToRun.WalkToRun"));
	if (!TestNotNull(TEXT("formal ABP is loadable"), Blueprint) || !TestNotNull(TEXT("formal locomotion blendspace is loadable"), BlendSpace)) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 ActiveNodes = 0;
	for (UEdGraph* Graph : Graphs)
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const bool bLinked = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin && !Pin->LinkedTo.IsEmpty(); });
			if (!bLinked) continue;
			const FString ClassName = Node->GetClass()->GetName();
			++ActiveNodes;
			AddInfo(FString::Printf(TEXT("Event=locomotion_abp_node_audited Graph=%s Class=%s Title=%s"), *Graph->GetName(), *ClassName,
				*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(" / "))));
			TestFalse(TEXT("existing ABP must not simultaneously own terrain/stride IK"), ClassName.Contains(TEXT("FootPlacement"))
				|| ClassName.Contains(TEXT("StrideWarping")) || ClassName.Contains(TEXT("TwoBoneIK"))
				|| ClassName.Contains(TEXT("Fabrik")) || ClassName.Contains(TEXT("ControlRig")));
		}
	TestTrue(TEXT("audit traverses real linked graph nodes"), ActiveNodes > 10);
	TSet<FString> Expected = {TEXT("Stand_00-IP"), TEXT("Loco_Walk-IP"), TEXT("Loco_Run-IP")};
	for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
	{
		if (!Sample.Animation) continue;
		TestTrue(TEXT("every production locomotion sample has a runtime calibration profile"), Expected.Contains(Sample.Animation->GetName()));
		Expected.Remove(Sample.Animation->GetName());
		AddInfo(FString::Printf(TEXT("Event=locomotion_blend_sample_audited Animation=%s Input=%s RateScale=%.3f"),
			*Sample.Animation->GetPathName(), *Sample.SampleValue.ToString(), Sample.RateScale));
	}
	TestEqual(TEXT("standing, walking and running consumers are all audited"), Expected.Num(), 0);
	return !HasAnyErrors();
}

#endif
