#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatItemBonusAttributeSet.h"
#include "AbilitySystem/Effects/CatItemEffects.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Inventory/Fragments/CatItemContainerOpenFragment.h"
#include "Items/CatIconItem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/CatFishingSession.h"
#include "Inventory/Fragments/CatItemDropFragment.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineSubsystemNames.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Data/CatFishCatalogSettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Fishing/Actors/CatCastNetCatchEmitter.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Data/CatFishDefinition.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"

namespace CatConsumableTests
{
	// 测试宿主流程：安装真实库存组件并设置格数；后续断言直接使用生产收货与消费入口。
	static UCatInventoryComponent* Inventory(AActor* Owner, int32 Slots)
	{
		auto* Result = NewObject<UCatInventoryComponent>(Owner); Owner->AddInstanceComponent(Result);
		Result->RegisterComponent(); Result->SetInventorySlotCountFromAuthority(Slots); return Result;
	}
	// 资产读取流程：读取实际生成的正式定义，缺失时由测试明确失败而不是用测试替身掩盖接线。
	static UCatInventoryItemDefinition* Item(const TCHAR* Name)
	{ return LoadObject<UCatInventoryItemDefinition>(nullptr, *FString::Printf(TEXT("/Game/Catfishing/Data/Items/Item_%s.Item_%s"), Name, Name)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableAssetTest, "Catfishing.Contract.Consumables.Assets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 资产契约：七件必须在正式总表可查、图标已导入、行为配置完整；假鱼同时持有两类标签且无真鱼实例。
bool FCatConsumableAssetTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Key : {TEXT("DriedFish"), TEXT("PawGloves"), TEXT("LuckyClover"), TEXT("WaterSprayer"), TEXT("FakeFish"), TEXT("Horn"), TEXT("CastNet")})
	{
		auto* Definition = CatConsumableTests::Item(Key);
		if (!TestNotNull(Key, Definition)) continue;
		TestTrue(TEXT("正式配置就绪"), Definition->IsInventoryRuntimeDefinitionReady());
		TestTrue(TEXT("总表指向同一资产"), GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Definition->ItemId) == Definition);
		TestTrue(TEXT("图标真实可加载"), Definition->GetInventoryThumbnail().LoadSynchronous() != nullptr);
		TestTrue(TEXT("普通道具分类"), Definition->HasSemanticTag(CatItemTags::Tool));
	}
	auto* Fake = CatConsumableTests::Item(TEXT("FakeFish"));
	if (Fake) { TestTrue(TEXT("假鱼也是鱼类"), Fake->HasSemanticTag(CatItemTags::Fish)); TestEqual(TEXT("假鱼不堆叠"), Fake->GetMaxStackCount(), 1); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableOverflowTest, "Catfishing.Contract.Consumables.OverflowPreservesResources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 真实收货回归：先填满库存再拾取有剩余水量的原实物；必须保留 GUID 和水量并移到新宿主旁，不复制或销毁原载体。
bool FCatConsumableOverflowTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); auto* Owner = World->SpawnActor<AActor>();
	Owner->SetActorLocation(FVector(1000,2000,200)); auto* Inventory = CatConsumableTests::Inventory(Owner, 1);
	auto* Water = CatConsumableTests::Item(TEXT("WaterSprayer")); auto* Filler = CatConsumableTests::Item(TEXT("DriedFish"));
	if (!Water || !Filler || !Inventory->AddItemDefinition(Filler, 1)) return false;
	auto* Source = World->SpawnActor<ACatIconItem>(); auto* Instance = NewObject<UCatInventoryItemInstance>(Source);
	Instance->SetRuntimeOwnerActor(Source); Instance->SetItemDefinition(Water);
	TestTrue(TEXT("消耗后剩余两次"), Instance->SetRemainingResourceFromAuthority(2));
	Source->InitializeFromInventoryFromAuthority(Instance, 1); const FGuid Identity = Instance->GetItemInstanceId();
	FCatInventoryReceiveBatch Batch; Batch.InstanceEntries.Add({1, Instance});
	TestTrue(TEXT("满库存也完成收货"), UCatInventoryStatics::ReceiveInventoryWithOverflowFromAuthority(Owner, Batch));
	TestFalse(TEXT("复用原物没有销毁"), Source->IsActorBeingDestroyed());
	const auto Payload = Source->GetPickupInventory();
	if (!TestEqual(TEXT("落地一件"), Payload.InstanceEntries.Num(), 1)) return false;
	TestEqual(TEXT("水量不恢复"), Payload.InstanceEntries[0].ItemInstance->GetRemainingResource(), 2);
	TestEqual(TEXT("身份不丢"), Payload.InstanceEntries[0].ItemInstance->GetItemInstanceId(), Identity);
	TestTrue(TEXT("靠近库存宿主"), FVector::Dist(Source->GetActorLocation(), Owner->GetActorLocation()) < 150);
	TestEqual(TEXT("原库存物品不变"), Inventory->GetInventoryEntryAtSlot(0)->Instance->GetItemId(), Filler->ItemId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumablePartialIntakeTest, "Catfishing.Contract.Consumables.PartialStackOverflow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 堆叠收货回归：已有四件、上限五件，收三件后只能入一件，余下两件具有独立落地身份。
bool FCatConsumablePartialIntakeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); auto* Owner = World->SpawnActor<AActor>(); auto* Inventory = CatConsumableTests::Inventory(Owner, 1);
	auto* Food = CatConsumableTests::Item(TEXT("DriedFish")); if (!Food || !Inventory->AddItemDefinition(Food, 4)) return false;
	FCatInventoryReceiveBatch Batch; Batch.DefinitionEntries.Add({3, Food, nullptr});
	TestTrue(TEXT("部分收货成功"), UCatInventoryStatics::ReceiveInventoryWithOverflowFromAuthority(Owner, Batch));
	TestEqual(TEXT("只填满原堆叠"), Inventory->GetInventoryEntryAtSlot(0)->StackCount, 5);
	int32 Drops = 0;
	for (TActorIterator<ACatIconItem> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed() && It->IsAwaitingPickup())
		{
			const auto Payload = It->GetPickupInventory(); for (const auto& Entry : Payload.InstanceEntries)
			{ Drops += Entry.Count; TestTrue(TEXT("溢出身份独立"), Entry.ItemInstance->GetItemInstanceId() != Inventory->GetInventoryEntryAtSlot(0)->Instance->GetItemInstanceId()); }
		}
	TestEqual(TEXT("数量守恒，余量两件"), Drops, 2); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableFakeOpenTest, "Catfishing.Contract.Consumables.OneFakePerOpening", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 开箱回归：真实鱼类库存容纳两件假鱼，同一请求重放不触发第二件，下一次独立开箱才消耗余下一件。
bool FCatConsumableFakeOpenTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); auto* Character = World->SpawnActor<ACatCharacter>(); auto* Owner = World->SpawnActor<AActor>();
	auto* Inventory = NewObject<UCatFishOnlyInventoryComponent>(Owner); Owner->AddInstanceComponent(Inventory); Inventory->RegisterComponent(); Inventory->SetInventorySlotCountFromAuthority(2);
	auto* Fake = CatConsumableTests::Item(TEXT("FakeFish")); if (!Fake) return false;
	TestTrue(TEXT("假鱼占真实鱼护格"), Inventory->AddItemDefinition(Fake, 2));
	auto* ASC = Character->GetCatAbilitySystemComponent(); ASC->InitAbilityActorInfo(Character, Character);
	const FGuid Request = FGuid::NewGuid();
	UCatItemContainerOpenFragment::TriggerFirstFromAuthority(Inventory, Character, Request);
	TestTrue(TEXT("开箱者获得湿身GE"), ASC->HasMatchingGameplayTag(CatStateTags::Wet));
	TestEqual(TEXT("只消耗第一件"), Inventory->GetAllItems().Num(), 1);
	UCatItemContainerOpenFragment::TriggerFirstFromAuthority(Inventory, Character, Request);
	TestEqual(TEXT("重放不消耗第二件"), Inventory->GetAllItems().Num(), 1);
	UCatItemContainerOpenFragment::TriggerFirstFromAuthority(Inventory, Character, FGuid::NewGuid());
	TestEqual(TEXT("下次开箱消耗第二件"), Inventory->GetAllItems().Num(), 0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableFightLifecycleTest, "Catfishing.Contract.Consumables.FightEffectLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 生命周期回归：待生效 GE 只绑定一场会话；另一场不能复用，绑定会话销毁时恢复倍率且释放归属标签。
bool FCatConsumableFightLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	if (!Scene.BeginPlayInTestWorld()) return false;
	auto* World = Scene.GetTestWorld(); auto* Character = World->SpawnActor<ACatCharacter>(); auto* ASC = Character->GetCatAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Character, Character);
	ASC->ApplyGameplayEffectToSelf(GetDefault<UCatGE_FightEfficiency>(), 1, ASC->MakeEffectContext());
	auto* First = World->SpawnActor<ACatFishingSession>(); auto* Other = World->SpawnActor<ACatFishingSession>();
	TestEqual(TEXT("首场使用七折成本"), First->ResolveItemStaminaCostMultiplier(ASC), double(.7f));
	TestEqual(TEXT("其他场不能复用该效果"), Other->ResolveItemStaminaCostMultiplier(ASC), 1.0);
	First->Destroy();
	TestFalse(TEXT("退出清理等待效果"), ASC->HasMatchingGameplayTag(CatItemEffectTags::NextFight));
	TestFalse(TEXT("退出清理归属"), ASC->HasMatchingGameplayTag(CatItemEffectTags::FightBound));
	TestEqual(TEXT("倍率恢复"), ASC->GetNumericAttribute(UCatItemBonusAttributeSet::GetStaminaCostMultiplierAttribute()), 1.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableDeadWaterTest, "Catfishing.Contract.Consumables.LocalSpawnSuppression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 死水回归：仅同水域半径内阻止新鱼，三十秒到期恢复；查询不要求或修改任何既有会话。
bool FCatConsumableDeadWaterTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); auto* Water = World->GetSubsystem<UCatWaterQuerySubsystem>();
	FCatWaterRegionHandle Region; Region.RegionId = TEXT("TestLake"); Region.GeometryRevision = 1;
	TestTrue(TEXT("登记区域"), Water->AddFishSpawnSuppressionFromAuthority(FVector::ZeroVector, Region, 300, 30));
	TestTrue(TEXT("范围内禁新鱼"), Water->IsFishSpawnSuppressed(FVector(299,0,0), Region));
	TestFalse(TEXT("范围外不受影响"), Water->IsFishSpawnSuppressed(FVector(301,0,0), Region));
	auto Other = Region; Other.RegionId = TEXT("OtherLake"); TestFalse(TEXT("相邻水域不受影响"), Water->IsFishSpawnSuppressed(FVector::ZeroVector, Other));
	// 引擎会限制单帧 DeltaSeconds；逐帧推进真实世界时钟，不能把一次 31 秒 Tick 当作经过了 31 秒。
	for (int32 Frame = 0; Frame < 310; ++Frame) World->Tick(LEVELTICK_All, .1f);
	TestFalse(TEXT("到期恢复生成"), Water->IsFishSpawnSuppressed(FVector::ZeroVector, Region)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableRewardTest, "Catfishing.Contract.Consumables.ExecutorBonusAndOverflow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 捕获奖励回归：由执行者 GAS 把零基础概率提高到必掉，满包时生成两件奖励；另一对象不因本次结算获得属性或物品。
bool FCatConsumableRewardTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); auto* Executor = World->SpawnActor<ACatCharacter>();
	auto* ASC = Executor->GetCatAbilitySystemComponent(); ASC->InitAbilityActorInfo(Executor, Executor);
	auto* Inventory = Executor->GetInventoryComponent();
	auto* Food = CatConsumableTests::Item(TEXT("DriedFish")); auto* Water = CatConsumableTests::Item(TEXT("WaterSprayer"));
	if (!Food || !Water) return false;
	for (int32 Index = 0; Index < Inventory->GetInventorySlotCount(); ++Index) if (!Inventory->AddItemDefinition(Water, 1)) return false;
	auto* Drops = NewObject<UCatItemDropFragment>(); auto& Drop = Drops->ExtraDrops.AddDefaulted_GetRef();
	Drop.Item = Food; Drop.Probability = 0; Drop.MinimumCount = Drop.MaximumCount = 2;
	ASC->SetNumericAttributeBase(UCatItemBonusAttributeSet::GetDropChanceBonusAttribute(), 2.f);
	TestTrue(TEXT("执行者加成归一成必掉且满包交付成功"), Drops->AwardCaptureDropsFromAuthority(Executor, FGuid::NewGuid()));
	int32 Total = 0; for (TActorIterator<ACatIconItem> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed()) for (const auto& Entry : It->GetPickupInventory().InstanceEntries) Total += Entry.Count;
	TestEqual(TEXT("奖励两件都在宿主旁落地"), Total, 2);
	ASC->SetNumericAttributeBase(UCatItemBonusAttributeSet::GetDropChanceBonusAttribute(), -2.f);
	TestTrue(TEXT("负加成归一成零概率仍算正常完成"), Drops->AwardCaptureDropsFromAuthority(Executor, FGuid::NewGuid()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableUseTest, "Catfishing.Contract.Consumables.FoodAndBuffThroughAbility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 生产输入回归：通过物品组件启动正式 GA 并推进前摇，检查满绿段拒用、绿段恢复、四叶草不能重复消费及搏斗禁用。
// 随后核水量、喊话次数和补水；渔网先核早期尚未整批公开，再推进独立队列确认全部鱼依次进入世界。
bool FCatConsumableUseTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	if (!Scene.BeginPlayInTestWorld()) return false;
	auto* World = Scene.GetTestWorld();
	// 提供实际地面，防止空白世界中持续坠落使冻结视线在前摇结束时超出合法范围。
	auto* Floor = World->SpawnActor<AActor>(); auto* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->AddInstanceComponent(FloorBody); Floor->SetRootComponent(FloorBody); FloorBody->SetBoxExtent(FVector(500,500,20));
	FloorBody->SetCollisionProfileName(TEXT("BlockAll")); FloorBody->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-20));
	auto* Character = World->SpawnActor<ACatCharacter>(FVector(0,0,30), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
	auto* Player = World->SpawnActor<ACatfishingPlayerState>(); Controller->PlayerState = Player; Character->SetPlayerState(Player);
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("ConsumableOwner"), FName(TEXT("CAT_TEST")));
	Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->Possess(Character);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine)); Controller->SetPlayer(LocalPlayer.Get()); Controller->SetViewTarget(Character);
	Controller->SetShowMouseCursor(false);
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	ACatfishingGameModeBase::FAdmissionRecord Admission; Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active; Admission.Controller = Controller;
	Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
	auto* Inventory = Character->GetInventoryComponent(); auto* ASC = Character->GetCatAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Character, Character);
	auto* Items = Character->FindComponentByClass<UCatItemAbilityComponent>();
	auto* Food = CatConsumableTests::Item(TEXT("DriedFish")); auto* Clover = CatConsumableTests::Item(TEXT("LuckyClover"));
	if (!Food || !Clover || !Inventory->AddItemDefinition(Food, 2) || !Inventory->AddItemDefinition(Clover, 2)) return false;
	FGuid FoodId, CloverId;
	for (auto* Item : Inventory->GetAllItems()) { if (Item->GetItemDefinition() == Food) FoodId = Item->GetItemInstanceId(); if (Item->GetItemDefinition() == Clover) CloverId = Item->GetItemInstanceId(); }
	// 测试包装器同时推进 GFrameCounter；直接连续 Tick 会被 TimerManager 的同帧保护挡住，GA 前摇不会完成。
	const auto Advance = [&]() { for (int32 Frame = 0; Frame < 15; ++Frame) Scene.TickTestWorld(.1f); };
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 100.f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 100.f);
	Items->RequestUse(Inventory, FoodId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("满体力不扣小鱼干"), Inventory->CountVisibleInventoryQuantityByItemId(Food->ItemId), 2);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 10.f);
	Items->RequestUse(Inventory, FoodId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("使用后只扣一件"), Inventory->CountVisibleInventoryQuantityByItemId(Food->ItemId), 1);
	TestEqual(TEXT("绿段恢复到上限"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 100.f);
	Items->RequestUse(Inventory, CloverId, FGuid::NewGuid()); Advance();
	TestTrue(TEXT("四叶草施加真实待上鱼GE"), ASC->HasMatchingGameplayTag(CatItemEffectTags::NextFish));
	TestEqual(TEXT("稀有权重从GAS读取"), ASC->GetNumericAttribute(UCatItemBonusAttributeSet::GetRareFishWeightMultiplierAttribute()), 2.f);
	Items->RequestUse(Inventory, CloverId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("同类增益不重复消耗"), Inventory->CountVisibleInventoryQuantityByItemId(Clover->ItemId), 1);
	ASC->SetStateTagsFromAuthority(TEXT("ConsumableTestFight"), FGameplayTagContainer(CatStateTags::FishingFight));
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 10.f);
	Items->RequestUse(Inventory, FoodId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("搏斗不允许吃小鱼干"), Inventory->CountVisibleInventoryQuantityByItemId(Food->ItemId), 1);
	ASC->SetStateTagsFromAuthority(TEXT("ConsumableTestFight"), {});
	// 次数道具使用同一正式输入；每步等前摇完成再读余额，防止只验证定义的初始值。
	for (auto* Item : Inventory->GetAllItems()) Inventory->RemoveItemInstance(Item);
	auto* WaterDefinition = CatConsumableTests::Item(TEXT("WaterSprayer")); auto* HornDefinition = CatConsumableTests::Item(TEXT("Horn"));
	if (!Inventory->AddItemDefinition(WaterDefinition, 1) || !Inventory->AddItemDefinition(HornDefinition, 1)) return false;
	UCatInventoryItemInstance* WaterItem = nullptr; UCatInventoryItemInstance* HornItem = nullptr;
	for (auto* Item : Inventory->GetAllItems()) { if (Item->GetItemDefinition() == WaterDefinition) WaterItem = Item; if (Item->GetItemDefinition() == HornDefinition) HornItem = Item; }
	if (!WaterItem || !HornItem) return false;
	const FGuid WaterId = WaterItem->GetItemInstanceId(), HornId = HornItem->GetItemInstanceId();
	Controller->SetControlRotation(FRotator::ZeroRotator); Controller->SetViewTarget(Character);
	if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->UpdateCamera(.1f);
	Items->RequestUse(Inventory, WaterId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("喷水未命中也消耗一水"), WaterItem->GetRemainingResource(), 4);
	WaterItem->SetRemainingResourceFromAuthority(0);
	Items->RequestUse(Inventory, WaterId, FGuid::NewGuid()); Advance();
	TestEqual(TEXT("空水不出现负余额"), WaterItem->GetRemainingResource(), 0);
	TestEqual(TEXT("湿毛器耗空保留本体"), Inventory->CountVisibleInventoryQuantityByItemId(WaterDefinition->ItemId), 1);
	Items->RequestUse(Inventory, HornId, FGuid::NewGuid(), false, false, TEXT("   ")); Advance();
	TestEqual(TEXT("空白喊话不扣次数"), HornItem->GetRemainingResource(), 3);
	for (int32 Count = 0; Count < 3; ++Count)
	{
		Items->RequestUse(Inventory, HornId, FGuid::NewGuid(), false, false, TEXT("收鱼啦")); Advance();
		TestEqual(TEXT("每次确认只扣一次"), HornItem->GetRemainingResource(), 2 - Count);
	}
	TestEqual(TEXT("响响筒最后一次消费本体"), Inventory->CountVisibleInventoryQuantityByItemId(HornDefinition->ItemId), 0);
	// 注册一片真实烘焙水域；相机朝下时走生产补水和撒网查询，而不是直接给能力伪造命中。
	FCatWaterGeometryBuildInput WaterInput;
	WaterInput.RegionId = GetDefault<UCatFishCatalogSettings>()->Definitions[0].LoadSynchronous()->RegionIds[0];
	WaterInput.BankHeightToleranceCm = 250; WaterInput.WaterPointVerticalToleranceCm = 100;
	WaterInput.BoundaryToleranceCm = 1; WaterInput.MinimumWaterInsetCm = 1; WaterInput.MaxLandingCorrectionCm = 100;
	auto& Boundary = WaterInput.Boundaries.AddDefaulted_GetRef(); Boundary.BoundaryId = TEXT("ConsumablesWater"); Boundary.Vertices = {{-500,-500},{500,-500},{500,500},{-500,500}};
	const auto Geometry = FCatWaterGeometry::Build(WaterInput); if (!Geometry.bSucceeded) return false;
	auto* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Geometry.Cache); Region->FinishSpawning(FTransform::Identity);
	Character->SetActorLocation(FVector(0,0,100), false, nullptr, ETeleportType::TeleportPhysics);
	Controller->SetControlRotation(FRotator(-60,0,0));
	if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->UpdateCamera(.1f);
	Items->RequestUse(Inventory, WaterId, FGuid::NewGuid(), false, true); Advance();
	TestEqual(TEXT("河边副操作补满同一湿毛器"), WaterItem->GetRemainingResource(), 5);
	auto* NetDefinition = CatConsumableTests::Item(TEXT("CastNet")); if (!Inventory->AddItemDefinition(NetDefinition, 1)) return false;
	FGuid NetId; for (auto* Item : Inventory->GetAllItems()) if (Item->GetItemDefinition() == NetDefinition) NetId = Item->GetItemInstanceId();
	Character->SetActorLocation(FVector(0,0,100), false, nullptr, ETeleportType::TeleportPhysics);
	if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->UpdateCamera(.1f);
	Items->RequestUse(Inventory, NetId, FGuid::NewGuid());
	for (int32 Frame = 0; Frame < 6; ++Frame) Scene.TickTestWorld(.1f);
	int32 FirstVisible = 0;
	for (TActorIterator<ACatFishPickupActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed() && !It->IsHidden()) ++FirstVisible;
	TestTrue(TEXT("扣网后尚未同时公开整批鱼"), FirstVisible <= 1);
	ASC->AddLooseGameplayTag(CatStateTags::Downed);
	Advance(); Advance();
	ASC->RemoveLooseGameplayTag(CatStateTags::Downed);
	int32 CatchCount = 0; for (TActorIterator<ACatFishPickupActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++CatchCount;
	TestTrue(TEXT("撒网生成五至八条实物鱼"), CatchCount >= 5 && CatchCount <= 8);
	int32 FinalVisible = 0;
	for (TActorIterator<ACatFishPickupActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed() && !It->IsHidden()) ++FinalVisible;
	TestEqual(TEXT("使用能力结束及倒地不打断逐条出鱼"), FinalVisible, CatchCount);
	TestEqual(TEXT("撒网仅消费一件"), Inventory->CountVisibleInventoryQuantityByItemId(NetDefinition->ItemId), 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConsumableTeamLimitTest, "Catfishing.Contract.Consumables.RunPurchaseLimitSurvivesShelfRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 团队额度回归：恢复本局已购一件后，可再买一件但不能买两件；换货架不能重置已购数，非法恢复不能覆盖原额度。
bool FCatConsumableTeamLimitTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game) || !Scene.BeginPlayInTestWorld()) return false;
	auto* World = Scene.GetTestWorld(); auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
	auto* Net = CatConsumableTests::Item(TEXT("CastNet"));
	UClass* KioskClass = LoadClass<ACatShopKioskActor>(nullptr, TEXT("/Game/UI/Shop/BP_CatShopKiosk.BP_CatShopKiosk_C"));
	if (!Shop || !Net || !KioskClass) return false;
	TestEqual(TEXT("策划资产配置每局两件"), Net->TeamPurchaseLimitPerRun, 2);
	if (!TestTrue(TEXT("设置本局钱包"), Shop->RestoreWalletFromAuthority(5000))) return false;
	TestTrue(TEXT("恢复历史已购数"), Shop->RestoreRunPurchaseCountsFromAuthority({{Net->ItemId, 1}}));
	TestFalse(TEXT("负数不能覆盖历史额度"), Shop->RestoreRunPurchaseCountsFromAuthority({{Net->ItemId, -1}}));
	for (int32 ShelfIndex = 0; ShelfIndex < 2; ++ShelfIndex)
	{
		auto* Kiosk = World->SpawnActor<ACatShopKioskActor>(KioskClass); auto* Shelf = Kiosk->GetShopInventory();
		TArray<FCatShopCatalogEntry> Entries; Shelf->CollectDisplayCatalogEntries(Entries);
		FCatShopCartCommand Command; Command.Context.RequestId = FGuid::NewGuid(); Command.Context.StableNetId = TEXT("ConsumableLimitTest"); Command.ShopInventoryId = Shelf->GetShopInventoryId();
		for (const auto& Entry : Entries) if (Entry.ItemId == Net->ItemId) Command.Lines.AddDefaulted_GetRef().EntryId = Entry.EntryId;
		if (!TestEqual(TEXT("正式货架只有一条渔网"), Command.Lines.Num(), 1)) return false;
		FCatShopResolvedCart Quote; ECatDomainCommandError Error;
		TestTrue(TEXT("剩余一件额度可以报价"), Shop->ResolveCatalogCartForAuthority(Command, Shelf, Quote, Error));
		Command.Lines[0].CartCount = 2;
		TestFalse(TEXT("更换货架后仍不能超过本局两件"), Shop->ResolveCatalogCartForAuthority(Command, Shelf, Quote, Error));
		TestEqual(TEXT("拒绝原因是团队额度"), Quote.FailureReason, FName(TEXT("TeamRunPurchaseLimit")));
	}
	TestEqual(TEXT("只读报价不改变额度"), Shop->GetRunPurchaseCounts().FindRef(Net->ItemId), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatNetShoreEmissionTest, "Catfishing.Contract.Consumables.NetFishActuallyFlyOnShore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 构造直岸、陆地碰撞和朝水角色，用真实鱼资产连续出鱼；首次公开时验证包围球未触水，再记录各鱼朝陆地的水平位移，纯下落不能算喷出。
// 要求三条鱼都公开且各自移动超过 50 厘米，用于识别出生即被防落水反复拉回出口的回归。
bool FCatNetShoreEmissionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene; if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	FCatWaterGeometryBuildInput Input; Input.RegionId = TEXT("NetShore"); Input.BankHeightToleranceCm = 500;
	Input.WaterPointVerticalToleranceCm = 100; Input.BoundaryToleranceCm = 1;
	auto& Boundary = Input.Boundaries.AddDefaulted_GetRef(); Boundary.BoundaryId = TEXT("Water");
	Boundary.Vertices = {{20,-1000},{1000,-1000},{1000,1000},{20,1000}};
	const auto Built = FCatWaterGeometry::Build(Input); if (!Built.bSucceeded) return false;
	auto* Region = World->SpawnActor<ACatWaterRegion>(); FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
	if (!Scene.BeginPlayInTestWorld()) return false;
	auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor); Floor->AddInstanceComponent(Box); Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->SetWorldLocation(FVector(-1000,0,-10)); Box->RegisterComponent();
	auto* Source = World->SpawnActor<ACatCharacter>(FVector(0,0,100), FRotator::ZeroRotator);
	auto* Definition = LoadObject<UCatFishDefinition>(nullptr,TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
	if (!Definition) return false;
	const auto* Presentation = Definition->LoadRuntimePresentationDefinition(); if (!Presentation) return false;
	TArray<ACatFishPickupActor*> Fish;
	FCatCaptureConditionSnapshot Capture; Capture.RegionId = Input.RegionId;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Pickup = World->SpawnActor<ACatFishPickupActor>(Definition->WorldActorClass.LoadSynchronous());
		if (!TestTrue(TEXT("真实鱼载体初始化"), Pickup && Pickup->InitializeFromAuthority(FGuid::NewGuid(),FGuid::NewGuid(),Definition,0.3,
			Presentation->ComputeUniformVisualScale(0.3),Capture,TEXT("NetTest"),{TEXT("NetTest")}))) return false;
		Pickup->SetActorHiddenInGame(true); Pickup->SetActorEnableCollision(false); Pickup->SetReplicates(false);
		CastChecked<UPrimitiveComponent>(Pickup->GetRootComponent())->SetSimulatePhysics(false); Fish.Add(Pickup);
	}
	auto* Emitter = World->SpawnActor<ACatCastNetCatchEmitter>(Source->GetActorLocation(), FRotator::ZeroRotator);
	Emitter->StartFromAuthority(Source,Fish,.2f);
	TMap<ACatFishPickupActor*,FVector> ReleasedAt;
	TMap<ACatFishPickupActor*,double> MaxLandwardDistance;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		Scene.TickTestWorld(.01f);
		for (auto* Pickup : Fish) if (!Pickup->IsHidden())
		{
			if (!ReleasedAt.Contains(Pickup))
			{
				ReleasedAt.Add(Pickup,Pickup->GetActorLocation());
				const auto* Body = CastChecked<UPrimitiveComponent>(Pickup->GetRootComponent());
				TestFalse(TEXT("喷出起点不在禁入水域里"), World->GetSubsystem<UCatWaterQuerySubsystem>()->DoesWorldDropSweepTouchWater(
					Body->Bounds.Origin,Body->Bounds.Origin,Body->Bounds.SphereRadius));
			}
			MaxLandwardDistance.FindOrAdd(Pickup) = FMath::Max(MaxLandwardDistance.FindRef(Pickup),ReleasedAt[Pickup].X - Pickup->GetActorLocation().X);
		}
	}
	TestEqual(TEXT("三条鱼全部逐条公开"), ReleasedAt.Num(), 3);
	for (auto* Pickup : Fish) TestTrue(TEXT("每条鱼朝陆地水平飞出超过五十厘米"), MaxLandwardDistance.FindRef(Pickup) > 50.0);
	return !HasAnyErrors();
}
#endif
