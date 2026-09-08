#include "CatFishStateTreeAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Fishing/Behavior/CatFishBehaviorStateTree.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Fishing/CatFishingStateTreeEvents.h"
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
#include <initializer_list>

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
	Root.Description = TEXT("三个真实行为叶子由固定步反馈条件选边；位置、鱼线、力量和费用仍只有原模拟写口。");
	UStateTreeState& Outward = Root.AddChildState(TEXT("Outward Rush"));
	UStateTreeState& Lateral = Root.AddChildState(TEXT("Lateral Arc"));
	UStateTreeState& EaseOff = Root.AddChildState(TEXT("Ease Off"));
	Outward.AddTask<FCatFishBehaviorStateTask>().GetInstanceData().Behavior = ECatFishBehavior::OutwardRush;
	Lateral.AddTask<FCatFishBehaviorStateTask>().GetInstanceData().Behavior = ECatFishBehavior::LateralArc;
	EaseOff.AddTask<FCatFishBehaviorStateTask>().GetInstanceData().Behavior = ECatFishBehavior::EaseOff;

	const auto AddFeedbackTransition = [](UStateTreeState& Source, UStateTreeState& Target,
		std::initializer_list<ECatFishBehaviorCondition> Conditions)
	{
		FStateTreeTransition& Transition = Source.AddTransition(EStateTreeTransitionTrigger::OnTick,
			EStateTreeTransitionType::GotoState, &Target);
		for (const ECatFishBehaviorCondition Condition : Conditions)
		{
			Transition.AddConditionWithOuter<FCatFishBehaviorFeedbackCondition>(&Source)
				.GetInstanceData().Condition = Condition;
		}
	};
	using ECondition = ECatFishBehaviorCondition;
	// 总对抗时限优先于局部承诺，保证反复改道也能给玩家恢复窗口。
	AddFeedbackTransition(Outward, EaseOff, { ECondition::NeedsRecovery });
	AddFeedbackTransition(Outward, Lateral, { ECondition::MinimumDurationElapsed, ECondition::SustainedBlocked });
	AddFeedbackTransition(Outward, EaseOff, { ECondition::DurationExpired });
	AddFeedbackTransition(Lateral, EaseOff, { ECondition::NeedsRecovery });
	AddFeedbackTransition(Lateral, Outward, { ECondition::MinimumDurationElapsed, ECondition::SustainedBlocked });
	AddFeedbackTransition(Lateral, Outward, { ECondition::DurationExpired });
	AddFeedbackTransition(EaseOff, Lateral, { ECondition::DurationExpired, ECondition::SustainedBlocked });
	AddFeedbackTransition(EaseOff, Outward, { ECondition::DurationExpired });

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
	HookedFight.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatFishingStateTreeEvents::FishExhausted,
		EStateTreeTransitionType::GotoState, &ExhaustedReelHold);

	// 鱼力竭只切生命周期叶子；同一个 Runner 继续以 AutoHauling 意图运行双端约束。
	ExhaustedReelHold.TasksCompletion = EStateTreeTaskCompletionType::All;
	auto& EnterExhaustedReelTask = ExhaustedReelHold.AddTask<FCatFishingEnterPhaseTask>();
	EnterExhaustedReelTask.GetInstanceData().Phase = ECatFishingPhase::ExhaustedReel;
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
