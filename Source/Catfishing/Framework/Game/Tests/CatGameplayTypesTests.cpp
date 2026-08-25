#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Character/CatCharacter.h"
#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Net/OnlineEngineInterface.h"
#include "Online/CatOnlineSettings.h"
#include "OnlineSubsystemTypes.h"
#include "Social/CatProtectionSignActor.h"
#include "Social/CatSocialSettings.h"

namespace CatGameplayTypesTest
{
	// 测试身份类型读取流程：PreLogin 会先走引擎底层身份兼容校验；无人值守 Editor 里 Steam 可能未实际加载，所以测试按当前引擎接口看到的默认类型造身份。
	static FName GetTestOnlineIdType()
	{
		const UOnlineEngineInterface* OnlineEngineInterface = UOnlineEngineInterface::Get();
		return OnlineEngineInterface
			? OnlineEngineInterface->GetDefaultOnlineSubsystemName()
			: NAME_None;
	}

	// 稳定身份构造流程：所有 GameMode 准入测试共用这一条入口，保证 PlayerState、PreLogin 和私有准入表看到的是同一个 StableNetId 文本。
	static FUniqueNetIdRef MakeStableUniqueId(const FString& StableNetId)
	{
		return FUniqueNetIdString::Create(StableNetId, GetTestOnlineIdType());
	}

	// 玩家身份夹具流程：创建真实项目 Controller/PlayerState，并把指定 StableNetId 写入继承 UniqueId；准入表仍由测试显式选择 Active 或 PreLogin 写入。
	static ACatfishingPlayerController* SpawnControllerWithStableNetId(
		FAutomationTestBase& Test, UWorld* World, const FString& StableNetId)
	{
		ACatfishingPlayerController* Controller = World
			? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
		ACatfishingPlayerState* PlayerState = World
			? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
		Test.TestNotNull(TEXT("可生成重连测试 Controller"), Controller);
		Test.TestNotNull(TEXT("可生成重连测试 PlayerState"), PlayerState);
		if (!Controller || !PlayerState)
		{
			return nullptr;
		}

		const FUniqueNetIdRef UniqueId = MakeStableUniqueId(StableNetId);
		PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		Controller->PlayerState = PlayerState;
		return Controller;
	}

