#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "UI/CatCommandPanelWidget.h"
#include "UI/CatLocalPlayerUISubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCommandPanelActionTablesCoverEveryEnumValueTest,
	"Catfishing.Unit.UI.CommandPanel.LabelAndDispatchTablesCoverEveryAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCommandPanelAvailabilityByPhaseTest,
	"Catfishing.Unit.UI.CommandPanel.AvailabilityFollowsRunPhaseAndLocalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：按 StaticEnum 逐个取 ECatCommandPanelAction 的值，分别调标签表和分派表（分派表给空输入、无 Controller，只描述不发送）；
// 任一值拿到 nullptr 标签、重复标签或空反馈串，就说明有人加了枚举值却没接表。它不证明 RPC 参数正确，只证明没有“按钮点了没人管”的漏项。
bool FCatCommandPanelActionTablesCoverEveryEnumValueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("CommandPanelWidget 是 UUserWidget 派生 View"), UCatCommandPanelWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));

	const UEnum* ActionEnum = StaticEnum<ECatCommandPanelAction>();
	TestNotNull(TEXT("面板意图枚举可反射"), ActionEnum);
	if (!ActionEnum)
	{
		return false;
	}

	const FCatCommandPanelDispatchInput EmptyInput;
	TSet<FString> SeenLabels;
	int32 CheckedCount = 0;
	for (int32 Index = 0; Index < ActionEnum->NumEnums(); ++Index)
	{
		if (ActionEnum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX")))
		{
			continue;
		}
		const ECatCommandPanelAction Action = static_cast<ECatCommandPanelAction>(ActionEnum->GetValueByIndex(Index));
		const FString ActionName = ActionEnum->GetNameStringByIndex(Index);
		++CheckedCount;

		const TCHAR* Label = UCatCommandPanelWidget::GetActionLabel(Action);
		TestNotNull(*FString::Printf(TEXT("意图 %s 有按钮标签"), *ActionName), Label);
		if (Label)
		{
			bool bAlreadySeen = false;
			SeenLabels.Add(Label, &bAlreadySeen);
			TestFalse(*FString::Printf(TEXT("意图 %s 的标签不与其他意图重复"), *ActionName), bAlreadySeen);
		}

		const FString Feedback = UCatLocalPlayerUISubsystem::DispatchCommandPanelAction(Action, EmptyInput);
		TestFalse(*FString::Printf(TEXT("意图 %s 在分派表里有分支（空输入也返回描述）"), *ActionName), Feedback.IsEmpty());
		TestTrue(*FString::Printf(TEXT("意图 %s 的反馈以 Server RPC 名开头：%s"), *ActionName, *Feedback), Feedback.StartsWith(TEXT("Server")));
		TestFalse(*FString::Printf(TEXT("意图 %s 无 Controller 时不得声称已发送：%s"), *ActionName, *Feedback), Feedback.Contains(TEXT(" sent ")));
	}
	TestEqual(TEXT("面板覆盖的意图条数与当前设计一致（加减按钮时同步更新此数）"), CheckedCount, 23);
	return !HasAnyErrors();
}

namespace
{
	// 造一份“白天、命令门开着、什么宿主都有、鱼护有鱼”的基线状态；各用例在它上面只改一两个事实，便于看出是哪个事实把按钮关掉的。
	FCatCommandPanelViewState MakeOpenDayState()
	{
		FCatCommandPanelViewState State;
		State.Phase = ECatRunPhase::DayActive;
		State.EndReason = ECatRunEndReason::None;
		State.bFishingAllowed = true;
		State.bQuotaOpen = true;
		State.bDowned = false;
		State.bWet = true;
		State.GuardFishCount = 1;
		State.bHasCamp = true;
		State.bHasActiveFishingSession = false;
		State.bHasRescueTarget = true;
		return State;
	}
}

