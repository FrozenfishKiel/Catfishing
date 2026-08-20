#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Engine/Player.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatRunSettings.h"
#include "Social/CatSocialService.h"
#include "Social/CatSocialSettings.h"
#include "TimerManager.h"

namespace CatfishingGameModeTest
{
    /** GameMode 启动测试的空 Data 包覆盖守卫；构造时暂时清空 Fish/Equipment 目录，析构时恢复默认配置。 */
    struct FEmptyDataCatalogOverride
    {
        /** 被临时覆盖的全局 Fish Catalog 默认对象；StartPlay 会通过它读取正式鱼表目录。 */
        UCatFishCatalogSettings* FishSettings = nullptr;

        /** 测试开始前的鱼目录 SchemaVersion；恢复时保持项目原有配置不被本用例污染。 */
        int32 SavedFishContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;

        /** 测试开始前的鱼目录数据修订；清空目录时置零以模拟未落盘内容包。 */
        int64 SavedFishDataRevision = 0;

        /** 测试开始前的鱼目录来源戳；清空目录时移除来源以触发 MissingSource。 */
        FCatDataCatalogSourceStamp SavedFishSourceStamp;

        /** 测试开始前的鱼定义清单；恢复时整体放回默认对象。 */
        TArray<TSoftObjectPtr<UCatFishDefinition>> SavedFishDefinitions;

        /** 被临时覆盖的全局 Equipment Catalog 默认对象；StartPlay 会把它和 Fish Catalog 作为一个内容包校验。 */
        UCatEquipmentSettings* EquipmentSettings = nullptr;

        /** 测试开始前的装备目录 SchemaVersion；恢复时保持项目原有配置不被本用例污染。 */
        int32 SavedEquipmentContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

        /** 测试开始前的装备目录数据修订；清空目录时置零以模拟未落盘内容包。 */
        int64 SavedEquipmentDataRevision = 0;

        /** 测试开始前的装备目录来源戳；清空目录时移除来源以触发 MissingSource。 */
        FCatDataCatalogSourceStamp SavedEquipmentSourceStamp;

        /** 测试开始前的装备定义清单；恢复时整体放回默认对象。 */
        TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedEquipmentDefinitions;

        /** 构造流程：保存两个默认目录，再把它们切成空来源、空修订、空定义，确保 GameMode 启动只能走 Data fail-closed 分支。 */
        FEmptyDataCatalogOverride()
        {
            FishSettings = GetMutableDefault<UCatFishCatalogSettings>();
            if (FishSettings)
            {
                SavedFishContentSchemaVersion = FishSettings->ContentSchemaVersion;
                SavedFishDataRevision = FishSettings->DataRevision;
                SavedFishSourceStamp = FishSettings->SourceStamp;
                SavedFishDefinitions = FishSettings->Definitions;
                FishSettings->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
                FishSettings->DataRevision = 0;
                FishSettings->SourceStamp = FCatDataCatalogSourceStamp();
                FishSettings->Definitions.Reset();
            }

            EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();
            if (EquipmentSettings)
            {
                SavedEquipmentContentSchemaVersion = EquipmentSettings->ContentSchemaVersion;
                SavedEquipmentDataRevision = EquipmentSettings->DataRevision;
                SavedEquipmentSourceStamp = EquipmentSettings->SourceStamp;
                SavedEquipmentDefinitions = EquipmentSettings->Definitions;
                EquipmentSettings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
                EquipmentSettings->DataRevision = 0;
                EquipmentSettings->SourceStamp = FCatDataCatalogSourceStamp();
                EquipmentSettings->Definitions.Reset();
            }
        }

        /** 析构流程：恢复 Fish/Equipment 默认目录字段，使后续自动化继续使用项目配置或各自测试夹具。 */
        ~FEmptyDataCatalogOverride()
        {
            if (FishSettings)
            {
                FishSettings->ContentSchemaVersion = SavedFishContentSchemaVersion;
                FishSettings->DataRevision = SavedFishDataRevision;
                FishSettings->SourceStamp = SavedFishSourceStamp;
                FishSettings->Definitions = SavedFishDefinitions;
            }
            if (EquipmentSettings)
            {
                EquipmentSettings->ContentSchemaVersion = SavedEquipmentContentSchemaVersion;
                EquipmentSettings->DataRevision = SavedEquipmentDataRevision;
                EquipmentSettings->SourceStamp = SavedEquipmentSourceStamp;
                EquipmentSettings->Definitions = SavedEquipmentDefinitions;
            }
        }
    };
    /** 一局白天参数覆盖守卫；用例期间显式打开 RunFlow 总开关并写入白天时长与固定额度目标，析构时整体恢复项目配置。 */
    struct FRunDayCycleSettingsOverride
    {
        /** 被临时覆盖的全局 Run Settings 默认对象；为空表示没取到 CDO，本守卫不改写也不恢复任何字段。 */
        UCatRunSettings* Settings = nullptr;

        /** 用例开始前的 RunFlow 总开关；析构时恢复，避免测试值泄漏给后续用例。 */
        bool bSavedEnableRunRuntime = false;

        /** 用例开始前的白天秒数；析构时恢复到项目配置原值。 */
        float SavedDayLengthSeconds = 0.0f;

        /** 用例开始前的固定额度目标；析构时恢复到项目配置原值。 */
        int32 SavedQuotaTarget = 0;

        /** 用例开始前的人数缩放策略；析构时恢复，避免把 FixedQuotaTarget 留给验证 fail-closed 的用例。 */
        ECatRunScalingPolicy SavedScalingPolicy = ECatRunScalingPolicy::Undecided;

