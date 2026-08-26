#include "CatFishStateTreeAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Fishing/Behavior/CatFishBehaviorStateTree.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Fishing/CatFishingStateTreeNodes.h"
#include "Fishing/StateTree/CatFishingSessionStateTreeSchema.h"
#include "Misc/PackageName.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeState.h"
#include "UObject/SavePackage.h"

bool UCatFishStateTreeAuthoringLibrary::CreateOrUpdateDefaultFishBehaviorStateTree()
{
	static const FString PackageName = TEXT("/Game/Data/StateTrees/ST_FishFight");
	static const FName AssetName = TEXT("ST_FishFight");
	const FString ObjectPath = PackageName + TEXT(".") + AssetName.ToString();
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return false;
	}

	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *ObjectPath);
	const bool bNewAsset = StateTree == nullptr;
	if (!StateTree)
	{
		StateTree = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	if (!StateTree)
	{
		return false;
	}

	// 整份 EditorData 由本工具拥有；重复执行会稳定重建默认拓扑，不在旧节点上做脆弱的增量修补。
	FStateTreeEditorModule& EditorModule = FStateTreeEditorModule::GetModule();
	const TSubclassOf<UStateTreeSchema> SchemaClass = UCatFishBehaviorStateTreeSchema::StaticClass();
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree,
		EditorModule.GetEditorDataClass(SchemaClass), NAME_None, RF_Transactional);
	if (!EditorData)
	{
		return false;
	}
	StateTree->EditorData = EditorData;
	EditorData->Schema = NewObject<UCatFishBehaviorStateTreeSchema>(EditorData, SchemaClass,
		NAME_None, RF_Transactional);
	EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(EditorData,
		EditorModule.GetEditorSchemaClass(SchemaClass), NAME_None, RF_Transactional);

	UStateTreeState& Root = EditorData->AddSubTree(TEXT("Hooked Fish Behavior"));
	Root.Description = TEXT("高层行为拓扑；位置、鱼线、力量和体力只由服务器固定步模拟结算。");
	UStateTreeState& Struggling = Root.AddChildState(TEXT("Struggling Outward"));
	UStateTreeState& Calm = Root.AddChildState(TEXT("Calm Direction Selection"));

	auto& StruggleTask = Struggling.AddTask<FCatFishBehaviorStateTask>();
	StruggleTask.GetInstanceData().MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	Struggling.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted,
		EStateTreeTransitionType::GotoState, &Calm);

	auto& CalmTask = Calm.AddTask<FCatFishBehaviorStateTask>();
	CalmTask.GetInstanceData().MotionIntent = ECatFishMotionIntent::CalmOrInward;
	Calm.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted,
		EStateTreeTransitionType::GotoState, &Struggling);

	FStateTreeCompilerLog CompilerLog;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog))
	{
		return false;
	}
	StateTree->MarkPackageDirty();
	if (bNewAsset)
	{
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName,
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, StateTree, *Filename, SaveArgs);
}

bool UCatFishStateTreeAuthoringLibrary::CreateOrUpdateDefaultFishingSessionStateTree()
{
	static const FString PackageName = TEXT("/Game/Data/StateTrees/ST_FishingSession");
	static const FName AssetName = TEXT("ST_FishingSession");
	const FString ObjectPath = PackageName + TEXT(".") + AssetName.ToString();
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return false;
	}

	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *ObjectPath);
	const bool bNewAsset = StateTree == nullptr;
	if (!StateTree)
	{
		StateTree = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	if (!StateTree)
	{
		return false;
	}

	// 与鱼行为树相同：整份 EditorData 稳定重建，避免依赖旧资产中的节点 Guid 做脆弱的二进制增量修补。
	FStateTreeEditorModule& EditorModule = FStateTreeEditorModule::GetModule();
	const TSubclassOf<UStateTreeSchema> SchemaClass = UCatFishingSessionStateTreeSchema::StaticClass();
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree,
		EditorModule.GetEditorDataClass(SchemaClass), NAME_None, RF_Transactional);
	if (!EditorData)
	{
		return false;
	}
	StateTree->EditorData = EditorData;
	EditorData->Schema = NewObject<UCatFishingSessionStateTreeSchema>(EditorData, SchemaClass,
		NAME_None, RF_Transactional);
	EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(EditorData,
		EditorModule.GetEditorSchemaClass(SchemaClass), NAME_None, RF_Transactional);

	UStateTreeState& Root = EditorData->AddSubTree(TEXT("Fishing Session"));
	Root.Description = TEXT("服务器会话阶段拓扑；鱼只在真咬窗口内收到左键后选择和生成。");
	UStateTreeState& Waiting = Root.AddChildState(TEXT("Waiting"));
	UStateTreeState& Probe = Root.AddChildState(TEXT("Probe"));
	UStateTreeState& HookedFight = Root.AddChildState(TEXT("HookedFight"));
	UStateTreeState& ExhaustedReelHold = Root.AddChildState(TEXT("ExhaustedReelHold"));

	Waiting.TasksCompletion = EStateTreeTaskCompletionType::All;
	Waiting.AddTask<FCatFishingScheduleWaitingProbeTask>();
	Waiting.AddTask<FCatFishingWaitTask>();
	Waiting.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatFishingGameplayTags::ProbeTriggered,
		EStateTreeTransitionType::GotoState, &Probe);

	Probe.TasksCompletion = EStateTreeTaskCompletionType::All;
	auto& EnterProbeTask = Probe.AddTask<FCatFishingEnterPhaseTask>();
	EnterProbeTask.GetInstanceData().Phase = ECatFishingPhase::Probe;
	Probe.AddTask<FCatFishingOpenTrueBiteWindowTask>();
	Probe.AddTask<FCatFishingWaitTask>();
	Probe.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatFishingGameplayTags::HookAccepted,
		EStateTreeTransitionType::GotoState, &HookedFight);
	Probe.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatFishingGameplayTags::WindowExpired,
		EStateTreeTransitionType::GotoState, &Waiting);

	HookedFight.TasksCompletion = EStateTreeTaskCompletionType::All;
	HookedFight.AddTask<FCatFishingStartFightRunnerTask>();
	HookedFight.AddTask<FCatFishingWaitForFightRunnerTask>();
	HookedFight.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,
		EStateTreeTransitionType::GotoState, &ExhaustedReelHold);

	// 搏斗 Runner 在 C++ 内进入 ExhaustedReel；该叶子只让树继续 Running，等待岸上拾取或主动取消。
	ExhaustedReelHold.AddTask<FCatFishingWaitTask>();

	FStateTreeCompilerLog CompilerLog;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog))
	{
		return false;
	}
	StateTree->MarkPackageDirty();
	if (bNewAsset)
	{
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName,
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, StateTree, *Filename, SaveArgs);
}
