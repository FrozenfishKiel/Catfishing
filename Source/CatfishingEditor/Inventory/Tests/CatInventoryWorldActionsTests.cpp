#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "AbilitySystem/Effects/CatShopEconomyTransactionEffect.h"
#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"
#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Items/CatItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "OnlineSubsystemTypes.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace CatInventoryWorldActionsTests
{
	// 世界夹具初始化：只在新 World 上指定原生 GameMode 并开始生命周期，避开整场 Run 与存档装配；退出由 wrapper 配对销毁。
	// 这里不修改 Settings/CDO，也不替换库存、经济服务或 GE；这些用例只验证其公开服务边界。
	bool StartWorld(FAutomationTestBase& Test, FTestWorldWrapper& Wrapper)
	{
		if (!Test.TestTrue(TEXT("创建独立 authority World"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		const bool bStarted = Wrapper.BeginPlayInTestWorld();
		Wrapper.ForwardErrorMessages(&Test);
		return Test.TestTrue(TEXT("启动测试 World 生命周期"), bStarted);
	}

	// 支撑地面构造：在角色脚底放一个静态阻挡盒，注册真实物理查询形状；放置求解仍完整走生产代码的坡度、视线和四角支撑检测。
	AActor* AddFloor(ACatCharacter& Character)
	{
		AActor* Floor = Character.GetWorld()->SpawnActor<AActor>();
		if (!Floor) return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->AddInstanceComponent(Box);
		Floor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(2000.0, 2000.0, 10.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetWorldLocation(Character.GetActorLocation() - FVector(0.0, 0.0,
			Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0));
		Box->SetMobility(EComponentMobility::Static);
		Box->RegisterComponent();
		return Floor;
	}

	// 普通数量载荷构造：内存定义显式声明五件一堆和正式 ACatItem 接收器，只服务堆叠边界，不注册目录或保存资产。
	UCatInventoryItemDefinition* MakeStackDefinition()
	{
		UCatInventoryItemDefinition* Definition = NewObject<UCatInventoryItemDefinition>();
		Definition->InventoryDefinitionId = TEXT("WorldActionsStackContract");
		Definition->InventoryMaxStackCount = 5;
		Definition->WorldActorClass = ACatItem::StaticClass();
		return Definition;
	}

	// 售鱼输入构造：为每条鱼分配独立身份并写入指定鱼种和千克重量；只构造命令，不在测试侧计算价格。
	FCatShopFishSaleLine MakeFishLine(FName DefinitionId, double Kilograms)
	{
		FCatShopFishSaleLine Line;
		Line.FishInstanceId = FGuid::NewGuid();
		Line.FishDefinitionId = DefinitionId;
		Line.WeightKilograms = Kilograms;
		return Line;
	}

	// 世界载荷定位：只读取当前 World 的正式拾取物，返回携带目标实例 ID 的 Actor；失败返回空供断言定位生成或身份丢失。
	ACatItem* FindPickup(UWorld& World, FGuid ItemId)
	{
		for (TActorIterator<ACatItem> It(&World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) continue;
			for (const FCatInventoryInstanceEntry& Entry : It->GetPickupInventory().InstanceEntries)
			{
				if (Entry.ItemInstance && Entry.ItemInstance->GetItemInstanceId() == ItemId) return *It;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryFishPriceContractTest,
	"Catfishing.Contract.Inventory.WorldActions.FishPriceRoundingAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 定价回归：以内存表调用真实计算器，先验证 2.5kg 的单鱼与双鱼舍入，再验证 double 边界、非法输入及零收入。
// 每次拒绝都要求输出清零，确保第二条坏鱼不会留下第一条已算出的部分收入；预期金额为需求常量，不复制实现公式。
bool FCatInventoryFishPriceContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
	Table->RowStruct = FCatShopFishSalePriceRow::StaticStruct();
	FCatShopFishSalePriceRow Row;
	Row.MoneyCoefficient = 19.0;
	Table->AddRow(TEXT("PricedFish"), Row);
	Row.MoneyCoefficient = 1.0;
	Table->AddRow(TEXT("BoundaryFish"), Row);
	int32 Value = -1;
	TArray<FCatShopFishSaleLine> Fish{MakeFishLine(TEXT("PricedFish"), 2.5)};
	TestTrue(TEXT("2.5kg 正常估价"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	TestEqual(TEXT("2.5kg 乘19逐鱼舍入为48"), Value, 48);
	Fish.Add(MakeFishLine(TEXT("PricedFish"), 2.5));
	TestTrue(TEXT("两鱼正常估价"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	TestEqual(TEXT("两鱼分别取整后合计96，不能先合计成95"), Value, 96);
	Fish = {MakeFishLine(TEXT("BoundaryFish"), 16777216.49)};
	TestTrue(TEXT("double 上限内小数舍入后仍可接收"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	TestEqual(TEXT("最大精确金币数"), Value, 16777216);
	for (const double InvalidWeight : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), 16777216.5})
	{
		Fish = {MakeFishLine(TEXT("BoundaryFish"), 1.0), MakeFishLine(TEXT("BoundaryFish"), InvalidWeight)};
		Value = 123;
		TestFalse(TEXT("坏重量令整批失败"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
		TestEqual(TEXT("失败不返回部分收入"), Value, 0);
	}
	Fish = {MakeFishLine(TEXT("BoundaryFish"), 16777216.0), MakeFishLine(TEXT("BoundaryFish"), 1.0)};
	TestFalse(TEXT("单鱼合法但总收入越界仍拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	TestEqual(TEXT("总额越界输出清零"), Value, 0);
	for (const double InvalidCoefficient : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max()})
	{
		Row.MoneyCoefficient = InvalidCoefficient;
		Table->AddRow(TEXT("InvalidPrice"), Row);
		Fish = {MakeFishLine(TEXT("PricedFish"), 2.5), MakeFishLine(TEXT("InvalidPrice"), 2.5)};
		Value = 123;
		TestFalse(TEXT("非法系数或乘积溢出整批拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
		TestEqual(TEXT("非法价格不残留前一条收入"), Value, 0);
	}
	Fish = {MakeFishLine(TEXT("MissingPrice"), 2.5)};
	TestFalse(TEXT("缺鱼种行拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	Fish = {MakeFishLine(NAME_None, 2.5)};
	TestFalse(TEXT("空鱼种拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	Fish.Reset();
	TestFalse(TEXT("空批次拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	Fish = {MakeFishLine(TEXT("PricedFish"), 0.01)};
	TestTrue(TEXT("正系数正重量允许舍入为零"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(Table.Get(), Fish, Value));
	TestEqual(TEXT("合法零收入"), Value, 0);
	TestFalse(TEXT("缺整张价格表拒绝"), UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(nullptr, Fish, Value));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryFishSaleGETest,
	"Catfishing.Runtime.Inventory.WorldActions.FishSaleGEWalletAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 经济服务回归：加载正式收购表中的19系数行，在原生 GameState ASC 上经服务执行单鱼、双鱼和零收入，观察真实经济 GE。
// 随后验证 RequestId 重放及换载荷拒绝、坏行整单失败和余额上限；只播种本测试 GameState，不修改默认设置或存档。
// InventoryCommitId 是服务输入边界的合成凭据；此用例不声称覆盖 Controller 的实物删除、Run 命令门或联机权限。
bool FCatInventoryFishSaleGETest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	UDataTable* Table = Settings->DefaultFishSalePriceTable.LoadSynchronous();
	if (!TestNotNull(TEXT("正式收购表必须已由资产流程生成"), Table)) return false;
	FName FishId;
	for (FName RowName : Table->GetRowNames())
	{
		const FCatShopFishSalePriceRow* Row = Table->FindRow<FCatShopFishSalePriceRow>(RowName, TEXT("WorldActionsTest"));
		if (Row && Row->MoneyCoefficient == 19.0) { FishId = RowName; break; }
	}
	if (!TestFalse(TEXT("正式表有系数19的鱼种，才能验证指定48金币案例"), FishId.IsNone())) return false;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatfishingGameState* State = World->SpawnActor<ACatfishingGameState>();
	if (!TestNotNull(TEXT("创建正式公款宿主"), State)) return false;
	World->SetGameState(State);
	UAbilitySystemComponent* ASC = State->GetRunAbilitySystemComponentFromAuthority();
	UCatEconomyAttributeSet* Attributes = State->GetEconomyAttributeSetFromAuthority();
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	if (!TestTrue(TEXT("真实 ASC、经济属性和服务就绪"), ASC && Attributes && Shop)) return false;
	Attributes->InitTeamWalletBalance(0.0f);
	int32 AppliedEconomicEffects = 0;
	// 订阅只计正式经济 GE，零收入也必须出现一次应用；本用例订阅之后不提前返回，末尾无条件解除局部引用。
	const FDelegateHandle EffectHandle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&AppliedEconomicEffects](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
		{
			if (Spec.Def && Spec.Def->IsA<UCatGE_ShopEconomyTransaction>()) ++AppliedEconomicEffects;
		});
	FCatShopFishSaleCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.StableNetId = TEXT("WorldActionsServiceBoundary");
	Command.Context.ExpectedRevision = -123;
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 2.5)};
	const FCatShopTransactionResult First = Shop->ApplyFishSale(Command);
	TestTrue(TEXT("单鱼实际成交且不依赖ExpectedRevision"), First.Command.bCommitted);
	TestEqual(TEXT("真实GE写入48"), Attributes->GetTeamWalletBalance(), 48.0f);
	TestEqual(TEXT("服务读取同一钱包"), Shop->GetWalletSnapshot().Balance, 48);
	TestEqual(TEXT("账本记录执行器的48金币"), First.Transaction.WalletDelta, 48);
	const FCatShopTransactionResult Replay = Shop->ApplyFishSale(Command);
	TestEqual(TEXT("同RequestId返回已处理"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重放返回同一交易"), Replay.Transaction.TransactionId, First.Transaction.TransactionId);
	Command.Fish[0].WeightKilograms = 3.0;
	TestEqual(TEXT("相同RequestId换重量拒绝"), Shop->ApplyFishSale(Command).Command.Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("重放和换载荷不重复应用GE"), AppliedEconomicEffects, 1);
	TestEqual(TEXT("重放没有新增账本"), Shop->GetTransactionLedgerSnapshot().Num(), 1);
	Command.Context.RequestId = FGuid::NewGuid();
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 2.5), MakeFishLine(FishId, 2.5)};
	const FCatShopTransactionResult Batch = Shop->ApplyFishSale(Command);
	TestTrue(TEXT("两鱼通过一笔GE成交"), Batch.Command.bCommitted);
	TestEqual(TEXT("逐鱼舍入的整批金额为96"), Batch.Transaction.WalletDelta, 96);
	TestEqual(TEXT("累计余额144"), Attributes->GetTeamWalletBalance(), 144.0f);
	TestEqual(TEXT("双鱼只增加一次GE"), AppliedEconomicEffects, 2);
	Command.Context.RequestId = FGuid::NewGuid();
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 0.01)};
	const FCatShopTransactionResult Zero = Shop->ApplyFishSale(Command);
	TestTrue(TEXT("零收入仍正式成交"), Zero.Command.bCommitted);
	TestEqual(TEXT("零收入账本金额为零"), Zero.Transaction.WalletDelta, 0);
	TestEqual(TEXT("零收入实际应用了GE"), AppliedEconomicEffects, 3);
	TestEqual(TEXT("零收入余额不变"), Attributes->GetTeamWalletBalance(), 144.0f);
	TestEqual(TEXT("零收入也生成独立账本"), Shop->GetTransactionLedgerSnapshot().Num(), 3);
	TestEqual(TEXT("零收入可重放"), Shop->ApplyFishSale(Command).Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("零收入重放不再应用GE"), AppliedEconomicEffects, 3);
	Command.Context.RequestId = FGuid::NewGuid();
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 2.5), MakeFishLine(TEXT("MissingPriceForWorldActions"), 2.5)};
	TestFalse(TEXT("GE遇到第二条缺价格整单拒绝"), Shop->ApplyFishSale(Command).Command.bCommitted);
	TestEqual(TEXT("坏行不提前入账首鱼"), Attributes->GetTeamWalletBalance(), 144.0f);
	Command.Context.RequestId = FGuid::NewGuid();
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 2.5)};
	// TArray扩容不接受源元素直接引用自身；先取值副本，再构造重复身份输入。
	const FCatShopFishSaleLine DuplicateFish = Command.Fish[0];
	Command.Fish.Add(DuplicateFish);
	TestEqual(TEXT("重复鱼ID拒绝"), Shop->ApplyFishSale(Command).Command.Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("重复鱼ID不改变余额"), Attributes->GetTeamWalletBalance(), 144.0f);
	Attributes->InitTeamWalletBalance(16777216.0f);
	Command.Context.RequestId = FGuid::NewGuid();
	Command.InventoryCommitId = Command.Context.RequestId;
	Command.Fish = {MakeFishLine(FishId, 2.5)};
	ECatDomainCommandError Error = ECatDomainCommandError::None;
	int64 Revision = 0;
	TestFalse(TEXT("上限余额在库存提交前预检失败"), Shop->ValidateFishSale(Command, Error, Revision));
	TestEqual(TEXT("预检报告容量上限"), Error, ECatDomainCommandError::CapacityExceeded);
	TestFalse(TEXT("直接进入GE仍拒绝上限溢出"), Shop->ApplyFishSale(Command).Command.bCommitted);
	TestEqual(TEXT("拒绝后基础余额与唯一属性一致"), ASC->GetNumericAttributeBase(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute()), 16777216.0f);
	TestEqual(TEXT("拒绝后当前余额保持上限"), Attributes->GetTeamWalletBalance(), 16777216.0f);
	TestEqual(TEXT("所有拒绝不新增成功账本"), Shop->GetTransactionLedgerSnapshot().Num(), 3);
	ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryReleaseStackTest,
	"Catfishing.Runtime.Inventory.WorldActions.DropPlaceStackIdentityAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 堆叠落地回归：给真实背包五件普通物，丢两件后读取世界载荷的新GUID，再把剩余三件整体放置并核对原GUID。
// 同时读取真实刚体状态与请求重放；拾回后检查原 Actor 引用和尺寸，再验证数量合并、部分丢弃和原物再次拾取。
bool FCatInventoryReleaseStackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	if (!TestTrue(TEXT("创建库存角色和控制器"), Character && Controller)) return false;
	Controller->Possess(Character);
	if (!TestNotNull(TEXT("创建真实放置支撑"), AddFloor(*Character))) return false;
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	TStrongObjectPtr<UCatInventoryItemDefinition> Definition(MakeStackDefinition());
	if (!TestTrue(TEXT("五件入正式背包"), Inventory && Inventory->AddItemDefinition(Definition.Get(), 5))) return false;
	const int32 Slot = Inventory->FindFirstInventorySlotIndexByDefinitionId(Definition->GetInventoryDefinitionId());
	UCatInventoryItemInstance* Original = Inventory->GetInventoryEntryAtSlot(Slot)->Instance;
	const FGuid OriginalId = Original->GetItemInstanceId();
	const FGuid DropRequest = FGuid::NewGuid();
	if (!TestTrue(TEXT("部分丢弃两件"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
		Character, DropRequest, Character, Slot, OriginalId, 2, ECatInventoryWorldAction::Drop).bCommitted)) return false;
	TestEqual(TEXT("原堆剩余三件"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 3);
	TestTrue(TEXT("部分扣量保留原库存实例"), Inventory->GetInventoryEntryAtSlot(Slot)->Instance == Original);
	ACatItem* Dropped = nullptr;
	int32 PickupCount = 0;
	for (TActorIterator<ACatItem> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed()) { Dropped = *It; ++PickupCount; }
	}
	if (!TestEqual(TEXT("仅生成一个世界载体"), PickupCount, 1) || !Dropped) return false;
	const FCatInventoryReceiveBatch DroppedBatch = Dropped->GetPickupInventory();
	if (!TestEqual(TEXT("落地保持实例批次"), DroppedBatch.InstanceEntries.Num(), 1)) return false;
	UCatInventoryItemInstance* Split = DroppedBatch.InstanceEntries[0].ItemInstance;
	if (!TestNotNull(TEXT("世界堆叠持有运行实例"), Split)) return false;
	TestEqual(TEXT("世界载荷两件"), DroppedBatch.InstanceEntries[0].Count, 2);
	TestTrue(TEXT("部分堆叠获得新有效GUID"), Split->GetItemInstanceId().IsValid() && Split->GetItemInstanceId() != OriginalId);
	TestTrue(TEXT("拆分保留定义和世界归属"), Split->GetItemDefinition() == Definition.Get() && Split->GetRuntimeOwnerActor() == Dropped);
	UPrimitiveComponent* DropBody = Cast<UPrimitiveComponent>(Dropped->GetRootComponent());
	TestTrue(TEXT("Drop开启真实模拟"), DropBody && DropBody->IsSimulatingPhysics());
	TestTrue(TEXT("同请求重放返回首次成功"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
		Character, DropRequest, Character, Slot, OriginalId, 2, ECatInventoryWorldAction::Drop).bCommitted);
	TestEqual(TEXT("重放不再扣量"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 3);
	TestEqual(TEXT("同RequestId改变数量拒绝"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
		Character, DropRequest, Character, Slot, OriginalId, 1, ECatInventoryWorldAction::Drop).Error, ECatDomainCommandError::InvalidPayload);
	PickupCount = 0;
	for (TActorIterator<ACatItem> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++PickupCount;
	TestEqual(TEXT("重放和换载荷没有额外生成载体"), PickupCount, 1);
	// 将已验证的丢弃物移出放置查询区，避免另一件合法世界物阻挡本用例的地面支撑。
	Dropped->SetActorLocation(FVector(1000, 1000, 300), false, nullptr, ETeleportType::TeleportPhysics);
	const FGuid PlaceRequest = FGuid::NewGuid();
	if (!TestTrue(TEXT("剩余整堆放置成功"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
		Character, PlaceRequest, Character, Slot, OriginalId, 3, ECatInventoryWorldAction::Place).bCommitted)) return false;
	TestFalse(TEXT("整堆移出后原格为空"), Inventory->HasItemAtSlot(Slot));
	ACatItem* Placed = FindPickup(*World, OriginalId);
	if (!TestNotNull(TEXT("整堆世界物保留原GUID"), Placed)) return false;
	const FCatInventoryReceiveBatch PlacedBatch = Placed->GetPickupInventory();
	if (!TestEqual(TEXT("整堆仍为单项实例载荷"), PlacedBatch.InstanceEntries.Num(), 1)) return false;
	TestEqual(TEXT("放置载荷保留三件"), PlacedBatch.InstanceEntries[0].Count, 3);
	UPrimitiveComponent* PlaceBody = Cast<UPrimitiveComponent>(Placed->GetRootComponent());
	TestTrue(TEXT("Place固定而不模拟物理"), PlaceBody && !PlaceBody->IsSimulatingPhysics());
	TestFalse(TEXT("放置不误入Use的held区"), Inventory->HasActiveHeldInventoryEntriesFromAuthority());
	TestTrue(TEXT("整堆放置后空格仍可重放原请求"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
		Character, PlaceRequest, Character, Slot, OriginalId, 3, ECatInventoryWorldAction::Place).bCommitted);
	const FVector OriginalScale(1.3, 1.1, 0.8);
	Placed->SetActorScale3D(OriginalScale);
	if (!TestTrue(TEXT("真实交互拾回整堆"), Placed->Interact_Implementation(Controller, FGuid::NewGuid()))) return false;
	const int32 ReturnedSlot = Inventory->FindInventorySlotIndexFromInstanceId(OriginalId);
	if (!TestTrue(TEXT("拾回后原GUID可定位"), ReturnedSlot != INDEX_NONE)) return false;
	TestEqual(TEXT("拾回不丢数量"), Inventory->GetInventoryEntryAtSlot(ReturnedSlot)->StackCount, 3);
	TestTrue(TEXT("拾回保留原Actor且隐藏碰撞"), IsValid(Placed) && !Placed->IsActorBeingDestroyed()
		&& Placed->IsHidden() && !Placed->GetActorEnableCollision());
	TestTrue(TEXT("现有实例保存原Actor"), Inventory->GetInventoryEntryAtSlot(ReturnedSlot)->Instance->GetWorldActor() == Placed);
	TestFalse(TEXT("库存保管的原物不可重复拾取"), Placed->CanInteract_Implementation(Controller));
	TestTrue(TEXT("堆叠仍只增加数量"), Inventory->AddItemDefinition(Definition.Get(), 2));
	if (!TestTrue(TEXT("有原载体仍可部分丢弃"), Inventory->ReleaseItemToWorldFromAuthority(Character,
		FGuid::NewGuid(), ReturnedSlot, OriginalId, 1, ECatInventoryWorldAction::Drop).bCommitted)) return false;
	TestTrue(TEXT("部分丢弃不取走库存原Actor"), Placed->IsHidden()
		&& Inventory->GetInventoryEntryAtSlot(ReturnedSlot)->Instance->GetWorldActor() == Placed);
	for (TActorIterator<ACatItem> It(World); It; ++It)
		if (*It != Placed) It->SetActorLocation(FVector(1500, 1500, 300), false, nullptr, ETeleportType::TeleportPhysics);
	if (!TestTrue(TEXT("剩余四件复用原Actor放置"), Inventory->ReleaseItemToWorldFromAuthority(Character,
		FGuid::NewGuid(), ReturnedSlot, OriginalId, 4, ECatInventoryWorldAction::Place).bCommitted)) return false;
	TestTrue(TEXT("没有替换原Actor或重置尺寸"), FindPickup(*World, OriginalId) == Placed
		&& Placed->GetActorScale3D().Equals(OriginalScale));
	TestEqual(TEXT("原Actor载荷更新为当前数量"), Placed->GetPickupInventory().InstanceEntries[0].Count, 4);
	TestTrue(TEXT("原Actor重新开放拾取"), Placed->Interact_Implementation(Controller, FGuid::NewGuid()));
	TestEqual(TEXT("再次拾取不恢复旧数量"), Inventory->CountVisibleInventoryQuantityByDefinitionId(Definition->GetInventoryDefinitionId()), 4);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryReleaseFailureTest,
	"Catfishing.Runtime.Inventory.WorldActions.ReleaseRejectsWithoutConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 失败回归：在没有地面的 World 先提交非法数量、过期身份和动作，再实际尝试放置；所有失败均保留原对象、GUID和数量。
// 最后检查世界没有活的拾取载体，覆盖延迟生成后空间检测失败的清理，不用返回 false 代替库存和世界副作用断言。
bool FCatInventoryReleaseFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	if (!TestTrue(TEXT("创建失败分支角色和控制器"), Character && Controller)) return false;
	// 玩家背包容量在 PossessedBy 中初始化；先完成真实占有，才能把后续失败归因于落地请求而非空背包夹具。
	Controller->Possess(Character);
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	TStrongObjectPtr<UCatInventoryItemDefinition> Definition(MakeStackDefinition());
	if (!TestTrue(TEXT("原堆五件入库"), Inventory && Inventory->AddItemDefinition(Definition.Get(), 5))) return false;
	const int32 Slot = Inventory->FindFirstInventorySlotIndexByDefinitionId(Definition->GetInventoryDefinitionId());
	UCatInventoryItemInstance* Original = Inventory->GetInventoryEntryAtSlot(Slot)->Instance;
	const FGuid Id = Original->GetItemInstanceId();
	for (const int32 Quantity : {0, -1, 6})
	{
		TestEqual(TEXT("非法数量拒绝"), Inventory->ReleaseItemToWorldFromAuthority(Character, FGuid::NewGuid(),
			Slot, Id, Quantity, ECatInventoryWorldAction::Drop).Error, ECatDomainCommandError::InvalidPayload);
	}
	TestEqual(TEXT("已换入实例的旧GUID拒绝"), Inventory->ReleaseItemToWorldFromAuthority(Character, FGuid::NewGuid(),
		Slot, FGuid::NewGuid(), 1, ECatInventoryWorldAction::Drop).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("非法动作拒绝"), Inventory->ReleaseItemToWorldFromAuthority(Character, FGuid::NewGuid(),
		Slot, Id, 1, static_cast<ECatInventoryWorldAction>(255)).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("无地面无法放置"), Inventory->ReleaseItemToWorldFromAuthority(Character, FGuid::NewGuid(),
		Slot, Id, 2, ECatInventoryWorldAction::Place).Error, ECatDomainCommandError::PermissionDenied);
	TestTrue(TEXT("失败保留原对象与GUID"), Inventory->GetInventoryEntryAtSlot(Slot)->Instance == Original && Original->GetItemInstanceId() == Id);
	TestEqual(TEXT("所有失败均未扣数量"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 5);
	int32 LivePickups = 0;
	for (TActorIterator<ACatItem> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++LivePickups;
	TestEqual(TEXT("失败生成的世界物已清理"), LivePickups, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryRodPayloadStateTest,
	"Catfishing.Contract.Inventory.WorldActions.WorldPickupPreservesRodState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 状态载荷回归：加载正式鱼竿定义并设置已断裂实例，经公开世界接收和拾取入口回包，再读取耐久、断裂和原GUID。
// 此处测试接收与拾取合同；鱼竿定义自身的 WorldActorClass 资产接线不在此用例中补写或替换。
bool FCatInventoryRodPayloadStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	UCatEquipmentDefinition* Definition = LoadObject<UCatEquipmentDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatItem* Pickup = World->SpawnActor<ACatItem>();
	if (!TestTrue(TEXT("正式定义和运行宿主就绪"), Definition && Character && Controller && Pickup)) return false;
	Controller->Possess(Character);
	UCatEquipmentInventoryItemInstance* Rod = NewObject<UCatEquipmentInventoryItemInstance>(Pickup);
	Rod->SetItemDefinition(Definition);
	Rod->SetRuntimeOwnerActor(Pickup);
	Rod->SetRodRuntimeStateFromAuthority(0.0, true);
	const FGuid Id = Rod->GetItemInstanceId();
	if (!TestTrue(TEXT("正式断竿状态有效"), Rod->IsRodBroken())) return false;
	if (!TestTrue(TEXT("世界接收正式实例"), Pickup->InitializeFromInventoryFromAuthority(Rod, 1))) return false;
	if (!TestTrue(TEXT("玩家拾取断竿"), Pickup->Interact_Implementation(Controller, FGuid::NewGuid()))) return false;
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	const int32 Slot = Inventory->FindInventorySlotIndexFromInstanceId(Id);
	const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Slot);
	const UCatEquipmentInventoryItemInstance* Returned = Entry ? Cast<UCatEquipmentInventoryItemInstance>(Entry->Instance) : nullptr;
	if (!TestNotNull(TEXT("原GUID对应装备实例"), Returned)) return false;
	TestEqual(TEXT("断竿数量仍是一件"), Entry->StackCount, 1);
	TestTrue(TEXT("原断裂状态未被定义初值覆盖"), Returned->IsRodBroken());
	TestEqual(TEXT("零耐久未被重置为满耐久"), Returned->GetRodDurability(), 0.0);
	TestTrue(TEXT("实例归属已到角色"), Returned->GetRuntimeOwnerActor() == Character);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryFishGuardRoundTripTest,
	"Catfishing.Runtime.Inventory.WorldActions.GuardPickupReleasePreservesFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 鱼护回归：在原生猫上装正式猫蓝图的骨架，给真实鱼护放入两条正式鱼；满包拾取失败后释放容量，再实际拾取和放置。
// 之后再次拾取并丢弃，每次都核对同一载体、同一FishInventory和同一内鱼实例；不生成替代鱼数组，也不改嘴部设置。
bool FCatInventoryFishGuardRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	UClass* CatClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	UCatInventoryItemDefinition* GuardDefinition = LoadObject<UCatInventoryItemDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Items/Item_FishGuard.Item_FishGuard"));
	UCatFishDefinition* FishDefinition = LoadObject<UCatFishDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
	if (!TestTrue(TEXT("正式猫、鱼护定义和鱼种均已生成"), CatClass && GuardDefinition && FishDefinition)) return false;
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatFishGuardActor* Guard = World->SpawnActor<ACatFishGuardActor>(FVector(100, 0, 100), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("鱼护及角色宿主就绪"), Character && Controller && Guard)) return false;
	Controller->Possess(Character);
	const USkeletalMeshComponent* FormalMesh = CatClass->GetDefaultObject<ACatCharacter>()->GetMesh();
	Character->GetMesh()->SetSkeletalMeshAsset(FormalMesh->GetSkeletalMeshAsset());
	Character->GetMesh()->SetRelativeTransform(FormalMesh->GetRelativeTransform());
	if (!TestTrue(TEXT("正式猫骨架具有配置嘴部Socket"), Character->GetMesh()->DoesSocketExist(
		GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName))) return false;
	if (!TestNotNull(TEXT("创建鱼护放置支撑"), AddFloor(*Character))) return false;
	UCatFishOnlyInventoryComponent* FishInventory = Guard->GetFishInventoryComponent();
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestTrue(TEXT("两个正式库存可用"), FishInventory && Inventory)) return false;
	TArray<UCatFishInventoryItemInstance*> FishInstances;
	TArray<FGuid> FishIds;
	for (const double Weight : {2.5, 3.75})
	{
		UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(Guard);
		Fish->SetItemDefinition(FishDefinition);
		Fish->SetRuntimeOwnerActor(Guard);
		if (!TestTrue(TEXT("按捕获合同初始化鱼"), Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(),
			TEXT("GuardWorldActions"), Weight))) return false;
		FCatInventoryReceiveBatch Batch;
		FCatInventoryInstanceEntry& Entry = Batch.InstanceEntries.AddDefaulted_GetRef();
		Entry.ItemInstance = Fish;
		Entry.Count = 1;
		if (!TestTrue(TEXT("实物鱼进入原鱼护库存"), FishInventory->TryAddInventoryBatch(Batch))) return false;
		FishInstances.Add(Fish);
		FishIds.Add(Fish->GetItemInstanceId());
	}
	const int32 Capacity = Inventory->GetInventorySlotCount();
	if (!TestTrue(TEXT("正式背包有容量"), Capacity > 0)) return false;
	// 用正式的一格一鱼填满背包，避免临时改小全局容量；拒绝后清掉这些填充物，内鱼库存完全不动。
	if (!TestTrue(TEXT("填满背包以验证失败"), Inventory->AddItemDefinition(FishDefinition, Capacity))) return false;
	TestFalse(TEXT("满包拾取鱼护失败"), Guard->PickUpFromAuthority(Controller, FGuid::NewGuid()));
	TestTrue(TEXT("满包失败仍在地面"), Guard->IsGrounded());
	TestEqual(TEXT("满包失败不扣背包数量"), Inventory->CountVisibleInventoryQuantityByDefinitionId(FishDefinition->GetInventoryDefinitionId()), Capacity);
	for (int32 Index = 0; Index < FishIds.Num(); ++Index)
	{
		const FCatInventoryEntry* Entry = FishInventory->GetInventoryEntryAtSlot(FishInventory->FindInventorySlotIndexFromInstanceId(FishIds[Index]));
		TestTrue(TEXT("满包失败不损失内鱼身份与数量"), Entry && Entry->Instance == FishInstances[Index] && Entry->StackCount == 1);
	}
	const TArray<FCatInventoryEntry> FillerEntries = Inventory->GetInventoryEntries();
	for (const FCatInventoryEntry& Entry : FillerEntries) if (Entry.Instance) Inventory->RemoveItemInstance(Entry.Instance);
	FGuid GuardId;
	const FVector GuardWorldScale(0.4, 0.6, 0.8);
	Guard->SetActorScale3D(GuardWorldScale);
	for (const ECatInventoryWorldAction Action : {ECatInventoryWorldAction::Place, ECatInventoryWorldAction::Drop})
	{
		if (!TestTrue(TEXT("公开入口拾取原鱼护"), Guard->PickUpFromAuthority(Controller, FGuid::NewGuid()))) return false;
		TestTrue(TEXT("叼起鱼护保留场景世界尺寸"), Guard->GetActorScale3D().Equals(GuardWorldScale, 0.001));
		TestTrue(TEXT("嘴上携带的是原Actor"), ACatFishGuardActor::FindCarriedGuard(Character) == Guard);
		TestFalse(TEXT("携带后不可再作为地面鱼护"), Guard->IsGrounded());
		TestTrue(TEXT("拾起瞬间仍持有原FishInventory"), Guard->GetFishInventoryComponent() == FishInventory);
		for (int32 Index = 0; Index < FishIds.Num(); ++Index)
		{
			const FCatInventoryEntry* FishEntry = FishInventory->GetInventoryEntryAtSlot(FishInventory->FindInventorySlotIndexFromInstanceId(FishIds[Index]));
			TestTrue(TEXT("携带期间每条内鱼仍是原实例"), FishEntry && FishEntry->Instance == FishInstances[Index] && FishEntry->StackCount == 1);
		}
		const int32 Slot = Inventory->FindFirstInventorySlotIndexByDefinitionId(GuardDefinition->GetInventoryDefinitionId());
		const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Slot);
		UCatFishGuardInventoryItemInstance* Item = Entry ? Cast<UCatFishGuardInventoryItemInstance>(Entry->Instance) : nullptr;
		if (!TestNotNull(TEXT("背包持有鱼护实例"), Item)) return false;
		if (!GuardId.IsValid()) GuardId = Item->GetItemInstanceId();
		TestEqual(TEXT("再次拾取沿用同一鱼护GUID"), Item->GetItemInstanceId(), GuardId);
		TestTrue(TEXT("库存实例仍指向原载体"), Item->GetWorldActor() == Guard);
		TestFalse(TEXT("不能以两件数量释放一件鱼护"), Inventory->ReleaseItemToWorldFromAuthority(
			Character, FGuid::NewGuid(), Slot, GuardId, 2, Action).bCommitted);
		TestEqual(TEXT("鱼护数量拒绝不扣"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 1);
		if (!TestTrue(TEXT("通用Release放下原鱼护"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(
			Character, FGuid::NewGuid(), Character, Slot, GuardId, 1, Action).bCommitted)) return false;
		TestTrue(TEXT("原鱼护回到地面且仍有效"), IsValid(Guard) && Guard->IsGrounded());
		TestFalse(TEXT("鱼护已离开背包格"), Inventory->HasItemAtSlot(Slot));
		UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Guard->GetRootComponent());
		TestTrue(TEXT("鱼护物理模式对应Drop或Place"), Body && Body->IsSimulatingPhysics() == (Action == ECatInventoryWorldAction::Drop));
		TestTrue(TEXT("内部库存组件没有重建"), Guard->GetFishInventoryComponent() == FishInventory);
		TestEqual(TEXT("内鱼总数量保持两条"), FishInventory->CountVisibleInventoryQuantityByDefinitionId(FishDefinition->GetInventoryDefinitionId()), 2);
		for (int32 Index = 0; Index < FishIds.Num(); ++Index)
		{
			const FCatInventoryEntry* FishEntry = FishInventory->GetInventoryEntryAtSlot(FishInventory->FindInventorySlotIndexFromInstanceId(FishIds[Index]));
			TestTrue(TEXT("每条内鱼GUID仍对应原UObject"), FishEntry && FishEntry->Instance == FishInstances[Index] && FishEntry->StackCount == 1);
			TestEqual(TEXT("内鱼重量未重置"), FishInstances[Index]->GetFishWeightKilograms(), Index == 0 ? 2.5 : 3.75);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldFishMouthDropTest,
	"Catfishing.Runtime.Inventory.WorldActions.MouthFishDropIdentityAndPhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 单鱼丢弃回归：复用独立 authority World 和正式猫骨架、鱼种，以有效玩家身份初始化库存鱼实例并经真实交互叼起。
// 先验证错误携带者与前方障碍均拒绝且保留嘴部、变换和鱼身份，再移除障碍，观察原 Actor 的物理状态和配置初速度。
bool FCatWorldFishMouthDropTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryWorldActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	UClass* CatClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	UCatFishDefinition* Definition = LoadObject<UCatFishDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
	if (!TestTrue(TEXT("正式猫和鱼种可加载"), CatClass && Definition)) return false;
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
	ACatCharacter* OtherCharacter = World->SpawnActor<ACatCharacter>(FVector(0, 1000, 100), FRotator::ZeroRotator);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerController* OtherController = World->SpawnActor<ACatfishingPlayerController>();
	if (!TestTrue(TEXT("创建两个独立玩家宿主"), Character && OtherCharacter && Controller && OtherController)) return false;
	// 两个请求者都持有合法身份并真实占有角色，保证错误携带者分支不会被无效身份提前截断。
	for (ACatfishingPlayerController* PlayerController : {Controller, OtherController})
	{
		ACatfishingPlayerState* State = World->SpawnActor<ACatfishingPlayerState>();
		if (!TestNotNull(TEXT("创建正式玩家身份"), State)) return false;
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(
			PlayerController == Controller ? TEXT("MouthFishOwner") : TEXT("MouthFishOther"), FName(TEXT("CAT_TEST")));
		State->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		PlayerController->PlayerState = State;
		ACatCharacter* Pawn = PlayerController == Controller ? Character : OtherCharacter;
		Pawn->SetPlayerState(State);
		PlayerController->Possess(Pawn);
	}
	const USkeletalMeshComponent* FormalMesh = CatClass->GetDefaultObject<ACatCharacter>()->GetMesh();
	if (!TestTrue(TEXT("正式猫骨架资源存在"), FormalMesh && FormalMesh->GetSkeletalMeshAsset())) return false;
	Character->GetMesh()->SetSkeletalMeshAsset(FormalMesh->GetSkeletalMeshAsset());
	Character->GetMesh()->SetRelativeTransform(FormalMesh->GetRelativeTransform());
	const FName MouthSocket = GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName;
	if (!TestTrue(TEXT("正式猫具有嘴部Socket"), Character->GetMesh()->DoesSocketExist(MouthSocket))) return false;
	if (!TestNotNull(TEXT("创建真实地面"), AddFloor(*Character))) return false;
	ACatFishPickupActor* Fish = World->SpawnActor<ACatFishPickupActor>(FVector(100, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("创建唯一世界鱼"), Fish)) return false;
	UCatFishInventoryItemInstance* Item = NewObject<UCatFishInventoryItemInstance>(Fish);
	Item->SetItemDefinition(Definition);
	Item->SetRuntimeOwnerActor(Fish);
	const FGuid FishId = FGuid::NewGuid();
	const FGuid SessionId = FGuid::NewGuid();
	constexpr double WeightKilograms = 2.5;
	if (!TestTrue(TEXT("初始化冻结身份和重量"), Item->InitializeFishFromAuthority(
		SessionId, FishId, TEXT("MouthFishOwner"), WeightKilograms))) return false;
	if (!TestTrue(TEXT("世界鱼接收原库存实例"), Fish->InitializeFromInventoryFromAuthority(Item, 1))) return false;
	UBoxComponent* Body = Cast<UBoxComponent>(Fish->GetRootComponent());
	USkeletalMeshComponent* FishMesh = Fish->FindComponentByClass<USkeletalMeshComponent>();
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestTrue(TEXT("鱼物理根、正式网格及玩家库存就绪"), Body && FishMesh && FishMesh->GetSkeletalMeshAsset() && Inventory)) return false;
	const int32 InitialInventoryFish = Inventory->CountVisibleInventoryQuantityByDefinitionId(Definition->GetInventoryDefinitionId());
	const FVector FishWorldScale(0.7, 0.8, 0.9);
	Fish->SetActorScale3D(FishWorldScale);
	const FVector FishVisualScale = FishMesh->GetComponentScale();
	if (!TestTrue(TEXT("通过真实Interact叼起原鱼"), ICatInteractable::Execute_Interact(Fish, Controller, FGuid::NewGuid()))) return false;
	TestTrue(TEXT("叼起原鱼保留场景世界尺寸"), Fish->GetActorScale3D().Equals(FishWorldScale, 0.001));
	const FTransform CarriedMeshTransform = FishMesh->GetRelativeTransform();
	const FVector CarriedBoxExtent = Body->GetUnscaledBoxExtent();

	// 每个阶段从世界枚举活鱼并核对原实例与公开身份，同时观察背包未接收鱼，防止仅以命令返回值掩盖替换或复制载体。
	const auto CheckIdentity = [&, this](const TCHAR* Phase)
	{
		const FString Prefix = FString(Phase) + TEXT(": ");
		int32 LiveFishCount = 0;
		for (TActorIterator<ACatFishPickupActor> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) continue;
			++LiveFishCount;
			TestTrue(Prefix + TEXT("世界仍是原Actor"), *It == Fish);
		}
		TestEqual(Prefix + TEXT("只有一条活鱼"), LiveFishCount, 1);
		TestTrue(Prefix + TEXT("原鱼未被销毁"), IsValid(Fish) && !Fish->IsActorBeingDestroyed());
		TestTrue(Prefix + TEXT("原鱼世界尺寸不变"), Fish->GetActorScale3D().Equals(FishWorldScale, 0.001));
		TestTrue(Prefix + TEXT("原鱼网格显示尺寸不变"), FishMesh->GetComponentScale().Equals(FishVisualScale, 0.001));
		TestEqual(Prefix + TEXT("世界鱼GUID不变"), Fish->GetPresentationState().FishInstanceId, FishId);
		TestEqual(Prefix + TEXT("世界鱼重量不变"), Fish->GetPresentationState().WeightKilograms, WeightKilograms);
		TestEqual(Prefix + TEXT("来源会话不变"), Fish->GetPresentationState().FishingSessionId, SessionId);
		TestEqual(Prefix + TEXT("原实例GUID不变"), Item->GetItemInstanceId(), FishId);
		TestEqual(Prefix + TEXT("原实例重量不变"), Item->GetFishWeightKilograms(), WeightKilograms);
		TestTrue(Prefix + TEXT("原实例运行宿主仍是原鱼"), Item->GetRuntimeOwnerActor() == Fish);
		TestEqual(Prefix + TEXT("没有经背包收货"), Inventory->CountVisibleInventoryQuantityByDefinitionId(
			Definition->GetInventoryDefinitionId()), InitialInventoryFish);
	};
	// 拒绝后读取真实嘴部附件、归属、几何与模拟状态；期望变换在请求前捕获，任何副作用都会留下断言失败。
	// 变换比较容差为位置0.001厘米、缩放及四元数分量0.001；盒尺寸容差为0.001厘米，不允许可见位移或切换姿态。
	const auto CheckMouth = [&, this](const TCHAR* Phase, const FTransform& ExpectedTransform)
	{
		const FString Prefix = FString(Phase) + TEXT(": ");
		CheckIdentity(Phase);
		TestTrue(Prefix + TEXT("原鱼仍占用原玩家嘴部"), ACatFishPickupActor::FindCarriedFish(Character) == Fish);
		TestEqual(Prefix + TEXT("保持Carried"), Fish->GetPresentationState().State, ECatFishPickupState::Carried);
		TestTrue(Prefix + TEXT("携带玩家身份不变"), Fish->GetPresentationState().CarriedByPlayerState == Controller->PlayerState);
		TestTrue(Prefix + TEXT("附着仍是角色Mesh"), Body->GetAttachParent() == Character->GetMesh());
		TestEqual(Prefix + TEXT("嘴部Socket不变"), Body->GetAttachSocketName(), MouthSocket);
		TestTrue(Prefix + TEXT("世界变换不变"), Fish->GetActorTransform().Equals(ExpectedTransform, 0.001));
		TestTrue(Prefix + TEXT("嘴叼网格姿态不变"), FishMesh->GetRelativeTransform().Equals(CarriedMeshTransform, 0.001));
		TestTrue(Prefix + TEXT("物理盒尺寸不变"), Body->GetUnscaledBoxExtent().Equals(CarriedBoxExtent, 0.001));
		TestTrue(Prefix + TEXT("归属仍是原角色"), Fish->GetOwner() == Character && Fish->GetInstigator() == Character);
		TestFalse(Prefix + TEXT("嘴叼鱼未启动物理"), Body->IsSimulatingPhysics());
		TestEqual(Prefix + TEXT("嘴叼碰撞保持关闭"), Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	};
	const FTransform BeforeReject = Fish->GetActorTransform();
	CheckMouth(TEXT("初次叼起"), BeforeReject);
	AddExpectedMessagePlain(TEXT("Event=fish_drop_rejected"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("错误携带者没有丢弃"), Fish->DropFromAuthority(OtherController));
	CheckMouth(TEXT("错误携带者拒绝"), BeforeReject);

	AActor* Obstacle = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建前方障碍宿主"), Obstacle)) return false;
	UBoxComponent* Blocker = NewObject<UBoxComponent>(Obstacle);
	Obstacle->AddInstanceComponent(Blocker);
	Obstacle->SetRootComponent(Blocker);
	Blocker->SetBoxExtent(FVector(20.0, 200.0, 200.0));
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Blocker->SetCollisionObjectType(ECC_WorldStatic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Block);
	Blocker->SetWorldLocation(Character->GetPawnViewLocation() + Character->GetActorForwardVector() * 40.0);
	Blocker->SetMobility(EComponentMobility::Static);
	Blocker->RegisterComponent();
	TestFalse(TEXT("真实空间查询拒绝障碍"), Fish->DropFromAuthority(Controller));
	CheckMouth(TEXT("障碍拒绝"), BeforeReject);
	if (!TestTrue(TEXT("移除障碍以打开同一释放区域"), Obstacle->Destroy())) return false;

	if (!TestTrue(TEXT("无遮挡时正式丢弃"), Fish->DropFromAuthority(Controller))) return false;
	CheckIdentity(TEXT("成功丢弃"));
	TestEqual(TEXT("原鱼切到Available"), Fish->GetPresentationState().State, ECatFishPickupState::Available);
	TestNull(TEXT("嘴部占用解除"), ACatFishPickupActor::FindCarriedFish(Character));
	TestNull(TEXT("附件解除"), Body->GetAttachParent());
	TestTrue(TEXT("携带归属全部清空"), !Fish->GetOwner() && !Fish->GetInstigator() && !Fish->GetPresentationState().CarriedByPlayerState);
	TestTrue(TEXT("原物理根正在模拟"), Body->IsSimulatingPhysics());
	TestTrue(TEXT("没有替换原物理根"), Fish->GetRootComponent() == Body);
	TestTrue(TEXT("释放到角色前方而非原嘴部"), FVector::DotProduct(Fish->GetActorLocation() - Character->GetPawnViewLocation(),
		Character->GetActorForwardVector()) > Character->GetCapsuleComponent()->GetScaledCapsuleRadius());
	TestEqual(TEXT("恢复物理碰撞"), Body->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	// 在任何物理帧推进前读初速度；各轴容差为0.01厘米/秒，允许物理存储精度而不接受零速度或漏掉向上轻抛。
	TestTrue(TEXT("水平初速度来自现有配置"), FMath::IsNearlyEqual(
		FVector::DotProduct(Velocity, Character->GetActorForwardVector().GetSafeNormal2D()), Settings->DropForwardSpeed, 0.01));
	TestTrue(TEXT("向上初速度来自现有配置"), FMath::IsNearlyEqual(Velocity.Z, Settings->DropUpwardSpeed, 0.01));
	TestTrue(TEXT("没有额外侧向速度"), FMath::IsNearlyZero(Velocity.Y, 0.01));
	return !HasAnyErrors();
}

#endif
