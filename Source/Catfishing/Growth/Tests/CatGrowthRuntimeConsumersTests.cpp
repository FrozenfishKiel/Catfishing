#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Growth/CatGrowthComponent.h"
#include "Growth/CatGrowthSettings.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "UI/CatFishingViewTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatGrowthRuntimeConsumersTest,
	"Catfishing.Unit.Growth.ChoicesReachLiveInventoryTimersAndOperator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatGrowthRuntimeConsumersTest::RunTest(const FString&)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* World = Wrapper.GetTestWorld();
	auto* Cat = World->SpawnActor<ACatCharacter>();
	auto* Controller = World->SpawnActor<APlayerController>();
	Controller->PlayerState = World->SpawnActor<APlayerState>();
	Controller->Possess(Cat);
	auto* Growth = Cat->GetGrowthComponent();
	auto* Inventory = Cat->GetInventoryComponent();
	auto* ASC = Cat->GetCatAbilitySystemComponent();
	if (!Growth || !Inventory || !ASC) return false;
	// 夹具只提供服务端待选面板；累计数值和真实消费者全部通过正式选择命令产生。
	Growth->Snapshot.CompletedChoiceCount = 4;
	auto Pick = [&](ECatGrowthOptionId Id)
	{
		Growth->Snapshot.PendingChoiceCount = 1;
		Growth->Snapshot.CurrentOffer = {Id};
		++Growth->Snapshot.OfferSerial;
		const FGuid Request = FGuid::NewGuid();
		const int32 Serial = Growth->Snapshot.OfferSerial;
		const auto Result = Growth->ChooseOfferedOptionFromAuthority(Controller, Request, Id, Serial);
		TestTrue(TEXT("正式选择提交"), Result.bCommitted);
		const double Total = Growth->GetTotalMagnitude(Id);
		Growth->ChooseOfferedOptionFromAuthority(Controller, Request, Id, Serial);
		TestEqual(TEXT("重发命令不重复叠加"), Growth->GetTotalMagnitude(Id), Total);
	};
	const int32 InitialSlots = Inventory->GetInventorySlotCount();
	Pick(ECatGrowthOptionId::InventorySlots);
	TestEqual(TEXT("选择当场新增真实背包格"), Inventory->GetInventorySlotCount(), InitialSlots + 1);
	Controller->UnPossess();
	Controller->Possess(Cat);
	TestEqual(TEXT("重新占有保留本局成长背包格"), Inventory->GetInventorySlotCount(), InitialSlots + 1);
	Pick(ECatGrowthOptionId::SupplyCapacity);
	for (const int32 Id : {4, 5})
	{
		auto* Item = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Id);
		if (!TestNotNull(TEXT("正式饵/窝料可加载"), Item)) return false;
		const auto Category = UCatInventorySettings::ResolveCarryCategory(*Item);
		const int32 Limit = GetDefault<UCatInventorySettings>()->GetCarryLimitForCategory(Category) + 2;
		FCatInventoryReceiveBatch Batch;
		auto& Entry = Batch.DefinitionEntries.AddDefaulted_GetRef();
		Entry.ItemDefinition = Item;
		Entry.Count = Limit;
		TestTrue(TEXT("成长后整批容量预演通过"), Inventory->CanFullyAcceptInventoryBatch(Batch));
		TestTrue(TEXT("成长后正式整批入库通过"), Inventory->TryAddInventoryBatch(Batch));
		Entry.Count = 1;
		TestFalse(TEXT("饵与窝分别达上限后预演拒绝额外一份"), Inventory->CanFullyAcceptInventoryBatch(Batch));
	}
	auto* Session = World->SpawnActor<ACatFishingSession>();
	Session->FisherCharacter = Cat;
	Session->CastEquipment = Cat->GetEquipmentComponent();
	Session->Snapshot.Phase = ECatFishingPhase::TrueBiteWindow;
	Session->Snapshot.PhaseStartedServerTime = World->GetTimeSeconds();
	Session->Snapshot.WindowEndsServerTime = World->GetTimeSeconds() + 12.0;
	Session->Snapshot.PerfectWindowEndsServerTime = World->GetTimeSeconds() + 1.0;
	Pick(ECatGrowthOptionId::PerfectWindow);
	TestEqual(TEXT("已有真咬窗口同步加宽半秒"), Session->Snapshot.PerfectWindowEndsServerTime,
		Session->Snapshot.PhaseStartedServerTime + 1.5);
	Pick(ECatGrowthOptionId::PerfectWindow);
	TestEqual(TEXT("加宽上限一秒"), Session->Snapshot.PerfectWindowEndsServerTime,
		Session->Snapshot.PhaseStartedServerTime + 2.0);
	TestEqual(TEXT("UI 预表现读取同一截止时间"), FCatFishingViewState::FromSnapshot(Session->Snapshot).PerfectWindowEndsServerTime,
		Session->Snapshot.PerfectWindowEndsServerTime);
	Session->Snapshot.Phase = ECatFishingPhase::Waiting;
	World->GetTimerManager().SetTimer(Session->ProbeTimerHandle, FTimerDelegate::CreateLambda([] {}), 20.0, false);
	Session->FisherCharacter.Reset(); // 原抛竿者离竿，浮漂仍属于这次抛竿。
	Pick(ECatGrowthOptionId::BiteInterval);
	Session->FisherCharacter = Cat;
	TestTrue(TEXT("已存在浮漂计时器缩短 5%"), FMath::IsNearlyEqual(double(World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle)), 19.0, 0.001));
	Pick(ECatGrowthOptionId::SlackStaminaRegen);
	Pick(ECatGrowthOptionId::RodWear);
	auto* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->State.bOperatorPresent = true;
	Runner->OperatorState.Character = Cat;
	Runner->OperatorState.AbilitySystem = ASC;
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.CatStaminaMaximum = 100.0;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->Config.RodDurability = 1000.0;
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute(), 7.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
	TestTrue(TEXT("实际主控采样读取成长"), Runner->UpdateOperatorIntentAndProperties());
	TestEqual(TEXT("放线成长到达运行配置"), Runner->Config.SlackStaminaGrowthPerSecond, 1.0);
	TestEqual(TEXT("竿磨损成长到达运行配置"), Runner->Config.RodWearMultiplier, 0.9);
	FCatFightStepResult Step;
	Step.bSlackRecoveryActive = true;
	TestTrue(TEXT("实际主控体力结算接收放线成长"), Runner->ApplyOperatorStaminaChanges(Step));
	TestTrue(TEXT("实际 ASC 绿体力增加每秒一点的一个固定步"), FMath::IsNearlyEqual(double(ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())), 30.05, 0.001));
	TestEqual(TEXT("黄色体力不被回填"), ASC->GetYellowFightStamina(), 7.0f);
	Pick(ECatGrowthOptionId::CatchWeight);
	FCatFishSelectionContext Context;
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	const auto* FormalFish = Catalog->Definitions.IsEmpty() ? nullptr : Catalog->Definitions[0].LoadSynchronous();
	if (!TestNotNull(TEXT("正式鱼用于重量消费者回归"), FormalFish)) return false;
	Context.WaterRegion.RegionId = FormalFish->RegionIds[0];
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.ActivePlayerCount = 8;
	Context.CombinedFishingStrength = Context.CombinedFightStamina = 1000000.0;
	Context.StrengthPerKilogram = 10.0;
	Context.RandomSeed = 7321;
	const auto BaseRoll = Catalog->SelectRuntimeDefinition(Context);
	Context.CatchWeightBonus = Session->GetFisherGrowthMagnitude(ECatGrowthOptionId::CatchWeight);
	const auto GrowthRoll = Catalog->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("真实选鱼器接收成长后的上下文"), BaseRoll.bSelected && GrowthRoll.bSelected);
	TestEqual(TEXT("不切 D-30 抽样次序"), GrowthRoll.ItemId, BaseRoll.ItemId);
	const auto* SelectedFish = Catalog->FindRuntimeDefinition(GrowthRoll.ItemId);
	if (SelectedFish)
		TestTrue(TEXT("实际冻结重量上浮且不超过鱼种上限"), FMath::IsNearlyEqual(GrowthRoll.WeightKilograms,
			FMath::Min(SelectedFish->MaximumWeightKilograms, BaseRoll.WeightKilograms * 1.05), 0.0001));
	FCatFightSimulationState WearState;
	WearState.CatStamina = 37.0;
	WearState.FishStamina = 100.0;
	WearState.LineLengthCentimeters = 500.0;
	WearState.FishWorldPosition = FVector(500, 0, 0);
	WearState.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;
	auto OriginalWearConfig = Runner->Config;
	OriginalWearConfig.RodWearMultiplier = 1.0;
	const auto OriginalWear = FCatFishingFightSimulator::Step(OriginalWearConfig, WearState, Constraint, FVector::ForwardVector);
	const auto ReducedWear = FCatFishingFightSimulator::Step(Runner->Config, WearState, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("连续磨损两次模拟都实际发生磨损"), OriginalWear.bSucceeded && ReducedWear.bSucceeded && OriginalWear.RodWearDelta > 0.0);
	TestTrue(TEXT("成长减免真正改变连续扣费量"), FMath::IsNearlyEqual(ReducedWear.RodWearDelta, OriginalWear.RodWearDelta * 0.9, 0.000001));
	World->GetTimerManager().ClearTimer(Session->ProbeTimerHandle);
	World->GetTimerManager().ClearAllTimersForObject(Session);
	return !HasAnyErrors();
}

#endif
