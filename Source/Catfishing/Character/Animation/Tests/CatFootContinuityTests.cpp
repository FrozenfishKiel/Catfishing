#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace CatFootContinuity
{
const TCHAR* Classes[] = {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"),
	TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")};
ACatCharacter* Spawn(CatPhysicalTest::FScene& Scene, const TCHAR* Path)
{
	UClass* Class = LoadClass<ACatCharacter>(nullptr, Path);
	if (!Class) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(Class, FVector(0,0,60), FRotator::ZeroRotator, Params);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFootReleaseContinuityTest,
	"Catfishing.Locomotion.Runtime.PlantReleasePreservesPoseContinuityOnBothRigs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFootReleaseContinuityTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : CatFootContinuity::Classes) for (const int32 Rate : {30,60,120})
	{
		CatPhysicalTest::FScene Scene;
		if (!Scene.Initialize(this)) return false;
		auto* Cat = CatFootContinuity::Spawn(Scene, Path);
		if (!TestNotNull(TEXT("formal character loads"), Cat)) return false;
		Scene.Step(120);
		auto* Consumer = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
		auto* Visual = Consumer->GetVisualMesh();
		auto* Source = Consumer->GetAnimationSource();
		auto* Body = Cat->GetPhysicalBodyComponent();
		if (!Visual || !Source) return false;
		FCatQuadrupedLocomotion Solver;
		Solver.ConfigureRig(Consumer->RigSettings);
		const auto Settings = Consumer->LocomotionSettings;
		const FTransform Original = Visual->GetComponentTransform();
		const FTransform PhysicalBefore = Cat->GetActorTransform();
		const double Scale = Original.GetScale3D().GetAbsMax();
		FVector Previous[4] = {};
		int32 Releases = 0;
		double MaxReleaseStep = 0, MaxPawStep = 0;
		// Freeze the actual standing animation; move only the presentation root continuously.
		// This isolates the lock's reach limit from authored motion or physical collision impulses.
		for (int32 Frame = 0; Frame < Rate * 3; ++Frame)
		{
			const double Travel = FMath::Max(0.0, double(Frame) / Rate - 1.0) * 9.0;
			Visual->SetWorldLocation(Original.GetLocation() + FVector(0, Travel * Scale, 0));
			Visual->CopyPoseFromSkeletalComponent(Source);
			Solver.Apply(Source, Visual, Body, 0, 0, 1.0f / Rate, Settings);
			Visual->RefreshBoneTransforms();
			const auto& Sample = Solver.GetObservation();
			for (int32 Foot = 0; Foot < 4; ++Foot)
			{
				const FVector Paw = Visual->GetBoneLocationByName(Consumer->RigSettings.Feet[Foot].Bones.Last(), EBoneSpaces::WorldSpace);
				if (Frame > Rate)
				{
					MaxPawStep = FMath::Max(MaxPawStep, FVector::Distance(Paw, Previous[Foot]) / Scale);
					if (Sample.ReleasedPlantMask & (1 << Foot))
					{
						++Releases;
						MaxReleaseStep = FMath::Max(MaxReleaseStep, FVector::Distance(Paw, Previous[Foot]) / Scale);
					}
				}
				Previous[Foot] = Paw;
			}
		}
		TestTrue(TEXT("scenario actually exceeds a planted foot's reach limit"), Releases > 0);
		TestTrue(TEXT("a released paw never jumps two mesh centimetres in one frame"), MaxReleaseStep < 2.0);
		TestTrue(TEXT("the recovery tail also remains continuous"), MaxPawStep < 2.0);
		TestTrue(TEXT("foot correction cannot move the physical actor"), PhysicalBefore.Equals(Cat->GetActorTransform()));
		Visual->SetWorldTransform(Original);
		AddInfo(FString::Printf(TEXT("Event=foot_release_continuity Class=%s Rate=%d Releases=%d MaxReleaseStepMeshCm=%.4f MaxPawStepMeshCm=%.4f"),
			Path, Rate, Releases, MaxReleaseStep, MaxPawStep));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCuteGaitContinuityTest,
	"Catfishing.Locomotion.Runtime.CuteCatWalkingHasNoLockReleasePop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCuteGaitContinuityTest::RunTest(const FString& Parameters)
{
	for (const double Speed : {100.0,200.0,300.0})
	{
		CatPhysicalTest::FScene Scene;
		if (!Scene.Initialize(this)) return false;
		auto* Cat = CatFootContinuity::Spawn(Scene, CatFootContinuity::Classes[1]);
		if (!Cat) return false;
		Scene.Step(90);
		auto* Body = Cat->GetPhysicalBodyComponent();
		auto* Consumer = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
		auto* Visual = Consumer->GetVisualMesh();
		auto* Source = Consumer->GetAnimationSource();
		const double Scale = Visual->GetComponentScale().GetAbsMax();
		Body->SetMovementSpeed(Speed);
		FVector PreviousCorrection[4] = {};
		double MaxReleaseCorrection = 0, MaxCorrection = 0, MaxError = 0;
		int32 Releases = 0, Walking = 0;
		// Six seconds keeps the 300 cm/s run inside this world's 2000 cm floor half-width.
		for (int32 Frame = 0; Frame < 360; ++Frame)
		{
			Body->SetMoveIntent(FVector::ForwardVector);
			Scene.Step(1);
			const auto& Sample = Consumer->GetLocomotionObservation();
			Walking += Sample.Mode == TEXT("Walking");
			for (int32 Foot = 0; Foot < 4; ++Foot)
			{
				const FName Ankle = Consumer->RigSettings.Feet[Foot].Bones.Last();
				const FVector Correction = Visual->GetBoneLocationByName(Ankle, EBoneSpaces::WorldSpace) - Source->GetBoneLocation(Ankle);
				if (Frame > 90)
				{
					const double Step = FVector::Distance(Correction, PreviousCorrection[Foot]) / Scale;
					MaxCorrection = FMath::Max(MaxCorrection, Step);
					MaxError = FMath::Max(MaxError, Sample.FootErrorCm[Foot] / Scale);
					if (Sample.ReleasedPlantMask & (1 << Foot)) { ++Releases; MaxReleaseCorrection = FMath::Max(MaxReleaseCorrection, Step); }
				}
				PreviousCorrection[Foot] = Correction;
			}
		}
		TestTrue(TEXT("the production CuteCat graph and CMC execute walking"), Walking > 300);
		TestTrue(TEXT("actual gait does not add an abrupt lock-release correction"), MaxReleaseCorrection < 2.0);
		AddInfo(FString::Printf(TEXT("Event=cute_gait_continuity Speed=%.1f Releases=%d MaxReleaseCorrectionMeshCm=%.4f MaxCorrectionMeshCm=%.4f MaxErrorMeshCm=%.4f"),
			Speed, Releases, MaxReleaseCorrection, MaxCorrection, MaxError));
	}
	return !HasAnyErrors();
}
#endif
