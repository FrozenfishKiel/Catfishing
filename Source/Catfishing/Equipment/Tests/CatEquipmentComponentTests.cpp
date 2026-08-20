#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentDefaultConsumableGateTest,
	"Catfishing.Unit.Equipment.Component.DefaultCatalogRejectsConsumableGrantWithoutMutatingSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCatEquipmentComponentStarterLoadoutDefaultFailClosedTest,
    "Catfishing.Unit.Equipment.Component.StarterLoadoutDefaultConfigFailsClosedWithoutMutation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentFishingFailureNoneTest,
	"Catfishing.Unit.Equipment.Component.FishingFailureNoneIsIdempotentAndDoesNotPunish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentFightRodDurabilityNoLoadoutTest,
	"Catfishing.Unit.Equipment.Component.FightRodDurabilityRequiresEquippedRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentConsumableStackCapacityTest,
	"Catfishing.Unit.Equipment.Component.RunConsumableGrantStopsAtConfiguredStackCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentTeamLibraryEquipGateTest,
	"Catfishing.Unit.Equipment.Component.TeamLibraryEquipRequiresCompleteLoadoutAndLoadoutKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentRunConsumableReservationTest,
	"Catfishing.Unit.Equipment.Component.RunConsumableReservationHoldsReleasesAndCommits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatEquipmentComponentTest
{
	// 测试辅助流程：本命名空间只放默认配置覆盖守卫和组件创建函数，不进入运行代码。
    /** Starter 默认配置覆盖守卫；测试期间清空默认对象的 starter 字段，析构时还原，避免项目配置变化污染 fail-closed 断言。 */
    struct FStarterSettingsOverride
    {
        /** 被临时覆盖的全局 Equipment Settings 默认对象；只在本测试生命周期内写入。 */
        UCatEquipmentSettings* Settings = nullptr;

        /** 测试开始前的 starter 鱼竿定义 ID；析构时按原值恢复。 */
        FName SavedStarterRodDefinitionId = NAME_None;

        /** 测试开始前的 starter 鱼饵定义 ID；析构时按原值恢复。 */
        FName SavedStarterBaitDefinitionId = NAME_None;

        /** 测试开始前的 starter 鱼漂定义 ID；析构时按原值恢复。 */
        FName SavedStarterFloatDefinitionId = NAME_None;

        /** 构造流程：捕获当前默认配置后清空 starter 三件套，让测试只观察无配置时的组件行为。 */
        FStarterSettingsOverride()
        {
            Settings = GetMutableDefault<UCatEquipmentSettings>();
            if (Settings)
            {
                SavedStarterRodDefinitionId = Settings->StarterRodDefinitionId;
                SavedStarterBaitDefinitionId = Settings->StarterBaitDefinitionId;
                SavedStarterFloatDefinitionId = Settings->StarterFloatDefinitionId;
                Settings->StarterRodDefinitionId = NAME_None;
                Settings->StarterBaitDefinitionId = NAME_None;
                Settings->StarterFloatDefinitionId = NAME_None;
            }
        }

        /** 析构流程：恢复测试前默认配置，避免后续 Equipment/Data 测试继承临时空 starter。 */
        ~FStarterSettingsOverride()
        {
            if (Settings)
            {
                Settings->StarterRodDefinitionId = SavedStarterRodDefinitionId;
                Settings->StarterBaitDefinitionId = SavedStarterBaitDefinitionId;
                Settings->StarterFloatDefinitionId = SavedStarterFloatDefinitionId;
            }
        }
    };

	/** 一局耗材目录覆盖守卫；测试期间只暴露一条正式 Chum 定义，析构时恢复默认目录并释放瞬态资产根引用。 */
	struct FRunConsumableSettingsOverride
	{
		/** 被临时覆盖的全局 Equipment Settings 默认对象；只在本测试生命周期内写目录字段。 */
		UCatEquipmentSettings* Settings = nullptr;

		/** 测试开始前的目录 SchemaVersion；析构时恢复，避免影响后续目录校验测试。 */
		int32 SavedContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

		/** 测试开始前的目录数据修订；析构时恢复到项目配置看到的原值。 */
		int64 SavedDataRevision = 0;

		/** 测试开始前的目录来源戳；析构时恢复，避免把自动化来源戳泄漏给其他测试。 */
		FCatDataCatalogSourceStamp SavedSourceStamp;

		/** 测试开始前的正式定义列表；析构时整体放回默认对象。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 测试开始前的 starter 鱼竿 ID；析构时恢复，避免单条测试目录被项目默认 starter 引用拖失败。 */
		FName SavedStarterRodDefinitionId = NAME_None;

		/** 测试开始前的 starter 鱼饵 ID；析构时恢复，保持默认对象只在本用例生命周期内被改写。 */
		FName SavedStarterBaitDefinitionId = NAME_None;

		/** 测试开始前的 starter 鱼漂 ID；析构时恢复，避免后续 starter 用例继承临时空值。 */
		FName SavedStarterFloatDefinitionId = NAME_None;

		/** 测试开始前的维修漂木 ID；析构时恢复，避免 Chum-only 目录因 repair 引用失败。 */
		FName SavedDriftwoodDefinitionId = NAME_None;

		/** 本测试创建的 Chum 稳定 ID；公开命令只通过这个 ID 读取临时目录定义。 */
		FName ChumDefinitionId = TEXT("ReservationChum");

		/** 测试创建的一条瞬态 Chum 定义；加根后可被软引用目录稳定解析到测试结束。 */
		TObjectPtr<UCatEquipmentDefinition> ChumDefinition = nullptr;

		/** 构造流程：保存当前目录，再创建一条运行时可用的 Chum 定义并替换目录，让耗材命令通过真实公开查表路径运行。 */
		FRunConsumableSettingsOverride()
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			if (!Settings)
			{
				return;
			}

			SavedContentSchemaVersion = Settings->ContentSchemaVersion;
			SavedDataRevision = Settings->DataRevision;
			SavedSourceStamp = Settings->SourceStamp;
			SavedDefinitions = Settings->Definitions;
			SavedStarterRodDefinitionId = Settings->StarterRodDefinitionId;
			SavedStarterBaitDefinitionId = Settings->StarterBaitDefinitionId;
			SavedStarterFloatDefinitionId = Settings->StarterFloatDefinitionId;
			SavedDriftwoodDefinitionId = Settings->DriftwoodDefinitionId;

			ChumDefinition = NewObject<UCatEquipmentDefinition>(
				GetTransientPackage(), TEXT("CatEquipmentReservationAutomationChum"));
			if (ChumDefinition)
			{
				ChumDefinition->AddToRoot();
				ChumDefinition->bEnableRuntimeDefinition = true;
				ChumDefinition->EquipmentDefinitionId = ChumDefinitionId;
				ChumDefinition->Kind = ECatEquipmentKind::Chum;
				ChumDefinition->bRunConsumable = true;
				ChumDefinition->FunctionalRouteId = TEXT("ReservationChumRoute");
				ChumDefinition->ChumContribution.Fishy = 1.0;
			}

			Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
			Settings->DataRevision = 1;
			Settings->SourceStamp.SourceKind = TEXT("Automation");
			Settings->SourceStamp.SourceNodeToken = TEXT("CatEquipmentReservationTest");
			Settings->SourceStamp.SourceRevision = 1;
			Settings->SourceStamp.SourceSliceName = TEXT("RunConsumableReservation");
			Settings->StarterRodDefinitionId = NAME_None;
			Settings->StarterBaitDefinitionId = NAME_None;
			Settings->StarterFloatDefinitionId = NAME_None;
			Settings->DriftwoodDefinitionId = NAME_None;
			Settings->Definitions.Reset();
			if (ChumDefinition)
			{
				Settings->Definitions.Add(ChumDefinition.Get());
			}
		}

		/** 析构流程：先恢复默认目录字段，再解除瞬态定义根引用，确保其他 Equipment 自动化不会看到本测试目录。 */
		~FRunConsumableSettingsOverride()
		{
			if (Settings)
			{
				Settings->ContentSchemaVersion = SavedContentSchemaVersion;
				Settings->DataRevision = SavedDataRevision;
				Settings->SourceStamp = SavedSourceStamp;
				Settings->Definitions = SavedDefinitions;
				Settings->StarterRodDefinitionId = SavedStarterRodDefinitionId;
				Settings->StarterBaitDefinitionId = SavedStarterBaitDefinitionId;
				Settings->StarterFloatDefinitionId = SavedStarterFloatDefinitionId;
				Settings->DriftwoodDefinitionId = SavedDriftwoodDefinitionId;
			}
			if (ChumDefinition)
			{
				ChumDefinition->RemoveFromRoot();
			}
		}
	};

	/** 读取公开 Snapshot 中某个耗材的数量；测试只用它观察命令效果，不访问组件内部预留表。 */
	static int32 GetConsumableQuantity(const FCatEquipmentLoadoutSnapshot& Snapshot, const FName DefinitionId)
	{
		const FCatRunConsumableStack* Stack = Snapshot.Consumables.FindByPredicate(
			[DefinitionId](const FCatRunConsumableStack& Candidate)
			{
				return Candidate.DefinitionId == DefinitionId;
			});
		return Stack ? Stack->Quantity : 0;
	}
	// 组件装配流程：用普通 authority Actor 承载 Equipment 组件；测试只通过组件公开命令和 Snapshot 观察行为。
	static UCatEquipmentComponent* AddEquipmentComponent(AActor* Host)
	{
		UCatEquipmentComponent* Component = Host ? NewObject<UCatEquipmentComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}
}

