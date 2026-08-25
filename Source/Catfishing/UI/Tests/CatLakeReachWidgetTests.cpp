#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "UI/CatLakeReachWidget.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLakeReachWidgetViewStateContractTest,
	"Catfishing.Unit.UI.Reach.BlueprintViewCarriesHudFishingGuardAndCollectionFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLakeReachWidgetFishGuardIntentTest,
	"Catfishing.Unit.UI.Reach.FishGuardWidgetEmitsPureSelectionAndActionIntents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只通过正式 View 基类的 DTO seam 填入身体、Fishing 反馈、鱼护和图鉴事实；native 副本必须保留 Profile 记录，蓝图副本必须通过 UI 专用条目承接。
bool FCatLakeReachWidgetViewStateContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("LakeReachWidget 是正式 WBP 的 UUserWidget 基类"),
		UCatLakeReachWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));
	TestNotNull(TEXT("LakeReach View 暴露蓝图渲染事件"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("BP_RenderViewState")));
	TestNotNull(TEXT("LakeReach View 暴露关闭菜单意图入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestCloseMenu")));
	TestNotNull(TEXT("LakeReach View 暴露离局意图入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestLeaveLake")));
	TestNotNull(TEXT("LakeReach View 暴露上一条鱼护选择入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestSelectPreviousFishGuardEntry")));
	TestNotNull(TEXT("LakeReach View 暴露下一条鱼护选择入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestSelectNextFishGuardEntry")));
	TestNotNull(TEXT("LakeReach View 暴露吃鱼意图入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestConsumeSelectedFish")));
	TestNotNull(TEXT("LakeReach View 暴露转缸意图入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestTransferSelectedFishToTank")));
	TestNotNull(TEXT("LakeReach View 暴露献祭意图入口"),
		UCatLakeReachWidget::StaticClass()->FindFunctionByName(TEXT("RequestSacrificeSelectedFish")));
	TestNotNull(TEXT("统一 DTO 暴露选中鱼护条目"),
		FindFProperty<FStructProperty>(FCatUIReachViewState::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FCatUIReachViewState, SelectedFishGuardFish)));
	TestNotNull(TEXT("统一 DTO 暴露鱼护动作公共结果"),
		FindFProperty<FStructProperty>(FCatUIReachViewState::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FCatUIReachViewState, LastFishGuardCommandResult)));
	TestNull(TEXT("Profile 图鉴记录保持原生 C++ 展示字段，不要求其权威类型支持 Blueprint"),
		FindFProperty<FProperty>(FCatUIReachViewState::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FCatUIReachViewState, FishCollection)));
	const FArrayProperty* BlueprintCollectionProperty = FindFProperty<FArrayProperty>(
		FCatUIReachViewState::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FCatUIReachViewState, FishCollectionEntries));
	TestNotNull(TEXT("统一 DTO 另有蓝图安全图鉴展示副本"), BlueprintCollectionProperty);
	if (BlueprintCollectionProperty)
	{
		TestTrue(TEXT("蓝图图鉴副本可供 WBP 读取"),
			BlueprintCollectionProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}

	FCatUIReachViewState ViewState;
	ViewState.Poison = 12.5f;
	ViewState.Growth.TotalExperience = 18;
	ViewState.Growth.ExperienceInCurrentSlot = 8;
	ViewState.Growth.PendingChoiceCount = 1;
	ViewState.Fishing.Phase = ECatFishingPhase::NearShore;
	ViewState.bHasFishingSession = true;
	ViewState.LastFishingCommandResult.CommandType = ECatFishingCommandType::RequestScoop;
	ViewState.LastFishingCommandResult.Error = ECatFishingCommandError::None;
	ViewState.bHasFishingCommandResult = true;
	ViewState.bMenuOpen = true;
	ViewState.MenuToggleKeyName = TEXT("Tab");
	ViewState.bCanRequestOnlineLeave = true;
	FCatFishInstance Fish;
	Fish.FishInstanceId = FGuid::NewGuid();
	Fish.FishDefinitionId = TEXT("Carp");
	Fish.WeightKilograms = 2.5;
	ViewState.PersonalFishGuard.Fish.Add(Fish);
	ViewState.SelectedFishGuardIndex = 0;
	ViewState.SelectedFishGuardFish = Fish;
	ViewState.bHasSelectedFishGuardFish = true;
	ViewState.bCanSubmitSelectedFishGuardAction = true;
	ViewState.LastFishGuardAction = ECatUIReachFishGuardAction::ConsumeSelectedFish;
	ViewState.LastFishGuardCommandResult.bCommitted = true;
	ViewState.LastFishGuardCommandResult.RequestId = FGuid::NewGuid();
	ViewState.LastFishGuardCommandResult.Error = ECatDomainCommandError::None;
	ViewState.LastFishGuardCommandResult.Revision = 2;
	ViewState.bHasFishGuardCommandResult = true;
	ViewState.LastFishGuardConsumeResult.Command = ViewState.LastFishGuardCommandResult;
	ViewState.LastFishGuardConsumeResult.Fish = Fish;
	FCatFishCollectionRecord Record;
	Record.FishDefinitionId = TEXT("Carp");
	Record.State = ECatFishCollectionState::Recorded;
	Record.BestWeightKilograms = 2.5;
	Record.EncounterCount = 1;
	ViewState.FishCollection.Add(Record);
	ViewState.bFishCollectionAvailable = true;

	UCatLakeReachWidget* Widget = NewObject<UCatLakeReachWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("可创建正式 LakeReach View 基类实例用于 DTO 测试"), Widget))
	{
		return false;
	}
	Widget->Render(ViewState);
	const FCatUIReachViewState& NativeState = Widget->GetLastNativeViewState();
	const FCatUIReachBlueprintViewState BlueprintState = Widget->GetLastBlueprintViewState();

	TestEqual(TEXT("native DTO 保留 Poison HUD"), NativeState.Poison, 12.5f);
	TestEqual(TEXT("native DTO 保留成长经验槽"), NativeState.Growth.ExperienceInCurrentSlot, 8);
	TestEqual(TEXT("native DTO 保留待选次数"), NativeState.Growth.PendingChoiceCount, 1);
	TestEqual(TEXT("native DTO 保留 Fishing 阶段"), NativeState.Fishing.Phase, ECatFishingPhase::NearShore);
	TestEqual(TEXT("native DTO 保留结构化命令反馈"), NativeState.LastFishingCommandResult.CommandType,
		ECatFishingCommandType::RequestScoop);
	TestEqual(TEXT("native DTO 保留个人鱼护实物"), NativeState.PersonalFishGuard.Fish.Num(), 1);
	TestTrue(TEXT("native DTO 保留选中鱼护条目"), NativeState.bHasSelectedFishGuardFish);
	TestEqual(TEXT("native DTO 保留选中鱼重量"), NativeState.SelectedFishGuardFish.WeightKilograms, 2.5);
	TestTrue(TEXT("native DTO 保留鱼护动作结果"), NativeState.bHasFishGuardCommandResult);
	TestEqual(TEXT("native DTO 保留 durable 图鉴"), NativeState.FishCollection.Num(), 1);
	TestTrue(TEXT("蓝图 DTO 收到选中鱼护条目"), BlueprintState.bHasSelectedFishGuardFish);
	TestEqual(TEXT("蓝图 DTO 收到鱼护结果 Revision"),
		BlueprintState.LastFishGuardCommandResult.Revision, int64{2});
	TestEqual(TEXT("蓝图 DTO 收到 UI 专用图鉴条目"), BlueprintState.FishCollectionEntries.Num(), 1);
	if (BlueprintState.FishCollectionEntries.Num() == 1)
	{
		TestEqual(TEXT("蓝图图鉴条目复制鱼定义 ID"),
			BlueprintState.FishCollectionEntries[0].FishDefinitionId, FName(TEXT("Carp")));
		TestEqual(TEXT("蓝图图鉴条目复制三态展示值"),
			BlueprintState.FishCollectionEntries[0].State, ECatFishCollectionState::Recorded);
	}
	TestTrue(TEXT("菜单、鱼护和图鉴仍属于同一根状态"), BlueprintState.bMenuOpen && BlueprintState.bFishCollectionAvailable);
	TestTrue(TEXT("Lake 菜单保留正式 Online 离局 gate"), BlueprintState.bCanRequestOnlineLeave);
	return !HasAnyErrors();
}

