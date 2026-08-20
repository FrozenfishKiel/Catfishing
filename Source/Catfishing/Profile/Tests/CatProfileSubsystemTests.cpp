#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Profile/CatProfileSettings.h"
#include "Profile/CatProfileSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Profile/CatProfileSaveGame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemCapturePlanGateTest,
	"Catfishing.Unit.Profile.Subsystem.CapturePlanBroadcastRequiresExplicitBridgeAndCompletePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemGrantJournalDurableReplayTest,
	"Catfishing.Unit.Profile.Subsystem.GrantJournalPersistsReplaysAndRejectsPayloadDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemImprintHiddenDurableTest,
	"Catfishing.Unit.Profile.Subsystem.ImprintHiddenPersistsLocally",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemFishTrackSeparationTest,
	"Catfishing.Unit.Profile.Subsystem.ScoopedGrantOnlyAdvancesScoopTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemAlbumReadTest,
	"Catfishing.Unit.Profile.Subsystem.AlbumReadGroupsByRunFiltersHiddenAndSurvivesReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemAlbumCapacityTest,
	"Catfishing.Unit.Profile.Subsystem.AlbumCapacityRejectsNewImprintsAndFailsClosedWhenUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatProfileSubsystemTest
{
	/** 临时覆盖 Profile 默认配置的守卫；ReceiveCapturePlan 只读取 GetDefault，因此测试必须恢复默认对象。 */
	struct FProfileSettingsOverride
	{
		/** 被临时改写的默认配置对象。 */
		UCatProfileSettings* Settings = GetMutableDefault<UCatProfileSettings>();

		/** 原始持久化 gate。 */
		bool bOldPersistence = false;

		/** 原始槽位名。 */
		FString OldSlotName;

		/** 原始成像桥 gate。 */
		bool bOldBridge = false;

		/** 原始跨局相册容量上限；印记写入路径会读它，因此必须与其他 gate 一起保存恢复。 */
		int32 OldMaxLocalAlbumImprints = 0;

		// 保存流程：记录旧值后写入一套可让外部成像桥和本地持久化成立的显式配置；调用方可传入唯一槽位名，避免自动化之间共享 SaveGame。
		// 相册容量默认取一个明显大于各用例写入条数的值，让不关心容量的用例不受 fail-closed 上限影响；专测容量的用例自己改小它。
		explicit FProfileSettingsOverride(const FString& InSlotBaseName = TEXT("AutomationProfile"),
			const int32 InMaxLocalAlbumImprints = 64)
		{
			if (Settings)
			{
				bOldPersistence = Settings->bEnableProfilePersistence;
				OldSlotName = Settings->SaveSlotBaseName;
				bOldBridge = Settings->bEnableExternalImprintCaptureBridge;
				OldMaxLocalAlbumImprints = Settings->MaxLocalAlbumImprints;
				Settings->bEnableProfilePersistence = true;
				Settings->SaveSlotBaseName = InSlotBaseName;
				Settings->bEnableExternalImprintCaptureBridge = true;
				Settings->MaxLocalAlbumImprints = InMaxLocalAlbumImprints;
			}
		}

		// 恢复流程：只还原内存默认对象，不写配置文件。
		~FProfileSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableProfilePersistence = bOldPersistence;
				Settings->SaveSlotBaseName = OldSlotName;
				Settings->bEnableExternalImprintCaptureBridge = bOldBridge;
				Settings->MaxLocalAlbumImprints = OldMaxLocalAlbumImprints;
			}
		}
	};

	/** 临时关闭 Profile 默认配置的测试守卫；CapturePlan 入口读取 GetDefault，因此默认失败分支必须隔离项目 Work7 默认值。 */
	struct FProfileSettingsClosedOverride
	{
		/** 被 ReceiveCapturePlan 读取的默认设置对象；只在本测试生命周期内改写。 */
		UCatProfileSettings* Settings = GetMutableDefault<UCatProfileSettings>();

		/** 原始持久化 gate；析构时恢复。 */
		bool bOldPersistence = false;

		/** 原始槽位名；析构时恢复。 */
		FString OldSlotName;

		/** 原始外部成像桥 gate；析构时恢复。 */
		bool bOldBridge = false;

		/** 构造流程：保存项目默认值后写入显式关闭配置，让广播 gate 用例只观察关闭语义。 */
		FProfileSettingsClosedOverride()
		{
			if (Settings)
			{
				bOldPersistence = Settings->bEnableProfilePersistence;
				OldSlotName = Settings->SaveSlotBaseName;
				bOldBridge = Settings->bEnableExternalImprintCaptureBridge;
				Settings->bEnableProfilePersistence = false;
				Settings->SaveSlotBaseName.Reset();
				Settings->bEnableExternalImprintCaptureBridge = false;
			}
		}

		/** 析构流程：恢复项目默认 Profile 配置，避免影响持久化与项目默认值测试。 */
		~FProfileSettingsClosedOverride()
		{
			if (Settings)
			{
				Settings->bEnableProfilePersistence = bOldPersistence;
				Settings->SaveSlotBaseName = OldSlotName;
				Settings->bEnableExternalImprintCaptureBridge = bOldBridge;
			}
		}
	};
	// 计划构造流程：构造一份字段完整的 CapturePlan；图片生成和文件落盘不在 Profile 子系统内伪造。
	static FCatCapturePlan MakeValidPlan()
	{
		FCatCapturePlan Plan;
		Plan.CapturePlanId = FGuid::NewGuid();
		Plan.CandidateId = FGuid::NewGuid();
		Plan.RunId = FGuid::NewGuid();
		Plan.RunAlbumId = FGuid::NewGuid();
		Plan.EventType = TEXT("CaptureMoment");
		Plan.SubjectId = FGuid::NewGuid();
		return Plan;
	}

	// Grant 构造流程：只填 Profile 持久化真正会保存和比较的鱼图鉴字段；收件人路由字段刻意留空，避免把服务器私有路由误写进本地幂等事实。
	static FCatProfileGrant MakeRecordedFishGrant(const FGuid GrantId, const double WeightKilograms)
	{
		FCatProfileGrant Grant;
		Grant.GrantId = GrantId;
		Grant.Kind = ECatProfileGrantKind::FishRecorded;
		Grant.FishDefinitionId = TEXT("Automation_Carp");
		Grant.WeightKilograms = WeightKilograms;
		Grant.CaptureCondition.RegionId = TEXT("Automation_Lake");
		Grant.CaptureCondition.TimeOfDayId = TEXT("Automation_Day");
		Grant.CaptureCondition.WeatherId = TEXT("Automation_Clear");
		return Grant;
	}

	// 抄获轨 Grant 构造流程：只填「谁抄到了哪个鱼种」这一个事实。刻意不填重量和捕获条件，
	// 因为抄获轨在合同里就不承载这些字段，填了反而会掩盖「抄获不该推进个人最佳」这条断言。
	static FCatProfileGrant MakeScoopedFishGrant(const FGuid GrantId)
	{
		FCatProfileGrant Grant;
		Grant.GrantId = GrantId;
		Grant.Kind = ECatProfileGrantKind::FishScooped;
		Grant.FishDefinitionId = TEXT("Automation_Carp");
		return Grant;
	}

	// 印记 Grant 构造流程：只填本地相册索引会保存的稳定键；图片文件、编码格式和外部成像桥结果不由 Profile 测试伪造。
	// bCover 单独开放是因为一局相册里只有篝火合影那一张是封面，普通印记必须能构造成非封面，否则测不出封面读取入口。
	static FCatProfileGrant MakeImprintGrant(const FGuid GrantId, const FGuid ImprintId, const FGuid RunAlbumId,
		const bool bCover = true)
	{
		FCatProfileGrant Grant;
		Grant.GrantId = GrantId;
		Grant.Kind = ECatProfileGrantKind::Imprint;
		Grant.ImprintId = ImprintId;
		Grant.RunAlbumId = RunAlbumId;
		Grant.bRunAlbumCover = bCover;
		return Grant;
	}
}

