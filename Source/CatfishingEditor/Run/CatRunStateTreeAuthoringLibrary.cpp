#include "CatRunStateTreeAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Run/CatRunStateTreeEvents.h"
#include "Run/CatRunStateTreeNodes.h"
#include "Run/StateTree/CatRunFlowStateTreeSchema.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeState.h"
#include "UObject/SavePackage.h"

// 默认 RunFlow 重建流程：
// 1. 先打开或创建 /Game/Data/StateTrees/ST_RunFlow，保证项目配置仍指向同一正式资产。
// 2. 再整体替换 EditorData，避免在已经丢失拓扑的二进制资产上做不可靠的增量修补。
// 3. 白天只等待 DayEnded；普通夜按供品事件选择毕业夜或下一天，世界进度归零直接进 Ending，不等待失败结算夜。
// 4. Ending 进入成功后自然转入 Ended；C++ 只执行阶段副作用和同步发布，不在这里以外再建第二套 Run 状态机。
// 5. 最后编译并保存资产，让下一次 PIE 或打包直接使用修复后的 StateTree 拓扑。
// 6. 任一创建、编译或保存步骤失败时返回 false；调用方据此知道资产没有形成可用拓扑，不在这里回退到 C++ 状态机。
bool UCatRunStateTreeAuthoringLibrary::CreateOrUpdateDefaultRunFlowStateTree()
{
	static const FString PackageName = TEXT("/Game/Data/StateTrees/ST_RunFlow");
	static const FName AssetName = TEXT("ST_RunFlow");
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

	FStateTreeEditorModule& EditorModule = FStateTreeEditorModule::GetModule();
	const TSubclassOf<UStateTreeSchema> SchemaClass = UCatRunFlowStateTreeSchema::StaticClass();
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree,
		EditorModule.GetEditorDataClass(SchemaClass), NAME_None, RF_Transactional);
	if (!EditorData)
	{
		return false;
	}
	StateTree->EditorData = EditorData;
	EditorData->Schema = NewObject<UCatRunFlowStateTreeSchema>(EditorData, SchemaClass,
		NAME_None, RF_Transactional);
	EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(EditorData,
		EditorModule.GetEditorSchemaClass(SchemaClass), NAME_None, RF_Transactional);

	UStateTreeState& Root = EditorData->AddSubTree(TEXT("Run Flow"));
	Root.Description = TEXT("服务器一局流程拓扑；环境事实、供品结算和同步发布只由 GameMode 写入。");
	UStateTreeState& DayActive = Root.AddChildState(TEXT("DayActive"));
	UStateTreeState& NormalNight = Root.AddChildState(TEXT("NormalNight"));
	UStateTreeState& SuccessSettlementNight = Root.AddChildState(TEXT("SuccessSettlementNight"));
	UStateTreeState& Ending = Root.AddChildState(TEXT("Ending"));
	UStateTreeState& Ended = Root.AddChildState(TEXT("Ended"));

	DayActive.TasksCompletion = EStateTreeTaskCompletionType::All;
	auto& EnterDayTask = DayActive.AddTask<FCatRunEnterPhaseTask>();
	EnterDayTask.GetInstanceData().Phase = ECatRunPhase::DayActive;
	EnterDayTask.GetInstanceData().Reason = ECatRunTransitionReason::None;
	DayActive.AddTask<FCatRunWaitForEventTask>();
	DayActive.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatRunStateTreeEvents::DayEnded,
		EStateTreeTransitionType::GotoState, &NormalNight);

	NormalNight.TasksCompletion = EStateTreeTaskCompletionType::All;
	auto& EnterNormalNightTask = NormalNight.AddTask<FCatRunEnterPhaseTask>();
	EnterNormalNightTask.GetInstanceData().Phase = ECatRunPhase::NormalNight;
	EnterNormalNightTask.GetInstanceData().Reason = ECatRunTransitionReason::DayEnded;
	NormalNight.AddTask<FCatRunWaitForEventTask>();
	NormalNight.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatRunStateTreeEvents::WorldProgressDepleted,
		EStateTreeTransitionType::GotoState, &Ending);
	FStateTreeTransition& SuccessTransition = NormalNight.AddTransition(EStateTreeTransitionTrigger::OnEvent,
		CatRunStateTreeEvents::AllEligibleReady, EStateTreeTransitionType::GotoState, &SuccessSettlementNight);
	SuccessTransition.AddConditionWithOuter<FCatRunSuccessSettlementEligibleCondition>(&NormalNight);
	NormalNight.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatRunStateTreeEvents::AllEligibleReady,
		EStateTreeTransitionType::GotoState, &DayActive);

	SuccessSettlementNight.TasksCompletion = EStateTreeTaskCompletionType::All;
	auto& EnterSuccessNightTask = SuccessSettlementNight.AddTask<FCatRunEnterPhaseTask>();
	EnterSuccessNightTask.GetInstanceData().Phase = ECatRunPhase::SuccessSettlementNight;
	EnterSuccessNightTask.GetInstanceData().Reason = ECatRunTransitionReason::AllEligibleReady;
	SuccessSettlementNight.AddTask<FCatRunWaitForEventTask>();
	SuccessSettlementNight.AddTransition(EStateTreeTransitionTrigger::OnEvent, CatRunStateTreeEvents::SettlementComplete,
		EStateTreeTransitionType::GotoState, &Ending);

	auto& EnterEndingTask = Ending.AddTask<FCatRunEnterPhaseTask>();
	EnterEndingTask.GetInstanceData().Phase = ECatRunPhase::Ending;
	EnterEndingTask.GetInstanceData().Reason = ECatRunTransitionReason::SettlementComplete;
	Ending.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,
		EStateTreeTransitionType::GotoState, &Ended);

	auto& EnterEndedTask = Ended.AddTask<FCatRunEnterPhaseTask>();
	EnterEndedTask.GetInstanceData().Phase = ECatRunPhase::Ended;
	EnterEndedTask.GetInstanceData().Reason = ECatRunTransitionReason::NaturalEnd;
	// 终态必须显式结束树；没有完成边时，默认根选择会重新进入首个白天并再加一天。
	Ended.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::Succeeded);

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
