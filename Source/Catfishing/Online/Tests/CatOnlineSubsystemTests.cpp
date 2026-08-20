#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/PackageName.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemInitialSnapshotTest,
	"Catfishing.Unit.Online.Subsystem.UnknownWorldRejectsRequestsWithoutStartingOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemLeaveWaitsForFrontendPostLoadTest,
	"Catfishing.Unit.Online.Subsystem.LeaveWaitsForFrontendPostLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemUnexpectedPostLoadFailsOperationTest,
	"Catfishing.Unit.Online.Subsystem.UnexpectedPostLoadFailsOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemTravelFailureDuringLeaveTest,
	"Catfishing.Unit.Online.Subsystem.TravelFailureDuringLeaveFailsOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemCreateTimeoutTest,
	"Catfishing.Unit.Online.Subsystem.CreateSessionTimeoutCompensatesAndFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemFindTimeoutTest,
	"Catfishing.Unit.Online.Subsystem.FindSessionsTimeoutCompensatesAndFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemJoinTimeoutTest,
	"Catfishing.Unit.Online.Subsystem.JoinSessionTimeoutCompensatesAndFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemDestroyTimeoutTest,
	"Catfishing.Unit.Online.Subsystem.DestroySessionTimeoutEndsInErrorSessionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemHostKickTest,
	"Catfishing.Unit.Online.Subsystem.HostKickRequiresHostIdentityAndReleasesMember",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Online 自动化使用的 Frontend 稳定包名；测试只把它作为状态机输入，不加载真实地图资产。 */
	const FString FrontendPackageForOnlineAutomation(TEXT("/Game/Catfishing/Maps/Frontend"));

	/** Online 自动化使用的 Lake 稳定包名；测试用它模拟来源 World 回载，避免把资产加载成功误当作旅行收口成功。 */
	const FString LakePackageForOnlineAutomation(TEXT("/Game/Catfishing/Maps/Lake"));

	/** Online 自动化使用的未知包名；状态机必须把它收敛为 UnexpectedMap，而不能静默完成当前操作。 */
	const FString UnknownPackageForOnlineAutomation(TEXT("/Game/Catfishing/Maps/AutomationUnknown"));

	/** 超时用例使用的短窗口秒数；它只是为了让下面这几帧手动推进就能跨过等待窗口，不代表任何产品或工程裁定的真实超时值。 */
	constexpr double OperationTimeoutSecondsForOnlineAutomation = 0.05;

	/** 超时用例单帧推进的世界时间；必须大于上面的窗口，才能证明是计时器而不是种入本身结束了操作。 */
	constexpr float TickSecondsForOnlineAutomation = 0.2f;

	/**
	 * 超时用例需要推进的帧数。计时器是在 FTimerManager 本帧还没 Tick 过的时候武装的，
	 * 这种计时器先进 PendingTimerSet，要等这一帧的 Tick 走完才会转成活动计时器，
	 * 所以第一帧不可能到期，至少要第二帧才轮得到它。
	 */
	constexpr int32 FrameCountForOnlineAutomation = 2;

	/**
	 * 按帧推进测试世界，让有界超时计时器真的能到期。
	 *
	 * FTimerManager::Tick 开头有一道同帧去重：LastTickedFrame 等于 GFrameCounter 就直接返回。
	 * 而 FTestWorldWrapper 只在测试世界 BeginPlay 之后才替我们递增 GFrameCounter，超时用例
	 * 只验状态机收口、不需要 GameMode 和 GameState，也就没有走 BeginPlay。因此这里必须自己
	 * 递增帧号，否则第二次以后的 Tick 会被当成同一帧整帧丢掉，计时器永远等不到到期检查。
	 *
	 * 推进结束后把帧号还原，避免这几帧假帧影响编辑器世界自己的计时器同帧判据。
	 * 返回值只表示每一帧的 Tick 是否都被测试世界接受，不代表计时器已经触发；到期与否必须由调用方自己断言。
	 */
	bool AdvanceTestWorldFramesForOnlineAutomation(FTestWorldWrapper& WorldWrapper)
	{
		const uint64 FrameCounterBeforeAdvance = GFrameCounter;
		bool bTickAccepted = true;
		for (int32 FrameIndex = 0; bTickAccepted && FrameIndex < FrameCountForOnlineAutomation; ++FrameIndex)
		{
			bTickAccepted = WorldWrapper.TickTestWorld(TickSecondsForOnlineAutomation);
			++GFrameCounter;
		}
		GFrameCounter = FrameCounterBeforeAdvance;
		return bTickAccepted;
	}

	/** 测试期间覆盖默认 Online Settings 平台操作超时的守卫；子系统读取 GetDefault，因此必须在用例结束时还原默认对象。 */
	struct FOnlineOperationTimeoutOverride
	{
		/** 被临时改写的默认配置对象。 */
		UCatOnlineSettings* Settings = GetMutableDefault<UCatOnlineSettings>();

		/** 覆盖前的项目配置值，用例结束后原样写回。 */
		double OldSessionOperationTimeoutSeconds = 0.0;

		// 保存流程：记录默认对象旧值后写入短窗口；不触碰磁盘配置，也不改动其他 Online 策略。
		FOnlineOperationTimeoutOverride()
		{
			if (Settings)
			{
				OldSessionOperationTimeoutSeconds = Settings->SessionOperationTimeoutSeconds;
				Settings->SessionOperationTimeoutSeconds = OperationTimeoutSecondsForOnlineAutomation;
			}
		}

		// 恢复流程：把默认对象写回覆盖前的值，避免超时用例污染后续读取项目配置的个案。
		~FOnlineOperationTimeoutOverride()
		{
			if (Settings)
			{
				Settings->SessionOperationTimeoutSeconds = OldSessionOperationTimeoutSeconds;
			}
		}
	};

	/**
	 * 超时用例共用的推进步骤：种入一个"平台请求已提交、仍在等待回调"的操作，先确认它确实还在等待，
	 * 再登记预期的超时错误日志并按帧推进世界时间。返回世界是否成功推进；具体收口事实由各用例自己断言。
	 */
	bool RunPendingOperationTimeoutForOnlineAutomation(FAutomationTestBase& Test, FTestWorldWrapper& WorldWrapper,
		UCatOnlineSubsystem& Online, const ECatOnlineOperation Operation, const ECatOnlineSessionState PendingSessionState)
	{
		if (!Test.TestTrue(TEXT("有界超时计时器按配置武装成功"),
			Online.SeedPendingPlatformOperationForAutomation(Operation, PendingSessionState)))
		{
			return false;
		}
		const FCatOnlineSnapshot Pending = Online.GetSnapshot();
		Test.TestEqual(TEXT("推进时间前操作仍在等待平台回调"), Pending.ActiveOperation, Operation);
		Test.TestEqual(TEXT("推进时间前会话事实保持提交时的等待态"), Pending.SessionState, PendingSessionState);
		Test.TestEqual(TEXT("种入本身不产生结构化错误"), Pending.LastError, ECatOnlineError::None);
		Test.AddExpectedErrorPlain(TEXT("Event=online_operation_timeout RequestId="),
			EAutomationExpectedErrorFlags::Contains, 1);
		return AdvanceTestWorldFramesForOnlineAutomation(WorldWrapper);
	}

	/**
	 * 把同一个测试 World 移到指定长包名下并使用唯一对象名；这样广播真实 PostLoadMap/TravelFailure 时，Online 生产入口
	 * 能读取到稳定地图事实，又不会撞上编辑器已加载的真实 Frontend/Lake World。
	 */
	bool MoveTestWorldToPackageForOnlineAutomation(UWorld* World, const FString& PackageName)
	{
		if (!World || PackageName.IsEmpty())
		{
			return false;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return false;
		}
		const FString WorldObjectName = FString::Printf(TEXT("AutomationOnlineWorld_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return World->Rename(*WorldObjectName, Package, REN_DontCreateRedirectors);
	}
}

// 测试流程：在 FTestWorldWrapper 的临时 Game World 中取得真实 GameInstanceSubsystem；未知地图包名下所有请求必须同步拒绝，不得启动平台操作。
bool FCatOnlineSubsystemInitialSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 OnlineSubsystem 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("Online 测试 World 可用"), World);
	TestNotNull(TEXT("测试 GameInstance 可用"), GameInstance);
	TestNotNull(TEXT("真实 Online GameInstanceSubsystem 已创建"), Online);
	if (!Online)
	{
		return false;
	}

	const FCatOnlineSnapshot Initial = Online->GetSnapshot();
	TestEqual(TEXT("测试 World 不是 Frontend/Lake 时 WorldState 为 Error"), Initial.WorldState, ECatOnlineWorldState::Error);
	TestEqual(TEXT("初始没有 NamedSession"), Initial.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("初始没有活动操作"), Initial.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("初始没有结构化错误"), Initial.LastError, ECatOnlineError::None);

	const FCatOnlineResult HostResult = Online->RequestCreateSession();
	TestFalse(TEXT("未知 World 下 Host 请求不被受理"), HostResult.bAccepted);
	TestEqual(TEXT("未知 World 下 Host 返回 InvalidState"), HostResult.Error, ECatOnlineError::InvalidState);
	const FCatOnlineSnapshot AfterHost = Online->GetSnapshot();
	TestEqual(TEXT("拒绝后仍没有活动操作"), AfterHost.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("拒绝后仍没有会话"), AfterHost.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("拒绝错误进入快照"), AfterHost.LastError, ECatOnlineError::InvalidState);

	FCatSessionSearchHandle InvalidSearchHandle;
	InvalidSearchHandle.Value = FGuid::NewGuid();
	const FCatOnlineResult JoinResult = Online->RequestJoinSession(InvalidSearchHandle);
	TestFalse(TEXT("未知句柄 Join 不被受理"), JoinResult.bAccepted);
	TestEqual(TEXT("未知句柄 Join 返回 InvalidHandle"), JoinResult.Error, ECatOnlineError::InvalidHandle);

	const FCatOnlineResult LeaveResult = Online->RequestLeave();
	TestFalse(TEXT("无 Lake/Session 时 Leave 不被受理"), LeaveResult.bAccepted);
	TestEqual(TEXT("无 Lake/Session Leave 返回 InvalidState"), LeaveResult.Error, ECatOnlineError::InvalidState);
	return !HasAnyErrors();
}