        /** 打开 runtime gate 并写入本用例需要的白天时长与额度目标；时长由调用方给出，因为只有验证到点入夜的用例才需要计时器真的触发。 */
        FRunDayCycleSettingsOverride(const float DayLengthSeconds, const int32 QuotaTarget)
        {
            Settings = GetMutableDefault<UCatRunSettings>();
            if (!Settings)
            {
                return;
            }
            bSavedEnableRunRuntime = Settings->bEnableRunRuntime;
            SavedDayLengthSeconds = Settings->DayLengthSeconds;
            SavedQuotaTarget = Settings->QuotaTarget;
            SavedScalingPolicy = Settings->PlayerScalingPolicy;
            Settings->bEnableRunRuntime = true;
            Settings->DayLengthSeconds = DayLengthSeconds;
            Settings->QuotaTarget = QuotaTarget;
            Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
        }

        /** 恢复项目原始 Run 配置；用例中途 return 也不会把测试用的 runtime gate 留在默认对象上。 */
        ~FRunDayCycleSettingsOverride()
        {
            if (!Settings)
            {
                return;
            }
            Settings->bEnableRunRuntime = bSavedEnableRunRuntime;
            Settings->DayLengthSeconds = SavedDayLengthSeconds;
            Settings->QuotaTarget = SavedQuotaTarget;
            Settings->PlayerScalingPolicy = SavedScalingPolicy;
        }
    };

    /**
     * 偷鱼准入守卫；只覆盖 BeginTheft 真正读到的那几项 Social 配置，其余字段保持项目原值。
     * 需要它是因为验证 teardown 收口失败必须先有一条真实的社交 escrow，而 escrow 只能由 BeginTheft 建立。
     * 不直接依赖 DefaultGame.ini 的原因和 FRunDayCycleSettingsOverride 一样：偷鱼权限和距离都是策划会改的产品数值，
     * 本用例验的是 teardown 分支，它的成立性不能挂在会变的外部配置上。
     * 必须在 CreateTestWorld 之前构造：Social 服务在 Initialize 时就把权限抄进运行期策略，建好 World 再改已经来不及。
     */
    struct FTheftAdmissionSettingsOverride
    {
        /** 被临时覆盖的 Social 设置默认对象；为空表示取不到 CDO，本守卫不改也不恢复任何字段。 */
        UCatSocialSettings* Settings = nullptr;

        /** 用例开始前的 Social 总 gate；析构时恢复。 */
        bool bSavedSocialRuntime = false;

        /** 用例开始前的偷鱼权限默认值；它是 Social 运行期策略的开局来源，析构时恢复。 */
        ECatDomainPolicy SavedTheftPermission = ECatDomainPolicy::Unset;

        /** 用例开始前的赃鱼进食窗口秒数；偷鱼参数 gate 要求它为正，析构时恢复。 */
        double SavedEatingWindowSeconds = 0.0;

        /** 用例开始前的偷鱼交互距离；析构时恢复。 */
        double SavedTheftRangeCentimeters = 0.0;

        /** 用例开始前的追回交互距离；偷鱼参数 gate 要求它为正，析构时恢复。 */
        double SavedCatchRangeCentimeters = 0.0;

        /** 用例开始前的共用鱼缸追回策略；未裁时共用鱼缸整条偷取不可达，析构时恢复。 */
        ECatSharedTankRecoveryPolicy SavedSharedTankPolicy = ECatSharedTankRecoveryPolicy::Undecided;

        /** 构造流程：抄下六个默认值，再写成一套"偷鱼这条路能走通"的最小组合；距离取得很宽，用例不验距离。 */
        FTheftAdmissionSettingsOverride()
        {
            Settings = GetMutableDefault<UCatSocialSettings>();
            if (!Settings)
            {
                return;
            }
            bSavedSocialRuntime = Settings->bEnableSocialRuntime;
            SavedTheftPermission = Settings->TheftPermission;
            SavedEatingWindowSeconds = Settings->TheftEatingWindowSeconds;
            SavedTheftRangeCentimeters = Settings->TheftInteractionRangeCentimeters;
            SavedCatchRangeCentimeters = Settings->TheftCatchRangeCentimeters;
            SavedSharedTankPolicy = Settings->SharedTankRecoveryPolicy;
            Settings->bEnableSocialRuntime = true;
            Settings->TheftPermission = ECatDomainPolicy::Enabled;
            Settings->TheftEatingWindowSeconds = 30.0;
            Settings->TheftInteractionRangeCentimeters = 1000.0;
            Settings->TheftCatchRangeCentimeters = 1000.0;
            Settings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::OriginalOwner;
        }

        /** 析构流程：六个值原样写回 CDO；不调用 SaveConfig，测试改动不落到项目 ini。 */
        ~FTheftAdmissionSettingsOverride()
        {
            if (!Settings)
            {
                return;
            }
            Settings->bEnableSocialRuntime = bSavedSocialRuntime;
            Settings->TheftPermission = SavedTheftPermission;
            Settings->TheftEatingWindowSeconds = SavedEatingWindowSeconds;
            Settings->TheftInteractionRangeCentimeters = SavedTheftRangeCentimeters;
            Settings->TheftCatchRangeCentimeters = SavedCatchRangeCentimeters;
            Settings->SharedTankRecoveryPolicy = SavedSharedTankPolicy;
        }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeRunCommandFailClosedTest,
	"Catfishing.Unit.Framework.GameMode.RunCommandsFailClosedBeforeRuntimeStart",
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
	Command.QuotaCount = 1;
	Command.WorldProgressDelta = 3;
	const FCatRunCommandResult First = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("Run 未启动前额度写口不提交"), First.bCommitted);
	TestEqual(TEXT("Run 未启动前返回 CommandsClosed"), First.Error, ECatRunCommandError::CommandsClosed);
	TestEqual(TEXT("拒绝结果关联原 RequestId"), First.RequestId, Command.Context.RequestId);

