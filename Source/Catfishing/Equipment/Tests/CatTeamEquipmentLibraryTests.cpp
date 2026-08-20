#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Tests/AutomationCommon.h"

namespace CatTeamEquipmentLibraryTest
{
	/** Equipment 目录覆盖守卫；装备库要从运行目录查定义，所以测试必须先摆出一份最小可运行目录。 */
	struct FCatalogFixture
	{
		/** 被覆盖的 Equipment Settings 默认对象。 */
		UCatEquipmentSettings* Settings = nullptr;

		/** 测试前目录 SchemaVersion；析构恢复。 */
		int32 SavedContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

		/** 测试前目录数据修订；析构恢复。 */
		int64 SavedDataRevision = 0;

		/** 测试前目录来源戳；析构恢复。 */
		FCatDataCatalogSourceStamp SavedSourceStamp;

		/** 测试前正式定义列表；析构恢复。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 测试前 starter 鱼竿 ID；析构恢复。 */
		FName SavedStarterRodDefinitionId = NAME_None;

		/** 测试前 starter 鱼饵 ID；析构恢复。 */
		FName SavedStarterBaitDefinitionId = NAME_None;

		/** 测试前 starter 鱼漂 ID；析构恢复。 */
		FName SavedStarterFloatDefinitionId = NAME_None;

		/** 测试前维修浮木 ID；析构恢复。 */
		FName SavedDriftwoodDefinitionId = NAME_None;

		/** 目录里唯一那条鱼漂的稳定 ID；入库命令引用它。 */
		FName FloatDefinitionId = TEXT("TeamLibraryFloat");

		/** 本测试创建的瞬态鱼漂定义；加根后软引用目录才能稳定解析到测试结束。 */
		TObjectPtr<UCatEquipmentDefinition> FloatDefinition = nullptr;

		/** 构造流程：保存原目录，再换成一份只含一条鱼漂、没有 starter 与维修引用的最小可运行目录。 */
		FCatalogFixture()
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

			FloatDefinition = NewObject<UCatEquipmentDefinition>(
				GetTransientPackage(), TEXT("CatTeamEquipmentLibraryAutomationFloat"));
			if (FloatDefinition)
			{
				FloatDefinition->AddToRoot();
				FloatDefinition->bEnableRuntimeDefinition = true;
				FloatDefinition->EquipmentDefinitionId = FloatDefinitionId;
				FloatDefinition->Kind = ECatEquipmentKind::Float;
				FloatDefinition->LoadoutSlotId = TEXT("Float");
				FloatDefinition->FunctionalRouteId = TEXT("TeamLibraryFloatRoute");
				// 漂的射程与精准度是运行目录的必填项（`IsRuntimeDefinitionReady` 对 Float 两者都要求有限正数）。
				// 本测试只关心团队装备库的入库/取用记账，不关心抛竿几何，所以给一组最小合法值即可；
				// 缺了它们这条定义进不了运行目录，`FindRuntimeDefinition` 会整表返回 nullptr，交付链会以"定义不可用"失败。
				FloatDefinition->FloatCastRangeMeters = 3.0;
				FloatDefinition->FloatAccuracyOffsetRadiusMeters = 0.3;
			}

			Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
			Settings->DataRevision = 1;
			Settings->SourceStamp.SourceKind = TEXT("Automation");
			Settings->SourceStamp.SourceNodeToken = TEXT("CatTeamEquipmentLibraryTest");
			Settings->SourceStamp.SourceRevision = 1;
			Settings->SourceStamp.SourceSliceName = TEXT("TeamEquipmentLibrary");
			Settings->StarterRodDefinitionId = NAME_None;
			Settings->StarterBaitDefinitionId = NAME_None;
			Settings->StarterFloatDefinitionId = NAME_None;
			Settings->DriftwoodDefinitionId = NAME_None;
			Settings->Definitions.Reset();
			if (FloatDefinition)
			{
				Settings->Definitions.Add(FloatDefinition.Get());
			}
		}

