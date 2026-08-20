#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/Player.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Framework/Core/CatStableNetId.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Integration/CatFishConsumptionCoordinator.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"

namespace CatfishingPlayerControllerTest
{
    /** 一局 Chum 目录覆盖守卫；入口测试期间只暴露一条可运行窝料定义，析构时恢复默认目录并解除瞬态资产根引用。 */
    struct FChumSettingsOverride
    {
        /** 被临时覆盖的全局 Equipment Settings 默认对象；测试只在自己的生命周期内改写目录字段。 */
        UCatEquipmentSettings* Settings = nullptr;

        /** 测试开始前的目录 SchemaVersion；析构时恢复，避免污染目录校验用例。 */
        int32 SavedContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

        /** 测试开始前的目录数据修订；析构时恢复到项目配置原值。 */
        int64 SavedDataRevision = 0;

        /** 测试开始前的目录来源戳；析构时恢复，避免自动化来源戳泄漏。 */
        FCatDataCatalogSourceStamp SavedSourceStamp;

        /** 测试开始前的正式定义列表；析构时整体放回默认对象。 */
        TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

        /** 测试开始前的 starter 鱼竿 ID；析构时恢复，避免单条 Chum 目录被项目默认 starter 引用拖失败。 */
        FName SavedStarterRodDefinitionId = NAME_None;

        /** 测试开始前的 starter 鱼饵 ID；析构时恢复，保持默认对象只在本用例生命周期内被改写。 */
        FName SavedStarterBaitDefinitionId = NAME_None;

        /** 测试开始前的 starter 鱼漂 ID；析构时恢复，避免后续 starter 用例继承临时空值。 */
        FName SavedStarterFloatDefinitionId = NAME_None;

        /** 测试开始前的维修漂木 ID；析构时恢复，避免 Chum-only 目录因 repair 引用失败。 */
        FName SavedDriftwoodDefinitionId = NAME_None;

        /** 入口测试使用的 Chum 稳定 ID；Controller RPC 和 Equipment 命令都通过该 ID 走真实目录查询。 */
        FName ChumDefinitionId = TEXT("ControllerChumReservation");

        /** 测试创建的瞬态 Chum 定义；加根后保证软引用目录在测试结束前能稳定解析。 */
        TObjectPtr<UCatEquipmentDefinition> ChumDefinition = nullptr;

        /** 构造流程：保存默认目录，创建一条 Fishy 贡献为 1 的正式 Chum，并替换目录让入口测试走公开查表链。 */
        FChumSettingsOverride()
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
                GetTransientPackage(), TEXT("CatPlayerControllerChumAutomationDefinition"));
            if (ChumDefinition)
            {
                ChumDefinition->AddToRoot();
                ChumDefinition->bEnableRuntimeDefinition = true;
                ChumDefinition->EquipmentDefinitionId = ChumDefinitionId;
                ChumDefinition->Kind = ECatEquipmentKind::Chum;
                ChumDefinition->bRunConsumable = true;
                ChumDefinition->FunctionalRouteId = TEXT("ControllerChumRoute");
                ChumDefinition->ChumContribution.Fishy = 1.0;
            }

            Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
            Settings->DataRevision = 1;
            Settings->SourceStamp.SourceKind = TEXT("Automation");
            Settings->SourceStamp.SourceNodeToken = TEXT("CatPlayerControllerChumTest");
            Settings->SourceStamp.SourceRevision = 1;
            Settings->SourceStamp.SourceSliceName = TEXT("ControllerChumReservation");
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

        /** 析构流程：恢复默认目录字段后解除瞬态定义根引用，使后续 Framework/Equipment 测试不会继承本用例目录。 */
        ~FChumSettingsOverride()
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

    /** 配置测试水域为一个覆盖原点的 AABB；水域本身只提供几何，窝料池由 UCatChumSpotSubsystem 持有。 */
    static void ConfigureChumRegion(ACatWaterRegion* Region)
    {
        if (!Region)
        {
            return;
        }
        Region->SetActorLocation(FVector::ZeroVector);
        Region->RegionId = TEXT("ControllerLake");
        Region->bEnablePrototypeBounds = true;
        Region->HalfExtent = FVector(100.0, 100.0, 50.0);
        Region->RegionRevision = 1;
    }

    /** 读取公开 Snapshot 中某个耗材的数量；入口测试只通过复制读模型判断是否发生了真实消耗。 */
    static int32 GetConsumableQuantity(const FCatEquipmentLoadoutSnapshot& Snapshot, const FName DefinitionId)
    {
        const FCatRunConsumableStack* Stack = Snapshot.Consumables.FindByPredicate(
            [DefinitionId](const FCatRunConsumableStack& Candidate)
            {
                return Candidate.DefinitionId == DefinitionId;
            });
        return Stack ? Stack->Quantity : 0;
    }

    /**
     * 一局可食用鱼目录覆盖守卫；用例期间只暴露一条 Safe 的正式鱼定义，析构时恢复项目鱼表并解除瞬态资产根引用。
     * 必须覆盖的原因是项目落盘的十二条鱼都没有裁定食用安全性（FoodSafety 停在 Unset），
     * UCatFishDefinition::HasRuntimeConsumptionEffect 对它们一律返回 false；
     * 用项目目录跑这条链只能走到身体 preflight 的 fail-closed 分支，验证不到"吃成功之后重放会怎样"。
     */
    struct FConsumableFishCatalogOverride
    {
        /** 被临时覆盖的全局 Fish Catalog 默认对象；进食链的 FindRuntimeDefinition 查的就是它。 */
        UCatFishCatalogSettings* Settings = nullptr;

        /** 用例开始前的鱼目录 SchemaVersion；析构时恢复，避免污染目录校验用例。 */
        int32 SavedContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;

        /** 用例开始前的鱼目录数据修订；析构时恢复到项目配置原值。 */
        int64 SavedDataRevision = 0;

        /** 用例开始前的鱼目录来源戳；析构时恢复，避免自动化来源戳泄漏。 */
        FCatDataCatalogSourceStamp SavedSourceStamp;

        /** 用例开始前的鱼定义清单；析构时整体放回默认对象。 */
        TArray<TSoftObjectPtr<UCatFishDefinition>> SavedDefinitions;

