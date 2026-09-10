#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerState.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Presentation/CatRodBendCurve.h"
#include "Fishing/Presentation/CatRodBendComponent.h"
#include "Fishing/Presentation/CatFishingLineCurveComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodBendGeometryTest,
	"Catfishing.Unit.Fishing.Presentation.RodBendDirectionGripLengthAndResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodBendGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Base(0, 0, 40), Tip(0, 0, 220);
	for (const FVector Force : {FVector(100, 0, 0), FVector(-100, 0, 0), FVector(0, 100, 0), FVector(0, -100, -50)})
	{
		FCatRodBendCurve Curve;
		const FVector Bend = FCatRodBendCurve::TargetBend(FVector::UpVector, Force, 50.0, UE_PI / 2.0);
		TestTrue(TEXT("curve accepts finite geometry"), Curve.Initialize(Base, Tip, Bend));
		const FVector Deflected = Curve.DeformPosition(Tip) - Tip;
		TestTrue(TEXT("tip moves in transverse load direction"), FVector::DotProduct(Deflected, Force.GetSafeNormal2D()) > 1.0);
		TestEqual(TEXT("grip/reel vertices stay exactly rigid"), Curve.DeformPosition(FVector(5, -6, 30)), FVector(5, -6, 30));
		double Arc = 0.0;
		FVector Previous = Base;
		for (int32 I = 1; I <= FCatRodBendCurve::Segments; ++I)
		{
			const FVector Next = Curve.DeformPosition(FMath::Lerp(Base, Tip, static_cast<double>(I) / FCatRodBendCurve::Segments));
			Arc += FVector::Distance(Previous, Next);
			Previous = Next;
		}
		TestEqual(TEXT("centreline does not stretch"), Arc, 180.0, 0.001);
		TestTrue(TEXT("bend remains bounded under load"), Bend.Size() < UE_PI / 2.0);
	}
	TestTrue(TEXT("pure axial tension causes no lateral bending"),
		FCatRodBendCurve::TargetBend(FVector::UpVector, FVector(0, 0, 1000), 50, 1.3).IsZero());
	FVector Reference;
	for (const int32 FPS : {15, 30, 60, 120, 240})
	{
		FVector Value = FVector::ZeroVector;
		for (int32 I = 0; I < FPS; ++I) Value = FCatRodBendCurve::SmoothBend(Value, FVector(0.8, 0, 0), 1.0 / FPS, 0.15);
		if (FPS == 15) Reference = Value;
		else TestTrue(TEXT("response is independent of frame rate"), Value.Equals(Reference, 1.e-6));
		for (int32 I = 0; I < FPS * 3; ++I) Value = FCatRodBendCurve::SmoothBend(Value, FVector::ZeroVector, 1.0 / FPS, 0.15);
		TestTrue(TEXT("slack releases stored visual bend"), Value.IsNearlyZero(1.e-6));
	}
	FCatRodBendCurve Straight;
	Straight.Initialize(Base, Tip, FVector::ZeroVector);
	TestEqual(TEXT("rest shape preserves original off-axis art"), Straight.DeformPosition(FVector(3, 4, 125)), FVector(3, 4, 125));
	TestFalse(TEXT("degenerate geometry rejected"), Straight.Initialize(Base, Base, FVector::ZeroVector));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodBendFormalRuntimeTest,
	"Catfishing.Unit.Fishing.Presentation.RodBendFormalMeshHookAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodBendFormalRuntimeTest::RunTest(const FString& Parameters)
{
	UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
	UClass* HookClass = LoadClass<ACatFishingHookActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingHookActor.BP_CatFishingHookActor_C"));
	if (!TestNotNull(TEXT("formal rod loads"), RodClass) || !TestNotNull(TEXT("formal hook loads"), HookClass)) return false;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>(RodClass);
	if (!TestNotNull(TEXT("formal rod spawns"), Rod)) return false;
	APlayerState* Owner = World->SpawnActor<APlayerState>();
	Rod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(161.52, -1.30, 151.89)), FTransform::Identity, FTransform::Identity);
	TestTrue(TEXT("rod identity initialized"), Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(),
		TEXT("Rod_Basic"), NAME_None, Owner, nullptr, true, false));
	FActorSpawnParameters Spawn;
	Spawn.Owner = Rod;
	ACatFishingHookActor* Hook = World->SpawnActor<ACatFishingHookActor>(HookClass, FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("formal hook spawns"), Hook)) return false;
	Hook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid());
	Wrapper.BeginPlayInTestWorld();
	// 仅观察弯曲表现，不向无地板夹具施加重力；真实掉落/受力另由 PhysicalRod 测试覆盖。
	Rod->GetPhysicalRodBody()->SetEnableGravity(false);
	UCatRodBendComponent* Bend = Rod->FindComponentByClass<UCatRodBendComponent>();
	if (!TestNotNull(TEXT("formal rod has deformation component"), Bend) || !TestTrue(TEXT("formal source mesh is copied"), Bend->IsVisualReady())) return false;
	UStaticMeshComponent* Source = nullptr;
	USceneComponent* Marker = nullptr;
	TInlineComponentArray<USceneComponent*> Components(Rod);
	for (USceneComponent* Component : Components)
	{
		if (Component->ComponentHasTag(TEXT("RodBendSource"))) Source = Cast<UStaticMeshComponent>(Component);
		if (Component->ComponentHasTag(TEXT("RodTipMarker"))) Marker = Component;
	}
	if (!TestNotNull(TEXT("formal source is explicitly tagged"), Source) || !TestNotNull(TEXT("formal tip exists"), Marker)) return false;
	TestTrue(TEXT("derived source supports cooked CPU access"), Source->GetStaticMesh()->bAllowCPUAccess);
	TestFalse(TEXT("rigid source is not drawn twice"), Source->IsVisible());
	TestEqual(TEXT("original material reused"), Bend->GetMaterial(0), Source->GetMaterial(0));
	TestEqual(TEXT("deformation has no collision"), Bend->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	const FVector RestTip = Bend->GetRestTipWorld();
	const TArray<FProcMeshVertex> OriginalVertices = Bend->GetProcMeshSection(0)->ProcVertexBuffer;
	const FTransform CanonicalBefore = Rod->GetRodTipWorldTransform();
	const FVector Landing = RestTip + FVector(0, 700, 0);
	Hook->FinalizeAuthoritativeLandingOnce(true, Landing);
	TestTrue(TEXT("final line load publishes in newtons"), Hook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, 100));
	TestEqual(TEXT("raw load is distinct from normalized display"), Hook->GetPresentationState().LineTensionNewtons, 100.0);
	for (int32 I = 0; I < 120; ++I) Wrapper.TickTestWorld(1.0f / 60.0f);
	TestTrue(TEXT("unheld rod bends under the observed line load"), Marker->GetComponentLocation().Y > RestTip.Y + 10);
	TestTrue(TEXT("visual bend leaves canonical physics tip unchanged"), Rod->GetRodTipWorldTransform().Equals(CanonicalBefore));
	const UCatFishingLineCurveComponent* Line = Hook->FindComponentByClass<UCatFishingLineCurveComponent>();
	if (TestNotNull(TEXT("formal hook has line mesh"), Line) && !Line->GetCurveWorldPoints().IsEmpty())
	{
		TestTrue(TEXT("line attaches exactly to bent marker"), Line->GetCurveWorldPoints().Last().Equals(Marker->GetComponentLocation(), 0.01));
		const auto& Points = Line->GetCurveWorldPoints();
		TestEqual(TEXT("taut line does not acquire artificial slack from bending"), Line->GetCurveLengthCentimeters(),
			FVector::Distance(Points[0], Points.Last()), 0.1);
	}
	int32 ChangedVertices = 0;
	const auto& BentVertices = Bend->GetProcMeshSection(0)->ProcVertexBuffer;
	for (int32 I = 0; I < OriginalVertices.Num(); ++I)
	{
		if (!BentVertices[I].Position.Equals(OriginalVertices[I].Position, 0.1)) ++ChangedVertices;
		TestEqual(TEXT("deforming preserves source UVs"), BentVertices[I].UV0, OriginalVertices[I].UV0);
	}
	TestTrue(TEXT("load deforms actual mesh vertices"), ChangedVertices > 100);
	TestFalse(TEXT("invalid load rejected"), Hook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, -1));
	Hook->SetFishingLinePresentationFromAuthority(800, 700, 100, 0, false, 100);
	TestEqual(TEXT("slack cannot carry stale force"), Hook->GetPresentationState().LineTensionNewtons, 0.0);
	for (int32 I = 0; I < 180; ++I) Wrapper.TickTestWorld(1.0f / 60.0f);
	TestTrue(TEXT("slack returns to original tip"), Marker->GetComponentLocation().Equals(RestTip, 0.05));
	const auto& RelaxedVertices = Bend->GetProcMeshSection(0)->ProcVertexBuffer;
	for (int32 I = 0; I < OriginalVertices.Num(); ++I)
		TestTrue(TEXT("relaxed mesh restores original art"), RelaxedVertices[I].Position.Equals(OriginalVertices[I].Position, 0.01));
	Hook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, 100);
	for (int32 I = 0; I < 90; ++I) Wrapper.TickTestWorld(1.0f / 60.0f);
	Hook->Destroy();
	for (int32 I = 0; I < 180; ++I) Wrapper.TickTestWorld(1.0f / 60.0f);
	TestTrue(TEXT("destroying hook releases bend without another network update"), Marker->GetComponentLocation().Equals(RestTip, 0.05));
	return !HasAnyErrors();
}

#endif