		/** 析构流程：先还原目录字段，再解除瞬态定义的根引用。 */
		~FCatalogFixture()
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
			if (FloatDefinition)
			{
				FloatDefinition->RemoveFromRoot();
			}
		}
	};

	/** 入库命令构造流程：填服务器身份、装备库版本前提、来源订单和定义。 */
	static FCatTeamEquipmentGrantCommand MakeGrantCommand(const FString& StableNetId, const int64 ExpectedRevision,
		const FGuid SourceTransactionId, const FName DefinitionId, const FGuid RequestId = FGuid::NewGuid())
	{
		FCatTeamEquipmentGrantCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.SourceTransactionId = SourceTransactionId;
		Command.DefinitionId = DefinitionId;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTeamEquipmentLibraryGrantTest,
	"Catfishing.Unit.Equipment.TeamLibrary.GrantCreatesOneInstancePerShopOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTeamEquipmentLibraryFailClosedTest,
	"Catfishing.Unit.Equipment.TeamLibrary.GrantFailsClosedOnPayloadDefinitionAndClosedCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：同一笔订单只入库一次——第一次真的造出实物并推进版本，之后无论重试同一个 RequestId 还是换新的 RequestId、
// 拿着已经过期的版本前提再打一次，都只能拿回第一件实物，库里不会出现第二件。
bool FCatTeamEquipmentLibraryGrantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatTeamEquipmentLibraryTest::FCatalogFixture Fixture;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建团队装备库测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatTeamEquipmentLibrary* Library = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
		TestNotNull(TEXT("团队装备库可创建"), Library);
		if (Library)
		{
			int32 ChangeCount = 0;
			FDelegateHandle Handle = Library->OnLibraryChanged.AddLambda([&ChangeCount]() { ++ChangeCount; });

			TestEqual(TEXT("开局库是空的"), Library->GetSnapshot().Instances.Num(), 0);
			TestEqual(TEXT("开局版本为 0"), Library->GetSnapshot().Revision, static_cast<int64>(0));

			const FGuid TransactionId = FGuid::NewGuid();
			const FGuid GrantRequestId = FGuid::NewGuid();
			const FCatTeamEquipmentGrantCommand GrantCommand = CatTeamEquipmentLibraryTest::MakeGrantCommand(
				TEXT("PlayerA"), 0, TransactionId, Fixture.FloatDefinitionId, GrantRequestId);
			const FCatTeamEquipmentGrantResult Grant = Library->GrantFromShopOrder(GrantCommand);
			TestTrue(TEXT("首次入库提交成功"), Grant.Command.bCommitted);
			TestEqual(TEXT("首次入库无错误"), Grant.Command.Error, ECatDomainCommandError::None);
			TestTrue(TEXT("首次入库生成实例 ID"), Grant.Instance.InstanceId.IsValid());
			TestEqual(TEXT("实例记住来源订单"), Grant.Instance.SourceTransactionId, TransactionId);
			TestEqual(TEXT("实例从定义冻结类别"), Grant.Instance.Kind, ECatEquipmentKind::Float);
			TestEqual(TEXT("入库推进版本"), Grant.Command.Revision, static_cast<int64>(1));
			TestEqual(TEXT("库里多了一件东西"), Library->GetSnapshot().Instances.Num(), 1);
			TestEqual(TEXT("入库广播一次"), ChangeCount, 1);

			const FCatTeamEquipmentGrantResult Replay = Library->GrantFromShopOrder(GrantCommand);
			TestFalse(TEXT("同 RequestId 重放不再入库"), Replay.Command.bCommitted);
			TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved"), Replay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("同 RequestId 重放拿回同一件实物"), Replay.Instance.InstanceId, Grant.Instance.InstanceId);

			const FCatTeamEquipmentGrantResult SameOrderNewRequest = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 0, TransactionId,
					Fixture.FloatDefinitionId));
			TestFalse(TEXT("同一笔订单换 RequestId 也不会入第二件"), SameOrderNewRequest.Command.bCommitted);
			TestEqual(TEXT("同一笔订单换 RequestId 返回 AlreadyResolved"), SameOrderNewRequest.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("同一笔订单换 RequestId 拿回同一件实物"), SameOrderNewRequest.Instance.InstanceId,
				Grant.Instance.InstanceId);
			TestEqual(TEXT("库里仍然只有一件东西"), Library->GetSnapshot().Instances.Num(), 1);
			TestEqual(TEXT("重放不再广播"), ChangeCount, 1);

			FCatTeamEquipmentInstance Found;
			TestTrue(TEXT("可按订单 ID 找回实物"),
				Library->TryFindInstanceBySourceTransaction(TransactionId, Found));
			TestEqual(TEXT("找回的就是那件实物"), Found.InstanceId, Grant.Instance.InstanceId);
			TestFalse(TEXT("陌生订单 ID 找不到实物"),
				Library->TryFindInstanceBySourceTransaction(FGuid::NewGuid(), Found));

			const FCatTeamEquipmentGrantResult SecondOrder = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerB"), Library->GetSnapshot().Revision,
					FGuid::NewGuid(), Fixture.FloatDefinitionId));
			TestTrue(TEXT("另一笔订单可以入第二件"), SecondOrder.Command.bCommitted);
			TestEqual(TEXT("库里现在有两件东西"), Library->GetSnapshot().Instances.Num(), 2);
			TestEqual(TEXT("第二次入库再广播一次"), ChangeCount, 2);

			Library->OnLibraryChanged.Remove(Handle);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：缺字段、载荷漂移、定义不在运行目录、版本前提陈旧、收摊之后这五种情况都不能入库，