// 测试流程：用纯函数 IsActionAvailable 核对关键相位下的推导——StartupFailed/NotStarted 全关；白天钓鱼/献祭/商店开、
// ready/结算关；普通夜 ready 开；结算夜结算开、商店关；
// 再核对局部事实：没鱼关掉“第一条鱼”三键、没营地关掉营地四键、有活动会话时开始钓鱼关/抢抄开、倒地关钓鱼但留求助并开野
// 外自救、不湿关抖干。这只证明客户端的按钮猜测，不证明服务器裁决。
bool FCatCommandPanelAvailabilityByPhaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using EAction = ECatCommandPanelAction;
	const auto Available = [](const FCatCommandPanelViewState& State, const EAction Action)
	{
		return UCatCommandPanelWidget::IsActionAvailable(State, Action);
	};

	FCatCommandPanelViewState Failed = MakeOpenDayState();
	Failed.Phase = ECatRunPhase::NotStarted;
	Failed.EndReason = ECatRunEndReason::StartupFailed;
	TestFalse(TEXT("StartupFailed 时开始钓鱼关闭"), Available(Failed, EAction::StartFishing));
	TestFalse(TEXT("StartupFailed 时手动求助也关闭（命令门没开过）"), Available(Failed, EAction::ManualHelp));
	TestFalse(TEXT("StartupFailed 时 starter 装配关闭"), Available(Failed, EAction::ConfigureStarterEquipment));

	FCatCommandPanelViewState Ended = MakeOpenDayState();
	Ended.Phase = ECatRunPhase::Ended;
	TestFalse(TEXT("Ended 时商店关闭"), Available(Ended, EAction::ShopClaimFreeBait));

	const FCatCommandPanelViewState Day = MakeOpenDayState();
	TestTrue(TEXT("白天可开始钓鱼"), Available(Day, EAction::StartFishing));
	TestFalse(TEXT("白天无活动会话时抢抄关闭"), Available(Day, EAction::Scoop));
	TestTrue(TEXT("白天可献祭第一条鱼"), Available(Day, EAction::SacrificeFirstFish));
	TestTrue(TEXT("白天商店可买"), Available(Day, EAction::ShopBuyFirstPaidEntry));
	TestTrue(TEXT("白天可卖第一条鱼"), Available(Day, EAction::SellFirstFish));
	TestTrue(TEXT("白天可营地休息"), Available(Day, EAction::CampRest));
	TestTrue(TEXT("白天可救援倒地者"), Available(Day, EAction::RescueDownedToCamp));
	TestTrue(TEXT("白天可手动求助"), Available(Day, EAction::ManualHelp));
	TestTrue(TEXT("白天湿着可抖干"), Available(Day, EAction::ShakeDry));
	TestFalse(TEXT("没倒地时野外自救关闭"), Available(Day, EAction::FieldSelfRecovery));
	TestFalse(TEXT("白天 ready 关闭"), Available(Day, EAction::RunReady));
	TestFalse(TEXT("白天撤回 ready 关闭"), Available(Day, EAction::RunUnready));
	TestFalse(TEXT("白天结算完成关闭"), Available(Day, EAction::SettlementComplete));

	FCatCommandPanelViewState Night = MakeOpenDayState();
	Night.Phase = ECatRunPhase::NormalNight;
	Night.bFishingAllowed = false;
	Night.bQuotaOpen = false;
	TestTrue(TEXT("普通夜 ready 开启"), Available(Night, EAction::RunReady));
	TestTrue(TEXT("普通夜撤回 ready 开启"), Available(Night, EAction::RunUnready));
	TestFalse(TEXT("普通夜服务器不允许钓鱼时开始钓鱼关闭"), Available(Night, EAction::StartFishing));
	TestFalse(TEXT("普通夜额度关闭时献祭关闭"), Available(Night, EAction::SacrificeFirstFish));
	TestTrue(TEXT("普通夜商店仍开"), Available(Night, EAction::ShopClaimFreeBait));
	TestTrue(TEXT("普通夜可篝火回看"), Available(Night, EAction::CampfirePlayback));

	FCatCommandPanelViewState Settlement = MakeOpenDayState();
	Settlement.Phase = ECatRunPhase::FailureSettlementNight;
	TestTrue(TEXT("结算夜结算完成开启"), Available(Settlement, EAction::SettlementComplete));
	TestFalse(TEXT("结算夜商店关闭"), Available(Settlement, EAction::ShopBuyFirstPaidEntry));
	TestFalse(TEXT("结算夜卖鱼关闭"), Available(Settlement, EAction::SellFirstFish));
	TestFalse(TEXT("结算夜 ready 关闭"), Available(Settlement, EAction::RunReady));
	Settlement.Phase = ECatRunPhase::SuccessSettlementNight;
	TestTrue(TEXT("成功结算夜结算完成同样开启"), Available(Settlement, EAction::SettlementComplete));

	FCatCommandPanelViewState NoFish = MakeOpenDayState();
	NoFish.GuardFishCount = 0;
	TestFalse(TEXT("鱼护空时献祭关闭"), Available(NoFish, EAction::SacrificeFirstFish));
	TestFalse(TEXT("鱼护空时卖鱼关闭"), Available(NoFish, EAction::SellFirstFish));
	TestFalse(TEXT("鱼护空时入缸关闭"), Available(NoFish, EAction::TransferFirstFishToTank));
	TestTrue(TEXT("鱼护空不影响开始钓鱼"), Available(NoFish, EAction::StartFishing));

	FCatCommandPanelViewState NoCamp = MakeOpenDayState();
	NoCamp.bHasCamp = false;
	TestFalse(TEXT("无营地时休息关闭"), Available(NoCamp, EAction::CampRest));
	TestFalse(TEXT("无营地时回看关闭"), Available(NoCamp, EAction::CampfirePlayback));
	TestFalse(TEXT("无营地时入缸关闭"), Available(NoCamp, EAction::TransferFirstFishToTank));
	TestFalse(TEXT("无营地时救援关闭"), Available(NoCamp, EAction::RescueDownedToCamp));

	FCatCommandPanelViewState NoTarget = MakeOpenDayState();
	NoTarget.bHasRescueTarget = false;
	TestFalse(TEXT("无倒地者时救援关闭"), Available(NoTarget, EAction::RescueDownedToCamp));

	FCatCommandPanelViewState Fishing = MakeOpenDayState();
	Fishing.bHasActiveFishingSession = true;
	TestFalse(TEXT("已有活动会话时开始钓鱼关闭"), Available(Fishing, EAction::StartFishing));
	TestTrue(TEXT("已有活动会话时抢抄开启"), Available(Fishing, EAction::Scoop));

	FCatCommandPanelViewState Downed = MakeOpenDayState();
	Downed.bDowned = true;
	Downed.bHasActiveFishingSession = true;
	TestFalse(TEXT("倒地时开始钓鱼关闭"), Available(Downed, EAction::StartFishing));
	TestFalse(TEXT("倒地时抢抄关闭"), Available(Downed, EAction::Scoop));
	TestFalse(TEXT("倒地时献祭关闭"), Available(Downed, EAction::SacrificeFirstFish));
	TestTrue(TEXT("倒地时手动求助仍开"), Available(Downed, EAction::ManualHelp));
	TestTrue(TEXT("倒地时野外自救开启"), Available(Downed, EAction::FieldSelfRecovery));

	FCatCommandPanelViewState Dry = MakeOpenDayState();
	Dry.bWet = false;
	TestFalse(TEXT("不湿时抖干关闭"), Available(Dry, EAction::ShakeDry));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