// 测试流程：默认装备目录为空时提交耗材授予；Result 必须拒绝，Snapshot Revision、耗材数组和装配字段保持初始值。
bool FCatEquipmentComponentDefaultConsumableGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 默认目录测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("Equipment 默认目录测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Equipment 默认目录测试组件可创建"), Component);
		if (Component)
		{
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatDomainCommandResult Result = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Before.Revision, TEXT("MissingConsumable"), 1);
			TestFalse(TEXT("默认目录下耗材授予不提交"), Result.bCommitted);
			TestEqual(TEXT("默认目录下耗材授予返回 InvalidPayload"), Result.Error, ECatDomainCommandError::InvalidPayload);
			const FCatEquipmentLoadoutSnapshot& After = Component->GetSnapshot();
			TestEqual(TEXT("拒绝后 Equipment Revision 不变"), After.Revision, Before.Revision);
			TestEqual(TEXT("拒绝后耗材数组仍为空"), After.Consumables.Num(), Before.Consumables.Num());
			TestEqual(TEXT("拒绝后 Rod 仍为空"), After.RodDefinitionId, Before.RodDefinitionId);
			TestEqual(TEXT("拒绝后 Bait 仍为空"), After.BaitDefinitionId, Before.BaitDefinitionId);
			TestEqual(TEXT("拒绝后 Float 仍为空"), After.FloatDefinitionId, Before.FloatDefinitionId);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：清空 starter 默认配置后触发组件初始装配入口；命令必须 fail-closed，且不写三槽、耐久、耗材或 Revision。
