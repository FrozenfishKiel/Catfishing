#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/HUD/CatHUDWidget.h"

/** 在真实双端 PIE 的客户端重放迟到就绪；只制造一次观测空窗，随后由正式 UI 生命周期自行撤罩。 */
class FCatLoadingReadinessLatentCommand final : public IAutomationLatentCommand
{
public:
	/** 保存当前 Automation 断言入口；后续阶段只在同一次 PIE 中向它提交客户端观察。 */
	explicit FCatLoadingReadinessLatentCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	/**
	 * 观测客户端遮罩的迟到就绪流程：先等待真实客户端和正式 UI 全部就绪，超时则结束并报错。
	 * 稳定后按 GameState、BeginPlay、HUD 视口三种 gate 逐项制造一帧空窗，立即恢复真实对象但不主动刷新 UI。
	 * 最后等待遮罩自己的 Slate 观察收口；若遮罩仍在视口中，测试先记录失败再清掉它，避免影响下一种 gate。
	 * 三种 gate 都通过后，再验证重复等待不会重复订阅、就绪回退会取消完成停留、错误快照会立即撤罩并释放观察。
	 */
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (StartedAt == 0.0) StartedAt = Now;
		if (Now - StartedAt > 45.0)
		{
			Test->AddError(TEXT("Loading readiness regression could not obtain a ready network client."));
			return true;
		}
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World() && Context.World()->GetNetMode() == NM_Client)
			{
				World = Context.World();
				break;
			}
		}
		ULocalPlayer* Player = World && World->GetGameInstance() ? World->GetGameInstance()->GetFirstGamePlayer() : nullptr;
		UCatLocalPlayerUISubsystem* UI = Player ? Player->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
		UCatOnlineSubsystem* Online = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
		if (!UI || !Online) return false;
		if (Stage == 0)
		{
			if (!UI->IsGameplayLoadingReadyToDismiss(Online->GetSnapshot())) return false;
			// 等启动期 Controller/Pawn 的通知退去，保证后面只靠条件晚到而非偶然广播收口。
			Stage = 1;
			StageAt = Now;
			return false;
		}
		if (Stage == 1)
		{
			if (Now - StageAt < 2.0) return false;
			AGameStateBase* GameState = World->GetGameState();
			if (!Test->TestNotNull(TEXT("real client GameState is available before fault window"), GameState)) return true;
			UI->GlobalLoadingOperation = ECatOnlineOperation::Start;
			UI->GlobalLoadingRequestId = FGuid::NewGuid();
			if (CaseIndex == 0) World->SetGameState(nullptr);
			else if (CaseIndex == 1) World->SetBegunPlay(false);
			else UI->HUDWidget->RemoveFromParent();
			UI->RefreshGlobalLoadingScreenFromCurrentSnapshot();
			const bool bShown = UI->GlobalLoadingScreenWidget && UI->GlobalLoadingScreenWidget->IsInViewport();
			// 同一帧恢复，不把人为缺失传播给玩法 Tick；恢复自身不发送 Online/Pawn 广播。
			if (CaseIndex == 0) World->SetGameState(GameState);
			else if (CaseIndex == 1) World->SetBegunPlay(true);
			else UI->HUDWidget->AddToViewport();
			if (!Test->TestTrue(TEXT("formal loading WBP covers the unavailable readiness gate"), bShown)) return true;
			if (!Test->TestTrue(TEXT("real readiness recovers without another Online snapshot"), UI->IsGameplayLoadingReadyToDismiss(Online->GetSnapshot()))) return true;
			Stage = 2;
			StageAt = Now;
			return false;
		}
		if (Now - StageAt < 2.0) return false;
		const bool bHidden = !UI->GlobalLoadingScreenWidget || !UI->GlobalLoadingScreenWidget->IsInViewport();
		const TCHAR* Gate = CaseIndex == 0 ? TEXT("GameState") : CaseIndex == 1 ? TEXT("BeginPlay") : TEXT("HUD viewport");
		Test->TestTrue(FString::Printf(TEXT("late %s readiness dismisses the actual loading WBP without an unrelated notification"), Gate), bHidden);
		if (bHidden)
		{
			Test->TestFalse(TEXT("automatic dismissal removes its Slate observer"), UI->GlobalLoadingPostTickHandle.IsValid());
			Test->TestFalse(TEXT("automatic dismissal clears completion hold"), UI->bGlobalLoadingDismissalPending);
		}
		if (!bHidden) UI->HideGlobalLoadingScreen();
		++CaseIndex;
		if (CaseIndex == 3)
		{
			// 复用、回退和错误都走正式刷新入口；只改变本次传入快照，不伪造 Online 的会话或网络状态。
			UI->GlobalLoadingOperation = ECatOnlineOperation::Start;
			UI->GlobalLoadingRequestId = FGuid::NewGuid();
			World->SetBegunPlay(false);
			UI->RefreshGlobalLoadingScreenFromCurrentSnapshot();
			const FDelegateHandle FirstObserver = UI->GlobalLoadingPostTickHandle;
			UI->RefreshGlobalLoadingScreenFromCurrentSnapshot();
			Test->TestTrue(TEXT("repeated waiting refresh retains exactly one observer"), FirstObserver.IsValid() && FirstObserver == UI->GlobalLoadingPostTickHandle);
			World->SetBegunPlay(true);
			UI->RefreshGlobalLoadingScreenFromCurrentSnapshot();
			Test->TestTrue(TEXT("real readiness starts completion hold"), UI->bGlobalLoadingDismissalPending);
			World->SetBegunPlay(false);
			UI->RefreshGlobalLoadingScreenFromCurrentSnapshot();
			Test->TestFalse(TEXT("lost readiness cancels an obsolete completion hold"), UI->bGlobalLoadingDismissalPending);
			Test->TestTrue(TEXT("lost readiness keeps the existing observer alive"), UI->GlobalLoadingPostTickHandle == FirstObserver);
			World->SetBegunPlay(true);
			FCatOnlineSnapshot Failure = Online->GetSnapshot();
			Failure.LastError = ECatOnlineError::TravelFailed;
			UI->RefreshGlobalLoadingScreen(Failure);
			Test->TestNull(TEXT("failure removes the actual loading WBP immediately"), UI->GlobalLoadingScreenWidget.Get());
			Test->TestFalse(TEXT("failure removes the Slate observer"), UI->GlobalLoadingPostTickHandle.IsValid());
			Test->TestFalse(TEXT("failure leaves no completion callback pending"), UI->bGlobalLoadingDismissalPending);
			return true;
		}
		Stage = 1;
		StageAt = Now;
		return false;
	}