	FCatQuotaContributionCommand QuotaDriftCommand = Command;
	QuotaDriftCommand.QuotaCount = 2;
	const FCatRunCommandResult QuotaDrift = GameMode->SubmitCommittedQuotaContributionFromCoordinator(QuotaDriftCommand);
	TestFalse(TEXT("同 RequestId 更换额度条数不提交"), QuotaDrift.bCommitted);
	TestEqual(TEXT("额度条数漂移返回 InvalidPayload"), QuotaDrift.Error, ECatRunCommandError::InvalidPayload);

	FCatQuotaContributionCommand WorldProgressDriftCommand = Command;
	WorldProgressDriftCommand.WorldProgressDelta = 4;
	const FCatRunCommandResult WorldProgressDrift =
		GameMode->SubmitCommittedQuotaContributionFromCoordinator(WorldProgressDriftCommand);
	TestFalse(TEXT("同 RequestId 更换世界进度增减不提交"), WorldProgressDrift.bCommitted);
	TestEqual(TEXT("世界进度载荷漂移返回 InvalidPayload"), WorldProgressDrift.Error, ECatRunCommandError::InvalidPayload);

	const FCatRunCommandResult Replay = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("同一请求重放不提交"), Replay.bCommitted);
	TestEqual(TEXT("同一请求重放返回 AlreadyResolved"), Replay.Error, ECatRunCommandError::AlreadyResolved);
	TestEqual(TEXT("重放保留首次 Revision"), Replay.Revision, First.Revision);
	TestEqual(TEXT("默认 Run 公开状态仍未开始"), GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::NotStarted);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeDataCatalogFailClosedStartupTest,
	"Catfishing.Unit.Framework.GameMode.DataCatalogInvalidBlocksRuntimeStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：临时清空 Fish+Equipment 默认目录后注册并启动真实 Lake GameMode；Data 包无效时 Run 必须停在 StartupFailed，额度写口也不能打开。
bool FCatGameModeDataCatalogFailClosedStartupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatfishingGameModeTest::FEmptyDataCatalogOverride EmptyCatalogs;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Data 启动 gate 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Data 启动 gate 测试 World"), World);
	if (!World)
	{
		return false;
	}

	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("Data 启动 gate 测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可取得项目 Lake authority GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}
	World->InitializeActorsForPlay(GameModeUrl);
	AddExpectedErrorPlain(TEXT("Event=data_catalog_invalid"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("Event=run_startup_failed"), EAutomationExpectedErrorFlags::Contains, 1);
	GameMode->StartPlay();

	const FCatRunPublicState& PublicState = GameMode->GetRunPublicState();
	TestEqual(TEXT("Data 无效时 Run 保持 NotStarted"), PublicState.Phase.Phase, ECatRunPhase::NotStarted);
	TestEqual(TEXT("Data 无效时终局原因是 StartupFailed"), PublicState.EndReason, ECatRunEndReason::StartupFailed);
	TestFalse(TEXT("Data 无效时不允许钓鱼"), PublicState.Phase.bFishingAllowed);
	TestFalse(TEXT("Data 无效时不打开额度写口"), PublicState.Phase.bQuotaOpen);

	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = PublicState.Revision;
	Command.Context.StableNetId = TEXT("DataCatalogGateStableId");
	Command.QuotaCount = 1;
	Command.WorldProgressDelta = 1;
	const FCatRunCommandResult QuotaResult = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("Data 启动失败后额度写口仍不提交"), QuotaResult.bCommitted);
	TestEqual(TEXT("Data 启动失败后额度写口返回 CommandsClosed"), QuotaResult.Error, ECatRunCommandError::CommandsClosed);
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeSuccessSettlementFinalDayGateTest,
	"Catfishing.Unit.Framework.GameMode.SuccessSettlementRequiresFinalDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用自动化专用入口建立最小 Run 聚合，模拟 StateTree 误连到成功结算夜；服务器必须先检查最终天数，只有第 10 天才接受成功终局。
bool FCatGameModeSuccessSettlementFinalDayGateTest::RunTest(const FString& Parameters)
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
	UCatRunSettings* Settings = GetMutableDefault<UCatRunSettings>();
	TestNotNull(TEXT("可读写 Run Settings 默认对象"), Settings);
	if (!GameMode || !Settings)
	{
		return false;
	}

	const ECatRunPolicyDecision SavedSuccessPolicy = Settings->SuccessSettlementPolicy;
	const int32 SavedFinalDayIndex = Settings->FinalDayIndex;
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Enabled;
	Settings->FinalDayIndex = 10;

	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::NormalNight, 9, 4);
	const FCatRunTransitionResult EarlyResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::SuccessSettlementNight, ECatRunTransitionReason::AllEligibleReady);
	TestFalse(TEXT("未到第 10 天时拒绝成功结算夜"), EarlyResult.bApplied);
	TestEqual(TEXT("未到最终天数返回策略未裁/未满足"), EarlyResult.Error, ECatRunCommandError::PolicyUndecided);
	TestEqual(TEXT("早期成功结算尝试不改写 Phase"),
		GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::NormalNight);

	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::NormalNight, 10, 8);
	const FCatRunTransitionResult FinalDayResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::SuccessSettlementNight, ECatRunTransitionReason::AllEligibleReady);
	TestTrue(TEXT("第 10 天允许进入成功结算夜"), FinalDayResult.bApplied);
	TestEqual(TEXT("成功结算夜写入公开 Phase"),
		GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::SuccessSettlementNight);
	TestEqual(TEXT("成功结算夜写入成功终局原因"),
		GameMode->GetRunPublicState().EndReason, ECatRunEndReason::Success);
	TestFalse(TEXT("成功结算夜不保留白天截止计时"), GameMode->GetRunPublicState().Phase.bHasDeadline);
	TestFalse(TEXT("成功结算夜不允许继续钓鱼"), GameMode->GetRunPublicState().Phase.bFishingAllowed);
	TestFalse(TEXT("成功结算夜不允许继续写额度"), GameMode->GetRunPublicState().Phase.bQuotaOpen);
	const FCatRunPublicState& FinalPublicState = GameMode->GetRunPublicState();
	TestEqual(TEXT("成功结算夜发布的环境快照对齐当前 Run Revision"),
		FinalPublicState.Environment.SourceRunRevision, FinalPublicState.Revision);
	TestEqual(TEXT("成功结算夜不保留白天环境时段"),
		FinalPublicState.Environment.TimeOfDay, ECatEnvironmentTimeOfDay::Unknown);
	TestFalse(TEXT("成功结算夜不保留自然事件"), FinalPublicState.Environment.bHasActiveEvent);

	Settings->SuccessSettlementPolicy = SavedSuccessPolicy;
	Settings->FinalDayIndex = SavedFinalDayIndex;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeExpiredReconnectAdmissionTest,
	"Catfishing.Unit.Framework.GameMode.ConnectionLostReconnectExpiresAfterSixtySeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：通过 World::SetGameMode 注册真实 authority Lake GameMode，并只初始化 Actor、不进入 BeginPlay；随后走真实