// 测试流程：直接种入 DestroySession 已完成但 Frontend 尚未到达的 Leave 状态；来源 Lake 回载只能保持 pending，只有
// Frontend PostLoadMap 才能把离局操作结成成功。
bool FCatOnlineSubsystemLeaveWaitsForFrontendPostLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Leave PostLoadMap 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	Online->SeedTravelClosureStateForAutomation(
		ECatOnlineOperation::Leave,
		ECatOnlineSessionState::NoSession,
		ECatOnlineSessionRole::None,
		ECatOnlineSessionRole::Host,
		ECatOnlineWorldState::TravelingToFrontend,
		ECatOnlineTransportState::TravelQueued,
		FrontendPackageForOnlineAutomation);

	TestTrue(TEXT("测试 World 切到来源 Lake 包名"), MoveTestWorldToPackageForOnlineAutomation(WorldWrapper.GetTestWorld(), LakePackageForOnlineAutomation));
	FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(WorldWrapper.GetTestWorld());
	const FCatOnlineSnapshot SourceReloaded = Online->GetSnapshot();
	TestEqual(TEXT("来源 Lake 回载不能结束 Leave 操作"), SourceReloaded.ActiveOperation, ECatOnlineOperation::Leave);
	TestEqual(TEXT("来源 Lake 回载只更新 World 事实"), SourceReloaded.WorldState, ECatOnlineWorldState::Lake);
	TestEqual(TEXT("来源 Lake 回载后运输仍等待最终 Frontend"), SourceReloaded.TransportState, ECatOnlineTransportState::TravelQueued);
	TestEqual(TEXT("来源 Lake 回载不制造成功或失败错误"), SourceReloaded.LastError, ECatOnlineError::None);

	TestTrue(TEXT("测试 World 切到 Frontend 包名"), MoveTestWorldToPackageForOnlineAutomation(WorldWrapper.GetTestWorld(), FrontendPackageForOnlineAutomation));
	FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(WorldWrapper.GetTestWorld());
	const FCatOnlineSnapshot FrontendArrived = Online->GetSnapshot();
	TestEqual(TEXT("Frontend PostLoadMap 才结束 Leave 操作"), FrontendArrived.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("Frontend 到达后 World 事实为 Frontend"), FrontendArrived.WorldState, ECatOnlineWorldState::Frontend);
	TestEqual(TEXT("Frontend 到达后运输回到 Idle"), FrontendArrived.TransportState, ECatOnlineTransportState::Idle);
	TestEqual(TEXT("正常 Frontend 到达清空 LastError"), FrontendArrived.LastError, ECatOnlineError::None);
	return !HasAnyErrors();
}

