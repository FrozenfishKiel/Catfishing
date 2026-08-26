#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/BoxComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "OnlineSubsystemTypes.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingPlayerEntryFullLoopTest,
	"Catfishing.PlayerEntry.FullLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingPlayerEntryTest
{
	/** 本测试的稳定玩家身份；同时写入 PlayerState、GameMode 准入表和个人鱼护私有主人，确保入口链条使用同一身份。 */
	static const FString StableNetIdValue(TEXT("CatPlayerEntryStableNetId"));
	/** 本测试的水域 ID；它对应夹具里的矩形湖面，避免借用正式地图资产状态。 */
	static const FName WaterRegionId(TEXT("River"));
	/** 本测试使用的鱼竿定义 ID；只存在于 CDO 临时目录，不写入配置文件。 */
	static const FName RodId(TEXT("PlayerEntryRod"));
	/** 本测试使用的普通鱼饵定义 ID；玩家入口闭环必须先通过正式数量栈授予一份鱼饵，才允许进入 Fishing use。 */
	static const FName BaitId(TEXT("PlayerEntryBait"));
	/** 本测试使用的鱼漂定义 ID；它只让装备装配和钓鱼占用记录完整。 */
	static const FName FloatId(TEXT("PlayerEntryFloat"));
	/** 本测试使用的抄网定义 ID；RequestScoop 会从玩家当前装备快照读取它。 */
	static const FName ScoopNetId(TEXT("PlayerEntryScoopNet"));
	/** 本测试使用的鱼种定义 ID；捕获后应原样进入 FishGuard 的 FishInstance。 */
	static const FName FishId(TEXT("PlayerEntryFish"));

	/** 构造流程：创建一条完整的运行时装备定义；调用方只提供类别，函数负责补齐该类别的最小正式数值。 */
	static UCatEquipmentDefinition* MakeEquipmentDefinition(const FName DefinitionId, const ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = Kind;
		Definition->FunctionalRouteId = *FString::Printf(TEXT("%sRoute"), *DefinitionId.ToString());
		Definition->RequiredUnlockId = NAME_None;
		Definition->LoadoutSlotId = *FString::Printf(TEXT("%sSlot"), *DefinitionId.ToString());
		if (Kind == ECatEquipmentKind::Rod)
		{
			Definition->MaximumRodDurability = 100.0;
			Definition->FishingStrength = 5.0;
			Definition->MaximumLineLengthCentimeters = 2000.0;
			Definition->BaseDurabilityWearPerSecond = 0.0;
			Definition->HighTensionWearMultiplier = 1.0;
		}
		else if (Kind == ECatEquipmentKind::Bait)
		{
			Definition->bRunConsumable = true;
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
		else if (Kind == ECatEquipmentKind::ScoopNet)
		{
			Definition->ScoopReachCentimeters = 500.0;
		}
		return Definition;
	}

	/** 构造流程：创建一条可进入正式捕获事务的鱼定义；可选图鉴事件保持 None，避免把成像计划混入玩家入口闭环。 */
	static UCatFishDefinition* MakeFishDefinition()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = FishId;
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 3;
		Definition->RarityTierId = TEXT("Common");
		Definition->RegionIds = { WaterRegionId };
		Definition->TimeOfDay = { ECatEnvironmentTimeOfDay::Day };
		Definition->Weather = { ECatEnvironmentWeather::Clear };
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 2.0;
		Definition->MaximumWeightKilograms = 3.0;
		Definition->ScoopTargetRadiusCentimeters = 120.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = TEXT("PlayerEntryBite");
		Definition->FightPersonalityId = TEXT("PlayerEntryFight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = 1.0;
		Definition->PoisonIncrease = 0.0;
		return Definition;
	}

	/** 构造流程：创建一块岸线在 X=0 的矩形水域；玩家站在负 X 岸上，鱼位于正 X 水内近岸带。 */
	static FCatWaterGeometryCache BuildWaterCache()
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = WaterRegionId;
		Input.PlaneToWorld = FTransform::Identity;
		Input.WaterPointVerticalToleranceCm = 200.0;
		Input.BankHeightToleranceCm = 200.0;
		Input.BoundaryToleranceCm = 1.0;
		Input.MaxLandingCorrectionCm = 25.0;
		Input.MinimumWaterInsetCm = 5.0;
		Input.MaxSampleSegmentLengthCm = 100.0;
		Input.MaxChordErrorCm = 5.0;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = { FVector2D(0.0, -400.0), FVector2D(500.0, -400.0),
			FVector2D(500.0, 400.0), FVector2D(0.0, 400.0) };
		return FCatWaterGeometry::Build(Input).Cache;
	}

	/** 单次测试夹具；保存 CDO 配置、运行世界、玩家、会话和关键聚合，析构时恢复所有临时设置。 */
	struct FPlayerEntryFixture
	{
		/** Fishing 配置 CDO；测试期间打开近岸和终态窗口，析构时恢复。 */
		UCatFishingSettings* FishingSettings = nullptr;
		/** Ability 配置 CDO；测试期间打开 Character ASC 与初始属性，析构时恢复。 */
		UCatAbilitySettings* AbilitySettings = nullptr;
		/** Items 配置 CDO；测试期间给个人鱼护一个正容量，析构时恢复。 */
		UCatItemsSettings* ItemsSettings = nullptr;
		/** Equipment 配置 CDO；测试期间挂入四个临时定义，析构时恢复。 */
		UCatEquipmentSettings* EquipmentSettings = nullptr;
		/** 原始 Fishing runtime gate；析构时按原值恢复，避免污染后续测试。 */
		bool bSavedFishingRuntime = false;
		/** 原始近岸校验 gate；析构时按原值恢复。 */
		bool bSavedNearShoreValidation = false;
		/** 原始抢抄 reach；析构时按原值恢复。 */
		double SavedScoopReachCentimeters = 0.0;
		/** 原始终态复制窗口；析构时按原值恢复。 */
		double SavedTerminalReplicationWindowSeconds = 0.0;
		/** 原始真咬窗口；析构时按原值恢复。 */
		double SavedTrueBiteWindowSeconds = 0.0;
		/** 原始近岸带宽；析构时按原值恢复。 */
		double SavedNearShoreWidthCentimeters = 0.0;
		/** 原始最大高度差；析构时按原值恢复。 */
		double SavedMaximumScoopVerticalDeltaCentimeters = 0.0;
		/** 原始 Character Ability runtime gate；析构时按原值恢复。 */
		bool bSavedAbilityRuntime = false;
		/** 原始初始属性 gate；析构时按原值恢复。 */
		bool bSavedInitialAttributeTuning = false;
		/** 原始 GameplayEffect 复制策略；析构时按原值恢复。 */
		ECatAbilityReplicationPolicy SavedReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;
		/** 原始 Poison 初值；析构时按原值恢复。 */
		float SavedInitialPoison = -1.0f;
		/** 原始 FishingStrength 初值；析构时按原值恢复。 */
		float SavedInitialFishingStrength = -1.0f;
		/** 原始 FightStamina 初值；析构时按原值恢复。 */
		float SavedInitialFightStamina = -1.0f;
		/** 原始个人鱼护容量；析构时按原值恢复。 */
		int32 SavedPersonalGuardCapacity = 0;
		/** 原始共享鱼缸容量；析构时按原值恢复。 */
		int32 SavedSharedFishTankCapacity = 0;
		/** 原始装备目录；析构时按原值恢复。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedEquipmentDefinitions;
		/** 原始装配信任策略；析构时按原值恢复。 */
		ECatDomainPolicy SavedProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;
		/** 原始自动 starter 装配开关；析构时按原值恢复。 */
		bool bSavedAutoConfigureStarterLoadout = false;
		/** 临时装备定义强引用；保证设置目录中的软指针在测试过程中不会悬空。 */
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> RuntimeEquipmentDefinitions;
		/** 临时鱼定义强引用；保证 Session 指针在捕获提交期间有效。 */
		TStrongObjectPtr<UCatFishDefinition> FishDefinition;
		/** 自动化测试世界；所有 Actor、Subsystem 和 GameMode 都在该世界内创建。 */
		FTestWorldWrapper WorldWrapper;
		/** 当前测试世界指针；CreateWorld 后可用，不拥有生命周期。 */
		UWorld* World = nullptr;
		/** Lake GameMode 实例；只打开命令 gate 和 Active 身份记录。 */
		ACatfishingGameModeBase* GameMode = nullptr;
		/** 真实玩家 Controller；SubmitScoop 从它的 UCatFishingCommandComponent 发起。 */
		ACatfishingPlayerController* Controller = nullptr;
		/** 测试专用 LocalPlayer；只用于让 Controller 满足正式本地输入 gate，不参与登录准入。 */
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		/** 真实 PlayerState；StableNetId 从这里读出并贯穿 GameMode/Fishing/Items。 */
		ACatfishingPlayerState* PlayerState = nullptr;
		/** 真实玩家 Character；个人鱼护和装备组件都由它持有。 */
		ACatCharacter* Character = nullptr;
		/** Fishing 世界服务；测试只向它登记已构造会话索引。 */
		UCatFishingService* FishingService = nullptr;
		/** Items 世界服务；个人鱼护注册和捕获写入都走它的正式接口。 */
		UCatItemsService* ItemsService = nullptr;
		/** Collection/Imprint 世界服务；捕获成功后应留下 FishRecorded 投递。 */
		UCatRunImprintService* ImprintService = nullptr;
		/** 测试水域 Actor；由注入几何 BeginPlay 后注册进 WaterQuerySubsystem。 */
		ACatWaterRegion* WaterRegion = nullptr;
		/** 近岸鱼表现 Actor；RequestScoop 的几何判定读取它的权威位置。 */
		ACatFishEncounterActor* FishEncounter = nullptr;
		/** Fishing Session Actor；夹具把它设置到 NearShore，再让 Controller 命令正式提交。 */
		ACatFishingSession* Session = nullptr;
		/** 岸上地面 Actor；只提供合法坡度和视线校验所需的碰撞。 */
		AActor* GroundActor = nullptr;
		/** 本次钓鱼会话 ID；同时写入 Session 和 FishingService 单活跃索引。 */
		FGuid FishingSessionId;
		/** 本次抛竿尝试 ID；用于 Session/FishEncounter 身份一致性。 */
		FGuid CastAttemptId;

		/** 设置流程：保存当前 CDO 后写入本测试所需的最小运行配置，不调用 SaveConfig。 */
		void OverrideSettings()
		{
			FishingSettings = GetMutableDefault<UCatFishingSettings>();
			AbilitySettings = GetMutableDefault<UCatAbilitySettings>();
			ItemsSettings = GetMutableDefault<UCatItemsSettings>();
			EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();

			bSavedFishingRuntime = FishingSettings->bEnableFishingRuntime;
			bSavedNearShoreValidation = FishingSettings->bEnableNearShoreValidation;
			SavedScoopReachCentimeters = FishingSettings->ScoopReachCentimeters;
			SavedTerminalReplicationWindowSeconds = FishingSettings->TerminalReplicationWindowSeconds;
			SavedTrueBiteWindowSeconds = FishingSettings->TrueBiteWindowSeconds;
			SavedNearShoreWidthCentimeters = FishingSettings->NearShoreWidthCentimeters;
			SavedMaximumScoopVerticalDeltaCentimeters = FishingSettings->MaximumScoopVerticalDeltaCentimeters;
			FishingSettings->bEnableFishingRuntime = true;
			FishingSettings->bEnableNearShoreValidation = true;
			FishingSettings->ScoopReachCentimeters = 500.0;
			FishingSettings->TerminalReplicationWindowSeconds = 3.0;
			FishingSettings->TrueBiteWindowSeconds = 1.0;
			FishingSettings->NearShoreWidthCentimeters = 300.0;
			FishingSettings->MaximumScoopVerticalDeltaCentimeters = 250.0;

			bSavedAbilityRuntime = AbilitySettings->bEnableCharacterAbilityRuntime;
			bSavedInitialAttributeTuning = AbilitySettings->bEnableInitialAttributeTuning;
			SavedReplicationPolicy = AbilitySettings->ReplicationPolicy;
			SavedInitialPoison = AbilitySettings->InitialPoison;
			SavedInitialFishingStrength = AbilitySettings->InitialFishingStrength;
			SavedInitialFightStamina = AbilitySettings->InitialFightStamina;
			AbilitySettings->bEnableCharacterAbilityRuntime = true;
			AbilitySettings->bEnableInitialAttributeTuning = true;
			AbilitySettings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
			AbilitySettings->InitialPoison = 0.0f;
			AbilitySettings->InitialFishingStrength = 5.0f;
			AbilitySettings->InitialFightStamina = 5.0f;

			SavedPersonalGuardCapacity = ItemsSettings->PersonalGuardCapacity;
			SavedSharedFishTankCapacity = ItemsSettings->SharedFishTankCapacity;
			ItemsSettings->PersonalGuardCapacity = 2;
			ItemsSettings->SharedFishTankCapacity = 2;

			SavedEquipmentDefinitions = EquipmentSettings->Definitions;
			SavedProfileLoadoutTrustPolicy = EquipmentSettings->ProfileLoadoutTrustPolicy;
			bSavedAutoConfigureStarterLoadout = EquipmentSettings->bAutoConfigureStarterLoadout;
			RuntimeEquipmentDefinitions.Reserve(4);
			RuntimeEquipmentDefinitions.Emplace(MakeEquipmentDefinition(RodId, ECatEquipmentKind::Rod));
			RuntimeEquipmentDefinitions.Emplace(MakeEquipmentDefinition(BaitId, ECatEquipmentKind::Bait));
			RuntimeEquipmentDefinitions.Emplace(MakeEquipmentDefinition(FloatId, ECatEquipmentKind::Float));
			RuntimeEquipmentDefinitions.Emplace(MakeEquipmentDefinition(ScoopNetId, ECatEquipmentKind::ScoopNet));
			EquipmentSettings->Definitions = {
				RuntimeEquipmentDefinitions[0].Get(),
				RuntimeEquipmentDefinitions[1].Get(),
				RuntimeEquipmentDefinitions[2].Get(),
				RuntimeEquipmentDefinitions[3].Get()
			};
			EquipmentSettings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			EquipmentSettings->bAutoConfigureStarterLoadout = false;

			FishDefinition.Reset(MakeFishDefinition());
			FishingSessionId = FGuid::NewGuid();
			CastAttemptId = FGuid::NewGuid();
		}

		/** 恢复流程：把所有 CDO 字段放回测试前状态，使本测试不影响同一进程后续自动化。 */
		void RestoreSettings()
		{
			if (FishingSettings)
			{
				FishingSettings->bEnableFishingRuntime = bSavedFishingRuntime;
				FishingSettings->bEnableNearShoreValidation = bSavedNearShoreValidation;
				FishingSettings->ScoopReachCentimeters = SavedScoopReachCentimeters;
				FishingSettings->TerminalReplicationWindowSeconds = SavedTerminalReplicationWindowSeconds;
				FishingSettings->TrueBiteWindowSeconds = SavedTrueBiteWindowSeconds;
				FishingSettings->NearShoreWidthCentimeters = SavedNearShoreWidthCentimeters;
				FishingSettings->MaximumScoopVerticalDeltaCentimeters = SavedMaximumScoopVerticalDeltaCentimeters;
			}
			if (AbilitySettings)
			{
				AbilitySettings->bEnableCharacterAbilityRuntime = bSavedAbilityRuntime;
				AbilitySettings->bEnableInitialAttributeTuning = bSavedInitialAttributeTuning;
				AbilitySettings->ReplicationPolicy = SavedReplicationPolicy;
				AbilitySettings->InitialPoison = SavedInitialPoison;
				AbilitySettings->InitialFishingStrength = SavedInitialFishingStrength;
				AbilitySettings->InitialFightStamina = SavedInitialFightStamina;
			}
			if (ItemsSettings)
			{
				ItemsSettings->PersonalGuardCapacity = SavedPersonalGuardCapacity;
				ItemsSettings->SharedFishTankCapacity = SavedSharedFishTankCapacity;
			}
			if (EquipmentSettings)
			{
				EquipmentSettings->Definitions = SavedEquipmentDefinitions;
				EquipmentSettings->ProfileLoadoutTrustPolicy = SavedProfileLoadoutTrustPolicy;
				EquipmentSettings->bAutoConfigureStarterLoadout = bSavedAutoConfigureStarterLoadout;
			}
		}

		/** 世界创建流程：启动一个 Game World 并预期 Run StateTree 因测试未配置而失败，随后只打开本闭环需要的命令入口。 */
		bool CreateWorld(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("Create Fishing player-entry Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
			{
				return false;
			}
			World = WorldWrapper.GetTestWorld();
			if (!Test.TestNotNull(TEXT("Read created player-entry World"), World))
			{
				return false;
			}
			World->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
			// 启动测试世界后才读取 GameMode 和各玩法 Subsystem，保证后续入口命令走正式 BeginPlay 接线。
			WorldWrapper.BeginPlayInTestWorld();
			GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
			FishingService = World->GetSubsystem<UCatFishingService>();
			ItemsService = World->GetSubsystem<UCatItemsService>();
			ImprintService = World->GetSubsystem<UCatRunImprintService>();
			return Test.TestNotNull(TEXT("Spawn Lake GameMode for player-entry command gate"), GameMode)
				&& Test.TestNotNull(TEXT("Create FishingService for player-entry route"), FishingService)
				&& Test.TestNotNull(TEXT("Create ItemsService for player-entry capture"), ItemsService)
				&& Test.TestNotNull(TEXT("Create ImprintService for player-entry archive"), ImprintService);
		}

		/** 玩家装配流程：创建 Controller、PlayerState 与 Character，完成占有后验证个人 FishGuard 已由 Character 正式注册。 */
		bool SpawnPlayer(FAutomationTestBase& Test)
		{
			LocalPlayer.Reset(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
			Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
			PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
			Character = World ? World->SpawnActor<ACatCharacter>(FVector(-100.0, 0.0, 150.0), FRotator::ZeroRotator) : nullptr;
			if (!Test.TestNotNull(TEXT("Create player-entry LocalPlayer for input gate"), LocalPlayer.Get())
				|| !Test.TestNotNull(TEXT("Spawn player-entry Controller"), Controller)
				|| !Test.TestNotNull(TEXT("Spawn player-entry PlayerState"), PlayerState)
				|| !Test.TestNotNull(TEXT("Spawn player-entry Character"), Character))
			{
				return false;
			}
			Controller->SetPlayer(LocalPlayer.Get());
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetIdValue, FName(TEXT("CAT_TEST")));
			PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			Controller->PlayerState = PlayerState;
			Character->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			ACatfishingGameModeBase::FAdmissionRecord Record;
			Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
			Record.Controller = Controller;
			GameMode->AdmissionRecords.Add(StableNetIdValue, Record);
			GameMode->bRunCommandsOpen = true;
			Test.TestTrue(TEXT("Player-entry Controller satisfies local input gate"), Controller->IsLocalController());
			FCatContainerSnapshot GuardSnapshot;
			const bool bHasGuard = Character->GetPersonalFishGuardId().IsValid()
				&& ItemsService->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), GuardSnapshot);
			return Test.TestTrue(TEXT("Possessed Character registers a real personal FishGuard"), bHasGuard)
				&& Test.TestEqual(TEXT("Initial personal FishGuard is empty"), GuardSnapshot.Fish.Num(), 0)
				&& Test.TestEqual(TEXT("Initial personal FishGuard is a personal container"), GuardSnapshot.Kind,
					ECatContainerKind::PersonalGuard);
		}

		/** 装备流程：通过正式 EquipmentComponent 装配 Fishing 入口装备、授予一份普通饵，并创建会话占用记录供捕获前 finalization 使用。 */
		bool ConfigureEquipment(FAutomationTestBase& Test)
		{
			UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
			if (!Test.TestNotNull(TEXT("Character owns EquipmentComponent"), Equipment))
			{
				return false;
			}
			const FCatDomainCommandResult Configure = Equipment->ConfigureLoadoutFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, RodId, BaitId, FloatId, ScoopNetId);
			if (!Test.TestTrue(TEXT("Configure rod/bait/float/scoop through Equipment authority API"), Configure.bCommitted))
			{
				return false;
			}
			const FCatDomainCommandResult BaitGrant = Equipment->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, BaitId, 1);
			if (!Test.TestTrue(TEXT("Grant one normal bait through Equipment authority API"), BaitGrant.bCommitted))
			{
				return false;
			}
			const FCatFishingUseReservationResult BeginUse = Equipment->BeginFishingUse(
				FishingSessionId, RodId, BaitId, FloatId, Equipment->GetSnapshot().Revision);
			if (!Test.TestEqual(TEXT("Begin Fishing use reservation succeeds"), BeginUse.Error, ECatDomainCommandError::None))
			{
				return false;
			}
			const FCatFishingUseOperationResult Wear = Equipment->SetAccumulatedFishingRodWear(FishingSessionId, 1, 0.1);
			return Test.TestTrue(TEXT("Seed one monotonic rod-wear sample for capture finalization"), Wear.bApplied);
		}

		/** 水域流程：注入一块已烘焙矩形水域，并在岸上放置一块可被抢抄地面射线命中的碰撞。 */
		bool SpawnWaterAndGround(FAutomationTestBase& Test)
		{
			const FCatWaterGeometryCache Cache = BuildWaterCache();
			if (!Test.TestTrue(TEXT("Build player-entry water geometry"), Cache.IsRuntimeReady()))
			{
				return false;
			}
			WaterRegion = World ? World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity) : nullptr;
			if (!Test.TestNotNull(TEXT("Spawn player-entry WaterRegion"), WaterRegion))
			{
				return false;
			}
			FCatWaterRegionTestAccess::InjectBakedGeometry(*WaterRegion, Cache);
			WaterRegion->FinishSpawning(FTransform::Identity);
			GroundActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			UBoxComponent* Ground = GroundActor ? NewObject<UBoxComponent>(GroundActor, TEXT("PlayerEntryScoopGround")) : nullptr;
			if (!Test.TestNotNull(TEXT("Spawn player-entry scoop ground Actor"), GroundActor)
				|| !Test.TestNotNull(TEXT("Create player-entry ground collision"), Ground))
			{
				return false;
			}
			GroundActor->AddInstanceComponent(Ground);
			GroundActor->SetRootComponent(Ground);
			Ground->SetBoxExtent(FVector(5.0, 50.0, 10.0));
			Ground->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Ground->SetCollisionResponseToAllChannels(ECR_Block);
			Ground->RegisterComponent();
			GroundActor->SetActorLocation(FVector(-100.0, 0.0, 90.0));
			return Test.TestTrue(TEXT("WaterRegion registers baked geometry for shore queries"),
				WaterRegion->HasValidBakedGeometry());
		}

		/** 会话夹具流程：构造一条已到 NearShore 的服务器会话事实，再登记到 FishingService 的单活跃索引。 */
		bool SpawnNearShoreSession(FAutomationTestBase& Test)
		{
			FishEncounter = World ? World->SpawnActor<ACatFishEncounterActor>(FVector(80.0, 0.0, 0.0), FRotator::ZeroRotator) : nullptr;
			Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
			if (!Test.TestNotNull(TEXT("Spawn player-entry fish encounter"), FishEncounter)
				|| !Test.TestNotNull(TEXT("Spawn player-entry FishingSession"), Session))
			{
				return false;
			}
			Test.TestTrue(TEXT("Initialize fish encounter identity"),
				FishEncounter->InitializeAuthoritativeIdentity(FishingSessionId, CastAttemptId, FishId, 80.0, 1.0));
			Session->Snapshot.FishingSessionId = FishingSessionId;
			Session->Snapshot.CastAttemptId = CastAttemptId;
			Session->Snapshot.Phase = ECatFishingPhase::NearShore;
			Session->Snapshot.Revision = 1;
			Session->Snapshot.PhaseEpoch = 1;
			Session->Snapshot.FisherPlayerState = PlayerState;
			Session->Snapshot.FishDefinitionId = FishId;
			Session->Snapshot.FishEncounterActor = FishEncounter;
			Session->Snapshot.bGiant = false;
			Session->AttemptSnapshot.WaterRegion = WaterRegion->GetWaterRegionHandle();
			Session->FishDefinition = FishDefinition.Get();
			Session->FisherCharacter = Character;
			Session->FisherStableNetId = StableNetIdValue;
			Session->FisherGuardContainerId = Character->GetPersonalFishGuardId();
			Session->CastEquipment = Character->GetEquipmentComponent();
			Session->ItemsService = ItemsService;
			Session->FishWeightKilograms = 2.5;
			Session->FightParticipantIds.Add(StableNetIdValue);
			Session->FightParticipantCharacters.Add(StableNetIdValue, Character);
			Session->bPrepared = true;
			Session->bPublished = true;
			Session->RefreshFightSummary();
			FishingService->Sessions.Add(FishingSessionId, Session);
			FishingService->SessionFisherById.Add(FishingSessionId, StableNetIdValue);
			FishingService->ActiveSessionByFisher.Add(StableNetIdValue, FishingSessionId);
			return Test.TestTrue(TEXT("FishingService can find the active player-entry session"),
				FishingService->FindSession(FishingSessionId) == Session);
		}

		/** 执行流程：从 Controller 命令组件发起一次 Scoop，并核对命令回执、Session 终态、FishGuard 实物和 Imprint 投递。 */
		bool SubmitAndVerifyScoop(FAutomationTestBase& Test)
		{
			UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
			if (!Test.TestNotNull(TEXT("Controller owns Fishing command component"), Commands))
			{
				return false;
			}
			const FCatFishingInputEdge Edge = Commands->SubmitScoop();
			FCatFishingCommandResult CommandResult;
			const bool bHasResult = Commands->TryGetResult(Edge.RequestId, CommandResult);
			if (!Test.TestTrue(TEXT("SubmitScoop returns a local authority command result"), bHasResult))
			{
				return false;
			}
			Test.TestEqual(TEXT("Scoop result keeps RequestScoop command type"), CommandResult.CommandType,
				ECatFishingCommandType::RequestScoop);
			Test.TestTrue(TEXT("Scoop result is committed from player command entry"), CommandResult.bCommitted);
			Test.TestEqual(TEXT("Scoop result reports no command-layer error"), CommandResult.Error,
				ECatFishingCommandError::None);
			Test.TestEqual(TEXT("Scoop result points to the active FishingSession"), CommandResult.FishingSessionId,
				FishingSessionId);
			Test.TestEqual(TEXT("Scoop result carries the resolved Session revision"), CommandResult.Revision,
				Session->GetSnapshot().Revision);
			Test.TestEqual(TEXT("Session resolves to caught after player command"), Session->GetSnapshot().Phase,
				ECatFishingPhase::Resolved);
			Test.TestEqual(TEXT("Session records caught outcome after player command"), Session->GetSnapshot().Outcome,
				ECatFishingOutcome::Caught);

			FCatContainerSnapshot GuardSnapshot;
			const bool bHasGuard = ItemsService->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), GuardSnapshot);
			if (!Test.TestTrue(TEXT("Read personal FishGuard after player command"), bHasGuard))
			{
				return false;
			}
			Test.TestEqual(TEXT("Player FishGuard receives exactly one fish"), GuardSnapshot.Fish.Num(), 1);
			if (GuardSnapshot.Fish.Num() == 1)
			{
				const FCatFishInstance& Fish = GuardSnapshot.Fish[0];
				Test.TestEqual(TEXT("FishGuard fish comes from the player-entry session"), Fish.SourceFishingSessionId,
					FishingSessionId);
				Test.TestEqual(TEXT("FishGuard fish keeps runtime FishDefinitionId"), Fish.FishDefinitionId, FishId);
				Test.TestEqual(TEXT("FishGuard fish owner is the command player's StableNetId"), Fish.OwnerStableNetId,
					StableNetIdValue);
				Test.TestTrue(TEXT("FishGuard fish has a real instance id"), Fish.FishInstanceId.IsValid());
			}
			Test.TestEqual(TEXT("Committed capture enqueues one FishRecorded grant"), ImprintService->GetPendingGrantAckCount(), 1);
			return true;
		}

		/** 析构流程：先恢复 CDO，再让成员析构销毁测试 World 与强引用，避免临时配置泄露到后续自动化。 */
		~FPlayerEntryFixture()
		{
			RestoreSettings();
		}
	};
}

bool FCatFishingPlayerEntryFullLoopTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingPlayerEntryTest;
	// 玩家入口闭环测试流程：先覆盖本例需要的运行期配置，再依次创建测试世界、装配本地玩家、
	// 配置装备、水域和 NearShore 会话，最后从 Controller 的抢抄入口提交命令并核验 FishGuard/Imprint 结果；任一步失败都会短路，夹具析构负责恢复临时 CDO 配置。
	FPlayerEntryFixture Fixture;
	Fixture.OverrideSettings();
	return Fixture.CreateWorld(*this)
		&& Fixture.SpawnPlayer(*this)
		&& Fixture.ConfigureEquipment(*this)
		&& Fixture.SpawnWaterAndGround(*this)
		&& Fixture.SpawnNearShoreSession(*this)
		&& Fixture.SubmitAndVerifyScoop(*this)
		&& !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
