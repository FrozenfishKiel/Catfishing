#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"

namespace CatPhysicalConsumerTest
{
struct FScene
{
	FTestWorldWrapper World;
	bool Initialize(FAutomationTestBase* Test)
	{
		if (!World.CreateTestWorld(EWorldType::Game)) return false;
		World.ForwardErrorMessages(Test);
		World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		AStaticMeshActor* Floor = World.GetTestWorld()->SpawnActor<AStaticMeshActor>();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Floor || !Cube) return false;
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Floor->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, -10), FVector(20, 20, .2)));
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		return World.BeginPlayInTestWorld();
	}
	ACatCharacter* Spawn(const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.GetTestWorld()->SpawnActor<ACatCharacter>(At, FRotator::ZeroRotator, Params);
	}
	void Step(int32 Frames, ACatCharacter* Cat = nullptr, const FRotator& View = FRotator::ZeroRotator)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (Cat) Cat->GetPhysicalBodyComponent()->SetViewIntent(View);
			World.TickTestWorld(1.0f / 60.0f);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalCharacterTeleportConsumerTest,
	"Catfishing.PhysicalGrab.Runtime.CharacterTeleportReleasesIncomingGripAndMovesThreeBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalCharacterTeleportConsumerTest::RunTest(const FString& Parameters)
{
	CatPhysicalConsumerTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	ACatCharacter* Grabbing = Scene.Spawn(FVector(0, 0, 20));
	ACatCharacter* Target = Scene.Spawn(FVector(40, 0, 20));
	if (!Grabbing || !Target) return false;
	Scene.Step(30);
	auto* Grab = Grabbing->GetPhysicalBodyComponent()->GetGrab();
	Grabbing->GetPhysicalBodyComponent()->SetViewIntent(FRotator::ZeroRotator);
	Grab->SetGrabInput(true, true);
	Scene.Step(60, Grabbing);
	if (!TestTrue(TEXT("正式猫伸爪建立真实猫对猫接触"),
		Grab->IsGripping(true) && Grab->GetGripTarget(true) == Target)) return false;
	auto* Body = Target->GetPhysicalBodyComponent();
	const uint32 Epoch = Body->GetResetEpoch();
	const FVector Destination(300, 0, 20);
	if (!TestTrue(TEXT("营地使用的Character传送入口成功"), Target->TeleportTo(Destination, FRotator::ZeroRotator, false, false))) return false;
	TestFalse(TEXT("传送先释放别人连到目标的约束"), Grab->IsGripping(true));
	TestTrue(TEXT("身体快照失效域增加"), Body->GetResetEpoch() > Epoch);
	TestTrue(TEXT("刚体落到指定位置"), Body->GetBody()->GetComponentLocation().Equals(Destination, .1));
	for (const bool bLeft : {true, false})
	{
		const FVector Expected = Body->GetBody()->GetComponentTransform().TransformPosition(UCatPhysicsGrabComponent::RestHandLocal(bLeft));
		TestTrue(TEXT("独立手刚体随同一传送重置"), Body->GetHand(bLeft)->GetComponentLocation().Equals(Expected, .1));
	}
	Scene.Step(10);
	TestTrue(TEXT("旧约束不会把被救援猫拉回旧地点"), Target->GetActorLocation().X > 290.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalCharacterWaterFootConsumerTest,
	"Catfishing.PhysicalGrab.Runtime.ConditionMeasuresPhysicalFeetAtWaterThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalCharacterWaterFootConsumerTest::RunTest(const FString& Parameters)
{
	CatPhysicalConsumerTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	UWorld* World = Scene.World.GetTestWorld();
	ACatWaterRegion* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
	ACatCharacter* Cat = Scene.Spawn(FVector(0, 0, 20));
	if (!Region || !Cat) return false;
	FCatWaterGeometryBuildInput Geometry;
	Geometry.RegionId = TEXT("PhysicalBodyWaterConsumer");
	Geometry.PlaneToWorld = FTransform(FVector(0, 0, 100));
	Geometry.WaterPointVerticalToleranceCm = 100;
	Geometry.BankHeightToleranceCm = 100;
	Geometry.BoundaryToleranceCm = 2;
	Geometry.MaxLandingCorrectionCm = 20;
	Geometry.MinimumWaterInsetCm = 5;
	auto& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
	Boundary.BoundaryId = TEXT("Outer");
	Boundary.Vertices = {FVector2D(-500, -500), FVector2D(500, -500), FVector2D(500, 500), FVector2D(-500, 500)};
	const auto Baked = FCatWaterGeometry::Build(Geometry);
	if (!TestTrue(TEXT("水深使用真实水域几何查询"), Baked.bSucceeded)) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
	// 注册仍走实际水域子系统，不伪造浸没查询结果。
	Region->FinishSpawning(FTransform::Identity);
	auto* Body = Cat->GetPhysicalBodyComponent();
	const auto* Settings = GetDefault<UCatConditionSettings>();
	const double DepthTarget = Settings->DangerousWaterDepthCentimeters + 1.0;
	if (!Body->TeleportBodyFromAuthority(FTransform(FVector(0, 0, Baked.Cache.WaterSurfaceZ + Body->GetStandRootHeightCm() - DepthTarget)), TEXT("WaterThresholdTest"))) return false;
	double Depth = 0;
	const auto Exposure = Cat->GetConditionComponent()->UpdateWaterExposureFromAuthority(
		Region->GetWaterRegionHandle(), Settings->DangerousWaterConfirmationSeconds + .01, Depth);
	TestEqual(TEXT("Condition读到物理脚点水深而非旧胶囊半高"), Depth, DepthTarget, .01);
	TestEqual(TEXT("满足确认时长后进入危险水域"), Exposure, ECatWaterExposureUpdate::DangerousEntered);
	return !HasAnyErrors();
}

#endif