// PreLogin→PostLogin→Logout 准入链建立断线恢复记录，超过 60 秒后同 StableNetId 不能被当作普通新玩家放行。
bool FCatGameModeExpiredReconnectAdmissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 GameMode 重连测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建重连测试 World"), World);
	if (!World)
	{
		return false;
	}

	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("重连测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可取得项目 Lake authority GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	// 只走 Actor 初始化来生成 GameSession，不调用 BeginPlay；这样保留 PreLogin 的真实引擎 gate，又不启动本用例无关的 Run StateTree。
	World->InitializeActorsForPlay(GameModeUrl);
	TestNotNull(TEXT("重连测试 GameSession 由 GameMode 初始化创建"), GameMode->GameSession.Get());
	if (!GameMode->GameSession)
	{
		return false;
	}

	const FString StableNetId(TEXT("player:work02d-expired-reconnect"));
	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString FirstPreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, FirstPreLoginError);
	TestTrue(TEXT("首次同身份 PreLogin 建立 Reserved 记录"), FirstPreLoginError.IsEmpty());

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("重连测试 Controller 可创建"), Controller);
	TestNotNull(TEXT("重连测试 PlayerState 可创建"), PlayerState);
	if (!Controller || !PlayerState || !FirstPreLoginError.IsEmpty())
	{
		return false;
	}

	// 父类 PostLogin 会读取 Player->CurrentNetSpeed；测试里手工生成 Controller 时补一个最小 UPlayer，避免伪造整条网络连接。
	UPlayer* TestPlayer = NewObject<UPlayer>(Controller);
	TestNotNull(TEXT("重连测试 Controller 可绑定最小 Player"), TestPlayer);
	if (!TestPlayer)
	{
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);

	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);
	GameMode->Logout(Controller);

	// 未进入 BeginPlay 的 TestWorld 不保证 Tick 推进游戏时间；生产过期 gate 读取 TimeSeconds，测试直接推进同一权威时钟。
	const double TimeBeforeExpiry = World->GetTimeSeconds();
	World->TimeSeconds = TimeBeforeExpiry + 61.0;
	TestTrue(TEXT("重连测试服务器时间已超过 60 秒 TTL"), World->GetTimeSeconds() > TimeBeforeExpiry + 60.0);
	FString ExpiredPreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, ExpiredPreLoginError);
	TestEqual(TEXT("超过 60 秒的恢复记录拒绝同身份重新准入"), ExpiredPreLoginError,
		FString(TEXT("CAT_POLICY_UNDECIDED:ExpiredReconnectAdmission")));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeNightSacrificeCountersTest,
	"Catfishing.Unit.Framework.GameMode.SacrificeIsNightOnlyAndCountsFishNotProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用自动化入口种入最小 Run，再走真实阶段入口进入白天和普通夜。白天必须整体拒绝献祭；夜晚每条鱼只加 1 点额
// 度，供奉进度单独累计并按下限 0 截断；达标不提前结束夜晚，翻天只清额度不清世界进度。
bool FCatGameModeNightSacrificeCountersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 白天时长取 600 秒，确保本用例期间截止计时器不会触发，断言只覆盖献祭窗口与两个计数器。
	CatfishingGameModeTest::FRunDayCycleSettingsOverride RunSettingsOverride(600.0f, 2);
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建献祭计数测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建献祭计数测试 World"), World);
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

	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::NotStarted, 0, 1);
	const FCatRunTransitionResult DayResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::DayActive, ECatRunTransitionReason::AllEligibleReady);
	TestTrue(TEXT("可进入白天"), DayResult.bApplied);
	TestFalse(TEXT("白天不开献祭窗口"), GameMode->GetRunPublicState().Phase.bQuotaOpen);
	TestTrue(TEXT("白天允许钓鱼"), GameMode->GetRunPublicState().Phase.bFishingAllowed);
	TestTrue(TEXT("白天持有唯一截止点"), GameMode->GetRunPublicState().Phase.bHasDeadline);

	FCatQuotaContributionCommand DayCommand;
	DayCommand.Context.RequestId = FGuid::NewGuid();
	DayCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	DayCommand.Context.StableNetId = TEXT("player:work02-night-sacrifice");
	DayCommand.QuotaCount = 1;
	DayCommand.WorldProgressDelta = 3;
	TestEqual(TEXT("白天献祭预检返回 InvalidPhase"),
		GameMode->ValidateCommittedQuotaContributionFromCoordinator(DayCommand).Error, ECatRunCommandError::InvalidPhase);
	TestEqual(TEXT("白天献祭提交返回 InvalidPhase"),
		GameMode->SubmitCommittedQuotaContributionFromCoordinator(DayCommand).Error, ECatRunCommandError::InvalidPhase);
	TestEqual(TEXT("白天献祭不写入额度"), GameMode->GetRunPublicState().QuotaProgress, 0);
	TestEqual(TEXT("白天献祭不写入世界进度"), GameMode->GetRunPublicState().WorldProgress, 0);

	const FCatRunTransitionResult NightResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::NormalNight, ECatRunTransitionReason::DayElapsed);
	TestTrue(TEXT("可进入普通夜"), NightResult.bApplied);
	TestTrue(TEXT("普通夜开启献祭窗口"), GameMode->GetRunPublicState().Phase.bQuotaOpen);
	TestFalse(TEXT("普通夜没有截止点"), GameMode->GetRunPublicState().Phase.bHasDeadline);
	TestFalse(TEXT("普通夜不允许钓鱼"), GameMode->GetRunPublicState().Phase.bFishingAllowed);

	// 臭臭鱼：额度照记一条，世界进度已经在 0 时不能被压成负数。
	FCatQuotaContributionCommand StinkyCommand = DayCommand;
	StinkyCommand.Context.RequestId = FGuid::NewGuid();
	StinkyCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	StinkyCommand.WorldProgressDelta = -1;
	TestTrue(TEXT("夜晚献臭臭鱼被受理"),
		GameMode->SubmitCommittedQuotaContributionFromCoordinator(StinkyCommand).bCommitted);
	TestEqual(TEXT("臭臭鱼照样计一条额度"), GameMode->GetRunPublicState().QuotaProgress, 1);
	TestEqual(TEXT("世界进度在下限 0 保持不变"), GameMode->GetRunPublicState().WorldProgress, 0);

	FCatQuotaContributionCommand OrdinaryCommand = DayCommand;
	OrdinaryCommand.Context.RequestId = FGuid::NewGuid();
	OrdinaryCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	OrdinaryCommand.WorldProgressDelta = 3;
	TestTrue(TEXT("夜晚献常规鱼被受理"),
		GameMode->SubmitCommittedQuotaContributionFromCoordinator(OrdinaryCommand).bCommitted);
	TestEqual(TEXT("常规鱼只加一条额度"), GameMode->GetRunPublicState().QuotaProgress, 2);
	TestEqual(TEXT("常规鱼按供奉进度累计世界进度"), GameMode->GetRunPublicState().WorldProgress, 3);

	// 巨影供奉进度 10，但它对当日额度仍然只算一条鱼。
	FCatQuotaContributionCommand GiantCommand = DayCommand;
	GiantCommand.Context.RequestId = FGuid::NewGuid();
	GiantCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	GiantCommand.WorldProgressDelta = 10;
	TestTrue(TEXT("夜晚献巨影被受理"),
		GameMode->SubmitCommittedQuotaContributionFromCoordinator(GiantCommand).bCommitted);
	TestEqual(TEXT("巨影只加一条额度"), GameMode->GetRunPublicState().QuotaProgress, 3);
	TestEqual(TEXT("巨影按供奉进度累计世界进度"), GameMode->GetRunPublicState().WorldProgress, 13);

	TestEqual(TEXT("额度达标后仍停在普通夜"),
		GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::NormalNight);
	TestTrue(TEXT("额度达标不提前关闭献祭窗口"), GameMode->GetRunPublicState().Phase.bQuotaOpen);
	TestTrue(TEXT("额度达标不产生新的转移原因"),
		GameMode->DoesLastRunFlowResultMatch(ECatRunTransitionReason::DayElapsed));

	const FCatRunCommandResult Replay = GameMode->SubmitCommittedQuotaContributionFromCoordinator(GiantCommand);
	TestFalse(TEXT("同 RequestId 重放不再提交"), Replay.bCommitted);
	TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved"), Replay.Error, ECatRunCommandError::AlreadyResolved);
	TestEqual(TEXT("重放不重复累加额度"), GameMode->GetRunPublicState().QuotaProgress, 3);
	TestEqual(TEXT("重放不重复累加世界进度"), GameMode->GetRunPublicState().WorldProgress, 13);

	FCatQuotaContributionCommand DriftCommand = GiantCommand;
	DriftCommand.WorldProgressDelta = 11;
	const FCatRunCommandResult Drift = GameMode->SubmitCommittedQuotaContributionFromCoordinator(DriftCommand);
	TestFalse(TEXT("同 RequestId 更换世界进度增减不提交"), Drift.bCommitted);
	TestEqual(TEXT("世界进度载荷漂移返回 InvalidPayload"), Drift.Error, ECatRunCommandError::InvalidPayload);
	TestEqual(TEXT("载荷漂移不改写世界进度"), GameMode->GetRunPublicState().WorldProgress, 13);

	const FCatRunTransitionResult NextDayResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::DayActive, ECatRunTransitionReason::AllEligibleReady);
	TestTrue(TEXT("可翻到下一天"), NextDayResult.bApplied);
	TestEqual(TEXT("翻天推进天序号"), GameMode->GetRunPublicState().Phase.DayIndex, 2);
	TestEqual(TEXT("翻天清空当日额度"), GameMode->GetRunPublicState().QuotaProgress, 0);
	TestEqual(TEXT("翻天保留跨天世界进度"), GameMode->GetRunPublicState().WorldProgress, 13);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeDayFlipSettlesQuotaTest,
	"Catfishing.Unit.Framework.GameMode.DayDeadlineEntersNightAndFlipConfirmSettlesQuota",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：走真实准入链建立一个在场玩家，再让白天截止计时器真的到点。到点只能给出 DayElapsed 而不能判额度；随后在夜
// 里确认翻天，未交齐时选 QuotaFailed，补交到达标后重新确认才选 AllEligibleReady。
bool FCatGameModeDayFlipSettlesQuotaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 白天只有 1 秒，本用例直接推进 TimerManager 让唯一截止回调真实触发。
	CatfishingGameModeTest::FRunDayCycleSettingsOverride RunSettingsOverride(1.0f, 2);
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建翻天结算测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建翻天结算测试 World"), World);
	if (!World)
	{
		return false;
	}

	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("翻天结算测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可取得项目 Lake authority GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	// 只初始化 Actor 而不调用 BeginPlay：保留真实 PreLogin/PostLogin 准入链，同时不启动与本用例无关的 Run StateTree 启动流程。
	World->InitializeActorsForPlay(GameModeUrl);
	const FString StableNetId(TEXT("player:work02-day-flip"));
	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString PreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
	TestTrue(TEXT("翻天结算测试 PreLogin 建立 Reserved 记录"), PreLoginError.IsEmpty());

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("翻天结算测试 Controller 可创建"), Controller);
	TestNotNull(TEXT("翻天结算测试 PlayerState 可创建"), PlayerState);
	if (!Controller || !PlayerState || !PreLoginError.IsEmpty())
	{
		return false;
	}

	// 父类 PostLogin 会读取 Player->CurrentNetSpeed；测试里手工生成 Controller 时补一个最小 UPlayer，避免伪造整条网络连接。
	UPlayer* TestPlayer = NewObject<UPlayer>(Controller);
	TestNotNull(TEXT("翻天结算测试 Controller 可绑定最小 Player"), TestPlayer);
	if (!TestPlayer)
	{
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);
	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);

	// 白天与普通夜每次发布都会调用 Environment provider；项目 ini 里的环境配置在自动化进程里同样生效，provider 全程应当
	// 求值成功——包括"白天截止已到、正要入夜"的那次发布（provider 把它按入夜边界处理，不再报 TimeOfDayUnavailable）。
	// 这里故意不放行任何 environment_evaluation_failed：它一旦出现就是回归，测试应当红。
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::NotStarted, 0, 1);
	const FCatRunTransitionResult DayResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::DayActive, ECatRunTransitionReason::AllEligibleReady);
	TestTrue(TEXT("可进入白天"), DayResult.bApplied);
	TestTrue(TEXT("白天持有唯一截止点"), GameMode->GetRunPublicState().Phase.bHasDeadline);

	// FTimerManager 对"本帧还没 Tick 过就设定的计时器"先放进 Pending，直到某次 Tick 结束才把它转成 Active 并按当时的
	// InternalTime 重算到期时刻；同一帧内第二次 Tick 又会被 HasBeenTickedThisFrame 直接挡回。所以推进一次是叫不醒截止回调的：
	// 第一拍只负责激活计时器，中间必须递增 GFrameCounter 骗过同帧保护，第二拍才真正跨过整段白天。
	World->GetTimerManager().Tick(0.0f);
	++GFrameCounter;
	World->GetTimerManager().Tick(2.0f);
	TestFalse(TEXT("白天到点后不再持有截止点"), GameMode->GetRunPublicState().Phase.bHasDeadline);
	TestFalse(TEXT("白天到点后不再允许钓鱼"), GameMode->GetRunPublicState().Phase.bFishingAllowed);
	TestEqual(TEXT("白天到点时额度仍未交齐"), GameMode->GetRunPublicState().QuotaProgress, 0);
	TestEqual(TEXT("白天到点只给出到点入夜原因"),
		GameMode->GetLastRunFlowResultForAutomation().Reason, ECatRunTransitionReason::DayElapsed);
	TestEqual(TEXT("白天到点不写入终局原因"),
		GameMode->GetRunPublicState().EndReason, ECatRunEndReason::None);

	const FCatRunTransitionResult NightResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::NormalNight, ECatRunTransitionReason::DayElapsed);
	TestTrue(TEXT("可进入普通夜"), NightResult.bApplied);
	TestTrue(TEXT("普通夜开启献祭窗口"), GameMode->GetRunPublicState().Phase.bQuotaOpen);

	FCatNextDayReadyCommand ReadyCommand;
	ReadyCommand.Context.RequestId = FGuid::NewGuid();
	ReadyCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	ReadyCommand.bReady = true;
	TestTrue(TEXT("未交齐时也可以确认翻天"),
		GameMode->SubmitNextDayReady(Controller, ReadyCommand).bCommitted);
	TestEqual(TEXT("未交齐时翻天确认裁定为额度失败"),
		GameMode->GetLastRunFlowResultForAutomation().Reason, ECatRunTransitionReason::QuotaFailed);
	TestTrue(TEXT("StateTree 不可用时献祭窗口保持开着"), GameMode->GetRunPublicState().Phase.bQuotaOpen);

	FCatQuotaContributionCommand SacrificeCommand;
	SacrificeCommand.Context.StableNetId = StableNetId;
	SacrificeCommand.QuotaCount = 1;
	SacrificeCommand.WorldProgressDelta = 1;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		SacrificeCommand.Context.RequestId = FGuid::NewGuid();
		SacrificeCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
		TestTrue(TEXT("夜里补交献祭被受理"),
			GameMode->SubmitCommittedQuotaContributionFromCoordinator(SacrificeCommand).bCommitted);
	}
	TestEqual(TEXT("补交后当日额度达标"), GameMode->GetRunPublicState().QuotaProgress, 2);

	FCatNextDayReadyCommand SecondReadyCommand;
	SecondReadyCommand.Context.RequestId = FGuid::NewGuid();
	SecondReadyCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	SecondReadyCommand.bReady = true;
	TestTrue(TEXT("补交后可以重新确认翻天"),
		GameMode->SubmitNextDayReady(Controller, SecondReadyCommand).bCommitted);
	TestEqual(TEXT("补交达标后翻天确认裁定为进入下一天"),
		GameMode->GetLastRunFlowResultForAutomation().Reason, ECatRunTransitionReason::AllEligibleReady);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeFailedTeardownClearsDayDeadlineTest,
	"Catfishing.Unit.Framework.GameMode.FailedRunTeardownClearsDayDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：守的是收口失败之后这一局必须真的停下来，而不是"门关了、计时器还在跑"。