	static FCatWaterGeometryCache BuildLakeCache()
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = TEXT("LakeA");
		Input.PlaneToWorld = FTransform::Identity;
		Input.WaterPointVerticalToleranceCm = 10;
		Input.BankHeightToleranceCm = 20;
		Input.BoundaryToleranceCm = 1;
		Input.MaxLandingCorrectionCm = 10;
		Input.MinimumWaterInsetCm = 2;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = {{-100,-100}, {100,-100}, {100,100}, {-100,100}};
		return FCatWaterGeometry::Build(Input).Cache;
	}

	// RunEnvironmentSocial 玩家夹具流程：
	// 1. 生成真实 Controller/PlayerState/Character 并完成占有，让 Social 查询 PlayerState、角色位置和 owning client 状态时走项目对象。
	// 2. 它不负责登录或准入，Active 记录由测试体统一种入，避免把入口闭环拆成多个可独立完成的小夹具。
	static ACatfishingPlayerController* SpawnSocialController(
		FAutomationTestBase& Test, UWorld* World, const FString& StableNetId,
		const FVector& Location, ACatCharacter*& OutCharacter)
	{
		ACatfishingPlayerController* Controller = World
			? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
		ACatfishingPlayerState* PlayerState = World
			? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
		OutCharacter = World ? World->SpawnActor<ACatCharacter>(Location, FRotator::ZeroRotator) : nullptr;
		Test.TestNotNull(TEXT("可生成 Social Controller"), Controller);
		Test.TestNotNull(TEXT("可生成 Social PlayerState"), PlayerState);
		Test.TestNotNull(TEXT("可生成 Social Character"), OutCharacter);
		if (!Controller || !PlayerState || !OutCharacter)
		{
			return nullptr;
		}

		const FUniqueNetIdRef UniqueId = MakeStableUniqueId(StableNetId);
		PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		Controller->PlayerState = PlayerState;
		OutCharacter->SetPlayerState(PlayerState);
		Controller->Possess(OutCharacter);
		return Controller;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeRunCommandFailClosedTest,
	"Catfishing.Unit.Framework.GameMode.RunCommandsFailClosedBeforeRuntimeStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeCommandIntentGateTest,
	"Catfishing.Unit.Framework.GameMode.CommandIntentPhaseGatesKeepSocialReadyAndSettlementOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeReconnectAdmissionWhitelistTest,
	"Catfishing.Unit.Framework.GameMode.ReconnectAdmissionWhitelistIsFailClosedEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeRunEnvironmentSocialPlayerEntrypointContractTest,
	"Catfishing.Unit.Framework.GameMode.RunEnvironmentSocialPlayerEntrypointsStayAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 合同测试类型：把献祭结果和营地命令结果的 owning-client RPC 声明、以及本地最近结果缓存放在同一处守护，避免 UI 读取入口和服务器回包合同分叉。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerControllerCampAndSacrificeResultContractTest,
	"Catfishing.Unit.Framework.PlayerController.CampAndSacrificeResultsUseReliableOwningClientRpcs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：生成真实 Lake GameMode 但不启动 Run StateTree；协调器额度写口必须在命令门关闭时拒绝，并把首次终态缓存为可重放结果。
bool FCatGameModeRunCommandFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 GameMode 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatfishingGameModeBase* GameMode = World->SpawnActor<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可生成项目 Lake GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = 0;
	Command.Context.StableNetId = TEXT("CoordinatorStableId");
	Command.Contribution = 3;
	const FCatRunCommandResult First = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("Run 未启动前额度写口不提交"), First.bCommitted);
	TestEqual(TEXT("Run 未启动前返回 CommandsClosed"), First.Error, ECatRunCommandError::CommandsClosed);
	TestEqual(TEXT("拒绝结果关联原 RequestId"), First.RequestId, Command.Context.RequestId);

	const FCatRunCommandResult Replay = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("同一请求重放不提交"), Replay.bCommitted);
	TestEqual(TEXT("同一请求重放返回 AlreadyResolved"), Replay.Error, ECatRunCommandError::AlreadyResolved);
	TestEqual(TEXT("重放保留首次 Revision"), Replay.Revision, First.Revision);
	TestEqual(TEXT("默认 Run 公开状态仍未开始"), GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::NotStarted);
	return !HasAnyErrors();
}

// 测试流程：直接种入一条 Active 身份记录并逐个切换公开 Phase；宽玩法 gate 代表 Social/Ready/Settlement 仍可收口，Fishing gate 只在 DayActive 且 bFishingAllowed 时开放新钓鱼或玩家打窝。
bool FCatGameModeCommandIntentGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 CommandIntent gate 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 CommandIntent 测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatfishingGameModeBase* GameMode = World->SpawnActor<ACatfishingGameModeBase>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("可生成 CommandIntent GameMode"), GameMode);
	TestNotNull(TEXT("可生成 CommandIntent Controller"), Controller);
	TestNotNull(TEXT("可生成 CommandIntent PlayerState"), PlayerState);
	if (!GameMode || !Controller || !PlayerState)
	{
		return false;
	}

	const FString StableNetId = TEXT("CommandIntentPlayer");
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId, FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;

	ACatfishingGameModeBase::FAdmissionRecord Record;
	Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Record.Controller = Controller;
	GameMode->AdmissionRecords.Add(StableNetId, Record);
	GameMode->RunPublicState.Phase.RunId = FGuid::NewGuid();
	GameMode->RunPublicState.Revision = 1;
	GameMode->bRunCommandsOpen = true;

	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	GameMode->RunPublicState.Phase.bFishingAllowed = true;
	TestTrue(TEXT("白天宽玩法命令可进入"), GameMode->CanAcceptGameplayCommand(Controller));
	TestTrue(TEXT("白天允许 Fishing/Chum 命令"), GameMode->CanAcceptFishingCommand(Controller));

	GameMode->RunPublicState.Phase.bFishingAllowed = false;
	TestTrue(TEXT("白天收口命令仍可进入宽 gate"), GameMode->CanAcceptGameplayCommand(Controller));
	TestFalse(TEXT("白天关闭 FishingAllowed 后拒绝 Fishing/Chum"), GameMode->CanAcceptFishingCommand(Controller));

	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::NormalNight;
	GameMode->RunPublicState.Phase.bFishingAllowed = false;
	TestTrue(TEXT("普通夜晚仍允许 ready/social 宽 gate"), GameMode->CanAcceptGameplayCommand(Controller));
	TestFalse(TEXT("普通夜晚拒绝新 Fishing/Chum"), GameMode->CanAcceptFishingCommand(Controller));

	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::SuccessSettlementNight;
	TestTrue(TEXT("成功结算夜仍允许结算/social 宽 gate"), GameMode->CanAcceptGameplayCommand(Controller));
	TestFalse(TEXT("成功结算夜拒绝新 Fishing/Chum"), GameMode->CanAcceptFishingCommand(Controller));

	GameMode->bRunCommandsOpen = false;
	TestFalse(TEXT("teardown 后宽命令关闭"), GameMode->CanAcceptGameplayCommand(Controller));
	TestFalse(TEXT("teardown 后 Fishing/Chum 也关闭"), GameMode->CanAcceptFishingCommand(Controller));
	return !HasAnyErrors();
}

