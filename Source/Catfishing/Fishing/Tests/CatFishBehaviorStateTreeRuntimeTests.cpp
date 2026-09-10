#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "OnlineSubsystemTypes.h"
#include "Components/StateTreeComponent.h"
#include "Data/CatFishDefinition.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "StateTree.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorStateTreeRuntimeTest,
	"Catfishing.Unit.Fishing.Behavior.FormalTreeSelectsFeedbackBranchesOnlyAtFixedSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorStateTreeRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, TEXT("/Game/Data/StateTrees/ST_FishFight.ST_FishFight"));
	if (!TestNotNull(TEXT("正式鱼树可加载"), Tree)) return false;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();

	const auto CreateFixture = [&](const TCHAR* Name, const FCatFishSteeringConfig& SteeringConfig,
		const double StaminaRatio)
	{
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		ACatFishEncounterActor* Fish = World->SpawnActor<ACatFishEncounterActor>();
		UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
		if (!Session || !Rod || !Fish || !Runner) return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(nullptr, nullptr);
		// 此夹具只旁路与行为树无关的中鱼/物品事务，真实 Actor、Runner、正式树与条件仍运行。
		Fish->bIdentityInitialized = true;
		Runner->Session = Session;
		Runner->RodActor = Rod;
		Runner->FishActor = Fish;
		Runner->bInitialized = Runner->bRunning = true;
		Runner->InitialFishStamina = 100.0;
		Runner->State.FishStamina = 100.0 * StaminaRatio;
		Runner->State.CatStamina = 100.0;
		Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		Runner->SteeringRandom.Initialize(2003);
		Runner->SteeringConfig = SteeringConfig;
		if (!TestTrue(FString::Printf(TEXT("%s启动正式行为树"), Name), Fish->StartFishBehaviorFromAuthority(Tree, Runner)))
			return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(nullptr, nullptr);
		return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(Fish, Runner);
	};
	FCatFishSteeringConfig FreeConfig;
	FreeConfig.MinimumBehaviorDurationSeconds = 0.2;
	FreeConfig.OutwardDurationRangeSeconds = FVector2D(0.8, 0.8);
	FreeConfig.LateralDurationRangeSeconds = FVector2D(0.7, 0.7);
	FreeConfig.EaseOffDurationRangeSeconds = FVector2D(0.6, 0.6);
	FreeConfig.ActiveBoutDurationRangeSeconds = FVector2D(4.0, 4.0);
	FreeConfig.BlockedConfirmationSeconds = 0.15;
	FreeConfig.LoadSmoothingSeconds = 0.0;
	FCatFishSteeringConfig BlockedConfig = FreeConfig;
	BlockedConfig.OutwardDurationRangeSeconds = FVector2D(10.0, 10.0);
	BlockedConfig.LateralDurationRangeSeconds = FVector2D(10.0, 10.0);
	// 预算必定在刚换向、局部承诺尚未完成时耗尽，用真实树检验恢复的优先级。
	BlockedConfig.ActiveBoutDurationRangeSeconds = FVector2D(1.21, 1.29);
	FCatFishSteeringConfig TiredConfig = FreeConfig;
	TiredConfig.MinimumBehaviorDurationSeconds = 1.25;
	TiredConfig.OutwardDurationRangeSeconds = FVector2D(5.0, 5.0);
	TiredConfig.EaseOffDurationRangeSeconds = FVector2D(1.0, 1.0);
	TiredConfig.OutwardEffortRange = FVector2D(0.9, 0.9);
	TiredConfig.EaseOffEffortRange = FVector2D(0.35, 0.35);
	TiredConfig.OutwardAngularSpreadDegrees = 0.0;
	TiredConfig.EaseOffInwardBias = 0.0;
	const auto Free = CreateFixture(TEXT("自由外游"), FreeConfig, 1.0);
	const auto Blocked = CreateFixture(TEXT("持续受阻"), BlockedConfig, 1.0);
	const auto Tired = CreateFixture(TEXT("低体力外游"), TiredConfig, 0.2);
	if (!Free.Key || !Blocked.Key || !Tired.Key) return false;
	TestEqual(TEXT("正式树首次选择外冲叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestFalse(TEXT("鱼树不会注册独立组件Tick"), Free.Key->FishBehaviorStateTree->PrimaryComponentTick.bCanEverTick);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 1.0;
	const double BeforeWorldTickBoutElapsed = Free.Value->SteeringState.ActiveBoutElapsedSeconds;
	const int32 BeforeWorldTickSeed = Free.Value->SteeringRandom.GetCurrentSeed();
	World->Tick(LEVELTICK_All, 0.5f);
	TestEqual(TEXT("即便已到期，普通WorldTick也不能替固定步转移"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestEqual(TEXT("普通WorldTick不推进连续对抗预算"), Free.Value->SteeringState.ActiveBoutElapsedSeconds, BeforeWorldTickBoutElapsed);
	TestEqual(TEXT("普通WorldTick不消耗策略随机流"), Free.Value->SteeringRandom.GetCurrentSeed(), BeforeWorldTickSeed);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 0.0;

	constexpr double FixedStepSeconds = 0.05;
	bool bContinuousControl = true;
	FCatFishBehaviorFeedback Feedback;
	Feedback.ExpectedFreeSpeedCentimetersPerSecond = 100.0;
	const auto Advance = [&](const TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>& Fixture, const bool bBlocked)
	{
		Feedback.NormalizedLineLoad = bBlocked ? 1.0 : 0.0;
		Feedback.bLineTaut = bBlocked;
		Feedback.ActiveSwimDirection = Fixture.Value->SteeringState.CurrentDirection;
		Feedback.FishStaminaRatio = Fixture.Value->State.FishStamina / Fixture.Value->InitialFishStamina;
		Feedback.ActualFishVelocityCentimetersPerSecond = bBlocked ? FVector::ZeroVector
			: Feedback.ActiveSwimDirection * 100.0;
		const FVector BeforeDirection = Fixture.Value->SteeringState.CurrentDirection;
		const double BeforeEffort = Fixture.Value->SteeringState.CurrentEffortRatio;
		FVector ActualDirection;
		if (!FCatFishSteeringModel::AdvanceFeedback(Fixture.Value->SteeringConfig, Feedback,
			FixedStepSeconds, Fixture.Value->SteeringState)
			|| !Fixture.Key->TickFishBehaviorFromAuthority(static_cast<float>(FixedStepSeconds))
			|| !FCatFishSteeringModel::Step(Fixture.Value->SteeringConfig, FVector::ForwardVector,
				FixedStepSeconds, Fixture.Value->SteeringRandom, Fixture.Value->SteeringState, ActualDirection)) return false;
		const double TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(BeforeDirection, ActualDirection), -1.0, 1.0)));
		const double EffortDelta = Fixture.Value->SteeringState.CurrentEffortRatio - BeforeEffort;
		bContinuousControl &= TurnDegrees <= Fixture.Value->SteeringConfig.MaximumTurnRateDegreesPerSecond * FixedStepSeconds + 0.001
			&& EffortDelta <= Fixture.Value->SteeringConfig.EffortRisePerSecond * FixedStepSeconds + 0.000001
			&& -EffortDelta <= Fixture.Value->SteeringConfig.EffortFallPerSecond * FixedStepSeconds + 0.000001;
		return true;
	};
	const auto IsActive = [](const ECatFishBehavior Behavior)
	{
		return Behavior == ECatFishBehavior::OutwardRush || Behavior == ECatFishBehavior::LateralArc;
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!TestTrue(TEXT("自由路径固定步执行"), Advance(Free, false))
			|| !TestTrue(TEXT("受阻路径固定步执行"), Advance(Blocked, true))) return false;
		if (Index < 3)
		{
			TestEqual(TEXT("受阻也必须完成最短承诺"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
		}
	}
	TestEqual(TEXT("相同种子下自由游仍在外冲"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestEqual(TEXT("相同种子下受阻由真实树边转横切"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::LateralArc);
	for (int32 Index = 4; Index < 17; ++Index)
		if (!Advance(Free, false)) return false;
	TestEqual(TEXT("外冲最长时限由树选择缓游叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);

	const double FirstBoutDuration = Blocked.Value->SteeringState.ActiveBoutDurationSeconds;
	int32 ActiveChanges = 1;
	bool bRecoveryBypassedLocalMinimum = false;
	for (int32 Index = 0; Index < 40 && IsActive(Blocked.Value->SteeringState.Behavior); ++Index)
	{
		const ECatFishBehavior BeforeBehavior = Blocked.Value->SteeringState.Behavior;
		const double BeforeLocalElapsed = Blocked.Value->SteeringState.BehaviorElapsedSeconds;
		const double BeforeBoutElapsed = Blocked.Value->SteeringState.ActiveBoutElapsedSeconds;
		if (!TestTrue(TEXT("反复受阻仍由正式树推进"), Advance(Blocked, true))) return false;
		if (IsActive(Blocked.Value->SteeringState.Behavior))
		{
			TestTrue(TEXT("主动换向不会重置已消耗的对抗预算"),
				Blocked.Value->SteeringState.ActiveBoutElapsedSeconds > BeforeBoutElapsed);
			TestEqual(TEXT("主动换向沿用本轮已抽取的预算"), Blocked.Value->SteeringState.ActiveBoutDurationSeconds, FirstBoutDuration);
			if (Blocked.Value->SteeringState.Behavior != BeforeBehavior) ++ActiveChanges;
		}
		else
		{
			bRecoveryBypassedLocalMinimum = BeforeLocalElapsed + FixedStepSeconds < BlockedConfig.MinimumBehaviorDurationSeconds;
		}
	}
	TestTrue(TEXT("持续受阻会在外冲和横切之间多次重试"), ActiveChanges >= 4);
	TestEqual(TEXT("多次受阻换向最终仍按总预算恢复"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestTrue(TEXT("总预算到期恢复不被刚进入行为的局部承诺挡住"), bRecoveryBypassedLocalMinimum);
	const double FinishedBoutElapsed = Blocked.Value->SteeringState.ActiveBoutElapsedSeconds;
	TestTrue(TEXT("预算到期在一个固定步内得到处理"), FinishedBoutElapsed <= FirstBoutDuration + FixedStepSeconds + 0.000001);
	for (int32 Index = 0; Index < 20 && Blocked.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff; ++Index)
	{
		if (!Advance(Blocked, true)) return false;
		if (Blocked.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff)
			TestEqual(TEXT("缓游不继续消耗连续对抗预算"), Blocked.Value->SteeringState.ActiveBoutElapsedSeconds, FinishedBoutElapsed);
	}
	TestEqual(TEXT("恢复后仍受阻时由正式树选择横切重试"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::LateralArc);
	TestEqual(TEXT("恢复结束才开始新一轮连续对抗计时"), Blocked.Value->SteeringState.ActiveBoutElapsedSeconds, 0.0);

	TestTrue(TEXT("低体力在进入首轮时缩短对抗预算"), FMath::IsNearlyEqual(
		Tired.Value->SteeringState.ActiveBoutDurationSeconds, 2.8, 0.000001));
	for (int32 Index = 0; Index < 26; ++Index)
		if (!TestTrue(TEXT("低体力也执行真实运动步"), Advance(Tired, false))) return false;
	TestEqual(TEXT("低体力越过最短承诺后仍能继续有效外冲"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	for (int32 Index = 0; Index < 40 && IsActive(Tired.Value->SteeringState.Behavior); ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("低体力主动阶段按缩短后的预算进入恢复"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	for (int32 Index = 0; Index < 40 && Tired.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff; ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("低体力恢复后仍有下一次外冲"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestTrue(TEXT("从缓游重新出力时保留实际力度爬升过程"), Tired.Value->SteeringState.CurrentEffortRatio < 0.5);
	TestTrue(TEXT("从缓游重新外冲时保留实际转向过程"),
		FVector::DotProduct(Tired.Value->SteeringState.CurrentDirection, FVector::ForwardVector) < 0.5);
	for (int32 Index = 0; Index < 26; ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("缓游后的外冲不会刚完成转向就因低体力被截断"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	const double ActiveOutwardFraction = Tired.Value->SteeringState.CurrentEffortRatio
		* FVector::DotProduct(Tired.Value->SteeringState.CurrentDirection, FVector::ForwardVector);
	TestTrue(TEXT("低体力仍有完成转向与爬升后的实际向外主动推力窗口"), ActiveOutwardFraction > 0.85);
	TestTrue(TEXT("真实树所有切换的运动输出遵守转向和力度变化上限"), bContinuousControl);

	Free.Key->StopFishBehaviorFromAuthority();
	Blocked.Key->StopFishBehaviorFromAuthority();
	Tired.Key->StopFishBehaviorFromAuthority();
	TestFalse(TEXT("停止后固定步不能推进树"), Free.Key->TickFishBehaviorFromAuthority(0.05f));
	TestFalse(TEXT("退出清除Runner行为代理"), Free.Key->AuthorityFightRunner.IsValid());
	Free.Value->bRunning = Blocked.Value->bRunning = Tired.Value->bRunning = false;

	// 补齐生产消费者：只准备已经中鱼的事务状态，之后由真正的 HandleFixedStep
	// 执行树、受力、最终水面落位、ASC 付款及 Session 发布，不手工提交 Step 或余额。
	UCatEquipmentSettings* EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();
	TGuardValue<TArray<TSoftObjectPtr<UCatEquipmentDefinition>>> SavedDefinitions(EquipmentSettings->Definitions, {});
	TGuardValue<ECatDomainPolicy> SavedTrust(EquipmentSettings->ProfileLoadoutTrustPolicy, ECatDomainPolicy::Enabled);
	TGuardValue<int32> SavedSlots(EquipmentSettings->InventorySlotCapacity, 12);
	TGuardValue<int32> SavedStacks(EquipmentSettings->InventoryQuantityStackCapacity, 20);
	TGuardValue<FName> SavedWood(EquipmentSettings->DriftwoodDefinitionId, FName(TEXT("IntentRuntimeWood")));
	TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
	const auto AddDefinition = [&](const FName Id, const ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
		Definitions.Emplace(Definition);
		Definition->EquipmentDefinitionId = Id;
		Definition->FunctionalRouteId = Definition->LoadoutSlotId = Id;
		Definition->Kind = Kind;
		Definition->bEnableRuntimeDefinition = true;
		EquipmentSettings->Definitions.Add(TSoftObjectPtr<UCatEquipmentDefinition>(Definition));
		return Definition;
	};
	UCatEquipmentDefinition* RodDefinition = AddDefinition(TEXT("IntentRuntimeRod"), ECatEquipmentKind::Rod);
	RodDefinition->MaximumRodDurability = 1000.0;
	RodDefinition->MaximumLineLengthCentimeters = 1500.0;
	RodDefinition->HighTensionWearMultiplier = 1.0;
	RodDefinition->UseActorClass = ACatFishingRodActor::StaticClass();
	RodDefinition->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
	UCatEquipmentDefinition* BaitDefinition = AddDefinition(TEXT("IntentRuntimeBait"), ECatEquipmentKind::Bait);
	BaitDefinition->bRunConsumable = true;
	BaitDefinition->BiteRateMultiplier = BaitDefinition->MinimumBiteDelayMultiplier = 1.0;
	AddDefinition(TEXT("IntentRuntimeFloat"), ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1000.0;
	AddDefinition(EquipmentSettings->DriftwoodDefinitionId, ECatEquipmentKind::Driftwood)->bRunConsumable = true;
	for (const TStrongObjectPtr<UCatEquipmentDefinition>& Definition : Definitions)
		if (!TestTrue(TEXT("生产付款夹具的装备定义完整"), Definition->IsRuntimeDefinitionReady())) return false;

	double PreviousFishDrain = 0.0;
	double ReferenceCatDrain = 0.0;
	FVector ReferenceFishLocation = FVector::ZeroVector;
	for (const double Price : {0.0, 2.25, 4.5})
	{
		FTestWorldWrapper PaymentWorld;
		if (!TestTrue(TEXT("创建独立生产付款世界"), PaymentWorld.CreateTestWorld(EWorldType::Game))) return false;
		PaymentWorld.ForwardErrorMessages(this);
		UWorld* Payment = PaymentWorld.GetTestWorld();
		FURL PaymentURL;
		PaymentURL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("生产固定步使用正式准入GameMode"), Payment->SetGameMode(PaymentURL))) return false;
		ACatCharacter* Character = Payment->SpawnActor<ACatCharacter>(FVector(-200.0, 0.0, 200.0), FRotator::ZeroRotator);
		ACatfishingPlayerState* Player = Payment->SpawnActor<ACatfishingPlayerState>();
		ACatWaterRegion* Region = Payment->SpawnActor<ACatWaterRegion>();
		ACatFishingSession* Session = Payment->SpawnActor<ACatFishingSession>();
		ACatFishingRodActor* Rod = Payment->SpawnActor<ACatFishingRodActor>();
		ACatFishEncounterActor* Fish = Payment->SpawnActor<ACatFishEncounterActor>();
		if (!TestTrue(TEXT("生产步骤依赖全部生成"), Character && Player && Region && Session && Rod && Fish)) return false;
		Character->SetPlayerState(Player);
		FCatWaterGeometryBuildInput Geometry;
		Geometry.RegionId = TEXT("IntentRuntimeWater");
		Geometry.WaterPointVerticalToleranceCm = 10.0;
		Geometry.BankHeightToleranceCm = 20.0;
		Geometry.BoundaryToleranceCm = 2.0;
		Geometry.MaxLandingCorrectionCm = 20.0;
		Geometry.MinimumWaterInsetCm = 5.0;
		FCatWaterPolygonBuildInput& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = {FVector2D(-2000.0, -2000.0), FVector2D(2000.0, -2000.0),
			FVector2D(2000.0, 2000.0), FVector2D(-2000.0, 2000.0)};
		const auto Baked = FCatWaterGeometry::Build(Geometry);
		if (!TestTrue(TEXT("烘焙付款夹具真实水域"), Baked.bSucceeded)) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
		// 与正式 SpawnActorDeferred 配置顺序一致，BeginPlay 创建刚体前冻结规范锚点。
		if (!TestTrue(TEXT("物理竿初始化前配置规范竿尖和握点"), Rod->ConfigureCanonicalAnchorsFromAuthority(
			FTransform::Identity, FTransform::Identity, FTransform(FVector(-200.0, 0.0, 0.0))))) return false;
		if (!TestTrue(TEXT("启动真实角色身体与水域生命周期"), PaymentWorld.BeginPlayInTestWorld())) return false;
		auto* Mode = Payment->GetAuthGameMode<ACatfishingGameModeBase>();
		Mode->bRunCommandsOpen = true;
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bFishingAllowed = true;
		auto* Controller = Payment->SpawnActor<ACatfishingPlayerController>();
		if (!Controller) return false;
		Controller->PlayerState = Player;
		Character->SetPlayerState(Player);
		Controller->Possess(Character);
		Controller->SetActorTickEnabled(false);
		Player->SetPlayerId(1);
		const FUniqueNetIdRef PlayerNetId = FUniqueNetIdString::Create(TEXT("BehaviorRuntimePayment"), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(PlayerNetId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		if (!TestTrue(TEXT("付款参与者持有正式准入和已初始化刚体"), Mode->CanAcceptFishingCommand(Controller)
			&& Character->GetPhysicalBodyComponent()->GetBody())) return false;
		if (!TestTrue(TEXT("生产付款水域已注册"), Payment->GetSubsystem<UCatWaterQuerySubsystem>()
			->QueryShoreRelation(FVector(500.0, 0.0, 0.0), Region->GetWaterRegionHandle()).bSucceeded)) return false;
		UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
		UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
		if (!TestTrue(TEXT("生产猫具有ASC与装备"), ASC && Equipment)) return false;
		ASC->InitAbilityActorInfo(Character, Character);
		if (!TestTrue(TEXT("生产ASC按正式身体定义播种属性"), ASC->InitializeCharacterAttributesFromDefinition(
			Character->GetCatDefinitionId()))) return false;
		const float CatStaminaMaximum = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
		if (!TestTrue(TEXT("生产猫具有有效ASC体力上限"), FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0f)) return false;
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), CatStaminaMaximum);
		for (const FName Id : {FName(TEXT("IntentRuntimeRod")), FName(TEXT("IntentRuntimeFloat"))})
			if (!TestTrue(TEXT("公开入口授予真实装备实例"), Equipment->GrantEquipmentFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
		if (!TestTrue(TEXT("公开入口授予一份鱼饵"), Equipment->GrantInventoryQuantityFromAuthority(
			FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("IntentRuntimeBait"), 1).bCommitted)) return false;
		const FGuid RodItemId = Equipment->GetSnapshot().RodItemInstanceId;
		if (!TestTrue(TEXT("部署已授予的同一鱼竿实例"), Equipment->Use(
			FGuid::NewGuid(), Equipment->GetSnapshot().Revision, RodItemId).bCommitted)) return false;
		const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
		const FGuid SessionId = FGuid::NewGuid();
		if (!TestTrue(TEXT("会话预留真实鱼竿和饵漂"), Equipment->BeginFishingUse(SessionId,
			RodItemId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
			Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision).bReserved)
			|| !TestTrue(TEXT("会话完成中鱼扣饵"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
		if (!TestTrue(TEXT("鱼竿绑定真实操作者及库存实例"), Rod->InitializeAuthoritativeIdentity(
			FGuid::NewGuid(), RodItemId, Loadout.RodDefinitionId, NAME_None, Player, nullptr, true, false))) return false;
		UCatFishingService* Service = Payment->GetSubsystem<UCatFishingService>();
		if (!TestTrue(TEXT("已部署的真实杆进入生产查找索引"), Service && Service->RegisterDeployedRod(Player, Rod))
			|| !TestTrue(TEXT("规范握点放在实际手爪后经生产校验建约束"), Rod->BeginPhysicalHoldFromAuthority(Player, true))
			|| !TestTrue(TEXT("实际握持后按部署事务显式授予拥有者主控"),
				Rod->SetPrimaryOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision) && Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Player))) return false;
		if (!TestEqual(TEXT("真实握边保留固定步主位"), Rod->GetOperatorCount(), 1)) return false;
		const FVector InitialFishPosition(500.0, 0.0, 0.0);
		Fish->SetActorLocation(InitialFishPosition);
		Fish->bIdentityInitialized = true;
		UCatFishDefinition* FishDefinition = NewObject<UCatFishDefinition>(Session);
		FishDefinition->FishFightStamina = 100.0;
		Session->FishDefinition = FishDefinition;
		Session->Snapshot.FishingSessionId = SessionId;
		Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
		Session->Snapshot.RodActor = Rod;
		Session->Snapshot.FishEncounterActor = Fish;
		Session->Snapshot.FishFightStaminaRemaining = FishDefinition->FishFightStamina;
		Session->AttemptSnapshot.RodItemInstanceId = RodItemId;
		Session->CastEquipment = Equipment;
		Session->FisherCharacter = Character;
		Service->Sessions.Add(SessionId, Session); // The rod receiver validates the same live session domain.
		UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
		Session->FightRunner = Runner;
		FCatFishingFightRunnerInit Init;
		Init.Session = Session;
		Init.RodActor = Rod;
		Init.FishActor = Fish;
		Init.PrimaryPlayerState = Player;
		Init.AbilitySystem = ASC;
		Init.WaterRegion = Region->GetWaterRegionHandle();
		Init.BehaviorStateTree = Tree;
		Init.RandomSeed = 2003;
		Init.bInitialPullHeld = true;
		Init.Config.FixedStepSeconds = FixedStepSeconds;
		Init.Config.PrimaryOperatorCatStrength = 50.0;
		Init.Config.PrimaryOperatorMassKilograms = Character->GetPhysicalBodyComponent()->GetBody()->GetMass();
		Init.Config.FishMassKilograms = 3.0;
		Init.Config.FishStrength = 30.0;
		Init.Config.CatStaminaMaximum = CatStaminaMaximum;
		Init.Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
		Init.Config.FishStaminaPerUnfulfilledMeter = Price;
		Init.Config.MaximumLineLengthCentimeters = RodDefinition->MaximumLineLengthCentimeters;
		Init.Config.RodDurability = RodDefinition->MaximumRodDurability;
		Init.Config.ReelSpeedCentimetersPerSecond = 80.0;
		Init.InitialState.CatStamina = CatStaminaMaximum;
		Init.InitialState.FishStamina = FishDefinition->FishFightStamina;
		Init.InitialState.FishWorldPosition = InitialFishPosition;
		Init.InitialState.LineLengthCentimeters = FVector::Distance(Rod->GetRodTipWorldTransform().GetLocation(), InitialFishPosition);
		Init.SteeringConfig.OutwardEffortRange = FVector2D(0.8, 0.8);
		Init.SteeringConfig.OutwardAngularSpreadDegrees = 0.0;
		if (!TestTrue(TEXT("生产Runner读取冻结配置并登记ASC参与者"), Runner->InitializeFromAuthority(Init))
			|| !TestTrue(TEXT("生产Runner启动正式树及固定步调度"), Runner->Start())) return false;
		int32 CatStaminaWrites = 0;
		int32 SessionPublications = 0;
		const FDelegateHandle StaminaHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda(
			[&](const FOnAttributeChangeData&) { ++CatStaminaWrites; });
		const FDelegateHandle SnapshotHandle = Session->OnSnapshotChanged.AddLambda([&]() { ++SessionPublications; });
		Runner->HandleFixedStep();
		ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(StaminaHandle);
		Session->OnSnapshotChanged.Remove(SnapshotHandle);
		TestTrue(TEXT("最终水面落位和所有资源消费者均成功"), Runner->IsRunning() && !Session->IsTerminal());
		TestEqual(TEXT("真实ASC每固定步仅有一次体力写入"), CatStaminaWrites, 1);
		TestEqual(TEXT("真实Session每固定步仅发布一次数值快照"), SessionPublications, 1);
		TestEqual(TEXT("真实Encounter已接收同一步实际鱼位置"), Runner->State.FishWorldPosition, Fish->GetActorLocation());
		// 初始鱼恰在线长球面上，因此本步没有历史超长误差；当前受力约束产生的
		// 实际收线位移仍须完整参与投影，不能误把总 ConstraintCorrection 全部扣除。
		const double IntendedDistance = Runner->State.FishEffortRatio * Init.Config.FishFullEffortSpeedCentimetersPerSecond * FixedStepSeconds;
		const double ActualProgress = FVector::DotProduct(Fish->GetActorLocation() - InitialFishPosition,
			Runner->PreviousFishEffortDirection);
		const double MissingDistance = FMath::Max(0.0, IntendedDistance - ActualProgress);
		const double FishDrain = FishDefinition->FishFightStamina - Runner->State.FishStamina;
		const double CatDrain = CatStaminaMaximum - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		TestTrue(TEXT("生产运动确实产生未完成意图和猫端工作"), MissingDistance > 0.0 && CatDrain > 0.0);
		TestEqual(TEXT("最终实际位移按冻结米价只付一次鱼费用"), FishDrain, MissingDistance / 100.0 * Price, 1e-8);
		TestEqual(TEXT("Session镜像实际鱼余额而不再次扣费"), Session->GetSnapshot().FishFightStaminaRemaining, Runner->State.FishStamina, 1e-8);
		TestEqual(TEXT("Runner猫余额与ASC付款结果一致"), Runner->State.CatStamina,
			static_cast<double>(ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())), 1e-4);
		TestEqual(TEXT("Session鱼体力比例由实际余额更新"), Session->GetSnapshot().NormalizedFishStamina,
			Runner->State.FishStamina / FishDefinition->FishFightStamina, 1e-8);
		if (Price == 0.0)
		{
			ReferenceCatDrain = CatDrain;
			ReferenceFishLocation = Fish->GetActorLocation();
		}
		else
		{
			TestEqual(TEXT("只改鱼米价不改变真实猫费用"), CatDrain, ReferenceCatDrain, 1e-6);
			TestEqual(TEXT("只改鱼米价不改变本步真实运动"), Fish->GetActorLocation(), ReferenceFishLocation);
			if (Price == 4.5) TestEqual(TEXT("生产Runner双倍米价只支付双倍鱼费用"), FishDrain, PreviousFishDrain * 2.0, 1e-8);
			PreviousFishDrain = FishDrain;
		}
		double RemainingRodDurability = 0.0;
		bool bBroken = false;
		TestTrue(TEXT("生产Session仍持有可结算的真实竿实例"), Equipment->GetFishingRodDurability(SessionId, RemainingRodDurability, bBroken) && !bBroken);
		TestEqual(TEXT("生产Session耐久镜像同一库存实例"), Session->GetSnapshot().RodDurabilityRemaining, RemainingRodDurability, 1e-8);
		AddInfo(FString::Printf(TEXT("Event=fish_intent_runtime_paid Price=%.6f IntentCm=%.6f ActualCm=%.6f MissingCm=%.6f FishDrain=%.6f CatDrain=%.6f ASCWrites=%d SessionPublications=%d"),
			Price, IntendedDistance, ActualProgress, MissingDistance, FishDrain, CatDrain, CatStaminaWrites, SessionPublications));
		Runner->Stop();
		Service->Sessions.Remove(SessionId);
	}
	return !HasAnyErrors();
}

#endif