// 测试流程：直接种入 DestroySession 已完成但 Frontend 尚未到达的 Leave 状态；未知包名必须以 UnexpectedMap 失败收口，防止错误地图被当作回前台成功。
bool FCatOnlineSubsystemUnexpectedPostLoadFailsOperationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 UnexpectedMap 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	Online->SeedTravelClosureStateForAutomation(
		ECatOnlineOperation::Leave,
		ECatOnlineSessionState::NoSession,
		ECatOnlineSessionRole::None,
		ECatOnlineSessionRole::Host,
		ECatOnlineWorldState::TravelingToFrontend,
		ECatOnlineTransportState::TravelQueued,
		FrontendPackageForOnlineAutomation);

	TestTrue(TEXT("测试 World 切到未知包名"), MoveTestWorldToPackageForOnlineAutomation(WorldWrapper.GetTestWorld(), UnknownPackageForOnlineAutomation));
	FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(WorldWrapper.GetTestWorld());
	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("未知包名结束当前操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("未知包名写入 Error World 事实"), Snapshot.WorldState, ECatOnlineWorldState::Error);
	TestEqual(TEXT("未知包名写入 Failed 运输事实"), Snapshot.TransportState, ECatOnlineTransportState::Failed);
	TestEqual(TEXT("未知包名以 UnexpectedMap 收口"), Snapshot.LastError, ECatOnlineError::UnexpectedMap);
	return !HasAnyErrors();
}