bool FCatEquipmentComponentStarterLoadoutDefaultFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatEquipmentComponentTest::FStarterSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment starter 默认配置测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("starter 默认配置测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("starter 默认配置测试组件可创建"), Component);
		if (Component)
		{
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatDomainCommandResult Result = Component->ConfigureStarterLoadoutFromAuthority(FGuid::NewGuid());
			TestFalse(TEXT("无 starter 配置时初始装配不提交"), Result.bCommitted);
			TestEqual(TEXT("无 starter 配置时返回 PolicyUndecided"), Result.Error, ECatDomainCommandError::PolicyUndecided);
			const FCatEquipmentLoadoutSnapshot& After = Component->GetSnapshot();
			TestEqual(TEXT("拒绝后 Equipment Revision 不变"), After.Revision, Before.Revision);
			TestEqual(TEXT("拒绝后 Rod 仍为空"), After.RodDefinitionId, NAME_None);
			TestEqual(TEXT("拒绝后 Bait 仍为空"), After.BaitDefinitionId, NAME_None);
			TestEqual(TEXT("拒绝后 Float 仍为空"), After.FloatDefinitionId, NAME_None);
			TestEqual(TEXT("拒绝后耐久仍为 0"), After.RodDurability, 0.0);
			TestEqual(TEXT("拒绝后耗材数组仍为空"), After.Consumables.Num(), 0);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：用 Penalty=None 提交一次失败预算后分别验证原样重放与同 RequestId 载荷漂移；只有同一 Penalty/Revision 才能读取首次终态。
bool FCatEquipmentComponentFishingFailureNoneTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 失败预算测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("失败预算测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("失败预算测试组件可创建"), Component);
		if (Component)
		{
			const FGuid RequestId = FGuid::NewGuid();
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatFishingFailureResult First = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::None);
			TestTrue(TEXT("None 失败预算首次提交成功"), First.Command.bCommitted);
			TestEqual(TEXT("None 失败预算无错误"), First.Command.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("None 失败预算不推进 Revision"), First.Command.Revision, Before.Revision);
			TestEqual(TEXT("None 失败预算保留惩罚类别"), First.Penalty, ECatFishingFailurePenalty::None);
			TestEqual(TEXT("None 失败预算不改变耐久"), Component->GetSnapshot().RodDurability, Before.RodDurability);
			TestEqual(TEXT("None 失败预算不创建耗材"), Component->GetSnapshot().Consumables.Num(), 0);

			const FCatFishingFailureResult Replay = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::None);
			TestFalse(TEXT("失败预算原样重放不再次提交"), Replay.Command.bCommitted);
			TestEqual(TEXT("失败预算原样重放返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("失败预算原样重放保留首次 None 惩罚"), Replay.Penalty, ECatFishingFailurePenalty::None);
			TestEqual(TEXT("失败预算原样重放不推进 Revision"), Replay.Command.Revision, First.Command.Revision);

			const FCatFishingFailureResult Drift = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::DamageRod);
			TestFalse(TEXT("失败预算同 RequestId 漂移不提交"), Drift.Command.bCommitted);
			TestEqual(TEXT("失败预算同 RequestId 漂移返回 InvalidPayload"), Drift.Command.Error, ECatDomainCommandError::InvalidPayload);

			const FCatFishingFailureResult DamageRod = Component->CommitFishingFailure(
				FGuid::NewGuid(), Before.Revision, ECatFishingFailurePenalty::DamageRod);
			TestFalse(TEXT("旧 DamageRod 失败预算不再提交"), DamageRod.Command.bCommitted);
			TestEqual(TEXT("旧 DamageRod 返回 PolicyUndecided"), DamageRod.Command.Error, ECatDomainCommandError::PolicyUndecided);
			TestEqual(TEXT("旧 DamageRod 不推进 Revision"), DamageRod.Command.Revision, First.Command.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：Fight cursor 的耐久写口没有当前鱼竿时必须 fail-closed；同 RequestId 只能原样重放同一成本，不能换成本重新解释。
bool FCatEquipmentComponentFightRodDurabilityNoLoadoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment Fight 耐久测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("Fight 耐久测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Fight 耐久测试组件可创建"), Component);
		if (Component)
		{
			const FGuid RequestId = FGuid::NewGuid();
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatDomainCommandResult Result = Component->CommitFightRodDurabilityFromAuthority(
				RequestId, Before.Revision, 1.0);
			TestFalse(TEXT("无鱼竿时 Fight 耐久写口不提交"), Result.bCommitted);
			TestEqual(TEXT("无鱼竿时返回 PolicyUndecided"), Result.Error, ECatDomainCommandError::PolicyUndecided);
			TestEqual(TEXT("无鱼竿时 Revision 不变"), Result.Revision, Before.Revision);

			const FCatDomainCommandResult Drift = Component->CommitFightRodDurabilityFromAuthority(
				RequestId, Before.Revision, 2.0);
			TestFalse(TEXT("同 RequestId Fight 耐久成本漂移不提交"), Drift.bCommitted);
			TestEqual(TEXT("同 RequestId Fight 耐久成本漂移返回 InvalidPayload"), Drift.Error, ECatDomainCommandError::InvalidPayload);

			const FCatDomainCommandResult Replay = Component->CommitFightRodDurabilityFromAuthority(
				RequestId, Before.Revision, 1.0);
			TestFalse(TEXT("原 Fight 耐久成本仍可幂等重放"), Replay.bCommitted);
			TestEqual(TEXT("原 Fight 耐久成本重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("原 Fight 耐久成本重放不推进 Revision"), Replay.Revision, Result.Revision);
			TestEqual(TEXT("漂移与重放后耐久仍为 0"), Component->GetSnapshot().RodDurability, Before.RodDurability);
			TestFalse(TEXT("漂移与重放后不会伪造断竿"), Component->GetSnapshot().bRodBroken);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：把随身携带上限临时设成 2，授予 1 份、再授予 1 份到顶、第三份必须以 CapacityExceeded 整笔拒绝且不动数量与 Revision；
// 再把上限设回 0（不设限）确认同一栈还能继续累加。上限值本身是飞书"暂定 5"的配置，这里只验"超出就拒、0 不设限"的判定，不验具体数字。
bool FCatEquipmentComponentConsumableStackCapacityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatEquipmentComponentTest::FRunConsumableSettingsOverride SettingsOverride;
	UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
	const int32 SavedCapacity = Settings ? Settings->RunConsumableStackCapacity : 0;
	if (Settings)
	{
		Settings->RunConsumableStackCapacity = 2;
	}
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 耗材上限测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("耗材上限测试组件可创建"), Component);
		if (Component && Settings)
		{
			const FName ChumDefinitionId = SettingsOverride.ChumDefinitionId;
			const FCatDomainCommandResult First = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, ChumDefinitionId, 1);
			TestTrue(TEXT("上限 2 时第一份授予成功"), First.bCommitted);
			const FCatDomainCommandResult Second = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, ChumDefinitionId, 1);
			TestTrue(TEXT("上限 2 时第二份授予成功（正好到顶）"), Second.bCommitted);
			const int64 RevisionAtCap = Component->GetSnapshot().Revision;
			const FCatDomainCommandResult Third = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), RevisionAtCap, ChumDefinitionId, 1);
			TestFalse(TEXT("超出上限的第三份不提交"), Third.bCommitted);
			TestEqual(TEXT("超出上限返回 CapacityExceeded"), Third.Error, ECatDomainCommandError::CapacityExceeded);
			TestEqual(TEXT("被拒后数量仍是 2"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 2);
			TestEqual(TEXT("被拒不推进 Revision"), Component->GetSnapshot().Revision, RevisionAtCap);
			const FCatDomainCommandResult Bulk = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), RevisionAtCap, ChumDefinitionId, 3);
			TestEqual(TEXT("一次授予多份同样按合计判上限"), Bulk.Error, ECatDomainCommandError::CapacityExceeded);

			Settings->RunConsumableStackCapacity = 0;
			const FCatDomainCommandResult Unlimited = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), RevisionAtCap, ChumDefinitionId, 3);
			TestTrue(TEXT("上限 0 表示不设限，可以继续累加"), Unlimited.bCommitted);
			TestEqual(TEXT("不设限后数量为 5"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 5);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	if (Settings)
	{
		Settings->RunConsumableStackCapacity = SavedCapacity;
	}
	return !HasAnyErrors();
}

