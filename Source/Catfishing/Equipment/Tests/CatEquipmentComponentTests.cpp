#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentDefaultConsumableGateTest,
	"Catfishing.Unit.Equipment.Component.DefaultCatalogRejectsConsumableGrantWithoutMutatingSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentFishingFailureNoneTest,
	"Catfishing.Unit.Equipment.Component.FishingFailureNoneIsIdempotentAndDoesNotPunish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentShopGrantEquipsPersonalRodTest,
	"Catfishing.Unit.Equipment.Component.ShopGrantEquipsPersonalRodAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatEquipmentComponentTest
{
	/** 测试用商店鱼竿定义 ID；它模拟商店目录里的 EquipmentGrant 目标。 */
	static const FName ShopGrantRodId(TEXT("ShopGrantPersonalRod"));

	/** 测试用第二条鱼竿定义 ID；它只用于证明同 RequestId 不能漂移到另一件装备。 */
	static const FName OtherRodId(TEXT("ShopGrantOtherRod"));

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

	/** 定义构造流程：创建最小可运行的 transient 鱼竿定义；测试只关心 Equipment 写槽和耐久，不依赖正式资产包。 */
	static UCatEquipmentDefinition* MakeRodDefinition(const FName DefinitionId, const double MaximumDurability)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = ECatEquipmentKind::Rod;
		Definition->LoadoutSlotId = *FString::Printf(TEXT("%sSlot"), *DefinitionId.ToString());
		Definition->RequiredUnlockId = NAME_None;
		Definition->MaximumRodDurability = MaximumDurability;
		Definition->FishingStrength = 1.0;
		Definition->MaximumLineLengthCentimeters = 2000.0;
		Definition->BaseDurabilityWearPerSecond = 0.1;
		Definition->HighTensionWearMultiplier = 1.0;
		return Definition;
	}

	/** 装备目录覆盖夹具；它把测试定义挂进默认 Settings，析构时恢复，避免影响其他自动化用例。 */
	struct FEquipmentSettingsOverride
	{
		/** 被覆盖的默认设置对象；构造写入测试目录，析构恢复原值。 */
		UCatEquipmentSettings* Settings = nullptr;

		/** 测试前装备定义清单；析构时原样恢复。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 持有 transient 定义的强引用；防止默认设置软引用在测试过程中解析到已释放对象。 */
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> RuntimeDefinitions;

		/** 构造流程：保存默认目录，再写入两条测试鱼竿，给商店式授予提供可运行定义。 */
		FEquipmentSettingsOverride()
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			if (!Settings)
			{
				return;
			}
			SavedDefinitions = Settings->Definitions;
			RuntimeDefinitions.Emplace(MakeRodDefinition(ShopGrantRodId, 120.0));
			RuntimeDefinitions.Emplace(MakeRodDefinition(OtherRodId, 90.0));
			Settings->Definitions = {
				RuntimeDefinitions[0].Get(),
				RuntimeDefinitions[1].Get()
			};
		}

		/** 析构流程：把默认设置恢复到测试前目录；transient 定义随后自然释放，不写入项目资产。 */
		~FEquipmentSettingsOverride()
		{
			if (Settings)
			{
				Settings->Definitions = SavedDefinitions;
			}
		}
	};
}