// RequestRunTeardown 里那两个收口调用一进函数就把各自的写口永久关掉，且没有任何重开入口；
// 所以收口一旦失败，准入门被关是对的，但白天截止计时器如果还活着，它到点就会推着流程再进一次相位，
// 而相位进入会把 bRunCommandsOpen 重新置回 true——于是又变成"门开着、房间全锁了"，
// 玩家点什么都能穿过门再撞上 CommandsClosed，其中取用团队装备那条会先把装备装到猫身上才发现取不走。
// 本用例锁的就是失败早退分支必须清掉白天截止这一步。
// 收口失败的现场是这样造出来的：先让一条真实的偷鱼 escrow 成立，再抢在 teardown 之前单独调用 Items 的关门写口。
// Items 关门时会把 escrow 里的鱼原位退回并把 escrow 记录删掉，但它不通知 Social；
// Social 那边的协议还挂着，teardown 走到 CloseRunDomainCommands 时就再也退不回去，收口整体失败。
// 这是 CatSocialService 注释里"调用方不得先关闭 Items"那条禁令被违反后的样子，也是自动化环境里
// 唯一能用公开写口做出"还不回去的 escrow"的办法——ReturnStolenFish 另外两条失败分支（源容器消失、售卖准备态）
// 都被生产代码自己堵死了，构造不出来。
//
// 说明一下这条用例覆盖不到的另外半个修复：失败分支除了清截止，还会 StopLogic 停掉 StateTree。
// 这里用的是 SeedRunPhaseEntryForAutomation，它会永久打开 bAutomationRunPhaseEntryBypass，
// 之后的相位进入不再看 StateTree 是否还在跑，所以"停了 StateTree 之后就再也进不了相位"这件事
// 在本层观测不到——停与不停，再进一次相位都会成功。那半个修复要靠带真实 ST_RunFlow 资产的功能测试来守。
bool FCatGameModeFailedTeardownClearsDayDeadlineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 收口失败会打一行 Error 日志；不先声明预期，自动化框架会把这行日志本身算成一次测试失败。
	AddExpectedErrorPlain(TEXT("Event=run_teardown_failed"), EAutomationExpectedErrorFlags::Contains, 1);
	// 白天取 600 秒，保证用例期间截止计时器不会自己到点；断言只覆盖收口失败有没有把截止清掉。
	CatfishingGameModeTest::FRunDayCycleSettingsOverride RunSettingsOverride(600.0f, 2);
	CatfishingGameModeTest::FTheftAdmissionSettingsOverride TheftSettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建收口失败测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建收口失败测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatfishingGameModeBase* GameMode = World->SpawnActor<ACatfishingGameModeBase>();
	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("可生成项目 Lake GameMode"), GameMode);
	TestNotNull(TEXT("可取得 Items 服务"), Items);
	TestNotNull(TEXT("可取得 Social 服务"), Social);
	if (!GameMode || !Items || !Social)
	{
		return false;
	}

	// ---- 进白天：白天是唯一会建立截止计时的相位，收口失败要清掉的就是它 ----
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::NotStarted, 0, 1);
	TestTrue(TEXT("可进入白天"), GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::DayActive, ECatRunTransitionReason::AllEligibleReady).bApplied);
	TestTrue(TEXT("白天持有唯一截止点"), GameMode->GetRunPublicState().Phase.bHasDeadline);
	TestTrue(TEXT("白天已经写出具体截止时间点"),
		GameMode->GetRunPublicState().Phase.DeadlineServerTimeSeconds > 0.0);

	// ---- 造一名真实小偷 ----
	APlayerController* ThiefController = World->SpawnActor<APlayerController>();
	ACatfishingPlayerState* ThiefState = World->SpawnActor<ACatfishingPlayerState>();
	ACatCharacter* ThiefCharacter = World->SpawnActor<ACatCharacter>();
	if (!ThiefController || !ThiefState || !ThiefCharacter)
	{
		TestTrue(TEXT("收口失败测试的小偷可完整生成"), false);
		return false;
	}
	const FUniqueNetIdRef ThiefUniqueId = FUniqueNetIdString::Create(
		TEXT("player:teardown-stuck-escrow-thief"), UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	ThiefState->SetUniqueId(FUniqueNetIdRepl(ThiefUniqueId));
	ThiefController->SetPlayerState(ThiefState);
	World->AddController(ThiefController);
	// 小偷站在离两个容器宿主 100 厘米的地方；上面的守卫把交互距离放到 1000，距离不是本用例的验证目标。
	ThiefCharacter->SetActorLocation(FVector(100.0, 0.0, 0.0));
	ThiefController->Possess(ThiefCharacter);

	// ---- 造一条真实的鱼，并把它放进共用鱼缸 ----
	// 走个人鱼护再转存的两步，是因为 CommitCapture 只收自己名下的个人鱼护；共用鱼缸里的鱼只能靠转存进来。
	// 用共用鱼缸而不是个人鱼护当偷取目标，是因为个人鱼护还要求原主人在场可交互，那样得再造第二名玩家。
	const FString FisherStableNetId(TEXT("player:teardown-stuck-escrow-fisher"));
	AActor* GuardHost = World->SpawnActor<AActor>();
	AActor* TankHost = World->SpawnActor<AActor>();
	UCatContainerReplicationComponent* GuardComponent = GuardHost
		? NewObject<UCatContainerReplicationComponent>(GuardHost) : nullptr;
	UCatContainerReplicationComponent* TankComponent = TankHost
		? NewObject<UCatContainerReplicationComponent>(TankHost) : nullptr;
	if (!GuardHost || !TankHost || !GuardComponent || !TankComponent)
	{
		TestTrue(TEXT("收口失败测试的两个容器宿主可完整生成"), false);
		return false;
	}
	GuardHost->AddInstanceComponent(GuardComponent);
	GuardComponent->RegisterComponent();
	TankHost->AddInstanceComponent(TankComponent);
	TankComponent->RegisterComponent();
	const FGuid GuardContainerId = FGuid::NewGuid();
	const FGuid TankContainerId = FGuid::NewGuid();
	Items->RegisterContainer(GuardComponent, GuardContainerId, ECatContainerKind::PersonalGuard, FisherStableNetId, 4);
	Items->RegisterContainer(TankComponent, TankContainerId, ECatContainerKind::SharedFishTank, FString(), 4);

	const auto ReadRevision = [Items](const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		Items->TryGetContainerSnapshot(ContainerId, Snapshot);
		return Snapshot.Revision;
	};
	const auto ContainsFish = [Items](const FGuid ContainerId, const FGuid FishInstanceId)
	{
		FCatContainerSnapshot Snapshot;
		Items->TryGetContainerSnapshot(ContainerId, Snapshot);
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	};

	const FGuid StolenFishId = FGuid::NewGuid();
	FCatCaptureCommitCommand CaptureCommand;
	CaptureCommand.Context.RequestId = FGuid::NewGuid();
	CaptureCommand.Context.ExpectedRevision = ReadRevision(GuardContainerId);
	CaptureCommand.Context.StableNetId = FisherStableNetId;
	CaptureCommand.FishingSessionId = FGuid::NewGuid();
	CaptureCommand.FishInstanceId = StolenFishId;
	CaptureCommand.FishDefinitionId = TEXT("TestFish");
	CaptureCommand.TargetContainerId = GuardContainerId;
	CaptureCommand.WeightKilograms = 2.5;
	CaptureCommand.SacrificeContribution = 3;
	TestTrue(TEXT("可往个人鱼护种一条真实的鱼"), Items->CommitCapture(CaptureCommand).Command.bCommitted);

	FCatFishTransferCommand TransferCommand;
	TransferCommand.Context.RequestId = FGuid::NewGuid();
	TransferCommand.Context.ExpectedRevision = ReadRevision(GuardContainerId);
	TransferCommand.Context.StableNetId = FisherStableNetId;
	TransferCommand.FishInstanceId = StolenFishId;
	TransferCommand.SourceContainerId = GuardContainerId;
	TransferCommand.TargetContainerId = TankContainerId;
	TransferCommand.ExpectedTargetRevision = ReadRevision(TankContainerId);
	TestTrue(TEXT("原钓手把鱼存进共用鱼缸"), Items->TransferOwnedFish(TransferCommand).bCommitted);

	// ---- 偷走它：这一步才产生 teardown 必须收口的那条 escrow ----
	FCatTheftCommand TheftCommand;
	TheftCommand.Context.RequestId = FGuid::NewGuid();
	TheftCommand.Context.ExpectedRevision = ReadRevision(TankContainerId);
	TheftCommand.FishInstanceId = StolenFishId;
	TheftCommand.SourceContainerId = TankContainerId;
	const FCatTheftResult Theft = Social->BeginTheft(ThiefController, TheftCommand);
	TestTrue(TEXT("偷鱼成立，escrow 已经建立"), Theft.Command.bCommitted);
	TestFalse(TEXT("鱼进了 escrow，已经不在共用鱼缸里"), ContainsFish(TankContainerId, StolenFishId));
	if (!Theft.Command.bCommitted)
	{
		return false;
	}

	// ---- 抢在 teardown 之前单独关掉 Items：escrow 被它自己退掉并删除，Social 却毫不知情 ----
	Items->CloseCommandsAndCancelReservations();
	TestTrue(TEXT("Items 单独关门时把赃鱼退回了共用鱼缸"), ContainsFish(TankContainerId, StolenFishId));

	// ---- 收口：Social 手里那条协议已经没有对应 escrow 可退，整条收口必须失败 ----
	FCatRunTeardownRequest TeardownRequest;
	TeardownRequest.RequestId = FGuid::NewGuid();
	TeardownRequest.OperationEpoch = 1;
	const FCatRunTeardownResult Teardown = GameMode->RequestRunTeardown(TeardownRequest);
	TestEqual(TEXT("有还不回去的社交协议时收口失败"), Teardown.Status, ECatRunTeardownStatus::Failed);
	TestEqual(TEXT("收口失败返回 TeardownFailed"), Teardown.Error, ECatRunCommandError::TeardownFailed);

	// 这两条是本用例真正守的东西：领域写口已经永久关掉了，白天截止绝不能留着继续推流程。
	TestFalse(TEXT("收口失败后不再持有白天截止"), GameMode->GetRunPublicState().Phase.bHasDeadline);
	TestEqual(TEXT("收口失败后截止时间点已经清零"),
		GameMode->GetRunPublicState().Phase.DeadlineServerTimeSeconds, 0.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