// 测试流程：直接种入等待 Frontend 到达的 Leave 状态；TravelFailure 必须保留来源 World、失败运输和 TravelFailed 终态，
// 不能把 DestroySession 完成误报成离局成功。
bool FCatOnlineSubsystemTravelFailureDuringLeaveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Leave TravelFailure 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	Online->SeedTravelClosureStateForAutomation(
		ECatOnlineOperation::Leave,
		ECatOnlineSessionState::NoSession,
		ECatOnlineSessionRole::None,
		ECatOnlineSessionRole::Host,
		ECatOnlineWorldState::TravelingToFrontend,
		ECatOnlineTransportState::TravelQueued,
		FrontendPackageForOnlineAutomation);

	TestNotNull(TEXT("GEngine 可广播 TravelFailure"), GEngine);
	AddExpectedErrorPlain(TEXT("Event=online_travel_failure"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("测试 World 切到来源 Lake 包名"), MoveTestWorldToPackageForOnlineAutomation(WorldWrapper.GetTestWorld(), LakePackageForOnlineAutomation));
	if (GEngine)
	{
		GEngine->OnTravelFailure().Broadcast(WorldWrapper.GetTestWorld(), ETravelFailure::LoadMapFailure, TEXT("AutomationTravelFailure"));
	}
	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("TravelFailure 结束 Leave 操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("TravelFailure 保留来源 Lake World 事实"), Snapshot.WorldState, ECatOnlineWorldState::Lake);
	TestEqual(TEXT("TravelFailure 写入 Failed 运输事实"), Snapshot.TransportState, ECatOnlineTransportState::Failed);
	TestEqual(TEXT("TravelFailure 以 TravelFailed 收口"), Snapshot.LastError, ECatOnlineError::TravelFailed);
	return !HasAnyErrors();
}

// 测试流程：把平台操作超时覆盖成极短窗口，种入"CreateSession 已提交、仍等待回调"的状态，再按帧推进世界时间直到计时器到期。
// 它锁住的不变量是：平台回调不来时 Create 不会永久停在 Creating，而是由计时器走补偿并以 OperationTimedOut 结案。
bool FCatOnlineSubsystemCreateTimeoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Create 超时测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	const FOnlineOperationTimeoutOverride TimeoutOverride;
	TestTrue(TEXT("推进测试世界时间"), RunPendingOperationTimeoutForOnlineAutomation(
		*this, WorldWrapper, *Online, ECatOnlineOperation::Create, ECatOnlineSessionState::Creating));

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("Create 超时结束当前操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	// 补偿会先尝试销毁"平台可能已经建成"的本地 NamedSession；测试环境里不存在该会话，因此两条补偿分支都收敛到没有会话。
	TestEqual(TEXT("Create 超时补偿后本地不留 NamedSession"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("Create 超时不留下会话角色"), Snapshot.SessionRole, ECatOnlineSessionRole::None);
	TestEqual(TEXT("Create 超时以 OperationTimedOut 收口"), Snapshot.LastError, ECatOnlineError::OperationTimedOut);
	return !HasAnyErrors();
}

// 测试流程：把平台操作超时覆盖成极短窗口，种入"FindSessions 已提交、仍等待回调"的状态，再按帧推进世界时间直到计时器到期。
// 它锁住的不变量是：搜索回调不来时操作不会永久停在 Searching，并且失败收口不会留下半份搜索结果。
bool FCatOnlineSubsystemFindTimeoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Find 超时测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	const FOnlineOperationTimeoutOverride TimeoutOverride;
	TestTrue(TEXT("推进测试世界时间"), RunPendingOperationTimeoutForOnlineAutomation(
		*this, WorldWrapper, *Online, ECatOnlineOperation::Find, ECatOnlineSessionState::Searching));

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("Find 超时结束当前操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("Find 超时不制造 NamedSession 事实"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("Find 超时以 OperationTimedOut 收口"), Snapshot.LastError, ECatOnlineError::OperationTimedOut);
	TestEqual(TEXT("Find 超时后快照不留搜索结果"), Snapshot.SearchResults.Num(), 0);
	return !HasAnyErrors();
}

// 测试流程：把平台操作超时覆盖成极短窗口，种入"JoinSession 已提交、仍等待回调"的状态，再按帧推进世界时间直到计时器到期。
// 它锁住的不变量是：Join 回调不来时不会永久停在 Joining，且和 Create 一样先走一次本地 NamedSession 补偿再收口。
bool FCatOnlineSubsystemJoinTimeoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Join 超时测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	const FOnlineOperationTimeoutOverride TimeoutOverride;
	TestTrue(TEXT("推进测试世界时间"), RunPendingOperationTimeoutForOnlineAutomation(
		*this, WorldWrapper, *Online, ECatOnlineOperation::Join, ECatOnlineSessionState::Joining));

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("Join 超时结束当前操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("Join 超时补偿后本地不留 NamedSession"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("Join 超时不留下会话角色"), Snapshot.SessionRole, ECatOnlineSessionRole::None);
	TestEqual(TEXT("Join 超时以 OperationTimedOut 收口"), Snapshot.LastError, ECatOnlineError::OperationTimedOut);
	return !HasAnyErrors();
}

// 测试流程：把平台操作超时覆盖成极短窗口，种入"DestroySession 已提交、仍等待回调"的状态，再按帧推进世界时间直到计时器到期。
// 它锁住的不变量是：Destroy 回调不来时操作必须结束，但本地 NamedSession 事实要留在 Error——此时没有任何证据说明平台那边到底销毁了没有，
// 所以这条路径不再发第二次 Destroy，也不据此发起回前台旅行。
bool FCatOnlineSubsystemDestroyTimeoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Destroy 超时测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	const FOnlineOperationTimeoutOverride TimeoutOverride;
	TestTrue(TEXT("推进测试世界时间"), RunPendingOperationTimeoutForOnlineAutomation(
		*this, WorldWrapper, *Online, ECatOnlineOperation::Leave, ECatOnlineSessionState::Destroying));

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	TestEqual(TEXT("Destroy 超时结束当前操作"), Snapshot.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("Destroy 超时把本地会话事实留在 Error"), Snapshot.SessionState, ECatOnlineSessionState::Error);
	TestEqual(TEXT("Destroy 超时以 OperationTimedOut 收口"), Snapshot.LastError, ECatOnlineError::OperationTimedOut);
	return !HasAnyErrors();
}

// 测试流程：种入已确认的 Host 会话角色，再逐项验证成员登记、房主唯一性和踢人授权。
// 它锁住的不变量是：只有已登记房主能踢人、房主不能踢自己、不在册身份不能被踢；成功踢人只释放目标的成员关系，其余成员的入局顺序不变。
bool FCatOnlineSubsystemHostKickTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建房主踢人测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UGameInstance* GameInstance = WorldWrapper.GetTestWorld() ? WorldWrapper.GetTestWorld()->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("真实 Online 子系统可用"), Online);
	if (!Online)
	{
		return false;
	}

	// 踢人只在承载 Listen Server 的房主进程成立，因此先种入已确认的 Host 会话角色和 Lake World 事实。
	Online->SeedTravelClosureStateForAutomation(
		ECatOnlineOperation::None,
		ECatOnlineSessionState::Host,
		ECatOnlineSessionRole::Host,
		ECatOnlineSessionRole::None,
		ECatOnlineWorldState::Lake,
		ECatOnlineTransportState::Connected,
		FString());

	const FString HostStableNetId(TEXT("AutomationHostIdentity"));
	const FString GuestStableNetId(TEXT("AutomationGuestIdentity"));
	const FString SecondHostStableNetId(TEXT("AutomationSecondHostIdentity"));
	const FString UnknownStableNetId(TEXT("AutomationUnknownIdentity"));

	TestEqual(TEXT("还没有登记房主时踢人被拒绝"),
		Online->CommitHostKick(HostStableNetId, GuestStableNetId), ECatOnlineError::InvalidState);

	TestTrue(TEXT("房主可以登记为成员"), Online->RegisterSessionMember(HostStableNetId, true));
	TestTrue(TEXT("访客可以登记为成员"), Online->RegisterSessionMember(GuestStableNetId, false));
	TestTrue(TEXT("重复登记同一成员被接受"), Online->RegisterSessionMember(GuestStableNetId, false));
	TestFalse(TEXT("第二个房主声明被拒绝"), Online->RegisterSessionMember(SecondHostStableNetId, true));
	// 踢人入口本身就会拒绝不在册的目标，因此它同时是"这个身份在不在成员关系里"的可用探针。
	TestEqual(TEXT("被拒绝的房主声明不进入成员关系"),
		Online->CommitHostKick(HostStableNetId, SecondHostStableNetId), ECatOnlineError::InvalidState);

	TestEqual(TEXT("非房主发起的踢人被拒绝"),
		Online->CommitHostKick(GuestStableNetId, HostStableNetId), ECatOnlineError::InvalidState);
	TestEqual(TEXT("房主不能踢自己"),
		Online->CommitHostKick(HostStableNetId, HostStableNetId), ECatOnlineError::InvalidState);
	TestEqual(TEXT("不在册身份不能被踢"),
		Online->CommitHostKick(HostStableNetId, UnknownStableNetId), ECatOnlineError::InvalidState);
	// 上面三次被拒绝的踢人都没有动过成员关系，所以接下来这次合法踢人仍然能成功。
	TestEqual(TEXT("房主踢在册访客被受理"),
		Online->CommitHostKick(HostStableNetId, GuestStableNetId), ECatOnlineError::None);
	TestEqual(TEXT("被踢者已不在册，重复踢同一目标被拒绝"),
		Online->CommitHostKick(HostStableNetId, GuestStableNetId), ECatOnlineError::InvalidState);

	TestTrue(TEXT("房主自身可以注销成员关系"), Online->UnregisterSessionMember(HostStableNetId));
	TestEqual(TEXT("房主离开后不自动移交，房主身份为空因而踢人被拒绝"),
		Online->CommitHostKick(HostStableNetId, GuestStableNetId), ECatOnlineError::InvalidState);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