// 测试流程：默认装备目录为空时提交数量型物品授予；Result 必须拒绝，Snapshot Revision、库存格数组和装配字段保持初始值。
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
			const FCatDomainCommandResult Result = Component->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Before.Revision, TEXT("MissingConsumable"), 1);
			TestFalse(TEXT("默认目录下数量库存授予不提交"), Result.bCommitted);
			TestEqual(TEXT("默认目录下数量库存授予返回 InvalidPayload"), Result.Error, ECatDomainCommandError::InvalidPayload);
			const FCatEquipmentLoadoutSnapshot& After = Component->GetSnapshot();
			TestEqual(TEXT("拒绝后 Equipment Revision 不变"), After.Revision, Before.Revision);
			TestEqual(TEXT("拒绝后库存格数组仍为空"), After.InventorySlots.Num(), Before.InventorySlots.Num());
			TestEqual(TEXT("拒绝后 Rod 仍为空"), After.RodDefinitionId, Before.RodDefinitionId);
			TestEqual(TEXT("拒绝后 Bait 仍为空"), After.BaitDefinitionId, Before.BaitDefinitionId);
			TestEqual(TEXT("拒绝后 Float 仍为空"), After.FloatDefinitionId, Before.FloatDefinitionId);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：用 Penalty=None 提交一次失败预算并原样重放；它应形成幂等终态，但不改变耐久、耗材或 Revision。
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
			TestEqual(TEXT("None 失败预算不创建库存格"), Component->GetSnapshot().InventorySlots.Num(), 0);

			const FCatFishingFailureResult Replay = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::DamageRod);
			TestFalse(TEXT("失败预算重放不再次提交"), Replay.Command.bCommitted);
			TestEqual(TEXT("失败预算重放返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("失败预算重放保留首次 None 惩罚"), Replay.Penalty, ECatFishingFailurePenalty::None);
			TestEqual(TEXT("失败预算重放不推进 Revision"), Replay.Command.Revision, First.Command.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：
// 1. 用 transient 装备目录创建一根商店鱼竿和另一根漂移鱼竿。
// 2. 通过本人 EquipmentComponent 的新授予入口提交鱼竿，确认 Snapshot 里的当前鱼竿和耐久立即变化。
// 3. 用同一 RequestId 重放同一鱼竿，确认它只返回 AlreadyResolved，不重复推进 Revision。
// 4. 用同一 RequestId 换另一根鱼竿，确认载荷漂移被拒绝，避免旧订单被挪作另一件装备。
bool FCatEquipmentComponentShopGrantEquipsPersonalRodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatEquipmentComponentTest::FEquipmentSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建商店装备授予测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("商店装备授予测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("商店装备授予测试组件可创建"), Component);
		if (Component)
		{
			const FGuid RequestId = FGuid::NewGuid();
			TestEqual(TEXT("商店鱼竿授予预检通过"),
				Component->ValidateEquipmentGrantFromAuthority(RequestId, CatEquipmentComponentTest::ShopGrantRodId),
				ECatDomainCommandError::None);
			const FCatDomainCommandResult Grant = Component->GrantEquipmentFromAuthority(
				RequestId, Component->GetSnapshot().Revision, CatEquipmentComponentTest::ShopGrantRodId);
			TestTrue(TEXT("商店鱼竿授予首次提交"), Grant.bCommitted);
			TestEqual(TEXT("商店鱼竿授予无错误"), Grant.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("商店鱼竿进入本人当前鱼竿槽"),
				Component->GetSnapshot().RodDefinitionId, CatEquipmentComponentTest::ShopGrantRodId);
			TestEqual(TEXT("商店鱼竿按定义刷新耐久"), Component->GetSnapshot().RodDurability, 120.0);

			const int64 RevisionAfterGrant = Component->GetSnapshot().Revision;
			const FCatDomainCommandResult Replay = Component->GrantEquipmentFromAuthority(
				RequestId, RevisionAfterGrant, CatEquipmentComponentTest::ShopGrantRodId);
			TestFalse(TEXT("商店鱼竿授予重放不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("商店鱼竿授予重放返回 AlreadyResolved"), Replay.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("商店鱼竿授予重放不推进 Revision"), Component->GetSnapshot().Revision,
				RevisionAfterGrant);

			const FCatDomainCommandResult Drift = Component->GrantEquipmentFromAuthority(
				RequestId, RevisionAfterGrant, CatEquipmentComponentTest::OtherRodId);
			TestFalse(TEXT("同 RequestId 更换装备定义不提交"), Drift.bCommitted);
			TestEqual(TEXT("同 RequestId 更换装备定义返回 InvalidPayload"), Drift.Error,
				ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("载荷漂移不替换当前鱼竿"), Component->GetSnapshot().RodDefinitionId,
				CatEquipmentComponentTest::ShopGrantRodId);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
