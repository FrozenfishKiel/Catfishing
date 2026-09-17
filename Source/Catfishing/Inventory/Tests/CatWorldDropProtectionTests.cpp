#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/BoxComponent.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Inventory/CatWorldDropProtectionComponent.h"
#include "Inventory/CatInventoryStatics.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Character/CatCharacter.h"
#include "Items/CatItem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatDropWholeTrajectoryTest,
	"Catfishing.Unit.Inventory.WorldDropProtectionCoversWholeTrajectoryAndReusesActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatDropWholeTrajectoryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建防护测试World"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	auto* World = Wrapper.GetTestWorld();
	FCatWaterGeometryBuildInput Input;
	Input.RegionId = TEXT("Batch7DDropWater");
	Input.WaterPointVerticalToleranceCm = 100;
	Input.BankHeightToleranceCm = 50;
	Input.BoundaryToleranceCm = 1;
	Input.MaxLandingCorrectionCm = 100;
	Input.MinimumWaterInsetCm = 1;
	FCatWaterPolygonBuildInput Polygon;
	Polygon.BoundaryId = TEXT("NarrowWater");
	Polygon.Vertices = {{400,-500},{450,-500},{450,500},{400,500}};
	Input.Boundaries.Add(Polygon);
	const auto Built = FCatWaterGeometry::Build(Input);
	if (!TestTrue(TEXT("窄湖几何烘焙成功"), Built.bSucceeded)) return false;
	auto* Region = World->SpawnActor<ACatWaterRegion>();
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
	if (!TestTrue(TEXT("启动防护World"), Wrapper.BeginPlayInTestWorld())) return false;
	auto* Water = World->GetSubsystem<UCatWaterQuerySubsystem>();
	TestTrue(TEXT("高空整段穿过窄湖也阻挡"), Water->DoesWorldDropSweepTouchWater(FVector(0,0,5000), FVector(1000,0,5000), 1));
	TestTrue(TEXT("物体边缘触水也阻挡"), Water->DoesWorldDropSweepTouchWater(FVector(350,-200,100), FVector(350,200,100), 60));
	TestFalse(TEXT("完整陆地轨迹放行"), Water->DoesWorldDropSweepTouchWater(FVector(0,600,100), FVector(1000,600,100), 10));
	auto* Cat = World->SpawnActor<ACatCharacter>(FVector(0,0,100), FRotator::ZeroRotator);
	auto* Item = World->SpawnActor<ACatItem>();
	auto* Definition = NewObject<UCatInventoryItemDefinition>();
	Definition->ItemId = 1767827;
	Definition->InventoryMaxStackCount = 1;
	Definition->WorldActorClass = ACatItem::StaticClass();
	auto* Instance = NewObject<UCatInventoryItemInstance>(Item);
	Instance->SetItemDefinition(Definition);
	if (!TestTrue(TEXT("原Actor初始化物品载荷"), Item->InitializeFromInventoryFromAuthority(Instance, 1))) return false;
	Item->SetActorHiddenInGame(true);
	Item->SetActorEnableCollision(false);
	auto* Inventory = Cat->GetInventoryComponent();
	// 原生未占有角色没有蓝图/玩家装配的背包容量，夹具显式准备一个接收格。
	Inventory->SetInventorySlotCountFromAuthority(1);
	if (!TestTrue(TEXT("原物品入背包夹具"), Inventory->AddItemInstance(Instance, 1))) return false;
	const FGuid Id = Instance->GetItemInstanceId();
	if (!TestTrue(TEXT("正式库存Drop启用防护"), UCatInventoryStatics::ReleaseItemToWorldFromAuthority(Cat,
		FGuid::NewGuid(), Cat, Inventory->FindInventorySlotIndexFromInstanceId(Id), Id, 1, ECatInventoryWorldAction::Drop).bCommitted)) return false;
	auto* Protection = Item->FindComponentByClass<UCatWorldDropProtectionComponent>();
	if (!TestNotNull(TEXT("原Actor接入运行防护组件"), Protection)) return false;
	const FTransform Safe = Item->GetActorTransform();
	// 模拟一次物理步跨过整条窄湖；最终点也在陆地，不能只查终点。
	Item->SetActorLocation(Safe.GetLocation() + FVector(1000,0,0), false, nullptr, ETeleportType::TeleportPhysics);
	Protection->TickComponent(1.0f/60, LEVELTICK_All, &Protection->PrimaryComponentTick);
	TestTrue(TEXT("中途穿水恢复原安全姿态"), Item->GetActorTransform().Equals(Safe));
	TestTrue(TEXT("防护保留原实例及Actor"), Instance->GetWorldActor() == Item && Instance->GetItemInstanceId() == Id && !Item->IsActorBeingDestroyed());
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Item->GetRootComponent());
	Body->SetPhysicsLinearVelocity(FVector(1000, 0, 200));
	bool bStayedOutOfWater = true;
	bool bActuallyMoved = false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Wrapper.TickTestWorld(1.0f / 60.0f);
		bActuallyMoved |= FVector::DistSquared(Item->GetActorLocation(), Safe.GetLocation()) > 100.0;
		bStayedOutOfWater &= !Water->DoesWorldDropSweepTouchWater(Body->Bounds.Origin, Body->Bounds.Origin, Body->Bounds.SphereRadius);
	}
	TestTrue(TEXT("真实物理抛落发生运动"), bActuallyMoved);
	TestTrue(TEXT("整个物理抛落保持水域外且未丢失原Actor"), bStayedOutOfWater && !Item->IsActorBeingDestroyed());
	Item->SetActorHiddenInGame(true);
	Protection->TickComponent(1.0f/60, LEVELTICK_All, &Protection->PrimaryComponentTick);
	TestFalse(TEXT("拾取保管态停止防护"), Protection->IsComponentTickEnabled());
	Item->SetActorHiddenInGame(false);
	UCatWorldDropProtectionComponent::ArmFromAuthority(Item);
	TestTrue(TEXT("再次丢出复用同一组件"), Item->FindComponentByClass<UCatWorldDropProtectionComponent>() == Protection && Protection->IsComponentTickEnabled());
	return !HasAnyErrors();
}
#endif
