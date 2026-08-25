#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Pawn.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace CatTeamEquipmentTakeFlowTest
{
	/** 测试用基础竿定义 ID；它只用于初始化个人三件套，不进入团队库。 */
	static const FName RodId(TEXT("TeamTakeRod"));

	/** 测试用基础饵定义 ID；它只用于让 Equipment 进入完整装配状态。 */
	static const FName BaitId(TEXT("TeamTakeBait"));

	/** 测试用初始鱼漂定义 ID；成功取用后应被团队库实例替换。 */
	static const FName InitialFloatId(TEXT("TeamTakeInitialFloat"));

	/** 测试用团队库鱼漂定义 ID；它代表被购买后放入公库、随后取走装备的实例。 */
	static const FName LibraryFloatId(TEXT("TeamTakeLibraryFloat"));

	/** 定义构造流程：生成最小可运行的 transient EquipmentDefinition，并按类别补齐 runtime gate 需要的字段。 */
	static UCatEquipmentDefinition* MakeDefinition(const FName Id, const ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = Id;
		Definition->Kind = Kind;
		Definition->FunctionalRouteId = *FString::Printf(TEXT("%sRoute"), *Id.ToString());
		Definition->RequiredUnlockId = NAME_None;
		if (Kind == ECatEquipmentKind::Rod || Kind == ECatEquipmentKind::Bait || Kind == ECatEquipmentKind::Float)
		{
			Definition->LoadoutSlotId = *FString::Printf(TEXT("%sSlot"), *Id.ToString());
		}
		if (Kind == ECatEquipmentKind::Rod)
		{
			Definition->MaximumRodDurability = 100.0;
			Definition->FishingStrength = 1.0;
			Definition->MaximumLineLengthCentimeters = 2000.0;
			Definition->HighTensionWearMultiplier = 1.0;
		}
		else if (Kind == ECatEquipmentKind::Bait)
		{
			Definition->BiteRateMultiplier = 1.0;
			Definition->MinimumBiteDelayMultiplier = 1.0;
		}
		else if (Kind == ECatEquipmentKind::Float)
		{
			Definition->MaximumCastDistanceCentimeters = 1500.0;
			Definition->CastErrorStandardDeviationCentimeters = 10.0;
			Definition->MaximumCastErrorRadiusCentimeters = 30.0;
			Definition->BiteSignalStability = 1.0;
		}
		return Definition;
	}

	/** 团队取用链测试夹具；它临时覆盖装备目录，创建 World、团队库、Pawn、PlayerState 和 EquipmentComponent。 */
	struct FTakeFlowFixture
	{
		/** 被覆盖的装备设置默认对象；夹具生命周期结束时恢复。 */
		UCatEquipmentSettings* Settings = nullptr;

		/** 测试前装备定义清单；析构恢复，避免影响其他装备测试。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 测试前装配信任策略；本用例需要 Enabled 让 starter 三件套可配置。 */
		ECatDomainPolicy SavedTrustPolicy = ECatDomainPolicy::Unset;

		/** 持有 transient 定义，保证 Settings 的软引用在测试期间可解析。 */
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> RuntimeDefinitions;

		/** 测试 World 包装器；Subsystem、Pawn 和 PlayerState 都在它的 Game World 中创建。 */
		FTestWorldWrapper WorldWrapper;

		/** 承载个人 EquipmentComponent 的 authority Pawn。 */
		APawn* Pawn = nullptr;

		/** 提供 starter 解锁证明的 PlayerState；None UnlockId 在当前工程视为已授权。 */
		ACatfishingPlayerState* PlayerState = nullptr;

		/** 本人一局装备聚合；测试通过公开命令初始化和装备团队库实例。 */
		UCatEquipmentComponent* Equipment = nullptr;

		/** 当前 World 的团队装备库 Subsystem；测试通过它入库、预检和取走实例。 */
		UCatTeamEquipmentLibrary* Library = nullptr;

		/** 创建流程：先覆盖装备目录，再创建 World 和组件，最后配置基础三件套，确保取用预检有完整装配前提。 */
		bool Create(FAutomationTestBase& Test)
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			if (!Settings)
			{
				return false;
			}
			SavedDefinitions = Settings->Definitions;
			SavedTrustPolicy = Settings->ProfileLoadoutTrustPolicy;
			RuntimeDefinitions.Reset();
			RuntimeDefinitions.Emplace(MakeDefinition(RodId, ECatEquipmentKind::Rod));
			RuntimeDefinitions.Emplace(MakeDefinition(BaitId, ECatEquipmentKind::Bait));
			RuntimeDefinitions.Emplace(MakeDefinition(InitialFloatId, ECatEquipmentKind::Float));
			RuntimeDefinitions.Emplace(MakeDefinition(LibraryFloatId, ECatEquipmentKind::Float));
			Settings->Definitions = {
				RuntimeDefinitions[0].Get(),
				RuntimeDefinitions[1].Get(),
				RuntimeDefinitions[2].Get(),
				RuntimeDefinitions[3].Get()
			};
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;

			if (!Test.TestTrue(TEXT("创建团队取用链测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
			{
				return false;
			}
			UWorld* World = WorldWrapper.GetTestWorld();
			Library = World ? World->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
			Pawn = World ? World->SpawnActor<APawn>() : nullptr;
			PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
			Test.TestNotNull(TEXT("团队取用链 Library 可创建"), Library);
			Test.TestNotNull(TEXT("团队取用链 Pawn 可创建"), Pawn);
			Test.TestNotNull(TEXT("团队取用链 PlayerState 可创建"), PlayerState);
			if (!Library || !Pawn || !PlayerState)
			{
				return false;
			}

			Pawn->SetPlayerState(PlayerState);
			Equipment = NewObject<UCatEquipmentComponent>(Pawn);
			Pawn->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
			Test.TestNotNull(TEXT("团队取用链 EquipmentComponent 可创建"), Equipment);
			if (!Equipment)
			{
				return false;
			}

			const FCatDomainCommandResult Configure = Equipment->ConfigureLoadoutFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, RodId, BaitId, InitialFloatId);
			Test.TestTrue(TEXT("团队取用链基础三件套配置成功"), Configure.bCommitted);
			return Configure.bCommitted;
		}

		/** 入库流程：模拟商店已付款交付，把测试鱼漂实例放入团队装备库并返回首次终态。 */
		FCatTeamEquipmentGrantResult GrantLibraryFloat()
		{
			FCatTeamEquipmentGrantCommand Grant;
			Grant.Context.RequestId = FGuid::NewGuid();
			Grant.Context.ExpectedRevision = Library ? Library->GetSnapshot().Revision : 0;
			Grant.Context.StableNetId = TEXT("TeamTakePlayer");
			Grant.SourceTransactionId = FGuid::NewGuid();
			Grant.DefinitionId = LibraryFloatId;
			return Library ? Library->GrantFromShopOrder(Grant) : FCatTeamEquipmentGrantResult();
		}

		/** 析构流程：恢复装备设置默认对象，transient 定义随后释放，不进入项目配置或资产。 */
		~FTakeFlowFixture()
		{
			if (Settings)
			{
				Settings->Definitions = SavedDefinitions;
				Settings->ProfileLoadoutTrustPolicy = SavedTrustPolicy;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTeamEquipmentLibraryAdapterLifecycleTest,
	"Catfishing.Unit.Equipment.TeamLibrary.GrantTakeReplayAndClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTeamEquipmentTakeFlowPrecheckOrderTest,
	"Catfishing.Unit.Equipment.TeamLibrary.TakeFlowPrecheckProtectsLibraryBeforeEquip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatTeamEquipmentLibraryAdapterLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建团队装备库测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UCatTeamEquipmentLibrary* Library = WorldWrapper.GetTestWorld()
		? WorldWrapper.GetTestWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	TestNotNull(TEXT("服务器团队装备库可创建"), Library);
	if (!Library)
	{
		return false;
	}

	FCatTeamEquipmentGrantCommand Grant;
	Grant.Context.RequestId = FGuid::NewGuid();
	Grant.Context.ExpectedRevision = 0;
	Grant.Context.StableNetId = TEXT("TeamLibraryPlayer");
	Grant.SourceTransactionId = FGuid::NewGuid();
	Grant.DefinitionId = TEXT("Rod_Basic");
	const FCatTeamEquipmentGrantResult Granted = Library->GrantFromShopOrder(Grant);
	TestTrue(TEXT("本地基础竿首次入库成功"), Granted.Command.bCommitted);
	TestEqual(TEXT("首次入库无错误"), Granted.Command.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("装备实例 ID 由服务器创建"), Granted.Instance.InstanceId.IsValid());
	TestEqual(TEXT("入库后版本推进"), Library->GetSnapshot().Revision, static_cast<int64>(1));
	TestEqual(TEXT("库里只有一件实物"), Library->GetSnapshot().Instances.Num(), 1);

	const FCatTeamEquipmentGrantResult GrantReplay = Library->GrantFromShopOrder(Grant);
	TestFalse(TEXT("相同入库命令重放不再创建"), GrantReplay.Command.bCommitted);
	TestEqual(TEXT("入库重放返回 AlreadyResolved"), GrantReplay.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("入库重放返回同一实例"), GrantReplay.Instance.InstanceId,
		Granted.Instance.InstanceId);
	TestEqual(TEXT("重放后仍只有一件"), Library->GetSnapshot().Instances.Num(), 1);

	FCatTeamEquipmentTakeCommand Take;
	Take.Context.RequestId = FGuid::NewGuid();
	Take.Context.ExpectedRevision = 1;
	Take.Context.StableNetId = TEXT("TeamLibraryPlayer");
	Take.InstanceId = Granted.Instance.InstanceId;
	FCatTeamEquipmentInstance Preview;
	TestEqual(TEXT("取用预检通过"), Library->ValidateTake(Take, Preview),
		ECatDomainCommandError::None);
	TestEqual(TEXT("预检返回目标实例"), Preview.InstanceId, Granted.Instance.InstanceId);

	const FCatTeamEquipmentGrantResult Taken = Library->TakeInstance(Take);
	TestTrue(TEXT("首次取用提交成功"), Taken.Command.bCommitted);
	TestEqual(TEXT("取用后版本推进"), Library->GetSnapshot().Revision, static_cast<int64>(2));
	TestTrue(TEXT("取用后公库为空"), Library->GetSnapshot().Instances.IsEmpty());

	const FCatTeamEquipmentGrantResult TakeReplay = Library->TakeInstance(Take);
	TestFalse(TEXT("取用重放不再删除"), TakeReplay.Command.bCommitted);
	TestEqual(TEXT("取用重放返回 AlreadyResolved"), TakeReplay.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("取用重放仍返回同一实例"), TakeReplay.Instance.InstanceId,
		Granted.Instance.InstanceId);

	Library->CloseCommands();
	FCatTeamEquipmentGrantCommand ClosedGrant = Grant;
	ClosedGrant.Context.RequestId = FGuid::NewGuid();
	ClosedGrant.Context.ExpectedRevision = Library->GetSnapshot().Revision;
	ClosedGrant.SourceTransactionId = FGuid::NewGuid();
	const FCatTeamEquipmentGrantResult Closed = Library->GrantFromShopOrder(ClosedGrant);
	TestEqual(TEXT("关门后新订单被拒绝"), Closed.Command.Error,
		ECatDomainCommandError::CommandsClosed);
	return !HasAnyErrors();
}

// 测试流程：先让 Equipment 预检因陈旧 Revision 失败并确认团队库实例仍在；随后按新顺序先 TakeInstance 再 Equip，确认公库消失且个人鱼漂改变。
bool FCatTeamEquipmentTakeFlowPrecheckOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatTeamEquipmentTakeFlowTest;

	FTakeFlowFixture Fixture;
	if (!Fixture.Create(*this))
	{
		return false;
	}

	const FCatTeamEquipmentGrantResult Granted = Fixture.GrantLibraryFloat();
	TestTrue(TEXT("团队取用链测试实例入库成功"), Granted.Command.bCommitted);
	TestEqual(TEXT("团队取用链入库后公库有一件"), Fixture.Library->GetSnapshot().Instances.Num(), 1);

	FCatTeamEquipmentTakeCommand Take;
	Take.Context.RequestId = FGuid::NewGuid();
	Take.Context.ExpectedRevision = Fixture.Library->GetSnapshot().Revision;
	Take.Context.StableNetId = TEXT("TeamTakePlayer");
	Take.InstanceId = Granted.Instance.InstanceId;

	FCatTeamEquipmentInstance Preview;
	TestEqual(TEXT("团队库取用预检通过"), Fixture.Library->ValidateTake(Take, Preview),
		ECatDomainCommandError::None);
	TestEqual(TEXT("团队库预检返回目标实例"), Preview.InstanceId, Granted.Instance.InstanceId);
	const ECatDomainCommandError StaleEquipment = Fixture.Equipment->ValidateTeamLibraryEquipFromAuthority(
		Take.Context.RequestId, Fixture.Equipment->GetSnapshot().Revision + 1, Preview);
	TestEqual(TEXT("个人 Equipment Revision 陈旧时预检拒绝"), StaleEquipment,
		ECatDomainCommandError::RevisionConflict);
	TestEqual(TEXT("Equipment 预检失败不会删除公库实例"), Fixture.Library->GetSnapshot().Instances.Num(), 1);
	TestEqual(TEXT("Equipment 预检失败不会改变个人鱼漂"), Fixture.Equipment->GetSnapshot().FloatDefinitionId,
		InitialFloatId);

	const int64 ExpectedEquipmentRevision = Fixture.Equipment->GetSnapshot().Revision;
	TestEqual(TEXT("个人 Equipment 正确 Revision 下预检通过"),
		Fixture.Equipment->ValidateTeamLibraryEquipFromAuthority(Take.Context.RequestId,
			ExpectedEquipmentRevision, Preview),
		ECatDomainCommandError::None);
	const FCatTeamEquipmentGrantResult Taken = Fixture.Library->TakeInstance(Take);
	TestTrue(TEXT("预检通过后先删除团队库实例"), Taken.Command.bCommitted);
	TestTrue(TEXT("团队库取用返回目标实例"), Taken.Instance.InstanceId == Granted.Instance.InstanceId);
	TestEqual(TEXT("成功取用后公库为空"), Fixture.Library->GetSnapshot().Instances.Num(), 0);
	const FCatDomainCommandResult Equipped = Fixture.Equipment->EquipFromTeamLibraryFromAuthority(
		Take.Context.RequestId, ExpectedEquipmentRevision, Taken.Instance);
	TestTrue(TEXT("公库实例删除后个人装备提交"), Equipped.bCommitted);
	TestEqual(TEXT("个人装备提交无错误"), Equipped.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("个人鱼漂已替换为团队库实例定义"), Fixture.Equipment->GetSnapshot().FloatDefinitionId,
		LibraryFloatId);

	Fixture.WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif
