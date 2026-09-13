#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatRunSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRunMorningTargetRecoveryTest,
	"Catfishing.Unit.Run.MorningTargetRecoversBeforePresentationAndDefersVisibleChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRunMorningTargetRecoveryTest::RunTest(const FString& Parameters)
{
	for (const bool bAlreadyPresented : {false, true})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* State = World->GetGameState<ACatfishingGameState>();
		if (!Mode || !State) return false;
		if (!TestTrue(TEXT("正式日程使用清晨人数"), GetDefault<UCatRunSettings>()->IsPerPlayerDailyOfferingTarget())) return false;
		if (!TestTrue(TEXT("零人数开局保持 fail-open 并等待补算"), Mode->bMorningTargetNeedsPlayerCountReconciliation)) return false;
		const int32 FallbackTarget = Mode->RunPublicState.DailyOfferingTarget;
		const int32 DayIndex = Mode->RunPublicState.Phase.DayIndex;
		const double Deadline = Mode->RunPublicState.Phase.DeadlineServerTimeSeconds;
		auto* ASC = State->GetRunAbilitySystemComponentFromAuthority();
		if (!ASC) return false;
		ASC->SetNumericAttributeBase(UCatRunAttributeSet::GetLastOfferingPointsAttribute(), 7.0f);
		ASC->SetNumericAttributeBase(UCatRunAttributeSet::GetLastWorldProgressDeltaAttribute(), -3.0f);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			auto* Player = World->SpawnActor<ACatfishingPlayerState>();
			if (!Player) return false;
			const FUniqueNetIdRef PlayerId = FUniqueNetIdString::Create(
				FString::Printf(TEXT("MorningPlayer%d"), Index), FName(TEXT("CAT_TEST")));
			Player->SetUniqueId(FUniqueNetIdRepl(PlayerId));
			if (Index == 0 && bAlreadyPresented)
			{
				Mode->HandlePendingMorningTargetRefresh();
				TestEqual(TEXT("0 到 1 的中间注册仍保持单人目标"), Mode->RunPublicState.DailyOfferingTarget, FallbackTarget);
				TestTrue(TEXT("首个玩家不能关闭后续人数核对"), Mode->bMorningTargetNeedsPlayerCountReconciliation);
				Mode->bMorningTargetPublishedToPlayer = true;
			}
		}
		TestEqual(TEXT("注册完成后统计四人"), Mode->CountMorningPlayersFromAuthority(), 4);
		Mode->bMorningTargetPublishedToPlayer = bAlreadyPresented;
		if (bAlreadyPresented)
			AddExpectedErrorPlain(TEXT("Event=RunMorningTargetChangeNeedsDesign"), EAutomationExpectedErrorFlags::Contains, 1);
		Mode->HandlePendingMorningTargetRefresh();
		const int32 ExpectedTarget = bAlreadyPresented ? FallbackTarget : FallbackTarget * 4;
		TestEqual(TEXT("未展示目标按四人补算；已展示的变化等待设计"), Mode->RunPublicState.DailyOfferingTarget, ExpectedTarget);
		TestEqual(TEXT("属性与公开目标一致"), ASC->GetNumericAttribute(UCatRunAttributeSet::GetDailyOfferingTargetAttribute()), float(ExpectedTarget));
		TestEqual(TEXT("GameState 消费补算结果"), State->GetRunPublicState().DailyOfferingTarget, ExpectedTarget);
		TestEqual(TEXT("不重放清晨供品清零"), ASC->GetNumericAttribute(UCatRunAttributeSet::GetLastOfferingPointsAttribute()), 7.0f);
		TestEqual(TEXT("不清空进度变化"), ASC->GetNumericAttribute(UCatRunAttributeSet::GetLastWorldProgressDeltaAttribute()), -3.0f);
		TestEqual(TEXT("不重开当天"), Mode->RunPublicState.Phase.DayIndex, DayIndex);
		TestEqual(TEXT("不重置日钟"), Mode->RunPublicState.Phase.DeadlineServerTimeSeconds, Deadline);
		const int64 Revision = Mode->RunPublicState.Revision;
		Mode->HandlePendingMorningTargetRefresh();
		TestEqual(TEXT("重复回调不二次结算或推进版本"), Mode->RunPublicState.Revision, Revision);
		Mode->ClearDayDeadline();
		TestFalse(TEXT("离开当日清理待补算状态"), Mode->bMorningTargetNeedsPlayerCountReconciliation);
	}
	return !HasAnyErrors();
}
#endif
