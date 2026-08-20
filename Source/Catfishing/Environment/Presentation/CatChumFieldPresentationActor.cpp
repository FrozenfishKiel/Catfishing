#include "Environment/Presentation/CatChumFieldPresentationActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACatChumFieldPresentationActor::ACatChumFieldPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);

	PlaceholderRing = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PlaceholderRing"));
	PlaceholderRing->SetupAttachment(VisualRoot);
	PlaceholderRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderRing->SetGenerateOverlapEvents(false);
	PlaceholderRing->SetCastShadow(false);
	PlaceholderRing->SetCanEverAffectNavigation(false);

	PlaceholderSpecks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PlaceholderSpecks"));
	PlaceholderSpecks->SetupAttachment(VisualRoot);
	PlaceholderSpecks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderSpecks->SetGenerateOverlapEvents(false);
	PlaceholderSpecks->SetCastShadow(false);
	PlaceholderSpecks->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeMesh.Succeeded()) PlaceholderRing->SetStaticMesh(CubeMesh.Object);
	if (SphereMesh.Succeeded()) PlaceholderSpecks->SetStaticMesh(SphereMesh.Object);
	if (BasicMaterial.Succeeded())
	{
		PlaceholderRing->SetMaterial(0, BasicMaterial.Object);
		PlaceholderSpecks->SetMaterial(0, BasicMaterial.Object);
	}
	SetActorEnableCollision(false);
}

void ACatChumFieldPresentationActor::ApplyPublicState(
	const FCatChumFieldPublicItem& NewState, const bool bAdded)
{
	PublicState = NewState;
	SetActorLocation(PublicState.CenterWorldPoint);
	RebuildPlaceholderVisual();
	if (bAdded) BP_OnFieldAdded(PublicState); else BP_OnFieldChanged(PublicState);
}

void ACatChumFieldPresentationActor::NotifyFieldRemoved()
{
	PlaceholderRing->SetVisibility(false, true);
	PlaceholderSpecks->SetVisibility(false, true);
	BP_OnFieldRemoved(PublicState);
}

void ACatChumFieldPresentationActor::RebuildPlaceholderVisual()
{
	PlaceholderRing->ClearInstances();
	PlaceholderSpecks->ClearInstances();
	const float Radius = static_cast<float>(PublicState.RadiusCentimeters);
	if (!FMath::IsFinite(Radius) || Radius <= KINDA_SMALL_NUMBER)
	{
		PlaceholderRing->SetVisibility(false, true);
		PlaceholderSpecks->SetVisibility(false, true);
		return;
	}

	PlaceholderRing->SetVisibility(true, true);
	PlaceholderSpecks->SetVisibility(true, true);
	if (UMaterialInstanceDynamic* RingMaterial = PlaceholderRing->CreateDynamicMaterialInstance(0))
	{
		RingMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.62f, 0.20f, 1.0f));
	}
	if (UMaterialInstanceDynamic* SpeckMaterial = PlaceholderSpecks->CreateDynamicMaterialInstance(0))
	{
		SpeckMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.38f, 0.16f, 0.035f, 1.0f));
	}

	// 用一个 ISM draw call 拼出离散圆环；目标弧段约 65cm，大小窝点都保持可辨认而不过度增加实例。
	const float Circumference = 2.0f * UE_PI * Radius;
	const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(Circumference / 65.0f), 24, 96);
	const float SegmentLength = 2.0f * Radius * FMath::Sin(UE_PI / static_cast<float>(SegmentCount)) * 0.86f;
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const float Angle = 2.0f * UE_PI * static_cast<float>(Index) / static_cast<float>(SegmentCount);
		const FVector Location(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 4.0f);
		const FRotator Rotation(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f);
		const FVector Scale(FMath::Max(SegmentLength / 100.0f, 0.02f), 0.07f, 0.012f);
		PlaceholderRing->AddInstance(FTransform(Rotation, Location, Scale), false);
	}

	// FieldId 作为随机种子：同一个复制事实在所有客户端得到相同碎屑布局，不需要额外复制实例 Transform。
	FRandomStream Random(GetTypeHash(PublicState.FieldId));
	const int32 SpeckCount = FMath::Clamp(FMath::RoundToInt(Radius / 18.0f), 18, 48);
	for (int32 Index = 0; Index < SpeckCount; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * UE_PI);
		const float Distance = FMath::Sqrt(Random.FRand()) * Radius * 0.82f;
		const FVector Location(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle), Random.FRandRange(5.0f, 9.0f));
		const float Size = Random.FRandRange(0.025f, 0.065f);
		const FRotator Rotation(Random.FRandRange(-18.0f, 18.0f), Random.FRandRange(0.0f, 360.0f),
			Random.FRandRange(-18.0f, 18.0f));
		PlaceholderSpecks->AddInstance(FTransform(Rotation, Location, FVector(Size)), false);
	}
}