// 测试流程：只通过正式 Widget 公共入口模拟玩家点击；关闭菜单、无可前移和 pending 状态都不能广播，打开菜单且有选择时只广播纯意图枚举和偏移。
bool FCatLakeReachWidgetFishGuardIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatLakeReachWidget* Widget = NewObject<UCatLakeReachWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("可创建鱼护意图测试 View"), Widget))
	{
		return false;
	}

	int32 SelectionOffsetTotal = 0;
	TArray<ECatUIReachFishGuardAction> Actions;
	Widget->OnFishGuardSelectionRequested.AddLambda([&SelectionOffsetTotal](const int32 Offset)
	{
		SelectionOffsetTotal += Offset;
	});
	Widget->OnFishGuardActionRequested.AddLambda([&Actions](const ECatUIReachFishGuardAction Action)
	{
		Actions.Add(Action);
	});

	FCatFishInstance FirstFish;
	FirstFish.FishInstanceId = FGuid::NewGuid();
	FirstFish.FishDefinitionId = TEXT("Carp");
	FirstFish.WeightKilograms = 1.25;
	FCatFishInstance SecondFish;
	SecondFish.FishInstanceId = FGuid::NewGuid();
	SecondFish.FishDefinitionId = TEXT("Bass");
	SecondFish.WeightKilograms = 2.0;

	FCatUIReachViewState ClosedState;
	ClosedState.PersonalFishGuard.Fish = {FirstFish, SecondFish};
	ClosedState.SelectedFishGuardIndex = 0;
	ClosedState.SelectedFishGuardFish = FirstFish;
	ClosedState.bHasSelectedFishGuardFish = true;
	ClosedState.bCanSelectNextFishGuardEntry = true;
	ClosedState.bCanSubmitSelectedFishGuardAction = true;
	ClosedState.bMenuOpen = false;
	Widget->Render(ClosedState);
	Widget->RequestSelectNextFishGuardEntry();
	Widget->RequestConsumeSelectedFish();
	TestEqual(TEXT("关闭菜单时不广播鱼护选择"), SelectionOffsetTotal, 0);
	TestEqual(TEXT("关闭菜单时不广播鱼护动作"), Actions.Num(), 0);

	FCatUIReachViewState OpenState = ClosedState;
	OpenState.bMenuOpen = true;
	Widget->Render(OpenState);
	Widget->RequestSelectPreviousFishGuardEntry();
	Widget->RequestSelectNextFishGuardEntry();
	Widget->RequestConsumeSelectedFish();
	Widget->RequestTransferSelectedFishToTank();
	Widget->RequestSacrificeSelectedFish();
	TestEqual(TEXT("不能前移时只广播一次后移偏移"), SelectionOffsetTotal, 1);
	TestEqual(TEXT("打开菜单且有选中鱼时广播三种鱼护动作"), Actions.Num(), 3);
	if (Actions.Num() == 3)
	{
		TestEqual(TEXT("第一种动作为吃鱼"), Actions[0], ECatUIReachFishGuardAction::ConsumeSelectedFish);
		TestEqual(TEXT("第二种动作为转缸"), Actions[1], ECatUIReachFishGuardAction::TransferSelectedFishToTank);
		TestEqual(TEXT("第三种动作为献祭"), Actions[2], ECatUIReachFishGuardAction::SacrificeSelectedFish);
	}

	FCatUIReachViewState PendingState = OpenState;
	PendingState.bCanSubmitSelectedFishGuardAction = false;
	PendingState.bFishGuardActionPending = true;
	PendingState.PendingFishGuardAction = ECatUIReachFishGuardAction::ConsumeSelectedFish;
	PendingState.PendingFishGuardRequestId = FGuid::NewGuid();
	Widget->Render(PendingState);
	Widget->RequestConsumeSelectedFish();
	TestEqual(TEXT("pending 状态不重复广播鱼护动作"), Actions.Num(), 3);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