// 测试流程：在只有 Chum 定义的目录下，用一件指向 Chum 的"实例"请求取用装配，必须因为类别不是 Rod/Bait/Float 而 InvalidPayload；
// 再用一件指向不存在定义的实例请求，同样 InvalidPayload；两次都不动 Snapshot。三件套已配齐时的正向换装由 Framework 的
// ShopOrdersDeliverByKindAndLibraryTakeEquipsRod 用真实项目目录覆盖，这里只锁 Equipment 侧的拒绝边界。
bool FCatEquipmentComponentTeamLibraryEquipGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatEquipmentComponentTest::FRunConsumableSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 取用装配 gate 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("取用装配 gate 测试组件可创建"), Component);
		if (Component)
		{
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			FCatTeamEquipmentInstance ChumInstance;
			ChumInstance.InstanceId = FGuid::NewGuid();
			ChumInstance.DefinitionId = SettingsOverride.ChumDefinitionId;
			ChumInstance.Kind = ECatEquipmentKind::Chum;
			const FCatDomainCommandResult ChumEquip = Component->EquipFromTeamLibraryFromAuthority(
				FGuid::NewGuid(), Before.Revision, ChumInstance);
			TestFalse(TEXT("非装配类别的实例不能装到槽位上"), ChumEquip.bCommitted);
			TestEqual(TEXT("非装配类别返回 InvalidPayload"), ChumEquip.Error, ECatDomainCommandError::InvalidPayload);

			FCatTeamEquipmentInstance GhostInstance;
			GhostInstance.InstanceId = FGuid::NewGuid();
			GhostInstance.DefinitionId = TEXT("NotInCatalogRod");
			GhostInstance.Kind = ECatEquipmentKind::Rod;
			const FCatDomainCommandResult GhostEquip = Component->EquipFromTeamLibraryFromAuthority(
				FGuid::NewGuid(), Before.Revision, GhostInstance);
			TestFalse(TEXT("定义不在运行目录里的实例不能装上"), GhostEquip.bCommitted);
			TestEqual(TEXT("定义缺失返回 InvalidPayload"), GhostEquip.Error, ECatDomainCommandError::InvalidPayload);

			const FCatEquipmentLoadoutSnapshot& After = Component->GetSnapshot();
			TestEqual(TEXT("拒绝后 Revision 不变"), After.Revision, Before.Revision);
			TestEqual(TEXT("拒绝后 Rod 仍为空"), After.RodDefinitionId, NAME_None);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：通过公开耗材命令验证预留不会提前扣库存，第二个请求会被占位挡住；释放后可重新预留，提交后才扣数量并推进 Revision。
bool FCatEquipmentComponentRunConsumableReservationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatEquipmentComponentTest::FRunConsumableSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 耗材预留测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("耗材预留测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("耗材预留测试组件可创建"), Component);
		if (Component)
		{
			const FName ChumDefinitionId = SettingsOverride.ChumDefinitionId;
			const FCatDomainCommandResult Grant = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, ChumDefinitionId, 1);
			TestTrue(TEXT("测试 Chum 耗材授予成功"), Grant.bCommitted);
			TestEqual(TEXT("授予后 Equipment Revision 推进到 1"), Grant.Revision, static_cast<int64>(1));
			TestEqual(TEXT("授予后公开数量为 1"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 1);

			const FGuid FirstReserveId = FGuid::NewGuid();
			const FCatDomainCommandResult FirstReserve = Component->ReserveRunConsumableFromAuthority(
				FirstReserveId, Grant.Revision, ChumDefinitionId);
			TestTrue(TEXT("第一份 Chum 预留成功"), FirstReserve.bCommitted);
			TestEqual(TEXT("预留不推进 Equipment Revision"), FirstReserve.Revision, Grant.Revision);
			TestEqual(TEXT("预留不提前扣公开数量"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 1);

			const FGuid BlockedReserveId = FGuid::NewGuid();
			const FCatDomainCommandResult BlockedReserve = Component->ReserveRunConsumableFromAuthority(
				BlockedReserveId, Grant.Revision, ChumDefinitionId);
			TestFalse(TEXT("同一份库存被占住后第二个预留不提交"), BlockedReserve.bCommitted);
			TestEqual(TEXT("第二个预留返回容量不足"), BlockedReserve.Error, ECatDomainCommandError::CapacityExceeded);
			TestEqual(TEXT("被挡住的预留不改变公开数量"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 1);

			const FCatDomainCommandResult Release = Component->ReleaseRunConsumableReservationFromAuthority(
				FirstReserveId, Grant.Revision, ChumDefinitionId);
			TestTrue(TEXT("外部领域拒绝后可释放预留"), Release.bCommitted);
			TestEqual(TEXT("释放不推进 Equipment Revision"), Release.Revision, Grant.Revision);

			const FCatDomainCommandResult ReleasedCommit = Component->CommitReservedRunConsumableFromAuthority(
				FirstReserveId, Grant.Revision, ChumDefinitionId);
			TestFalse(TEXT("已释放的预留不能再提交消耗"), ReleasedCommit.bCommitted);
			TestEqual(TEXT("已释放预留提交返回 InvalidPayload"), ReleasedCommit.Error, ECatDomainCommandError::InvalidPayload);

			const FGuid FinalReserveId = FGuid::NewGuid();
			const FCatDomainCommandResult FinalReserve = Component->ReserveRunConsumableFromAuthority(
				FinalReserveId, Grant.Revision, ChumDefinitionId);
			TestTrue(TEXT("释放后新请求可重新占住库存"), FinalReserve.bCommitted);

			const FCatDomainCommandResult Commit = Component->CommitReservedRunConsumableFromAuthority(
				FinalReserveId, Grant.Revision, ChumDefinitionId);
			TestTrue(TEXT("领域成功后预留可提交成真实消耗"), Commit.bCommitted);
			TestEqual(TEXT("提交消耗后 Revision 推进到 2"), Commit.Revision, static_cast<int64>(2));
			TestEqual(TEXT("提交消耗后公开数量归零"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 0);

			const FCatDomainCommandResult CommitReplay = Component->CommitReservedRunConsumableFromAuthority(
				FinalReserveId, Grant.Revision, ChumDefinitionId);
			TestFalse(TEXT("已提交消耗的 RequestId 重放不再次扣库存"), CommitReplay.bCommitted);
			TestEqual(TEXT("已提交消耗重放返回 AlreadyResolved"), CommitReplay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重放后 Revision 保持提交结果"), CommitReplay.Revision, Commit.Revision);
			TestEqual(TEXT("重放后公开数量仍为 0"),
				CatEquipmentComponentTest::GetConsumableQuantity(Component->GetSnapshot(), ChumDefinitionId), 0);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
