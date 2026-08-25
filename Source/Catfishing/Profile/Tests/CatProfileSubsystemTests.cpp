#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Profile/CatProfileSettings.h"
#include "Profile/CatProfileSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemCapturePlanGateTest,
	"Catfishing.Unit.Profile.Subsystem.CapturePlanBroadcastRequiresExplicitBridgeAndCompletePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemNotificationIsolationTest,
	"Catfishing.Unit.Profile.Subsystem.NonFishInputsDoNotPublishFishCollectionChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemGrantJournalMergeTest,
	"Catfishing.Unit.Profile.Subsystem.GrantJournalFishAlbumAndUnlocksMergeDurably",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatProfileSubsystemTest
{
	/** 仅供测试手动初始化 LocalPlayerSubsystem 的集合外壳；它不拥有其他子系统，也不改变运行时注册方式。 */
	struct FProfileSubsystemTestCollection : FSubsystemCollectionBase
	{
		// 构造流程：把集合基类限定到 LocalPlayerSubsystem 类型，满足 Profile::Initialize 的父类契约。
		FProfileSubsystemTestCollection()
			: FSubsystemCollectionBase(ULocalPlayerSubsystem::StaticClass())
		{
		}
	};

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

		// 保存流程：记录旧值后写入一套可让外部成像桥成立的显式配置。
		FProfileSettingsOverride()
		{
			if (Settings)
			{
				bOldPersistence = Settings->bEnableProfilePersistence;
				OldSlotName = Settings->SaveSlotBaseName;
				bOldBridge = Settings->bEnableExternalImprintCaptureBridge;
				Settings->bEnableProfilePersistence = true;
				Settings->SaveSlotBaseName = TEXT("AutomationProfile");
				Settings->bEnableExternalImprintCaptureBridge = true;
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
			}
		}
	};

	/** 一次可写 Profile 子系统测试夹具；它只使用临时存档槽位，析构时删除该槽位并恢复默认配置。 */
	struct FPersistentProfileFixture
	{
		/** 被临时改写的默认 Profile 设置；Profile 初始化和 CapturePlan gate 都从这里读取。 */
		UCatProfileSettings* Settings = GetMutableDefault<UCatProfileSettings>();

		/** 原始持久化 gate；析构时恢复，避免影响后续自动化或人工运行。 */
		bool bOldPersistence = false;

		/** 原始槽位基础名；析构时恢复，避免测试槽名泄露到项目默认配置对象。 */
		FString OldSlotName;

		/** 原始外部成像桥 gate；析构时恢复，保证本测试不改变 CapturePlan 默认行为。 */
		bool bOldBridge = false;

		/** 本次测试独占的槽位基础名；追加 ControllerId 后形成实际 SaveGame 槽。 */
		FString SlotBaseName;

		/** 本次测试写入的实际 SaveGame 槽；构造前后都会按精确槽名清理。 */
		FString SlotName;

		/** 承载 LocalPlayerSubsystem 的本地玩家对象；Profile 通过它解析 ControllerId。 */
		TObjectPtr<ULocalPlayer> LocalPlayer = nullptr;

		/** 被测 Profile 子系统；它持有内存 SaveGame、Journal 和公开快照入口。 */
		TObjectPtr<UCatProfileSubsystem> Profile = nullptr;

		/** Profile 是否已经完成手动 Initialize；析构只对已初始化对象调用 Deinitialize。 */
		bool bInitialized = false;

		// 构造流程：保存默认配置，切到唯一临时槽位，创建 LocalPlayer/Profile 并执行正式 Initialize，使 ApplyGrant 走真实 SaveGame 路径。
		FPersistentProfileFixture()
		{
			if (Settings)
			{
				bOldPersistence = Settings->bEnableProfilePersistence;
				OldSlotName = Settings->SaveSlotBaseName;
				bOldBridge = Settings->bEnableExternalImprintCaptureBridge;
				SlotBaseName = FString::Printf(TEXT("AutomationProfileGrant_%s"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits));
				SlotName = FString::Printf(TEXT("%s_0"), *SlotBaseName);
				Settings->bEnableProfilePersistence = true;
				Settings->SaveSlotBaseName = SlotBaseName;
				Settings->bEnableExternalImprintCaptureBridge = true;
				UGameplayStatics::DeleteGameInSlot(SlotName, 0);
			}

			LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
			if (LocalPlayer)
			{
				LocalPlayer->SetControllerId(0);
				Profile = NewObject<UCatProfileSubsystem>(LocalPlayer);
			}
			if (Profile)
			{
				FProfileSubsystemTestCollection Collection;
				Profile->Initialize(Collection);
				bInitialized = true;
			}
		}

		// 析构流程：先让 Profile 走正式 Deinitialize，再删除本测试槽位并恢复默认配置对象；失败删除不影响测试结论但不会删除非测试槽。
		~FPersistentProfileFixture()
		{
			if (Profile && bInitialized)
			{
				Profile->Deinitialize();
			}
			if (!SlotName.IsEmpty())
			{
				UGameplayStatics::DeleteGameInSlot(SlotName, 0);
			}
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

	// Grant 构造流程：生成一份最小鱼图鉴授予；调用方按种类补齐重量、印记或解锁字段。
	static FCatProfileGrant MakeFishGrant(const ECatProfileGrantKind Kind, const FName FishDefinitionId)
	{
		FCatProfileGrant Grant;
		Grant.GrantId = FGuid::NewGuid();
		Grant.Kind = Kind;
		Grant.FishDefinitionId = FishDefinitionId;
		return Grant;
	}
}

// 测试流程：直接通过 Profile 子系统公开入口接收 CapturePlan；缺桥或缺稳定字段时不得广播，桥和计划都完整时只广播一次原计划。
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

	int32 BroadcastCount = 0;
	FCatCapturePlan LastPlan;
	Profile->OnCapturePlanReceived.AddLambda([&BroadcastCount, &LastPlan](const FCatCapturePlan& Plan)
	{
		++BroadcastCount;
		LastPlan = Plan;
	});

	const FCatCapturePlan ValidPlan = CatProfileSubsystemTest::MakeValidPlan();
	TestFalse(TEXT("默认配置下 CapturePlan 不广播"), Profile->ReceiveCapturePlan(ValidPlan));
	TestEqual(TEXT("默认配置广播次数为 0"), BroadcastCount, 0);

	{
		CatProfileSubsystemTest::FProfileSettingsOverride Override;
		FCatCapturePlan InvalidPlan = ValidPlan;
		InvalidPlan.RunAlbumId.Invalidate();
		TestFalse(TEXT("字段不完整的 CapturePlan 不广播"), Profile->ReceiveCapturePlan(InvalidPlan));
		TestEqual(TEXT("无效计划后广播次数仍为 0"), BroadcastCount, 0);

		TestTrue(TEXT("桥和计划都完整时 CapturePlan 被接管"), Profile->ReceiveCapturePlan(ValidPlan));
		TestEqual(TEXT("完整计划只广播一次"), BroadcastCount, 1);
		TestEqual(TEXT("广播保留 CapturePlanId"), LastPlan.CapturePlanId, ValidPlan.CapturePlanId);
		TestEqual(TEXT("广播保留 CandidateId"), LastPlan.CandidateId, ValidPlan.CandidateId);
		TestEqual(TEXT("广播保留 RunAlbumId"), LastPlan.RunAlbumId, ValidPlan.RunAlbumId);
	}
	return !HasAnyErrors();
}