// 测试流程：直接通过 Profile 子系统公开入口接收 CapturePlan；缺配置、缺真实外部桥订阅或缺稳定字段时不得广播，桥、订阅
// 者和计划都完整时只广播一次原计划。
bool FCatProfileSubsystemCapturePlanGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("Profile 子系统测试存在引擎 Outer"), GEngine);
	ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
	TestNotNull(TEXT("Profile 子系统测试 LocalPlayer 可用合法 Engine Outer 创建"), LocalPlayer);
	UCatProfileSubsystem* Profile = LocalPlayer ? NewObject<UCatProfileSubsystem>(LocalPlayer) : nullptr;
	TestNotNull(TEXT("可创建 Profile 子系统对象"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FCatCapturePlan ValidPlan = CatProfileSubsystemTest::MakeValidPlan();
	{
		CatProfileSubsystemTest::FProfileSettingsClosedOverride ClosedOverride;
		TestFalse(TEXT("默认配置且没有外部桥订阅时 CapturePlan 不广播"), Profile->ReceiveCapturePlan(ValidPlan));
	}
	{
		CatProfileSubsystemTest::FProfileSettingsOverride Override;
		TestFalse(TEXT("配置开启但没有外部桥订阅时 CapturePlan 仍不被接管"), Profile->ReceiveCapturePlan(ValidPlan));
	}

	int32 BroadcastCount = 0;
	FCatCapturePlan LastPlan;
	Profile->OnCapturePlanReceived.AddLambda([&BroadcastCount, &LastPlan](const FCatCapturePlan& Plan)
	{
		++BroadcastCount;
		LastPlan = Plan;
	});
	{
		CatProfileSubsystemTest::FProfileSettingsClosedOverride ClosedOverride;
		TestFalse(TEXT("只有订阅者但默认配置未开时 CapturePlan 不广播"), Profile->ReceiveCapturePlan(ValidPlan));
		TestEqual(TEXT("默认配置广播次数为 0"), BroadcastCount, 0);
	}

	{
		CatProfileSubsystemTest::FProfileSettingsOverride Override;
		FCatCapturePlan InvalidPlan = ValidPlan;
		InvalidPlan.RunAlbumId.Invalidate();
		TestFalse(TEXT("字段不完整的 CapturePlan 不广播"), Profile->ReceiveCapturePlan(InvalidPlan));
		TestEqual(TEXT("无效计划后广播次数仍为 0"), BroadcastCount, 0);

		TestTrue(TEXT("桥、订阅者和计划都完整时 CapturePlan 被接管"), Profile->ReceiveCapturePlan(ValidPlan));
		TestEqual(TEXT("完整计划只广播一次"), BroadcastCount, 1);
		TestEqual(TEXT("广播保留 CapturePlanId"), LastPlan.CapturePlanId, ValidPlan.CapturePlanId);
		TestEqual(TEXT("广播保留 CandidateId"), LastPlan.CandidateId, ValidPlan.CandidateId);
		TestEqual(TEXT("广播保留 RunAlbumId"), LastPlan.RunAlbumId, ValidPlan.RunAlbumId);
	}
	return !HasAnyErrors();
}

// 测试流程：用唯一 SaveGame 槽位创建真实 LocalPlayer Profile，先提交一份鱼图鉴 Grant 并检查磁盘 Journal，再重放同一
// Grant 和同 GrantId 漂移载荷；成功重放允许 ACK，漂移必须拒绝且不改变图鉴快照。
bool FCatProfileSubsystemGrantJournalDurableReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SlotBaseName = FString::Printf(TEXT("AutomationProfileGrant_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const int32 UserIndex = 0;
	const FString SlotName = FString::Printf(TEXT("%s_%d"), *SlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	};

	CatProfileSubsystemTest::FProfileSettingsOverride Override(SlotBaseName);
	TestNotNull(TEXT("Profile Grant 测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建 Profile Grant 测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, UserIndex);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};
	UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>();
	TestNotNull(TEXT("真实 LocalPlayer Profile 子系统可用"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FGuid GrantId = FGuid::NewGuid();
	const FCatProfileGrant Grant = CatProfileSubsystemTest::MakeRecordedFishGrant(GrantId, 2.5);
	const FCatProfileApplyResult Applied = Profile->ApplyGrant(Grant);
	TestEqual(TEXT("首次 Grant 写入返回成功"), Applied.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("首次 Grant 已合并到本地 Profile"), Applied.bApplied);
	TestTrue(TEXT("首次 Grant 持久化后允许 ACK"), Applied.bAckAllowed);

	TArray<FCatFishCollectionRecord> Records;
	TestTrue(TEXT("成功 Grant 后可读取鱼图鉴快照"), Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("鱼图鉴只新增一条记录"), Records.Num(), 1);
	if (Records.Num() == 1)
	{
		TestEqual(TEXT("鱼图鉴记录鱼种 ID"), Records[0].FishDefinitionId, Grant.FishDefinitionId);
		TestEqual(TEXT("鱼图鉴进入 Recorded 状态"), Records[0].State, ECatFishCollectionState::Recorded);
		TestEqual(TEXT("鱼图鉴记录首次重量"), Records[0].BestWeightKilograms, Grant.WeightKilograms);
		TestEqual(TEXT("鱼图鉴记录首次捕获水域"), Records[0].FirstCaptureCondition.RegionId, Grant.CaptureCondition.RegionId);
		TestEqual(TEXT("鱼图鉴 encounter 计数为一次"), Records[0].EncounterCount, 1);
	}

	UCatProfileSaveGame* LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	TestNotNull(TEXT("首次 Grant 后 SaveGame 可从磁盘读回"), LoadedProfile);
	if (LoadedProfile)
	{
		TestEqual(TEXT("磁盘 Journal 只保留一条 Grant"), LoadedProfile->GrantJournal.Num(), 1);
		if (LoadedProfile->GrantJournal.Num() == 1)
		{
			TestEqual(TEXT("磁盘 Journal 已进入 Complete"), LoadedProfile->GrantJournal[0].Stage, ECatGrantJournalStage::Complete);
			TestEqual(TEXT("磁盘 Journal 保留 GrantId"), LoadedProfile->GrantJournal[0].Grant.GrantId, GrantId);
		}
		TestTrue(TEXT("磁盘 AppliedGrantIds 记录 GrantId"), LoadedProfile->AppliedGrantIds.Contains(GrantId));
		TestEqual(TEXT("磁盘鱼图鉴保存一条记录"), LoadedProfile->FishCollection.Num(), 1);
	}

	const FCatProfileApplyResult Replay = Profile->ApplyGrant(Grant);
	TestEqual(TEXT("同载荷 Grant 重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("重放不再次合并"), Replay.bApplied);
	TestTrue(TEXT("重放仍允许 ACK"), Replay.bAckAllowed);

	FCatProfileGrant DriftedGrant = Grant;
	DriftedGrant.WeightKilograms = 3.75;
	const FCatProfileApplyResult Drifted = Profile->ApplyGrant(DriftedGrant);
	TestEqual(TEXT("同 GrantId 载荷漂移被拒绝"), Drifted.Error, ECatDomainCommandError::InvalidPayload);
	Records.Reset();
	TestTrue(TEXT("漂移拒绝后仍可读取鱼图鉴快照"), Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("漂移拒绝后仍只有一条记录"), Records.Num(), 1);
	if (Records.Num() == 1)
	{
		TestEqual(TEXT("漂移拒绝不覆盖最佳重量"), Records[0].BestWeightKilograms, 2.5);
		TestEqual(TEXT("漂移拒绝不增加 encounter 次数"), Records[0].EncounterCount, 1);
	}
	return !HasAnyErrors();
}

// 测试流程：用真实 SaveGame 槽位先通过 Grant 建立本地印记，再通过公开隐藏接口切换本人本地隐藏状态；每次提交后都从磁盘
// 读回，证明隐藏只落在 Profile 本地索引中。
bool FCatProfileSubsystemImprintHiddenDurableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SlotBaseName = FString::Printf(TEXT("AutomationProfileImprint_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const int32 UserIndex = 0;
	const FString SlotName = FString::Printf(TEXT("%s_%d"), *SlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	};

	CatProfileSubsystemTest::FProfileSettingsOverride Override(SlotBaseName);
	TestNotNull(TEXT("Profile 印记隐藏测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建 Profile 印记隐藏测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, UserIndex);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};
	UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>();
	TestNotNull(TEXT("真实 LocalPlayer Profile 子系统可用于印记隐藏测试"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FGuid ImprintId = FGuid::NewGuid();
	const FGuid RunAlbumId = FGuid::NewGuid();
	const FCatProfileGrant Grant = CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), ImprintId, RunAlbumId);
	const FCatProfileApplyResult Applied = Profile->ApplyGrant(Grant);
	TestEqual(TEXT("印记 Grant 首次写入成功"), Applied.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("印记 Grant 已合并到本地 Profile"), Applied.bApplied);
	TestTrue(TEXT("印记 Grant 持久化后允许 ACK"), Applied.bAckAllowed);

	UCatProfileSaveGame* LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	TestNotNull(TEXT("印记 Grant 后 SaveGame 可读回"), LoadedProfile);
	if (!LoadedProfile)
	{
		return false;
	}
	TestEqual(TEXT("本地相册新增一条印记索引"), LoadedProfile->Imprints.Num(), 1);
	if (LoadedProfile->Imprints.Num() == 1)
	{
		TestEqual(TEXT("磁盘印记保留 ImprintId"), LoadedProfile->Imprints[0].ImprintId, ImprintId);
		TestEqual(TEXT("磁盘印记保留 RunAlbumId"), LoadedProfile->Imprints[0].RunAlbumId, RunAlbumId);
		TestFalse(TEXT("新印记默认不隐藏"), LoadedProfile->Imprints[0].bHidden);
	}
	TestTrue(TEXT("封面 Grant 写入相册封面映射"), LoadedProfile->RunAlbumCovers.Contains(RunAlbumId));

	const FCatDomainCommandResult HideResult = Profile->SetImprintHidden(FGuid::NewGuid(), ImprintId, true);
	TestEqual(TEXT("隐藏已有印记提交成功"), HideResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("隐藏已有印记返回 committed"), HideResult.bCommitted);
	LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	TestNotNull(TEXT("隐藏提交后 SaveGame 仍可读回"), LoadedProfile);
	if (LoadedProfile && LoadedProfile->Imprints.Num() == 1)
	{
		TestTrue(TEXT("隐藏状态已 durable 写入磁盘"), LoadedProfile->Imprints[0].bHidden);
	}

	const FCatDomainCommandResult MissingResult = Profile->SetImprintHidden(FGuid::NewGuid(), FGuid::NewGuid(), false);
	TestEqual(TEXT("未知印记不能修改本地相册"), MissingResult.Error, ECatDomainCommandError::NotFound);
	LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (LoadedProfile && LoadedProfile->Imprints.Num() == 1)
	{
		TestTrue(TEXT("未知印记拒绝后不会回滚已有隐藏状态"), LoadedProfile->Imprints[0].bHidden);
	}

	const FCatDomainCommandResult ShowResult = Profile->SetImprintHidden(FGuid::NewGuid(), ImprintId, false);
	TestEqual(TEXT("取消隐藏已有印记提交成功"), ShowResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("取消隐藏已有印记返回 committed"), ShowResult.bCommitted);
	LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (LoadedProfile && LoadedProfile->Imprints.Num() == 1)
	{
		TestFalse(TEXT("取消隐藏状态已 durable 写入磁盘"), LoadedProfile->Imprints[0].bHidden);
	}
	return !HasAnyErrors();
}

// 测试流程：先只用抄获轨 Grant 建立同一鱼种的图鉴页，再补钓起轨，最后再来一条抄获轨。
// 锁住的不变量是两轨在同一页上互不覆盖：抄获只累加 ScoopedCount，不推进 State、不改个人最佳、也不计入 EncounterCount；
// 钓起轨照旧推进状态与最佳重量，且不会把抄获次数清掉。旧的单轨实现里 FishScooped 根本不存在，这条会直接暴露。
bool FCatProfileSubsystemFishTrackSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SlotBaseName = FString::Printf(TEXT("AutomationProfileTracks_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const int32 UserIndex = 0;
	const FString SlotName = FString::Printf(TEXT("%s_%d"), *SlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	};

	CatProfileSubsystemTest::FProfileSettingsOverride Override(SlotBaseName);
	TestNotNull(TEXT("两轨记录测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建两轨记录测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, UserIndex);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};
	UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>();
	TestNotNull(TEXT("两轨记录测试 Profile 子系统可用"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FCatProfileApplyResult ScoopApplied = Profile->ApplyGrant(
		CatProfileSubsystemTest::MakeScoopedFishGrant(FGuid::NewGuid()));
	TestEqual(TEXT("抄获轨 Grant 被接受"), ScoopApplied.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("抄获轨 Grant 已合并"), ScoopApplied.bApplied);

	TArray<FCatFishCollectionRecord> Records;
	TestTrue(TEXT("抄获轨合并后可读取图鉴快照"), Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("抄获轨也会建立该鱼种的图鉴页"), Records.Num(), 1);
	if (Records.Num() == 1)
	{
		TestEqual(TEXT("只抄不钓时抄获次数为一次"), Records[0].ScoopedCount, 1);
		TestEqual(TEXT("只抄不钓不推进图鉴状态"), Records[0].State, ECatFishCollectionState::Unknown);
		TestEqual(TEXT("只抄不钓不产生个人最佳"), Records[0].BestWeightKilograms, 0.0);
		TestEqual(TEXT("只抄不钓不计入钓起轨 encounter"), Records[0].EncounterCount, 0);
	}

	const FCatProfileGrant RecordedGrant = CatProfileSubsystemTest::MakeRecordedFishGrant(FGuid::NewGuid(), 2.5);
	TestEqual(TEXT("同一鱼种的钓起轨 Grant 被接受"), Profile->ApplyGrant(RecordedGrant).Error,
		ECatDomainCommandError::None);
	Records.Reset();
	TestTrue(TEXT("钓起轨合并后可读取图鉴快照"), Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("两轨共用同一张图鉴页"), Records.Num(), 1);
	if (Records.Num() == 1)
	{
		TestEqual(TEXT("钓起轨把图鉴推进到 Recorded"), Records[0].State, ECatFishCollectionState::Recorded);
		TestEqual(TEXT("钓起轨写入个人最佳"), Records[0].BestWeightKilograms, 2.5);
		TestEqual(TEXT("钓起轨记录首次捕获水域"), Records[0].FirstCaptureCondition.RegionId,
			RecordedGrant.CaptureCondition.RegionId);
		TestEqual(TEXT("钓起轨只计一次 encounter"), Records[0].EncounterCount, 1);
		TestEqual(TEXT("钓起轨不清掉已有抄获次数"), Records[0].ScoopedCount, 1);
	}

	TestEqual(TEXT("第二条抄获轨 Grant 被接受"),
		Profile->ApplyGrant(CatProfileSubsystemTest::MakeScoopedFishGrant(FGuid::NewGuid())).Error,
		ECatDomainCommandError::None);
	Records.Reset();
	TestTrue(TEXT("第二次抄获后可读取图鉴快照"), Profile->GetFishCollectionSnapshot(Records));
	if (Records.Num() == 1)
	{
		TestEqual(TEXT("抄获次数累加到两次"), Records[0].ScoopedCount, 2);
		TestEqual(TEXT("抄获不改写个人最佳"), Records[0].BestWeightKilograms, 2.5);
		TestEqual(TEXT("抄获不增加钓起轨 encounter"), Records[0].EncounterCount, 1);
		TestEqual(TEXT("抄获不改写图鉴状态"), Records[0].State, ECatFishCollectionState::Recorded);
	}

	const UCatProfileSaveGame* LoadedProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	TestNotNull(TEXT("两轨记录后 SaveGame 可从磁盘读回"), LoadedProfile);
	if (LoadedProfile && LoadedProfile->FishCollection.Num() == 1)
	{
		TestEqual(TEXT("抄获次数已 durable 写入磁盘"), LoadedProfile->FishCollection[0].ScoopedCount, 2);
		TestEqual(TEXT("个人最佳已 durable 写入磁盘"), LoadedProfile->FishCollection[0].BestWeightKilograms, 2.5);
	}
	return !HasAnyErrors();
}

// 测试流程：先在一个真实 SaveGame 槽位上写入两局相册（A 局两张、其中一张是封面；B 局一张、无封面），
// 用三个读取入口断言按局分组、页序、封面来源和隐藏过滤；再隐藏普通印记和封面印记各验一次；
// 最后销毁这个 LocalPlayer、用同一槽位重建一个新的 Profile 子系统，证明这些页是从磁盘读回来的，不是内存残留。
// 锁住的不变量：一局一页、页序等于印记落盘顺序、封面只来自封面映射、隐藏可逆且只改视图不改存储。
bool FCatProfileSubsystemAlbumReadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SlotBaseName = FString::Printf(TEXT("AutomationProfileAlbum_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const int32 UserIndex = 0;
	const FString SlotName = FString::Printf(TEXT("%s_%d"), *SlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	};

	CatProfileSubsystemTest::FProfileSettingsOverride Override(SlotBaseName);
	TestNotNull(TEXT("相册读取测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}

	// A 局先落两张（第二张是封面），B 局后落一张且没有封面；两局的 ID 与落盘顺序都要在断言里被用到。
	const FGuid AlbumA = FGuid::NewGuid();
	const FGuid AlbumB = FGuid::NewGuid();
	const FGuid ImprintA1 = FGuid::NewGuid();
	const FGuid ImprintA2 = FGuid::NewGuid();
	const FGuid ImprintB1 = FGuid::NewGuid();

	{
		ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
		TestNotNull(TEXT("可创建相册读取测试 LocalPlayer"), LocalPlayer);
		if (!LocalPlayer)
		{
			return false;
		}
		LocalPlayer->PlayerAdded(nullptr, UserIndex);
		ON_SCOPE_EXIT
		{
			LocalPlayer->PlayerRemoved();
		};
		UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>();
		TestNotNull(TEXT("相册读取测试 Profile 子系统可用"), Profile);
		if (!Profile)
		{
			return false;
		}

		TestEqual(TEXT("A 局第一张印记写入成功"),
			Profile->ApplyGrant(CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), ImprintA1, AlbumA, false)).Error,
			ECatDomainCommandError::None);
		TestEqual(TEXT("A 局封面印记写入成功"),
			Profile->ApplyGrant(CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), ImprintA2, AlbumA, true)).Error,
			ECatDomainCommandError::None);
		TestEqual(TEXT("B 局普通印记写入成功"),
			Profile->ApplyGrant(CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), ImprintB1, AlbumB, false)).Error,
			ECatDomainCommandError::None);

		TArray<FCatRunAlbumSummary> Albums;
		TestTrue(TEXT("可列出本人相册分组"), Profile->GetRunAlbumSummaries(true, Albums));
		TestEqual(TEXT("三张印记按局分成两页"), Albums.Num(), 2);
		if (Albums.Num() == 2)
		{
			TestEqual(TEXT("页序按首张印记落盘顺序，A 局在前"), Albums[0].RunAlbumId, AlbumA);
			TestEqual(TEXT("A 局这一页有两张印记"), Albums[0].ImprintCount, 2);
			TestEqual(TEXT("A 局封面是被标记为封面的那张"), Albums[0].CoverImprintId, ImprintA2);
			TestEqual(TEXT("页序中 B 局在后"), Albums[1].RunAlbumId, AlbumB);
			TestEqual(TEXT("B 局这一页有一张印记"), Albums[1].ImprintCount, 1);
			TestFalse(TEXT("没有封面 Grant 的一局不挑印记顶替封面"), Albums[1].CoverImprintId.IsValid());
		}

		FGuid CoverImprintId;
		TestTrue(TEXT("可单独读取 A 局封面"), Profile->TryGetRunAlbumCover(AlbumA, true, CoverImprintId));
		TestEqual(TEXT("单独读取的封面与分组摘要一致"), CoverImprintId, ImprintA2);
		TestFalse(TEXT("B 局没有封面可读"), Profile->TryGetRunAlbumCover(AlbumB, true, CoverImprintId));
		TestFalse(TEXT("读不到封面时输出保持无效"), CoverImprintId.IsValid());
		TestFalse(TEXT("未知相册读不到封面"), Profile->TryGetRunAlbumCover(FGuid::NewGuid(), true, CoverImprintId));

		TArray<FCatLocalImprintRecord> AlbumAImprints;
		TestTrue(TEXT("可读取 A 局印记列表"), Profile->GetRunAlbumImprints(AlbumA, true, AlbumAImprints));
		TestEqual(TEXT("A 局印记列表只含本局两张"), AlbumAImprints.Num(), 2);
		if (AlbumAImprints.Num() == 2)
		{
			TestEqual(TEXT("A 局列表保持落盘顺序的第一张"), AlbumAImprints[0].ImprintId, ImprintA1);
			TestEqual(TEXT("A 局列表保持落盘顺序的第二张"), AlbumAImprints[1].ImprintId, ImprintA2);
		}
		TArray<FCatLocalImprintRecord> UnknownAlbumImprints;
		TestTrue(TEXT("未知相册是合法查询"), Profile->GetRunAlbumImprints(FGuid::NewGuid(), true, UnknownAlbumImprints));
		TestEqual(TEXT("未知相册返回空列表"), UnknownAlbumImprints.Num(), 0);

		// 隐藏普通印记：只从过滤视图里消失，包含隐藏时仍在，封面不受影响。
		TestEqual(TEXT("隐藏 A 局普通印记成功"),
			Profile->SetImprintHidden(FGuid::NewGuid(), ImprintA1, true).Error, ECatDomainCommandError::None);
		AlbumAImprints.Reset();
		TestTrue(TEXT("隐藏后仍可读取 A 局过滤列表"), Profile->GetRunAlbumImprints(AlbumA, false, AlbumAImprints));
		TestEqual(TEXT("过滤视图里隐藏的普通印记不出现"), AlbumAImprints.Num(), 1);
		if (AlbumAImprints.Num() == 1)
		{
			TestEqual(TEXT("过滤视图剩下的是未隐藏的封面印记"), AlbumAImprints[0].ImprintId, ImprintA2);
		}
		AlbumAImprints.Reset();
		TestTrue(TEXT("要求包含隐藏时仍可读取 A 局列表"), Profile->GetRunAlbumImprints(AlbumA, true, AlbumAImprints));
		TestEqual(TEXT("包含隐藏时 A 局仍是两张"), AlbumAImprints.Num(), 2);
		Albums.Reset();
		TestTrue(TEXT("隐藏后仍可列出过滤分组"), Profile->GetRunAlbumSummaries(false, Albums));
		TestEqual(TEXT("隐藏一张不会让 A 局这一页消失"), Albums.Num(), 2);
		if (Albums.Num() == 2)
		{
			TestEqual(TEXT("过滤后 A 局条数减为一张"), Albums[0].ImprintCount, 1);
			TestEqual(TEXT("隐藏普通印记不影响封面"), Albums[0].CoverImprintId, ImprintA2);
		}

		// 隐藏封面本身：过滤视图里既没有封面也没有这一页，但包含隐藏时两者都还在——证明隐藏没有删存储。
		TestEqual(TEXT("隐藏 A 局封面印记成功"),
			Profile->SetImprintHidden(FGuid::NewGuid(), ImprintA2, true).Error, ECatDomainCommandError::None);
		TestFalse(TEXT("封面被隐藏后过滤视图读不到封面"), Profile->TryGetRunAlbumCover(AlbumA, false, CoverImprintId));
		TestTrue(TEXT("封面被隐藏后包含隐藏仍能读到封面"), Profile->TryGetRunAlbumCover(AlbumA, true, CoverImprintId));
		TestEqual(TEXT("包含隐藏读到的仍是同一张封面"), CoverImprintId, ImprintA2);
		Albums.Reset();
		TestTrue(TEXT("封面被隐藏后仍可列出过滤分组"), Profile->GetRunAlbumSummaries(false, Albums));
		TestEqual(TEXT("整局都被隐藏后过滤视图只剩 B 局"), Albums.Num(), 1);
		if (Albums.Num() == 1)
		{
			TestEqual(TEXT("过滤视图剩下的是 B 局"), Albums[0].RunAlbumId, AlbumB);
		}
		Albums.Reset();
		TestTrue(TEXT("封面被隐藏后包含隐藏仍可列出分组"), Profile->GetRunAlbumSummaries(true, Albums));
		TestEqual(TEXT("包含隐藏时两页都还在"), Albums.Num(), 2);

		// 取消隐藏：页原位复现，证明隐藏是可逆的本人索引开关。
		TestEqual(TEXT("取消隐藏 A 局封面成功"),
			Profile->SetImprintHidden(FGuid::NewGuid(), ImprintA2, false).Error, ECatDomainCommandError::None);
		TestEqual(TEXT("取消隐藏 A 局普通印记成功"),
			Profile->SetImprintHidden(FGuid::NewGuid(), ImprintA1, false).Error, ECatDomainCommandError::None);
		Albums.Reset();
		TestTrue(TEXT("取消隐藏后仍可列出过滤分组"), Profile->GetRunAlbumSummaries(false, Albums));
		TestEqual(TEXT("取消隐藏后 A 局这一页原位复现"), Albums.Num(), 2);
		if (Albums.Num() == 2)
		{
			TestEqual(TEXT("复现的 A 局仍排在第一页"), Albums[0].RunAlbumId, AlbumA);
			TestEqual(TEXT("复现的 A 局恢复两张"), Albums[0].ImprintCount, 2);
		}
	}

	// 换一个 LocalPlayer 用同一槽位重新初始化：这一段读到的任何东西都只能来自磁盘。
	{
		ULocalPlayer* ReloadedPlayer = NewObject<ULocalPlayer>(GEngine);
		TestNotNull(TEXT("可创建相册重载测试 LocalPlayer"), ReloadedPlayer);
		if (!ReloadedPlayer)
		{
			return false;
		}
		ReloadedPlayer->PlayerAdded(nullptr, UserIndex);
		ON_SCOPE_EXIT
		{
			ReloadedPlayer->PlayerRemoved();
		};
		UCatProfileSubsystem* ReloadedProfile = ReloadedPlayer->GetSubsystem<UCatProfileSubsystem>();
		TestNotNull(TEXT("重载后 Profile 子系统可用"), ReloadedProfile);
		if (!ReloadedProfile)
		{
			return false;
		}

		TArray<FCatRunAlbumSummary> ReloadedAlbums;
		TestTrue(TEXT("重载后可列出相册分组"), ReloadedProfile->GetRunAlbumSummaries(true, ReloadedAlbums));
		TestEqual(TEXT("重载后仍是两页"), ReloadedAlbums.Num(), 2);
		if (ReloadedAlbums.Num() == 2)
		{
			TestEqual(TEXT("重载后页序不变"), ReloadedAlbums[0].RunAlbumId, AlbumA);
			TestEqual(TEXT("重载后 A 局仍是两张"), ReloadedAlbums[0].ImprintCount, 2);
			TestEqual(TEXT("重载后 A 局封面不变"), ReloadedAlbums[0].CoverImprintId, ImprintA2);
			TestEqual(TEXT("重载后第二页仍是 B 局"), ReloadedAlbums[1].RunAlbumId, AlbumB);
		}
		TArray<FCatLocalImprintRecord> ReloadedImprints;
		TestTrue(TEXT("重载后可读取 B 局印记列表"), ReloadedProfile->GetRunAlbumImprints(AlbumB, true, ReloadedImprints));
		TestEqual(TEXT("重载后 B 局仍是一张"), ReloadedImprints.Num(), 1);
		if (ReloadedImprints.Num() == 1)
		{
			TestEqual(TEXT("重载后 B 局印记 ID 不变"), ReloadedImprints[0].ImprintId, ImprintB1);
		}
	}
	return !HasAnyErrors();
}

// 测试流程：先把相册容量上限设为 1，写满后再投一条新印记 Grant，断言它被拒、档案没长大、已有印记没被淘汰；
// 同 GrantId 重放该被拒授予仍然被拒（没有半条 Journal 残留）；同 ImprintId 的授予不占新名额；
// 最后把上限恢复成未配置的 0，断言这时连第一条印记都进不来。
// 锁住的不变量：容量只拒绝新增、不做淘汰，未配置上限与已存满走同一条 fail-closed 路径。
bool FCatProfileSubsystemAlbumCapacityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SlotBaseName = FString::Printf(TEXT("AutomationProfileCapacity_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const int32 UserIndex = 0;
	const FString SlotName = FString::Printf(TEXT("%s_%d"), *SlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	};

	CatProfileSubsystemTest::FProfileSettingsOverride Override(SlotBaseName, 1);
	TestNotNull(TEXT("相册容量测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建相册容量测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, UserIndex);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};
	UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>();
	TestNotNull(TEXT("相册容量测试 Profile 子系统可用"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FGuid AlbumId = FGuid::NewGuid();
	const FGuid FirstImprintId = FGuid::NewGuid();
	TestEqual(TEXT("上限之内的第一条印记写入成功"),
		Profile->ApplyGrant(CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), FirstImprintId, AlbumId, true)).Error,
		ECatDomainCommandError::None);

	const FGuid OverflowGrantId = FGuid::NewGuid();
	const FCatProfileGrant OverflowGrant = CatProfileSubsystemTest::MakeImprintGrant(
		OverflowGrantId, FGuid::NewGuid(), AlbumId, false);
	const FCatProfileApplyResult Overflow = Profile->ApplyGrant(OverflowGrant);
	TestEqual(TEXT("超出相册容量的印记被拒"), Overflow.Error, ECatDomainCommandError::CapacityExceeded);
	TestFalse(TEXT("超容印记没有被合并"), Overflow.bApplied);
	TestFalse(TEXT("超容印记不允许 ACK，服务器会保留该 Grant"), Overflow.bAckAllowed);

	TArray<FCatLocalImprintRecord> Imprints;
	TestTrue(TEXT("超容拒绝后仍可读取相册"), Profile->GetRunAlbumImprints(AlbumId, true, Imprints));
	TestEqual(TEXT("超容拒绝不让相册变长"), Imprints.Num(), 1);
	if (Imprints.Num() == 1)
	{
		TestEqual(TEXT("超容拒绝不淘汰已有印记"), Imprints[0].ImprintId, FirstImprintId);
	}

	const FCatProfileApplyResult OverflowReplay = Profile->ApplyGrant(OverflowGrant);
	TestEqual(TEXT("被拒授予重放仍然被拒"), OverflowReplay.Error, ECatDomainCommandError::CapacityExceeded);
	TestFalse(TEXT("被拒授予重放仍不允许 ACK"), OverflowReplay.bAckAllowed);

	// 同一张印记换一份授予：它不会让数组变长，因此不该被容量挡住，而是照常走到合并去重。
	const FCatProfileApplyResult SameImprintDifferentGrant = Profile->ApplyGrant(
		CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), FirstImprintId, AlbumId, true));
	TestEqual(TEXT("同 ImprintId 的另一份授予不占新名额"), SameImprintDifferentGrant.Error, ECatDomainCommandError::None);
	Imprints.Reset();
	TestTrue(TEXT("同 ImprintId 授予后仍可读取相册"), Profile->GetRunAlbumImprints(AlbumId, true, Imprints));
	TestEqual(TEXT("同 ImprintId 授予不重复写入索引"), Imprints.Num(), 1);

	// 把上限改回项目默认的未配置态，验证 fail-closed：这时连空相册的第一条都进不来。
	const FString EmptySlotBaseName = FString::Printf(TEXT("AutomationProfileCapacityUnset_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString EmptySlotName = FString::Printf(TEXT("%s_%d"), *EmptySlotBaseName, UserIndex);
	UGameplayStatics::DeleteGameInSlot(EmptySlotName, UserIndex);
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(EmptySlotName, UserIndex);
	};
	{
		CatProfileSubsystemTest::FProfileSettingsOverride UnsetCapacityOverride(EmptySlotBaseName, 0);
		ULocalPlayer* UnsetPlayer = NewObject<ULocalPlayer>(GEngine);
		TestNotNull(TEXT("可创建未配置上限测试 LocalPlayer"), UnsetPlayer);
		if (!UnsetPlayer)
		{
			return false;
		}
		UnsetPlayer->PlayerAdded(nullptr, UserIndex);
		ON_SCOPE_EXIT
		{
			UnsetPlayer->PlayerRemoved();
		};
		UCatProfileSubsystem* UnsetProfile = UnsetPlayer->GetSubsystem<UCatProfileSubsystem>();
		TestNotNull(TEXT("未配置上限测试 Profile 子系统可用"), UnsetProfile);
		if (!UnsetProfile)
		{
			return false;
		}
		const FCatProfileApplyResult UnsetResult = UnsetProfile->ApplyGrant(
			CatProfileSubsystemTest::MakeImprintGrant(FGuid::NewGuid(), FGuid::NewGuid(), FGuid::NewGuid(), false));
		TestEqual(TEXT("上限未配置时空相册的第一条印记也被拒"), UnsetResult.Error, ECatDomainCommandError::CapacityExceeded);
		TArray<FCatRunAlbumSummary> UnsetAlbums;
		TestTrue(TEXT("未配置上限时仍可列出相册"), UnsetProfile->GetRunAlbumSummaries(true, UnsetAlbums));
		TestEqual(TEXT("未配置上限时相册保持为空"), UnsetAlbums.Num(), 0);
	}
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