        /** 本用例使用的可食用鱼稳定 ID；捕获与进食两条链都通过它走真实目录查询。 */
        FName FishDefinitionId = TEXT("ControllerConsumeSafeFish");

        /** 测试创建的瞬态鱼定义；加根后保证软引用目录在用例结束前能稳定解析。 */
        TObjectPtr<UCatFishDefinition> Definition = nullptr;

        /** 构造流程：保存项目鱼目录，创建一条 readiness 与食用效果都成立的 Safe 鱼，并整体替换目录清单与来源戳。 */
        FConsumableFishCatalogOverride()
        {
            Settings = GetMutableDefault<UCatFishCatalogSettings>();
            if (!Settings)
            {
                return;
            }
            SavedContentSchemaVersion = Settings->ContentSchemaVersion;
            SavedDataRevision = Settings->DataRevision;
            SavedSourceStamp = Settings->SourceStamp;
            SavedDefinitions = Settings->Definitions;

            Definition = NewObject<UCatFishDefinition>(
                GetTransientPackage(), TEXT("CatPlayerControllerConsumeFishAutomationDefinition"));
            if (Definition)
            {
                Definition->AddToRoot();
                Definition->bEnableRuntimeDefinition = true;
                Definition->FishDefinitionId = FishDefinitionId;
                Definition->BodyClass = ECatFishBodyClass::Standard;
                Definition->SacrificeContribution = 1;
                Definition->RegionIds = {TEXT("ControllerLake")};
                Definition->ChumAffinities = {ECatChumAffinity::Fishy};
                Definition->MinimumWeightKilograms = 1.0;
                Definition->MaximumWeightKilograms = 2.0;
                Definition->MinimumFightParticipants = 1;
                Definition->FishStrength = 1.0;
                Definition->FishFightStamina = 1.0;
                // Safe + Poison 为 0 是 HasRuntimeConsumptionEffect 的"无毒鱼"分支要求的组合，缺一条这条鱼就吃不下去。
                Definition->FoodSafety = ECatFishFoodSafety::Safe;
                Definition->PoisonIncrease = 0.0;
            }

            Settings->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
            Settings->DataRevision = 1;
            Settings->SourceStamp.SourceKind = TEXT("Automation");
            Settings->SourceStamp.SourceNodeToken = TEXT("CatPlayerControllerConsumeFishTest");
            Settings->SourceStamp.SourceRevision = 1;
            Settings->SourceStamp.SourceSliceName = TEXT("ControllerConsumeSafeFish");
            Settings->Definitions.Reset();
            if (Definition)
            {
                Settings->Definitions.Add(Definition.Get());
            }
        }

        /** 析构流程：恢复鱼目录四个字段后解除瞬态定义根引用，使后续用例继续使用项目鱼表。 */
        ~FConsumableFishCatalogOverride()
        {
            if (Settings)
            {
                Settings->ContentSchemaVersion = SavedContentSchemaVersion;
                Settings->DataRevision = SavedDataRevision;
                Settings->SourceStamp = SavedSourceStamp;
                Settings->Definitions = SavedDefinitions;
            }
            if (Definition)
            {
                Definition->RemoveFromRoot();
            }
        }
    };

    /**
     * 身体命令准入守卫；吃鱼的 preflight 只从 CDO 读 Ability runtime 开关与倒地阈值，用例必须自己把准入摆正。
     * 之所以不直接依赖项目 ini：那份 ini 是产品配置，策划随时可能改 gate 或阈值，用例的成立性不能挂在会变的外部数值上。
     * 之所以逐项写回：改的是进程内唯一的 CDO，同一批自动化里别的用例也会临时改同一份对象。
     */
    struct FScopedBodyGates
    {
        /** 被临时改写的 Ability 默认配置；它决定 Character-owned ASC 是否被当成正式可用的 runtime。 */
        UCatAbilitySettings* AbilitySettings = GetMutableDefault<UCatAbilitySettings>();

        /** 被临时改写的 Condition 默认配置；它提供倒地裁决唯一需要的中毒阈值。 */
        UCatConditionSettings* ConditionSettings = GetMutableDefault<UCatConditionSettings>();

        /** 进入用例前的 Character ASC runtime 开关原值；析构时写回。 */
        bool bSavedAbilityRuntime = false;

        /** 进入用例前的 GameplayEffect 复制策略原值；析构时写回。 */
        ECatAbilityReplicationPolicy SavedReplication = ECatAbilityReplicationPolicy::Undecided;

        /** 进入用例前的 Condition runtime 开关原值；析构时写回。 */
        bool bSavedConditionRuntime = false;

        /** 进入用例前的中毒倒地阈值原值；析构时写回。 */
        double SavedPoisonDownedThreshold = 0.0;

        // 准入流程：先抄下四个默认值，再写成用例需要的可运行组合。阈值取 100，而本用例吃的是 Safe 鱼、一点毒都不加，
        // 所以整个用例里进食者不会被判倒地，进食被拒时的原因只可能来自本轮要验证的终态缓存。
        FScopedBodyGates()
        {
            if (AbilitySettings)
            {
                bSavedAbilityRuntime = AbilitySettings->bEnableCharacterAbilityRuntime;
                SavedReplication = AbilitySettings->ReplicationPolicy;
                AbilitySettings->bEnableCharacterAbilityRuntime = true;
                AbilitySettings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
            }
            if (ConditionSettings)
            {
                bSavedConditionRuntime = ConditionSettings->bEnableConditionRuntime;
                SavedPoisonDownedThreshold = ConditionSettings->PoisonDownedThreshold;
                ConditionSettings->bEnableConditionRuntime = true;
                ConditionSettings->PoisonDownedThreshold = 100.0;
            }
        }