// 测试流程：先保存并临时改写 OnlineSettings CDO 的重连策略，退出时恢复；再让 Active 玩家按非主动断线 Logout 建立唯一 TTL 记录，走 PreLogin 消费未过期记录，并分别制造主动离局、未知白名单和过期记录，证明它们不会被误当成可恢复重连。
bool FCatGameModeReconnectAdmissionWhitelistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatOnlineSettings* OnlineSettings = GetMutableDefault<UCatOnlineSettings>();
	TestNotNull(TEXT("可读取 OnlineSettings CDO"), OnlineSettings);
	if (!OnlineSettings)
	{
		return false;
	}

	const ECatPolicyDecision SavedVoluntaryLeaveRecovery = OnlineSettings->VoluntaryLeaveRecovery;
	const int32 SavedReconnectRecordTtlSeconds = OnlineSettings->ReconnectRecordTtlSeconds;
	const int64 SavedRecoverableFailureMask = OnlineSettings->RecoverableFailureMask;
	const ECatPolicyDecision SavedExpiredAdmission = OnlineSettings->ExpiredAdmission;
	ON_SCOPE_EXIT
	{
		OnlineSettings->VoluntaryLeaveRecovery = SavedVoluntaryLeaveRecovery;
		OnlineSettings->ReconnectRecordTtlSeconds = SavedReconnectRecordTtlSeconds;
		OnlineSettings->RecoverableFailureMask = SavedRecoverableFailureMask;
		OnlineSettings->ExpiredAdmission = SavedExpiredAdmission;
	};

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建重连准入测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建重连准入测试 World"), World);
	if (!World)
	{
		return false;
	}

	World->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
	// GameMode 装配流程：通过测试 World 的正式 BeginPlay 链创建 AuthGameMode、GameSession 与 GameState，避免手工 Spawn 漏掉 PreLogin 的父类前置条件。
	if (!TestTrue(TEXT("启动重连准入测试 World"), WorldWrapper.BeginPlayInTestWorld()))
	{
		return false;
	}
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可读取重连准入测试 GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	// Active 准入种入流程：只在 friend 测试体内写私有准入表；正式恢复仍由 Logout/PreLogin 判断，不新增生产 setter。
	const auto AddActiveAdmission = [GameMode](AController* Controller, const FString& StableNetId)
	{
		ACatfishingGameModeBase::FAdmissionRecord Record;
		Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Record.Controller = Controller;
		GameMode->AdmissionRecords.Add(StableNetId, Record);
	};

	OnlineSettings->ReconnectRecordTtlSeconds = 30;
	OnlineSettings->RecoverableFailureMask = static_cast<int64>(ECatRecoverableFailure::ConnectionLost);
	OnlineSettings->VoluntaryLeaveRecovery = ECatPolicyDecision::Disabled;
	OnlineSettings->ExpiredAdmission = ECatPolicyDecision::Undecided;

	const FString ReconnectId = TEXT("76561198000000001");
	ACatfishingPlayerController* ReconnectController =
		CatGameplayTypesTest::SpawnControllerWithStableNetId(*this, World, ReconnectId);
	AddActiveAdmission(ReconnectController, ReconnectId);
	GameMode->Logout(ReconnectController);
	TestFalse(TEXT("非主动断线释放 Active 准入记录"),
		GameMode->AdmissionRecords.Contains(ReconnectId));
	TestTrue(TEXT("非主动断线按白名单建立 TTL 重连记录"),
		GameMode->ReconnectExpiryByStableNetId.Contains(ReconnectId));

	FString ReconnectError;
	const FUniqueNetIdRef ReconnectUniqueId = CatGameplayTypesTest::MakeStableUniqueId(ReconnectId);
	GameMode->PreLogin(TEXT(""), TEXT("ReconnectAddress"),
		FUniqueNetIdRepl(ReconnectUniqueId), ReconnectError);
	TestTrue(TEXT("未过期白名单记录允许 PreLogin 继续"), ReconnectError.IsEmpty());
	const ACatfishingGameModeBase::FAdmissionRecord* ReconnectRecord =
		GameMode->AdmissionRecords.Find(ReconnectId);
	TestTrue(TEXT("未过期重连写入 Reserved 准入记录"),
		ReconnectRecord && ReconnectRecord->Phase == ACatfishingGameModeBase::EAdmissionPhase::Reserved);
	TestTrue(TEXT("未过期重连写入 PendingReconnect 集合"),
		GameMode->PendingReconnectStableNetIds.Contains(ReconnectId));

	const FString VoluntaryId = TEXT("76561198000000002");
	ACatfishingPlayerController* VoluntaryController =
		CatGameplayTypesTest::SpawnControllerWithStableNetId(*this, World, VoluntaryId);
	AddActiveAdmission(VoluntaryController, VoluntaryId);
	GameMode->MarkVoluntaryLeave(VoluntaryController);
	GameMode->Logout(VoluntaryController);
	TestFalse(TEXT("主动离局在 Disabled 策略下不保留重连记录"),
		GameMode->ReconnectExpiryByStableNetId.Contains(VoluntaryId));

	// 未知恢复位夹具：第 7 位当前不属于任何已裁恢复原因；把它混入白名单后必须整体 fail-closed，避免未来新增原因被旧配置误放行。
	OnlineSettings->RecoverableFailureMask = static_cast<int64>(ECatRecoverableFailure::ConnectionLost) | (1LL << 7);
	const FString UnknownMaskId = TEXT("76561198000000003");
	ACatfishingPlayerController* UnknownMaskController =
		CatGameplayTypesTest::SpawnControllerWithStableNetId(*this, World, UnknownMaskId);
	AddActiveAdmission(UnknownMaskController, UnknownMaskId);
	GameMode->Logout(UnknownMaskController);
	TestFalse(TEXT("白名单含未知位时不建立重连记录"),
		GameMode->ReconnectExpiryByStableNetId.Contains(UnknownMaskId));

	OnlineSettings->RecoverableFailureMask = static_cast<int64>(ECatRecoverableFailure::ConnectionLost);
	const FString ExpiredId = TEXT("76561198000000004");
	GameMode->ReconnectExpiryByStableNetId.Add(ExpiredId, World->GetTimeSeconds() - 1.0);
	FString ExpiredError;
	const FUniqueNetIdRef ExpiredUniqueId = CatGameplayTypesTest::MakeStableUniqueId(ExpiredId);
	GameMode->PreLogin(TEXT(""), TEXT("ExpiredAddress"),
		FUniqueNetIdRepl(ExpiredUniqueId), ExpiredError);
	TestEqual(TEXT("过期记录在未裁策略下 fail-closed"),
		ExpiredError, FString(TEXT("CAT_POLICY_UNDECIDED:ExpiredReconnectAdmission")));
	TestFalse(TEXT("过期记录不会新建 Reserved 准入"),
		GameMode->AdmissionRecords.Contains(ExpiredId));
	TestFalse(TEXT("过期记录不会进入 PendingReconnect"),
		GameMode->PendingReconnectStableNetIds.Contains(ExpiredId));
	return !HasAnyErrors();
}