// 而且失败之后库存和版本必须一件不多、一个数不动。
bool FCatTeamEquipmentLibraryFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatTeamEquipmentLibraryTest::FCatalogFixture Fixture;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建团队装备库 fail-closed 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatTeamEquipmentLibrary* Library = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
		TestNotNull(TEXT("fail-closed 测试装备库可创建"), Library);
		if (Library)
		{
			const FCatTeamEquipmentGrantResult NoTransaction = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 0, FGuid(),
					Fixture.FloatDefinitionId));
			TestFalse(TEXT("没有来源订单不能入库"), NoTransaction.Command.bCommitted);
			TestEqual(TEXT("没有来源订单返回无效载荷"), NoTransaction.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			const FCatTeamEquipmentGrantResult NoIdentity = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(FString(), 0, FGuid::NewGuid(),
					Fixture.FloatDefinitionId));
			TestFalse(TEXT("没有服务器身份不能入库"), NoIdentity.Command.bCommitted);
			TestEqual(TEXT("没有服务器身份返回无效载荷"), NoIdentity.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			const FCatTeamEquipmentGrantResult UnknownDefinition = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 0, FGuid::NewGuid(),
					TEXT("NotInRuntimeCatalog")));
			TestFalse(TEXT("运行目录里查不到的定义不能入库"), UnknownDefinition.Command.bCommitted);
			TestEqual(TEXT("查不到定义返回依赖不可用"), UnknownDefinition.Command.Error,
				ECatDomainCommandError::DependencyUnavailable);

			const FGuid DriftRequestId = FGuid::NewGuid();
			const FCatTeamEquipmentGrantResult StaleRevision = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 7, FGuid::NewGuid(),
					Fixture.FloatDefinitionId, DriftRequestId));
			TestFalse(TEXT("陈旧版本前提不能入库"), StaleRevision.Command.bCommitted);
			TestEqual(TEXT("陈旧版本前提返回版本冲突"), StaleRevision.Command.Error,
				ECatDomainCommandError::RevisionConflict);

			const FCatTeamEquipmentGrantResult Drift = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 0, FGuid::NewGuid(),
					Fixture.FloatDefinitionId, DriftRequestId));
			TestFalse(TEXT("同 RequestId 换订单不能入库"), Drift.Command.bCommitted);
			TestEqual(TEXT("同 RequestId 换订单返回无效载荷"), Drift.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			TestEqual(TEXT("全部失败之后库仍然是空的"), Library->GetSnapshot().Instances.Num(), 0);
			TestEqual(TEXT("全部失败之后版本仍然是 0"), Library->GetSnapshot().Revision, static_cast<int64>(0));

			Library->CloseCommands();
			const FCatTeamEquipmentGrantResult Closed = Library->GrantFromShopOrder(
				CatTeamEquipmentLibraryTest::MakeGrantCommand(TEXT("PlayerA"), 0, FGuid::NewGuid(),
					Fixture.FloatDefinitionId));
			TestFalse(TEXT("收摊后不能入库"), Closed.Command.bCommitted);
			TestEqual(TEXT("收摊后返回 CommandsClosed"), Closed.Command.Error,
				ECatDomainCommandError::CommandsClosed);
			TestEqual(TEXT("收摊后库仍然是空的"), Library->GetSnapshot().Instances.Num(), 0);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