        // 还原流程：把四个默认值原样写回两个 CDO；不调用 SaveConfig，测试改动不落到项目 ini。
        ~FScopedBodyGates()
        {
            if (AbilitySettings)
            {
                AbilitySettings->bEnableCharacterAbilityRuntime = bSavedAbilityRuntime;
                AbilitySettings->ReplicationPolicy = SavedReplication;
            }
            if (ConditionSettings)
            {
                ConditionSettings->bEnableConditionRuntime = bSavedConditionRuntime;
                ConditionSettings->PoisonDownedThreshold = SavedPoisonDownedThreshold;
            }
        }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCatPlayerControllerChumReservationFlowTest,
    "Catfishing.Unit.Framework.PlayerController.ChumUsesReservationBeforeWaterDeposit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 测试流程：走真实 GameMode 准入、Controller gate、Character Equipment、WaterRegion 几何和窝点子系统；首次 Chum 必须
// 先预留库存、写入窝点后再提交消耗，同 RequestId 重放不再扣库存或加池。
bool FCatPlayerControllerChumReservationFlowTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    CatfishingPlayerControllerTest::FChumSettingsOverride SettingsOverride;
    FTestWorldWrapper WorldWrapper;
    TestTrue(TEXT("创建 PlayerController Chum 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
    WorldWrapper.ForwardErrorMessages(this);
    UWorld* World = WorldWrapper.GetTestWorld();
    TestNotNull(TEXT("可创建 PlayerController Chum 测试 World"), World);
    if (!World)
    {
        return false;
    }

    const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
    FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
    TestTrue(TEXT("Chum 测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
    ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
    TestNotNull(TEXT("可取得 Chum 测试 authority GameMode"), GameMode);
    if (!GameMode)
    {
        return false;
    }
    World->InitializeActorsForPlay(GameModeUrl);
    GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

    const FString StableNetId(TEXT("player:work03-chum-reservation"));
    const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
        StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
    const FUniqueNetIdRepl UniqueId(StableUniqueId);
    FString PreLoginError;
    GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
    TestTrue(TEXT("Chum 测试身份 PreLogin 成功保留"), PreLoginError.IsEmpty());

    ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
    ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
    TestNotNull(TEXT("Chum 测试 Controller 可创建"), Controller);
    TestNotNull(TEXT("Chum 测试 PlayerState 可创建"), PlayerState);
    if (!Controller || !PlayerState || !PreLoginError.IsEmpty())
    {
        return false;
    }

    UPlayer* TestPlayer = NewObject<UPlayer>(Controller);
    TestNotNull(TEXT("Chum 测试 Controller 可绑定最小 Player"), TestPlayer);
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
    TestTrue(TEXT("Chum 测试 Controller 通过玩法命令 gate"), GameMode->CanAcceptGameplayCommand(Controller));

    ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
    ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
    TestNotNull(TEXT("Chum 测试 Character 可创建"), Character);
    TestNotNull(TEXT("Chum 测试 WaterRegion 可创建"), Region);
    if (!Character || !Region)
    {
        return false;
    }
    Character->SetActorLocation(FVector::ZeroVector);
    Controller->Possess(Character);
    CatfishingPlayerControllerTest::ConfigureChumRegion(Region);
    TestTrue(TEXT("Chum 测试角色位于水域内"), Region->ContainsWorldPoint(Character->GetActorLocation()));

    UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
    TestNotNull(TEXT("Chum 测试 Character 拥有 Equipment 组件"), Equipment);
    if (!Equipment)
    {
        return false;
    }

    const FName ChumDefinitionId = SettingsOverride.ChumDefinitionId;
    const FCatDomainCommandResult Grant = Equipment->GrantRunConsumableFromAuthority(
        FGuid::NewGuid(), Equipment->GetSnapshot().Revision, ChumDefinitionId, 1);
    TestTrue(TEXT("Chum 测试先授予一份窝料"), Grant.bCommitted);
    TestEqual(TEXT("授予后一局 Chum 数量为 1"),
        CatfishingPlayerControllerTest::GetConsumableQuantity(Equipment->GetSnapshot(), ChumDefinitionId), 1);

    UCatChumSpotSubsystem* ChumSpots = World->GetSubsystem<UCatChumSpotSubsystem>();
    TestNotNull(TEXT("Chum 测试 World 提供窝点子系统"), ChumSpots);
    if (!ChumSpots)
    {
        return false;
    }

    const FVector DropLocation = Character->GetActorLocation();
    const int64 ExpectedChumRevision = ChumSpots->GetAggregationRevision();
    const FGuid RequestId = FGuid::NewGuid();
    Controller->ServerContributeChum(DropLocation, RequestId, Grant.Revision, ExpectedChumRevision, ChumDefinitionId);
    const FCatChumSpotSnapshot AfterFirstSpot = ChumSpots->QueryChumSpot(DropLocation);
    TestTrue(TEXT("首次 Controller Chum 在落点建出窝点"), AfterFirstSpot.bHasSpot);
    TestEqual(TEXT("首次 Controller Chum 推进窝点 Revision"),
        AfterFirstSpot.AggregationRevision, ExpectedChumRevision + 1);
    TestEqual(TEXT("首次 Controller Chum 增加窝料池"), AfterFirstSpot.Pool.Fishy, 1.0);
    TestEqual(TEXT("首次 Controller Chum 提交后扣掉 Equipment 库存"),
        CatfishingPlayerControllerTest::GetConsumableQuantity(Equipment->GetSnapshot(), ChumDefinitionId), 0);
    TestEqual(TEXT("首次 Controller Chum 提交后推进 Equipment Revision"), Equipment->GetSnapshot().Revision, static_cast<int64>(2));

    Controller->ServerContributeChum(DropLocation, RequestId, Grant.Revision, ExpectedChumRevision, ChumDefinitionId);
    const FCatChumSpotSnapshot AfterReplaySpot = ChumSpots->QueryChumSpot(DropLocation);
    TestEqual(TEXT("Controller Chum 重放不再次推进窝点 Revision"),
        AfterReplaySpot.AggregationRevision, AfterFirstSpot.AggregationRevision);
    TestEqual(TEXT("Controller Chum 重放不重复增加窝料池"), AfterReplaySpot.Pool.Fishy, 1.0);
    TestEqual(TEXT("Controller Chum 重放不重复扣 Equipment 库存"),
        CatfishingPlayerControllerTest::GetConsumableQuantity(Equipment->GetSnapshot(), ChumDefinitionId), 0);
    TestEqual(TEXT("Controller Chum 重放不再次推进 Equipment Revision"), Equipment->GetSnapshot().Revision, static_cast<int64>(2));

    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerControllerShopDeliveryAndTeamEquipmentTakeTest,
	"Catfishing.Unit.Framework.PlayerController.ShopOrdersDeliverByKindAndLibraryTakeEquipsRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：不覆盖任何配置，直接用项目 DefaultGame.ini 的装备目录、starter 三件套和商店目录（这些就是 PIE 里真正跑的数据）：
// 1. 真实 GameMode 准入一名玩家并占有 Character，占有时 starter 三件套自动装上（Rod=StarterRodT1）。
// 2. 通过 Controller 的免费领饵 RPC 领 FreeBugBait：普通饵现在是一局消耗品，必须落到这只猫的耗材栈（BugBait x1），团
// 队装备库不能因此多出实物，账本推到已交付。
// 3. 同 RequestId 重放不再给第二份。
// 4. 通过购买 RPC 买 ShopRodT2Order：钱包扣 3、装备库多一件 ShopRodT2 实例。
// 5. 通过取用 RPC 把那件实例取走：Rod 槽换成 ShopRodT2、耐久重置为 70、库变空；同 RequestId 重放不再换第二次；再拿同
// 一实例 ID 用新 RequestId 取会被 NotFound 挡住。
// 这条测试锁的是差距清单 G-29（耗材订单走错分支）和 G-28（买到的竿装不上）两条链在真实产品入口上的闭合。
bool FCatPlayerControllerShopDeliveryAndTeamEquipmentTakeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建商店交付测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建商店交付测试 World"), World);
	if (!World)
	{
		return false;
	}
	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("商店交付测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	UCatTeamEquipmentLibrary* Library = World->GetSubsystem<UCatTeamEquipmentLibrary>();
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	TestNotNull(TEXT("可取得 authority GameMode"), GameMode);
	TestNotNull(TEXT("可取得团队装备库"), Library);
	TestNotNull(TEXT("可取得商店服务"), Shop);
	if (!GameMode || !Library || !Shop)
	{
		return false;
	}
	World->InitializeActorsForPlay(GameModeUrl);
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

	const FString StableNetId(TEXT("player:loop06-shop-delivery"));
	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
		StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString PreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
	if (!Controller || !PlayerState || !TestPlayer || !PreLoginError.IsEmpty())
	{
		TestTrue(TEXT("商店交付测试准入前提齐全"), false);
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);
	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);
	TestTrue(TEXT("商店交付测试 Controller 通过玩法命令 gate"), GameMode->CanAcceptGameplayCommand(Controller));

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("商店交付测试 Character 可创建"), Character);
	if (!Character)
	{
		return false;
	}
	Controller->Possess(Character);
	UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestNotNull(TEXT("Character 拥有 Equipment 组件"), Equipment);
	if (!Equipment)
	{
		return false;
	}
	TestEqual(TEXT("占有后 starter 鱼竿已按项目配置装上"), Equipment->GetSnapshot().RodDefinitionId, FName(TEXT("StarterRodT1")));
	if (Equipment->GetSnapshot().RodDefinitionId != TEXT("StarterRodT1"))
	{
		return false;
	}

	// ---- 普通饵免费自取必须落到角色耗材栈，而不是团队装备库 ----
	const FName FreeBaitEntryId = GetDefault<UCatShopEconomySettings>()->FreeOrdinaryBaitEntryId;
	TestEqual(TEXT("项目配置的免费普通饵目录项是 FreeBugBait"), FreeBaitEntryId, FName(TEXT("FreeBugBait")));
	const FGuid BaitRequestId = FGuid::NewGuid();
	Controller->ServerClaimFreeShopEntry(FreeBaitEntryId, BaitRequestId, Shop->GetWalletSnapshot().Revision);
	TestEqual(TEXT("免费领饵后 BugBait 落到猫的耗材栈"),
		CatfishingPlayerControllerTest::GetConsumableQuantity(Equipment->GetSnapshot(), TEXT("BugBait")), 1);
	TestEqual(TEXT("免费领饵不会往团队装备库塞实物"), Library->GetSnapshot().Instances.Num(), 0);
	TestEqual(TEXT("免费领饵的账本已推到已交付"),
		Shop->BuildPublicSnapshot().Transactions.Last().DeliveryState, ECatShopDeliveryState::Delivered);
	Controller->ServerClaimFreeShopEntry(FreeBaitEntryId, BaitRequestId, Shop->GetWalletSnapshot().Revision);
	TestEqual(TEXT("同 RequestId 重放不再给第二份饵"),
		CatfishingPlayerControllerTest::GetConsumableQuantity(Equipment->GetSnapshot(), TEXT("BugBait")), 1);

	// ---- 买 2 级竿：进团队装备库 ----
	const int32 BalanceBefore = Shop->GetWalletSnapshot().Balance;
	Controller->ServerSubmitShopPurchase(TEXT("ShopRodT2Order"), FGuid::NewGuid(), Shop->GetWalletSnapshot().Revision);
	TestEqual(TEXT("买 2 级竿扣掉公款 3"), Shop->GetWalletSnapshot().Balance, BalanceBefore - 3);
	TestEqual(TEXT("买 2 级竿后装备库多一件实物"), Library->GetSnapshot().Instances.Num(), 1);
	if (Library->GetSnapshot().Instances.Num() != 1)
	{
		return false;
	}
	const FCatTeamEquipmentInstance Bought = Library->GetSnapshot().Instances[0];
	TestEqual(TEXT("库里那件就是 ShopRodT2"), Bought.DefinitionId, FName(TEXT("ShopRodT2")));

	// ---- 按实例取用：竿真的换上 ----
	const FGuid TakeRequestId = FGuid::NewGuid();
	const int64 EquipmentRevisionBeforeTake = Equipment->GetSnapshot().Revision;
	const int64 LibraryRevisionBeforeTake = Library->GetSnapshot().Revision;
	Controller->ServerTakeTeamEquipment(Bought.InstanceId, TakeRequestId, LibraryRevisionBeforeTake, EquipmentRevisionBeforeTake);
	TestEqual(TEXT("取用后 Rod 槽换成 ShopRodT2"), Equipment->GetSnapshot().RodDefinitionId, FName(TEXT("ShopRodT2")));
	TestEqual(TEXT("取用后耐久重置为新竿上限 70"), Equipment->GetSnapshot().RodDurability, 70.0);
	TestEqual(TEXT("取用后 Equipment Revision 推进一次"), Equipment->GetSnapshot().Revision, EquipmentRevisionBeforeTake + 1);
	TestEqual(TEXT("取用后装备库变空"), Library->GetSnapshot().Instances.Num(), 0);
	TestEqual(TEXT("取用不动 Bait 槽"), Equipment->GetSnapshot().BaitDefinitionId, FName(TEXT("BugBait")));

	// 重放必须带首次一模一样的载荷（同实例、同两个版本前提），否则走的是载荷漂移拒绝而不是重放。
	Controller->ServerTakeTeamEquipment(Bought.InstanceId, TakeRequestId, LibraryRevisionBeforeTake, EquipmentRevisionBeforeTake);
	TestEqual(TEXT("同 RequestId 重放不再推进 Equipment Revision"), Equipment->GetSnapshot().Revision, EquipmentRevisionBeforeTake + 1);
	TestEqual(TEXT("同 RequestId 重放不再推进装备库 Revision"), Library->GetSnapshot().Revision, LibraryRevisionBeforeTake + 1);
	Controller->ServerTakeTeamEquipment(Bought.InstanceId, FGuid::NewGuid(), Library->GetSnapshot().Revision, Equipment->GetSnapshot().Revision);
	TestEqual(TEXT("已取走的实例用新 RequestId 再取不会再换一次装"), Equipment->GetSnapshot().Revision, EquipmentRevisionBeforeTake + 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerControllerTeamEquipmentTakeRejectedWhenLibraryClosedTest,
	"Catfishing.Unit.Framework.PlayerController.TeamEquipmentTakeRejectedWithoutEquippingWhenLibraryClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：锁的是"装备库收摊之后取用必须整条链都不发生"这条回归。走的是真实可达路径，不是人为构造的库状态：
// 1. 真实 GameMode 准入一名玩家、占有 Character、买下 ShopRodT2 进团队装备库（和上一条测试同一套产品入口）。
// 2. 用真实阶段入口进失败结算夜。这一步同时做两件相反的事：玩法命令 gate 仍然开着（结算夜还要能卖鱼、能确认收口），
//    团队装备库却被 CloseShopForSettlementNight 关掉了写口——这正是缺陷能被触发的现场，所以测试先把 gate 仍然放行断言出来。
// 3. 这时提交取用 RPC，四个事实必须同时成立：装备没装到猫身上、Equipment 版本没动、那件实物还在库里、库版本没动。
//    只断言"库里还在"是不够的：缺陷的形态恰恰是装备已经装上、库里那件也还在，同一件东西被算了两份。
bool FCatPlayerControllerTeamEquipmentTakeRejectedWhenLibraryClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建结算夜取用测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建结算夜取用测试 World"), World);
	if (!World)
	{
		return false;
	}
	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("结算夜取用测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	UCatTeamEquipmentLibrary* Library = World->GetSubsystem<UCatTeamEquipmentLibrary>();
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	TestNotNull(TEXT("结算夜取用测试可取得 authority GameMode"), GameMode);
	TestNotNull(TEXT("结算夜取用测试可取得团队装备库"), Library);
	TestNotNull(TEXT("结算夜取用测试可取得商店服务"), Shop);
	if (!GameMode || !Library || !Shop)
	{
		return false;
	}
	World->InitializeActorsForPlay(GameModeUrl);
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

	const FString StableNetId(TEXT("player:settlement-night-take"));
	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
		StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString PreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
	if (!Controller || !PlayerState || !TestPlayer || !PreLoginError.IsEmpty())
	{
		TestTrue(TEXT("结算夜取用测试准入前提齐全"), false);
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);
	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("结算夜取用测试 Character 可创建"), Character);
	if (!Character)
	{
		return false;
	}
	Controller->Possess(Character);
	UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestNotNull(TEXT("结算夜取用测试 Character 拥有 Equipment 组件"), Equipment);
	if (!Equipment)
	{
		return false;
	}

	// ---- 白天先把 2 级竿买进团队装备库 ----
	Controller->ServerSubmitShopPurchase(TEXT("ShopRodT2Order"), FGuid::NewGuid(), Shop->GetWalletSnapshot().Revision);
	TestEqual(TEXT("结算夜取用测试白天买到一件实物"), Library->GetSnapshot().Instances.Num(), 1);
	if (Library->GetSnapshot().Instances.Num() != 1)
	{
		return false;
	}
	const FCatTeamEquipmentInstance Bought = Library->GetSnapshot().Instances[0];

	// ---- 进失败结算夜：玩法命令门还开着，装备库写口已经关了 ----
	const FCatRunTransitionResult SettlementResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::FailureSettlementNight, ECatRunTransitionReason::QuotaFailed);
	TestTrue(TEXT("可进入失败结算夜"), SettlementResult.bApplied);
	TestTrue(TEXT("结算夜里玩法命令门仍然放行（缺陷正是靠这道门还开着才够得到）"),
		GameMode->CanAcceptGameplayCommand(Controller));

	const FName RodBeforeTake = Equipment->GetSnapshot().RodDefinitionId;
	const int64 EquipmentRevisionBeforeTake = Equipment->GetSnapshot().Revision;
	const int64 LibraryRevisionBeforeTake = Library->GetSnapshot().Revision;
	Controller->ServerTakeTeamEquipment(Bought.InstanceId, FGuid::NewGuid(), LibraryRevisionBeforeTake,
		EquipmentRevisionBeforeTake);
	TestEqual(TEXT("装备库关闭后取用没有换掉 Rod 槽"), Equipment->GetSnapshot().RodDefinitionId, RodBeforeTake);
	TestEqual(TEXT("装备库关闭后取用没有推进 Equipment Revision"), Equipment->GetSnapshot().Revision,
		EquipmentRevisionBeforeTake);
	TestEqual(TEXT("装备库关闭后那件实物仍留在库里"), Library->GetSnapshot().Instances.Num(), 1);
	if (Library->GetSnapshot().Instances.Num() == 1)
	{
		TestEqual(TEXT("留在库里的就是原来那一件"), Library->GetSnapshot().Instances[0].InstanceId, Bought.InstanceId);
	}
	TestEqual(TEXT("装备库关闭后库版本没有推进"), Library->GetSnapshot().Revision, LibraryRevisionBeforeTake);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerControllerDeliveredOrderReplaySurvivesSettlementNightTest,
	"Catfishing.Unit.Framework.PlayerController.DeliveredEquipmentOrderReplayReturnsReceiptAfterSettlementNight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：守的是"钱已经花了、东西也已经发了，之后再问一次却被告知现在不能发货"这一类回执丢失。
// 网络重试、UI 重复点击、断线重连都会让同一个 RequestId 再来一趟，这时候调用方要的是首次那份回执，
// 不是"此刻还能不能交付"的重新裁定——后者在结算夜必然是否定的，于是玩家侧看到的就是钱扣了、竿不知去向。
// 现场是真实可达的：进结算夜时 CloseShopForSettlementNight 把团队装备库的入库写口关掉，
// 而玩法命令门仍然放行，所以重放确实能一路走到订单链里，撞上那道本来只该对首次下单生效的交付前置校验。
// 步骤：
// 1. 真实 GameMode 准入一名玩家、占有 Character，白天用产品购买 RPC 买下 ShopRodT2 并入库（走 DefaultGame.ini 的真实目录）。
// 2. 用真实相位入口进失败结算夜，先把"装备库确实已经关了"单独断言出来——这正是重放会撞上的那道拒绝。
// 3. 用与首次逐字相同的命令（同 RequestId、同钱包版本前提、同 EntryId、同服务器身份）再走一次订单链。
//    这一步直接打协调器而不是再发一次 RPC，因为 RPC 是 void、拿不到回执；协调器就是修复所在的那一层，命令内容与 RPC 组装的完全一致。
// 4. 断言重放拿回的是 AlreadyResolved 和首次那件实物的 InstanceId，而不是 CommandsClosed；
//    同时钱包余额、钱包版本、账本条数、装备库内容和版本一个都没有再动——重放只能取回执，不能产生第二次副作用。
bool FCatPlayerControllerDeliveredOrderReplaySurvivesSettlementNightTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建已交付订单重放测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建已交付订单重放测试 World"), World);
	if (!World)
	{
		return false;
	}
	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("已交付订单重放测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	UCatTeamEquipmentLibrary* Library = World->GetSubsystem<UCatTeamEquipmentLibrary>();
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	UCatShopOrderCoordinator* Coordinator = World->GetSubsystem<UCatShopOrderCoordinator>();
	TestNotNull(TEXT("已交付订单重放测试可取得 authority GameMode"), GameMode);
	TestNotNull(TEXT("已交付订单重放测试可取得团队装备库"), Library);
	TestNotNull(TEXT("已交付订单重放测试可取得商店服务"), Shop);
	TestNotNull(TEXT("已交付订单重放测试可取得订单协调器"), Coordinator);
	if (!GameMode || !Library || !Shop || !Coordinator)
	{
		return false;
	}
	World->InitializeActorsForPlay(GameModeUrl);
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
		TEXT("player:delivered-order-replay"), UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString PreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
	if (!Controller || !PlayerState || !TestPlayer || !PreLoginError.IsEmpty())
	{
		TestTrue(TEXT("已交付订单重放测试准入前提齐全"), false);
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);
	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("已交付订单重放测试 Character 可创建"), Character);
	if (!Character)
	{
		return false;
	}
	Controller->Possess(Character);
	UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestNotNull(TEXT("已交付订单重放测试 Character 拥有 Equipment 组件"), Equipment);
	if (!Equipment)
	{
		return false;
	}

	// ---- 白天用产品购买 RPC 买下 2 级竿并入库 ----
	// RequestId 和钱包版本前提要留在手上：重放必须拿着与首次逐字相同的载荷，否则商店会判成载荷漂移，
	// 那样验的就是另一条路（InvalidPayload），跟本轮要守的东西无关。
	const FGuid OrderRequestId = FGuid::NewGuid();
	const int64 WalletRevisionAtOrder = Shop->GetWalletSnapshot().Revision;
	Controller->ServerSubmitShopPurchase(TEXT("ShopRodT2Order"), OrderRequestId, WalletRevisionAtOrder);
	TestEqual(TEXT("白天下单后装备库多一件实物"), Library->GetSnapshot().Instances.Num(), 1);
	if (Library->GetSnapshot().Instances.Num() != 1)
	{
		return false;
	}
	const FCatTeamEquipmentInstance Bought = Library->GetSnapshot().Instances[0];
	TestEqual(TEXT("买到的就是 ShopRodT2"), Bought.DefinitionId, FName(TEXT("ShopRodT2")));
	const TArray<FCatShopTransactionRecord> LedgerAfterOrder = Shop->GetTransactionLedgerSnapshot();
	TestEqual(TEXT("下单只写了一条账本记录"), LedgerAfterOrder.Num(), 1);
	if (LedgerAfterOrder.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("首次订单已经推到已交付"), LedgerAfterOrder[0].DeliveryState, ECatShopDeliveryState::Delivered);

	// ---- 进失败结算夜：玩法命令门还开着，装备库写口已经被收摊关掉 ----
	const FCatRunTransitionResult SettlementResult = GameMode->EnterRunPhaseFromStateTree(
		ECatRunPhase::FailureSettlementNight, ECatRunTransitionReason::QuotaFailed);
	TestTrue(TEXT("可进入失败结算夜"), SettlementResult.bApplied);
	TestTrue(TEXT("结算夜里玩法命令门仍然放行，所以重放确实够得到订单链"),
		GameMode->CanAcceptGameplayCommand(Controller));
	// 这一条把"重放会撞上什么"钉死：交付前置校验问的就是这个问题，此刻它的答案是拒绝。
	// 修复的全部内容就是让重放根本不去问它，所以先证明这个答案确实是否定的，后面的 AlreadyResolved 才有意义。
	TestEqual(TEXT("结算夜里装备库入库写口已关，交付前置校验此刻只会给出 CommandsClosed"),
		Library->ValidateShopOrderGrant(TEXT("ShopRodT2")), ECatDomainCommandError::CommandsClosed);

	// ---- 用与首次逐字相同的命令重放 ----
	const FCatShopWalletSnapshot WalletBeforeReplay = Shop->GetWalletSnapshot();
	const int64 LibraryRevisionBeforeReplay = Library->GetSnapshot().Revision;
	FCatShopPurchaseCommand ReplayCommand;
	ReplayCommand.Context.RequestId = OrderRequestId;
	ReplayCommand.Context.ExpectedRevision = WalletRevisionAtOrder;
	// 身份必须和 RPC 层重建出来的那一个一致：幂等键里就有它，换一个身份等于换了一笔订单。
	ReplayCommand.Context.StableNetId = CatResolveStableNetId(PlayerState);
	ReplayCommand.EntryId = TEXT("ShopRodT2Order");
	const FCatShopOrderResult Replay = Coordinator->SubmitPurchase(ReplayCommand, Equipment);

	TestFalse(TEXT("重放不会再付一次款"), Replay.Transaction.Command.bCommitted);
	TestEqual(TEXT("重放的订单这一段返回首次终态 AlreadyResolved，而不是 CommandsClosed"),
		Replay.Transaction.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重放的交付这一段同样返回 AlreadyResolved"),
		Replay.Delivery.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重放拿回的是首次那件实物的 InstanceId"), Replay.Instance.InstanceId, Bought.InstanceId);
	TestEqual(TEXT("重放读回的账本仍指向那件实物"), Replay.Transaction.Transaction.DeliveryReceiptId, Bought.InstanceId);
	TestEqual(TEXT("重放读回的账本仍是已交付"), Replay.Transaction.Transaction.DeliveryState,
		ECatShopDeliveryState::Delivered);

	// 重放只能取回执，不能有第二次副作用：钱包、账本和装备库都必须原地不动。
	TestEqual(TEXT("重放不再扣一次公款"), Shop->GetWalletSnapshot().Balance, WalletBeforeReplay.Balance);
	TestEqual(TEXT("重放不推进钱包版本"), Shop->GetWalletSnapshot().Revision, WalletBeforeReplay.Revision);
	TestEqual(TEXT("重放不写第二条账本记录"), Shop->GetTransactionLedgerSnapshot().Num(), LedgerAfterOrder.Num());
	TestEqual(TEXT("重放没有让装备库多出第二件"), Library->GetSnapshot().Instances.Num(), 1);
	TestEqual(TEXT("重放不推进装备库版本"), Library->GetSnapshot().Revision, LibraryRevisionBeforeReplay);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerControllerConsumeFishTerminalCacheTest,
	"Catfishing.Unit.Framework.PlayerController.ConsumeFishCachesTerminalPerRequestId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：走真实 GameMode 准入、Controller gate、Character 占有时自动注册的个人鱼护和真实 Items 捕获入口种两条鱼，
// 然后锁住进食链终态缓存的三条不变量：
// 1. 通过 Controller RPC 吃掉第一条鱼后，同 RequestId 再进一次协调器必须返回 AlreadyResolved 而不是 NotFound。
//    这条是本轮修的缺陷本身：鱼被吃掉后就不在容器里了，先读当前容器再判重放的话，一次网络重试会把成功报成“鱼找不到”。
// 2. 身体准入关着时第一次进食被拒，这个失败终态同样被永久记住：把准入重新打开、拿同一个 RequestId 再来一次仍然被拒，
//    容器里那条鱼一条都不少。口径与 UCatConditionComponent::ConsumeCommittedFish 相同——同 RequestId 只有一次结算机会。
// 3. 换一个新 RequestId 重试同一条鱼才真的吃得掉，证明第 2 条锁住的是“这次意图已经收口”，不是把这条鱼永久锁死。
bool FCatPlayerControllerConsumeFishTerminalCacheTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const CatfishingPlayerControllerTest::FScopedBodyGates BodyGates;
	const CatfishingPlayerControllerTest::FConsumableFishCatalogOverride FishCatalog;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建吃鱼入口测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建吃鱼入口测试 World"), World);
	if (!World)
	{
		return false;
	}

	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	TestTrue(TEXT("吃鱼测试 World 可注册项目 Lake GameMode"), World->SetGameMode(GameModeUrl));
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatFishConsumptionCoordinator* Coordinator = World->GetSubsystem<UCatFishConsumptionCoordinator>();
	TestNotNull(TEXT("可取得吃鱼测试 authority GameMode"), GameMode);
	TestNotNull(TEXT("可取得 Items 服务"), Items);
	TestNotNull(TEXT("可取得进食协调器"), Coordinator);
	if (!GameMode || !Items || !Coordinator)
	{
		return false;
	}
	World->InitializeActorsForPlay(GameModeUrl);
	GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

	const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
		TEXT("player:loop10-consume-fish"), UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
	const FUniqueNetIdRepl UniqueId(StableUniqueId);
	FString PreLoginError;
	GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
	if (!Controller || !PlayerState || !TestPlayer || !PreLoginError.IsEmpty())
	{
		TestTrue(TEXT("吃鱼测试准入前提齐全"), false);
		return false;
	}
	TestPlayer->CurrentNetSpeed = 10000;
	Controller->SetPlayer(TestPlayer);
	PlayerState->SetUniqueId(UniqueId);
	Controller->SetPlayerState(PlayerState);
	World->AddController(Controller);
	GameMode->PostLogin(Controller);
	TestTrue(TEXT("吃鱼测试 Controller 通过玩法命令 gate"), GameMode->CanAcceptGameplayCommand(Controller));

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("吃鱼测试 Character 可创建"), Character);
	if (!Character)
	{
		return false;
	}
	Controller->Possess(Character);
	// 个人鱼护由 Character 在占有时按自己的 PlayerState 身份向 Items 注册，用例不另建容器，走的就是生产里那一个。
	const FString StableNetId = CatResolveStableNetId(PlayerState);
	const FGuid ContainerId = Character->GetPersonalFishGuardId();
	FCatContainerSnapshot Snapshot;
	TestTrue(TEXT("占有后个人鱼护已注册进 Items"), Items->TryGetContainerSnapshot(ContainerId, Snapshot));
	if (!Items->TryGetContainerSnapshot(ContainerId, Snapshot))
	{
		return false;
	}

	// 两条鱼都通过真实捕获入口种进鱼护：第一条用于验证成功后的重放，第二条用于验证失败缓存与换 RequestId 重试。
	auto SeedFish = [&](const FGuid FishInstanceId)
	{
		FCatCaptureCommitCommand Capture;
		Capture.Context.RequestId = FGuid::NewGuid();
		Capture.Context.StableNetId = StableNetId;
		Capture.Context.ExpectedRevision = Items->TryGetContainerSnapshot(ContainerId, Snapshot) ? Snapshot.Revision : 0;
		Capture.FishingSessionId = FGuid::NewGuid();
		Capture.FishInstanceId = FishInstanceId;
		Capture.FishDefinitionId = FishCatalog.FishDefinitionId;
		Capture.TargetContainerId = ContainerId;
		Capture.WeightKilograms = 1.5;
		Capture.SacrificeContribution = 1;
		return Items->CommitCapture(Capture).Command.bCommitted;
	};
	const FGuid FirstFishId = FGuid::NewGuid();
	const FGuid SecondFishId = FGuid::NewGuid();
	TestTrue(TEXT("第一条测试鱼种进鱼护"), SeedFish(FirstFishId));
	TestTrue(TEXT("第二条测试鱼种进鱼护"), SeedFish(SecondFishId));
	Items->TryGetContainerSnapshot(ContainerId, Snapshot);
	TestEqual(TEXT("鱼护里现在有两条鱼"), Snapshot.Fish.Num(), 2);
	if (Snapshot.Fish.Num() != 2)
	{
		return false;
	}

	// ---- 1. 成功后同 RequestId 重放必须拿回首次终态，而不是“鱼找不到” ----
	FCatFishConsumeCommand FirstCommand;
	FirstCommand.Context.RequestId = FGuid::NewGuid();
	FirstCommand.Context.ExpectedRevision = Snapshot.Revision;
	FirstCommand.FishInstanceId = FirstFishId;
	FirstCommand.SourceContainerId = ContainerId;
	Controller->ServerConsumeFish(Character, FirstCommand);
	Items->TryGetContainerSnapshot(ContainerId, Snapshot);
	TestEqual(TEXT("通过 Controller RPC 吃掉一条鱼后鱼护只剩一条"), Snapshot.Fish.Num(), 1);
	const int64 RevisionAfterFirstConsume = Snapshot.Revision;

	// 协调器的返回值是这条链唯一能读到的终态，RPC 本身没有回执，所以重放语义只能从这里断言。
	FCatFishConsumeCommand FirstReplay = FirstCommand;
	FirstReplay.Context.StableNetId = StableNetId;
	const FCatFishConsumeResult ReplayResult = Coordinator->ConsumeFishFromContainer(FirstReplay, Character);
	TestFalse(TEXT("同 RequestId 重放不再发生第二次不可逆写入"), ReplayResult.Command.bCommitted);
	TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved 而不是 NotFound"),
		ReplayResult.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重放返回首次那条被吃掉的鱼"), ReplayResult.Fish.FishInstanceId, FirstFishId);
	Items->TryGetContainerSnapshot(ContainerId, Snapshot);
	TestEqual(TEXT("重放不再动鱼护内容"), Snapshot.Fish.Num(), 1);
	TestEqual(TEXT("重放不推进鱼护 Revision"), Snapshot.Revision, RevisionAfterFirstConsume);

	// ---- 2. 身体准入关着时的失败终态同样被永久记住 ----
	FCatFishConsumeCommand SecondCommand;
	SecondCommand.Context.RequestId = FGuid::NewGuid();
	SecondCommand.Context.ExpectedRevision = Snapshot.Revision;
	SecondCommand.Context.StableNetId = StableNetId;
	SecondCommand.FishInstanceId = SecondFishId;
	SecondCommand.SourceContainerId = ContainerId;
	GetMutableDefault<UCatAbilitySettings>()->bEnableCharacterAbilityRuntime = false;
	const FCatFishConsumeResult BlockedResult = Coordinator->ConsumeFishFromContainer(SecondCommand, Character);
	GetMutableDefault<UCatAbilitySettings>()->bEnableCharacterAbilityRuntime = true;
	TestFalse(TEXT("身体准入关着时进食不提交"), BlockedResult.Command.bCommitted);
	TestEqual(TEXT("身体准入关着时进食返回 DependencyUnavailable"),
		BlockedResult.Command.Error, ECatDomainCommandError::DependencyUnavailable);

	const FCatFishConsumeResult BlockedRetry = Coordinator->ConsumeFishFromContainer(SecondCommand, Character);
	TestFalse(TEXT("准入恢复后同 RequestId 仍不提交"), BlockedRetry.Command.bCommitted);
	TestEqual(TEXT("准入恢复后同 RequestId 返回 AlreadyResolved"),
		BlockedRetry.Command.Error, ECatDomainCommandError::AlreadyResolved);
	Items->TryGetContainerSnapshot(ContainerId, Snapshot);
	TestEqual(TEXT("失败重试不吃掉第二条鱼"), Snapshot.Fish.Num(), 1);

	// ---- 3. 换新 RequestId 才是这条鱼的正确重试方式 ----
	FCatFishConsumeCommand RetryCommand = SecondCommand;
	RetryCommand.Context.RequestId = FGuid::NewGuid();
	const FCatFishConsumeResult RetryResult = Coordinator->ConsumeFishFromContainer(RetryCommand, Character);
	TestTrue(TEXT("换新 RequestId 后第二条鱼真的被吃掉"), RetryResult.Command.bCommitted);
	TestEqual(TEXT("换新 RequestId 的进食没有拒绝原因"), RetryResult.Command.Error, ECatDomainCommandError::None);
	Items->TryGetContainerSnapshot(ContainerId, Snapshot);
	TestEqual(TEXT("鱼护在两次成功进食后清空"), Snapshot.Fish.Num(), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