// 测试流程：给同一 Profile 对象订阅图鉴通知，再提交一份可接管的 CapturePlan；计划广播可以发生，但它不代表图鉴 durable 合并，因此图鉴通知必须保持为零。
bool FCatProfileSubsystemNotificationIsolationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("Profile 通知隔离测试存在引擎 Outer"), GEngine);
	ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
	UCatProfileSubsystem* Profile = LocalPlayer ? NewObject<UCatProfileSubsystem>(LocalPlayer) : nullptr;
	TestNotNull(TEXT("可创建 Profile 通知隔离对象"), Profile);
	if (!Profile)
	{
		return false;
	}

	int32 FishCollectionChangedCount = 0;
	Profile->OnFishCollectionChanged.AddLambda([&FishCollectionChangedCount]()
	{
		++FishCollectionChangedCount;
	});
	{
		CatProfileSubsystemTest::FProfileSettingsOverride Override;
		TestTrue(TEXT("完整 CapturePlan 仍由成像入口接管"),
			Profile->ReceiveCapturePlan(CatProfileSubsystemTest::MakeValidPlan()));
	}
	TestEqual(TEXT("CapturePlan 不冒充 durable 图鉴变化"), FishCollectionChangedCount, 0);
	return !HasAnyErrors();
}

// 测试流程：用真实 SaveGame 槽执行 Profile Grant 两阶段落盘，先验证 FishSilhouette/FishRecorded 会推进公开图鉴并只触发对应通知，再验证重复 GrantId 只允许 ACK 重放而不重复合并或广播；随后检查 Imprint Grant 进入本地相册后才能隐藏，Unlock Grant 进入解锁摘要且不会触发图鉴通知。
bool FCatProfileSubsystemGrantJournalMergeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatProfileSubsystemTest::FPersistentProfileFixture Fixture;
	TestNotNull(TEXT("Profile Grant 测试 LocalPlayer 可创建"), Fixture.LocalPlayer.Get());
	TestNotNull(TEXT("Profile Grant 测试子系统可创建"), Fixture.Profile.Get());
	if (!Fixture.Profile)
	{
		return false;
	}

	int32 FishCollectionChangedCount = 0;
	Fixture.Profile->OnFishCollectionChanged.AddLambda([&FishCollectionChangedCount]()
	{
		++FishCollectionChangedCount;
	});

	TArray<FCatFishCollectionRecord> Records;
	TestTrue(TEXT("初始化后 Profile 图鉴快照可读"), Fixture.Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("新临时档案初始没有图鉴记录"), Records.Num(), 0);

	FCatProfileGrant SilhouetteGrant = CatProfileSubsystemTest::MakeFishGrant(
		ECatProfileGrantKind::FishSilhouette, TEXT("GrantFishA"));
	FCatProfileApplyResult SilhouetteResult = Fixture.Profile->ApplyGrant(SilhouetteGrant);
	TestEqual(TEXT("剪影 Grant 合并无错误"), SilhouetteResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("剪影 Grant durable 后允许 ACK"), SilhouetteResult.bAckAllowed);
	TestTrue(TEXT("剪影 Grant 首次应用"), SilhouetteResult.bApplied);
	TestTrue(TEXT("剪影 Grant 后图鉴快照可读"), Fixture.Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("剪影 Grant 建立一条鱼图鉴记录"), Records.Num(), 1);
	TestEqual(TEXT("剪影 Grant 把 Unknown 推进到 Silhouette"), Records[0].State,
		ECatFishCollectionState::Silhouette);
	TestEqual(TEXT("剪影 Grant 记录一次遭遇"), Records[0].EncounterCount, 1);
	TestEqual(TEXT("剪影 Grant 触发一次公开图鉴通知"), FishCollectionChangedCount, 1);

	FCatProfileGrant RecordedGrant = CatProfileSubsystemTest::MakeFishGrant(
		ECatProfileGrantKind::FishRecorded, TEXT("GrantFishA"));
	RecordedGrant.WeightKilograms = 4.25;
	RecordedGrant.CaptureCondition.RegionId = TEXT("River");
	RecordedGrant.CaptureCondition.TimeOfDayId = TEXT("Day");
	RecordedGrant.CaptureCondition.WeatherId = TEXT("Clear");
	FCatProfileApplyResult RecordedResult = Fixture.Profile->ApplyGrant(RecordedGrant);
	TestEqual(TEXT("捕获 Grant 合并无错误"), RecordedResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("捕获 Grant durable 后允许 ACK"), RecordedResult.bAckAllowed);
	TestTrue(TEXT("捕获 Grant 首次应用"), RecordedResult.bApplied);
	TestTrue(TEXT("捕获 Grant 后图鉴快照可读"), Fixture.Profile->GetFishCollectionSnapshot(Records));
	TestEqual(TEXT("同一 FishDefinition 仍只有一条图鉴记录"), Records.Num(), 1);
	TestEqual(TEXT("捕获 Grant 把剪影推进到 Recorded"), Records[0].State,
		ECatFishCollectionState::Recorded);
	TestEqual(TEXT("捕获 Grant 写入最佳重量"), Records[0].BestWeightKilograms, 4.25);
	TestEqual(TEXT("捕获 Grant 保留首次区域条件"), Records[0].FirstCaptureCondition.RegionId, FName(TEXT("River")));
	TestEqual(TEXT("捕获 Grant 追加遭遇次数"), Records[0].EncounterCount, 2);
	TestEqual(TEXT("捕获 Grant 再触发一次图鉴通知"), FishCollectionChangedCount, 2);

	const FCatProfileApplyResult ReplayResult = Fixture.Profile->ApplyGrant(RecordedGrant);
	TestEqual(TEXT("重复 GrantId 返回 AlreadyResolved"), ReplayResult.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("重复 GrantId 仍允许 ACK 丢失重放"), ReplayResult.bAckAllowed);
	TestFalse(TEXT("重复 GrantId 不重新应用内容"), ReplayResult.bApplied);
	TestEqual(TEXT("重复 GrantId 不重复广播图鉴通知"), FishCollectionChangedCount, 2);

	FCatProfileGrant ImprintGrant;
	ImprintGrant.GrantId = FGuid::NewGuid();
	ImprintGrant.Kind = ECatProfileGrantKind::Imprint;
	ImprintGrant.ImprintId = FGuid::NewGuid();
	ImprintGrant.RunAlbumId = FGuid::NewGuid();
	ImprintGrant.bRunAlbumCover = true;
	FCatProfileApplyResult ImprintResult = Fixture.Profile->ApplyGrant(ImprintGrant);
	TestEqual(TEXT("印记 Grant 合并无错误"), ImprintResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("印记 Grant durable 后允许 ACK"), ImprintResult.bAckAllowed);
	const FCatDomainCommandResult HideResult = Fixture.Profile->SetImprintHidden(
		FGuid::NewGuid(), ImprintGrant.ImprintId, true);
	TestEqual(TEXT("印记进入本地相册后可以切换隐藏"), HideResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("隐藏印记只提交本地 Profile 写入"), HideResult.bCommitted);

	FCatProfileGrant UnlockGrant;
	UnlockGrant.GrantId = FGuid::NewGuid();
	UnlockGrant.Kind = ECatProfileGrantKind::Unlock;
	UnlockGrant.UnlockId = TEXT("ProfilePremiumRod");
	FCatProfileApplyResult UnlockResult = Fixture.Profile->ApplyGrant(UnlockGrant);
	TestEqual(TEXT("解锁 Grant 合并无错误"), UnlockResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("解锁 Grant durable 后允许 ACK"), UnlockResult.bAckAllowed);
	TArray<FName> Unlocks;
	TestTrue(TEXT("解锁摘要可读"), Fixture.Profile->GetEquipmentUnlockSnapshot(Unlocks));
	TestTrue(TEXT("解锁摘要包含 Grant 授予的 UnlockId"), Unlocks.Contains(TEXT("ProfilePremiumRod")));
	TestEqual(TEXT("非鱼 Grant 不触发图鉴通知"), FishCollectionChangedCount, 2);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