private:
	/** 当前回归的断言接收者；Automation 拥有其生命周期，本命令只读指针并写入结果。 */
	FAutomationTestBase* Test;
	/** 双端启动的单调时间起点，单位秒；Update 写入一次并用于限制等待。 */
	double StartedAt = 0.0;
	/** 当前观测阶段的单调时间，单位秒；阶段切换更新，避免启动通知干扰缺失条件回放。 */
	double StageAt = 0.0;
	/** 当前回放阶段；Update 依次等待稳定、制造空窗和观察正式撤罩。 */
	int32 Stage = 0;
	/** 本轮控制的迟到条件序号；依次为 GameState、BeginPlay 与 HUD 视口，不改变产品配置。 */
	int32 CaseIndex = 0;
};

/** 保存双端回归使用的内存配置；结束 PIE 后恢复，不写项目配置或地图资产。 */
class FCatLoadingReadinessRestore final : public IAutomationLatentCommand
{
public:
	/** 读取现有编辑器和地图配置，用于回归结束后逐项恢复。 */
	FCatLoadingReadinessRestore()
	{
		const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
		Settings->GetPlayNetMode(NetMode);
		Settings->GetPlayNumberOfClients(ClientCount);
		Settings->GetRunUnderOneProcess(bOneProcess);
		NetDrivers = GEngine->NetDriverDefinitions;
		GameplayMap = GetDefault<UCatOnlineSettings>()->GameplayMap;
	}

	/** 等 PIE 完全结束后还原测试改过的配置；仍有 World 时继续等待，避免影响退出链。 */
	bool Update() override
	{
		if (GEditor->PlayWorld) return false;
		ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(NetMode);
		Settings->SetPlayNumberOfClients(ClientCount);
		Settings->SetRunUnderOneProcess(bOneProcess);
		GEngine->NetDriverDefinitions = NetDrivers;
		GetMutableDefault<UCatOnlineSettings>()->GameplayMap = GameplayMap;
		return true;
	}

private:
	/** 原 PIE 网络模式；构造读取、结束写回，不持久化回归的 ListenServer 配置。 */
	EPlayNetMode NetMode = PIE_Standalone;
	/** 原玩家数量；恢复命令写回，防止后续测试继承双端数量。 */
	int32 ClientCount = 1;
	/** 原单进程选择；只在本次双端回归中临时改变。 */
	bool bOneProcess = true;
	/** 原网络驱动定义；回归临时使用 IP 驱动，结束整体还原。 */
	TArray<FNetDriverDefinition> NetDrivers;
	/** 原玩法地图软引用；让 TestMap 走同一到达判断，结束恢复正式目标。 */
	TSoftObjectPtr<UWorld> GameplayMap;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLoadingReadinessNetworkTest,
	"Catfishing.Editor.UI.Loading.LateReadinessNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 启动回归流程：
// 1. 拒绝已有 PIE，避免把人工会话或其他测试 World 当成本轮客户端。
// 2. 保存当前内存配置后切到单进程 ListenServer 双端，并把玩法地图临时指向 TestMap，不写项目配置或资产。
// 3. 使用 IP NetDriver 启动正式 PIE，再排入迟到就绪观测、结束 PIE 和配置恢复命令，保证失败时也有恢复链路。
bool FCatLoadingReadinessNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor && !GEditor->PlayWorld)) return false;
	const TSharedRef<FCatLoadingReadinessRestore> Restore = MakeShared<FCatLoadingReadinessRestore>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	GetMutableDefault<UCatOnlineSettings>()->GameplayMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Catfishing/Maps/TestMap.TestMap")));
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCatLoadingReadinessLatentCommand>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
