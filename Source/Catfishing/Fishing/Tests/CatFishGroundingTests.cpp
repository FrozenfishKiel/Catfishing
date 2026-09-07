#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Items/World/CatFishPickupActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGroundPresentationTest,
	"Catfishing.Unit.Fishing.Presentation.DeadFishStayAboveGroundAtTheirOriginalSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishGroundPresentationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("create fish grounding world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = Wrapper.GetTestWorld();
	const FVector Contact(100, -200, 50);
	const auto CheckGround = [&](USkeletalMeshComponent* Mesh, const FVector& Normal)
	{
		const FBox Bounds = Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox()
			+ Mesh->CalcBounds(FTransform::Identity).GetBox();
		double Lowest = TNumericLimits<double>::Max();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
				(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
			Lowest = FMath::Min(Lowest, FVector::DotProduct(Mesh->GetComponentTransform().TransformPosition(Local) - Contact, Normal));
		}
		TestTrue(FString::Printf(TEXT("entire posed/scaled mesh clears slope: %.3f cm"), Lowest), Lowest >= 0.99);
	};
	for (const TCHAR* FishName : {TEXT("Salted"), TEXT("LakeGiantShadow"), TEXT("LittleSilver")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Catfishing/Data/Fish/Fish_%s.Fish_%s"), FishName, FishName);
		UCatFishDefinition* Definition = LoadObject<UCatFishDefinition>(nullptr, *Path);
		if (!TestNotNull(TEXT("formal fish definition"), Definition)) return false;
		const auto* Presentation = Definition->LoadRuntimePresentationDefinition();
		if (!TestNotNull(TEXT("formal presentation"), Presentation)) return false;
		for (const double Scale : {0.8, 1.25})
		{
			for (const FVector Normal : {FVector::UpVector, FVector(0.35, -0.2, 1.0).GetSafeNormal()})
			{
				const FGuid SessionId = FGuid::NewGuid();
				ACatFishEncounterActor* Fish = World->SpawnActor<ACatFishEncounterActor>();
				Fish->SetActorLocationAndRotation(Contact, FRotator(0, 37, 0));
				TestTrue(TEXT("initialize original fish size"), Fish->InitializeAuthoritativeIdentity(SessionId, FGuid::NewGuid(), Definition->FishDefinitionId, 100, Scale));
				TestTrue(TEXT("grounded dead fish"), Fish->ApplyFightStepFromAuthority(ECatFishMotionIntent::AutoHauling,
					100, Contact, 0, 0, 0, 0, false, true, Normal));
				auto* FishMesh = Fish->FindComponentByClass<USkeletalMeshComponent>();
				if (!FishMesh || !FishMesh->GetSkeletalMeshAsset()) return false;
				CheckGround(FishMesh, Normal);
				ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>();
				Pickup->SetActorLocationAndRotation(Contact, FRotator(0, 37, Presentation->LandedActorRollDegrees));
				TestTrue(TEXT("pickup inherits frozen fish size"), Pickup->InitializeFromAuthority(SessionId, FGuid::NewGuid(), Definition,
					1.0, Scale, TEXT("TestWater"), {}, Normal));
				auto* PickupMesh = Pickup->FindComponentByClass<USkeletalMeshComponent>();
				if (!PickupMesh || !PickupMesh->GetSkeletalMeshAsset()) return false;
				CheckGround(PickupMesh, Normal);
				TestTrue(TEXT("pickup and original fish share world size"), PickupMesh->GetComponentScale().Equals(FishMesh->GetComponentScale(), 0.001));
				const FVector BeforeRepeat = FishMesh->GetComponentLocation();
				Fish->ApplyFightStepFromAuthority(ECatFishMotionIntent::AutoHauling, 100, Contact, 0, 0, 0, 0, false, true, Normal);
				TestTrue(TEXT("ground lift does not accumulate"), FishMesh->GetComponentLocation().Equals(BeforeRepeat, 0.001));
				Fish->ApplyFightStepFromAuthority(ECatFishMotionIntent::AutoHauling, 100, Contact, 0, 0, 0, 0, false, false);
				const FTransform Expected = Presentation->EncounterMeshRelativeTransform
					* FTransform(FRotator(0, 0, Presentation->ExhaustedVisualRollDegrees)) * Fish->GetActorTransform();
				TestTrue(TEXT("water reentry clears ground-only offset"), FishMesh->GetComponentLocation().Equals(Expected.GetLocation(), 0.001));
				Pickup->Destroy();
				Fish->Destroy();
			}
		}
	}
	return !HasAnyErrors();
}

#endif