// 玩家入口闭环测试流程：
// 1. 先锁住 Controller Social RPC、偷鱼结果 RPC 与 Chum RPC 的网络声明，确认玩家只从正式入口进出。
// 2. 再在真实 Lake GameMode 测试 World 中种入同一名 Active 玩家，并把 Run 固定在普通夜晚。
// 3. 同一个 Controller 继续验证求助、保护牌、偷鱼结果缓存和打窝回执，证明 Social 宽 gate 与 Chum 窄 gate 在同一入口闭环里协同。
// 4. 本测试是机器侧入口合同证据，不证明真人多客户端同步、UI 操作、跨进程 RPC 传输或整局人工验收；这些仍作为模块级同一闭环处理。
bool FCatGameModeRunEnvironmentSocialPlayerEntrypointContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UClass* ControllerClass = ACatfishingPlayerController::StaticClass();
	for (const FName FunctionName : {
		FName(TEXT("ServerBeginTheft")),
		FName(TEXT("ServerCatchTheft")),
		FName(TEXT("ServerRequestManualHelp")),
		FName(TEXT("ServerRequestMischief")),
		FName(TEXT("ServerPlaceProtectionSign"))})
	{
		const UFunction* Function = ControllerClass->FindFunctionByName(FunctionName);
		TestNotNull(*FString::Printf(TEXT("%s Social RPC 已反射"), *FunctionName.ToString()), Function);
		if (Function)
		{
			TestTrue(*FString::Printf(TEXT("%s 只能发往服务器"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetServer));
			TestTrue(*FString::Printf(TEXT("%s 使用 Reliable"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetReliable));
		}
	}
	const UFunction* TheftResultRpc = ControllerClass->FindFunctionByName(TEXT("ClientReceiveTheftResult"));
	TestNotNull(TEXT("偷鱼结果 owning-client RPC 已反射"), TheftResultRpc);
	if (TheftResultRpc)
	{
		TestTrue(TEXT("偷鱼结果只发 owning client"), TheftResultRpc->HasAnyFunctionFlags(FUNC_NetClient));
		TestTrue(TEXT("偷鱼结果使用 Reliable"), TheftResultRpc->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	const UFunction* PlaceChumRpc = UCatFishingCommandComponent::StaticClass()->FindFunctionByName(
		TEXT("ServerSubmitPlaceChum"));
	TestNotNull(TEXT("显式打窝服务器 RPC 已反射"), PlaceChumRpc);
	if (PlaceChumRpc)
	{
		TestTrue(TEXT("显式打窝只能发往服务器"), PlaceChumRpc->HasAnyFunctionFlags(FUNC_NetServer));
		TestTrue(TEXT("显式打窝使用 Reliable"), PlaceChumRpc->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	UCatSocialSettings* SocialSettings = GetMutableDefault<UCatSocialSettings>();
	TestNotNull(TEXT("可读取 SocialSettings CDO"), SocialSettings);
	if (!SocialSettings)
	{
		return false;
	}

	const bool bSavedEnableSocialRuntime = SocialSettings->bEnableSocialRuntime;
	const ECatDomainPolicy SavedMischiefPermission = SocialSettings->MischiefPermission;
	const double SavedMischiefCooldownSeconds = SocialSettings->MischiefCooldownSeconds;
	const double SavedMischiefInteractionRangeCentimeters = SocialSettings->MischiefInteractionRangeCentimeters;
	const double SavedProtectionSignRadiusCentimeters = SocialSettings->ProtectionSignRadiusCentimeters;
	const double SavedProtectionSignPlacementRangeCentimeters = SocialSettings->ProtectionSignPlacementRangeCentimeters;
	const double SavedManualHelpRadiusCentimeters = SocialSettings->ManualHelpRadiusCentimeters;
	const double SavedManualHelpCooldownSeconds = SocialSettings->ManualHelpCooldownSeconds;
	ON_SCOPE_EXIT
	{
		SocialSettings->bEnableSocialRuntime = bSavedEnableSocialRuntime;
		SocialSettings->MischiefPermission = SavedMischiefPermission;
		SocialSettings->MischiefCooldownSeconds = SavedMischiefCooldownSeconds;
		SocialSettings->MischiefInteractionRangeCentimeters = SavedMischiefInteractionRangeCentimeters;
		SocialSettings->ProtectionSignRadiusCentimeters = SavedProtectionSignRadiusCentimeters;
		SocialSettings->ProtectionSignPlacementRangeCentimeters = SavedProtectionSignPlacementRangeCentimeters;
		SocialSettings->ManualHelpRadiusCentimeters = SavedManualHelpRadiusCentimeters;
		SocialSettings->ManualHelpCooldownSeconds = SavedManualHelpCooldownSeconds;
	};
	SocialSettings->bEnableSocialRuntime = true;
	SocialSettings->MischiefPermission = ECatDomainPolicy::Enabled;
	SocialSettings->MischiefCooldownSeconds = 5.0;
	SocialSettings->MischiefInteractionRangeCentimeters = 300.0;
	SocialSettings->ProtectionSignRadiusCentimeters = 200.0;
	SocialSettings->ProtectionSignPlacementRangeCentimeters = 120.0;
	SocialSettings->ManualHelpRadiusCentimeters = 450.0;
	SocialSettings->ManualHelpCooldownSeconds = 6.0;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 RunEnvironmentSocial 玩家入口测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("RunEnvironmentSocial 玩家入口 World 可用"), World);
	if (!World)
	{
		return false;
	}
	World->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
	// 测试 World 生命周期由 FTestWorldWrapper 成对管理；这里必须走正式 BeginPlay 链，让 GameMode/GameState/Subsystem 与真实 Lake 入口同源。
	if (!TestTrue(TEXT("启动 RunEnvironmentSocial 玩家入口测试 World"), WorldWrapper.BeginPlayInTestWorld()))
	{
		return false;
	}
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	ACatfishingGameState* GameState = World->GetGameState<ACatfishingGameState>();
	TestNotNull(TEXT("可读取 RunEnvironmentSocial GameMode"), GameMode);
	TestNotNull(TEXT("可读取 RunEnvironmentSocial GameState"), GameState);
	if (!GameMode || !GameState)
	{
		return false;
	}

	ACatCharacter* Character = nullptr;
	const FString StableNetId = TEXT("RunEnvironmentSocialPlayer");
	ACatfishingPlayerController* Controller = CatGameplayTypesTest::SpawnSocialController(
		*this, World, StableNetId, FVector::ZeroVector, Character);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
	TestNotNull(TEXT("可创建本地玩家以触发公开打窝入口"), LocalPlayer.Get());
	if (!Controller || !Character || !LocalPlayer)
	{
		return false;
	}
	// 公开打窝组件需要本地 owning Controller 才能走 SubmitPlaceChum；这里只补 LocalPlayer 身份，后续是否允许仍由 GameMode gate 裁决。
	Controller->SetPlayer(LocalPlayer.Get());

	// Phase 夹具只固定同一 Run 的普通夜晚事实：Social 应继续收口，新的 Fishing/Chum 必须被窄 gate 拒绝。
	ACatfishingGameModeBase::FAdmissionRecord Record;
	Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Record.Controller = Controller;
	GameMode->AdmissionRecords.Add(StableNetId, Record);
	GameMode->RunPublicState.Phase.RunId = FGuid::NewGuid();
	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::NormalNight;
	GameMode->RunPublicState.Phase.bFishingAllowed = false;
	GameMode->RunPublicState.Revision = 1;
	GameMode->bRunCommandsOpen = true;

	TestTrue(TEXT("普通夜晚 Social 宽 gate 仍允许玩家收口命令"), GameMode->CanAcceptGameplayCommand(Controller));
	TestFalse(TEXT("普通夜晚 Fishing/玩家打窝窄 gate 关闭"), GameMode->CanAcceptFishingCommand(Controller));

	const FGuid HelpRequestId = FGuid::NewGuid();
	Controller->ServerRequestManualHelp_Implementation(HelpRequestId, ECatHelpSignalKind::ManualFishing);
	const FCatHelpSignalSnapshot& HelpSignal = GameState->GetLastHelpSignal();
	TestEqual(TEXT("手动求助通过 Controller 发布到 GameState"), HelpSignal.SignalId, HelpRequestId);
	TestEqual(TEXT("手动求助保持 ManualFishing 类型"), HelpSignal.Kind, ECatHelpSignalKind::ManualFishing);
	TestFalse(TEXT("普通手动求助不会升级为全局提示"), HelpSignal.bGlobal);
	TestEqual(TEXT("手动求助使用 Social 配置半径"), HelpSignal.RadiusCentimeters,
		SocialSettings->ManualHelpRadiusCentimeters);

	const FVector SignLocation = Character->GetActorLocation() + FVector(25.0, 0.0, 0.0);
	Controller->ServerPlaceProtectionSign_Implementation(FGuid::NewGuid(), SignLocation);
	int32 ProtectingSigns = 0;
	for (TActorIterator<ACatProtectionSignActor> It(World); It; ++It)
	{
		if (It->ProtectsMischiefAgainst(Controller->PlayerState, Character->GetActorLocation()))
		{
			++ProtectingSigns;
		}
	}
	TestEqual(TEXT("保护牌通过 Controller 入口生成唯一有效保护 Actor"), ProtectingSigns, 1);

	FCatTheftResult TheftResult;
	TheftResult.Command.RequestId = FGuid::NewGuid();
	TheftResult.Command.Error = ECatDomainCommandError::PermissionDenied;
	TheftResult.TheftProtocolId = FGuid::NewGuid();
	TheftResult.FishInstanceId = FGuid::NewGuid();
	TheftResult.bRecoveryWindowOpen = true;
	Controller->ClientReceiveTheftResult_Implementation(TheftResult);
	const FCatTheftResult StoredTheft = Controller->GetLastTheftResult();
	TestEqual(TEXT("偷鱼结果缓存保留 ProtocolId"), StoredTheft.TheftProtocolId, TheftResult.TheftProtocolId);
	TestEqual(TEXT("偷鱼结果缓存保留 FishInstanceId"), StoredTheft.FishInstanceId, TheftResult.FishInstanceId);
	TestEqual(TEXT("偷鱼结果缓存保留追回窗口状态"), StoredTheft.bRecoveryWindowOpen,
		TheftResult.bRecoveryWindowOpen);
	TestEqual(TEXT("偷鱼结果缓存保留错误"), StoredTheft.Command.Error, TheftResult.Command.Error);

	UCatFishingCommandComponent* Commands = Controller->GetFishingCommandComponent();
	TestNotNull(TEXT("Controller 持有玩家 Fishing/Chum 命令组件"), Commands);
	if (!Commands)
	{
		return false;
	}
	// 打窝失败会写结构化日志；这里把预期日志纳入测试，避免把 fail-closed 误判为异常噪声。
	AddExpectedErrorPlain(TEXT("Event=place_chum_result Committed=false"), EAutomationExpectedErrorFlags::Contains, 1);
	FCatPlaceChumCommand ChumCommand;
	ChumCommand.RequestId = FGuid::NewGuid();
	Commands->SubmitPlaceChum(ChumCommand);
	FCatPlaceChumResult ChumResult;
	TestTrue(TEXT("普通夜晚显式打窝返回结构化回执"),
		Commands->TryGetPlaceChumResult(ChumCommand.RequestId, ChumResult));
	TestFalse(TEXT("普通夜晚显式打窝不会提交"), ChumResult.bCommitted);
	TestEqual(TEXT("普通夜晚显式打窝返回 CommandsClosed"), ChumResult.Error,
		ECatChumFieldError::CommandsClosed);
	return !HasAnyErrors();
}

// 测试装配与证据边界：
// 1. 先从反射合同确认两个结果入口都是 Reliable Client RPC，锁住服务器只回发 owning client 的网络边界。
// 2. 再在 Standalone 测试 World 中生成项目 PlayerController，并分别调用两个本地实现入口。
// 3. 逐字段核对最近结果读模型完整替换，避免为了测试新增生产 setter 或只验证部分字段。
// 4. 本测试只证明反射标记和本地缓存替换，不证明跨进程 RPC 传输、协调器落盘或 UI 消费链路。
bool FCatPlayerControllerCampAndSacrificeResultContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UClass* ControllerClass = ACatfishingPlayerController::StaticClass();
	for (const FName FunctionName : {
		FName(TEXT("ClientReceiveSacrificeResult")),
		FName(TEXT("ClientReceiveCampCommandResult"))})
	{
		const UFunction* Function = ControllerClass->FindFunctionByName(FunctionName);
		TestNotNull(*FString::Printf(TEXT("%s RPC 已反射"), *FunctionName.ToString()), Function);
		if (Function)
		{
			TestTrue(*FString::Printf(TEXT("%s 具备网络标记"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_Net));
			TestTrue(*FString::Printf(TEXT("%s 只发给 owning client"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetClient));
			TestTrue(*FString::Printf(TEXT("%s 使用 Reliable"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetReliable));
		}
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 PlayerController 结果测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World
		? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	TestNotNull(TEXT("可生成项目 PlayerController"), Controller);
	if (!Controller)
	{
		return false;
	}

	FCatSacrificeResult SacrificeResult;
	SacrificeResult.RequestId = FGuid::NewGuid();
	SacrificeResult.Stage = ECatSacrificeStage::ItemsCommitted;
	SacrificeResult.bCompleted = false;
	SacrificeResult.Error = ECatDomainCommandError::DependencyUnavailable;
	SacrificeResult.ItemsRevision = 17;
	SacrificeResult.RunRevision = 23;
	SacrificeResult.AppliedContribution = 5;
	Controller->ClientReceiveSacrificeResult_Implementation(SacrificeResult);
	const FCatSacrificeResult StoredSacrifice = Controller->GetLastSacrificeResult();
	TestEqual(TEXT("献祭结果保存 RequestId"), StoredSacrifice.RequestId, SacrificeResult.RequestId);
	TestEqual(TEXT("献祭结果保存阶段"), StoredSacrifice.Stage, SacrificeResult.Stage);
	TestEqual(TEXT("献祭结果保存完成标记"), StoredSacrifice.bCompleted, SacrificeResult.bCompleted);
	TestEqual(TEXT("献祭结果保存错误"), StoredSacrifice.Error, SacrificeResult.Error);
	TestEqual(TEXT("献祭结果保存 Items Revision"), StoredSacrifice.ItemsRevision, SacrificeResult.ItemsRevision);
	TestEqual(TEXT("献祭结果保存 Run Revision"), StoredSacrifice.RunRevision, SacrificeResult.RunRevision);
	TestEqual(TEXT("献祭结果保存贡献"), StoredSacrifice.AppliedContribution, SacrificeResult.AppliedContribution);

	FCatDomainCommandResult CampResult;
	CampResult.bCommitted = true;
	CampResult.RequestId = FGuid::NewGuid();
	CampResult.Error = ECatDomainCommandError::None;
	CampResult.Revision = 31;
	Controller->ClientReceiveCampCommandResult_Implementation(CampResult);
	const FCatDomainCommandResult StoredCamp = Controller->GetLastCampCommandResult();
	TestEqual(TEXT("营地结果保存提交标记"), StoredCamp.bCommitted, CampResult.bCommitted);
	TestEqual(TEXT("营地结果保存 RequestId"), StoredCamp.RequestId, CampResult.RequestId);
	TestEqual(TEXT("营地结果保存错误"), StoredCamp.Error, CampResult.Error);
	TestEqual(TEXT("营地结果保存 Revision"), StoredCamp.Revision, CampResult.Revision);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
