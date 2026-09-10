#if WITH_DEV_AUTOMATION_TESTS
#include "Interaction/Grab/CatPhysicsGrabProp.h"

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Animation/AnimInstance.h"
#include "Character/Animation/CatQuadrupedLocomotion.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"

namespace CatLocomotionTest
{
	const FName Ankles[] = {TEXT("RigLFLegAnkle"), TEXT("RigRFLegAnkle"), TEXT("RigLBLegAnkle"), TEXT("RigRBLegAnkle")};
	struct FScene
	{
		FTestWorldWrapper World;
		AStaticMeshActor* AddBox(FVector Position, FVector HalfExtent)
		{
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			const FTransform Transform(FRotator::ZeroRotator, Position, HalfExtent / 50.0);
			auto* Box = World.GetTestWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
			if (!Cube || !Box) return nullptr;
			Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Box->FinishSpawning(Transform);
			return Box;
		}
		bool Initialize(FAutomationTestBase* Test)
		{
			if (!World.CreateTestWorld(EWorldType::Game)) return false;
			World.ForwardErrorMessages(Test);
			World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
			return AddBox(FVector(0,0,-10), FVector(8000,8000,10)) && World.BeginPlayInTestWorld();
		}
		ACatPhysicsPrototypePawn* Spawn()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World.GetTestWorld()->SpawnActor<ACatPhysicsPrototypePawn>(FVector(0,0,20), FRotator::ZeroRotator, Params);
		}
		void Step(int32 Frames, UCatPhysicalBodyComponent* Body = nullptr, FVector Intent = FVector::ZeroVector)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				if (Body) Body->SetMoveIntent(Intent);
				World.TickTestWorld(1.0f / 60.0f);
			}
		}
	};

	bool CheckSkeletonContract(FAutomationTestBase* Test, USkeletalMeshComponent* Source, UPoseableMeshComponent* Visual)
	{
		const auto& Authored = Source->GetBoneSpaceTransforms();
		const auto& Solved = Visual->BoneSpaceTransforms;
		if (!Test->TestEqual(TEXT("same complete skeletal pose"), Authored.Num(), Solved.Num())) return false;
		const int32 Pelvis = Visual->GetBoneIndex(TEXT("RigPelvis"));
		double MaxLengthError = 0.0, MaxScaleError = 0.0;
		bool bFinite = true;
		for (int32 Bone = 0; Bone < Authored.Num(); ++Bone)
		{
			bFinite &= !Solved[Bone].ContainsNaN();
			if (Bone != Pelvis)
				MaxLengthError = FMath::Max(MaxLengthError, FVector::Distance(Authored[Bone].GetTranslation(), Solved[Bone].GetTranslation()));
			MaxScaleError = FMath::Max(MaxScaleError, FVector::Distance(Authored[Bone].GetScale3D(), Solved[Bone].GetScale3D()));
		}
		Test->TestTrue(TEXT("IK retains every bone translation except bounded pelvis adjustment"), MaxLengthError < 0.001);
		Test->TestTrue(TEXT("IK never stretches bone scales"), MaxScaleError < 0.001);
		Test->TestTrue(TEXT("every solved transform remains finite"), bFinite);
		Test->TestEqual(TEXT("visible pose creates no physical collision"), Visual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		return !Test->HasAnyErrors();
	}

	bool MeasureWalking(FAutomationTestBase* Test, FScene& Scene, AActor* Actor, double Speed)
	{
		auto* Body = Actor->FindComponentByClass<UCatPhysicalBodyComponent>();
		auto* Consumer = Actor->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
		if (!Body || !Consumer || !Consumer->GetVisualMesh() || !Consumer->GetAnimationSource()) return false;
		Body->SetMovementSpeed(Speed);
		Scene.Step(90, Body, FVector::ForwardVector);
		FVector PreviousSolved[4], PreviousAuthored[4];
		uint8 PreviousPlanted = 0;
		double AuthoredSlide = 0.0, SolvedSlide = 0.0, MinStride = 100.0, MaxStride = 0.0;
		int32 Samples = 0, GroundedFrames = 0, MovingFrames = 0;
		const FVector Start = Actor->GetActorLocation();
		for (int32 Frame = 0; Frame < 360; ++Frame)
		{
			Scene.Step(1, Body, FVector::ForwardVector);
			const auto& Observation = Consumer->GetLocomotionObservation();
			GroundedFrames += Observation.GroundMask == 15;
			MovingFrames += Observation.Mode == TEXT("Walking") && Observation.AnimationSpeedCmS > 1.0;
			MinStride = FMath::Min(MinStride, Observation.StrideScale);
			MaxStride = FMath::Max(MaxStride, Observation.StrideScale);
			for (int32 Foot = 0; Foot < 4; ++Foot)
			{
				const FVector Solved = Consumer->GetVisualMesh()->GetBoneLocationByName(Ankles[Foot], EBoneSpaces::WorldSpace);
				const FVector Authored = Consumer->GetAnimationSource()->GetBoneLocation(Ankles[Foot]);
				if ((Observation.PlantMask & PreviousPlanted & (1 << Foot)) && Observation.Alpha > 0.99)
				{
					AuthoredSlide += FVector::Dist2D(Authored, PreviousAuthored[Foot]);
					SolvedSlide += FVector::Dist2D(Solved, PreviousSolved[Foot]);
					++Samples;
				}
				PreviousSolved[Foot] = Solved;
				PreviousAuthored[Foot] = Authored;
			}
			PreviousPlanted = Observation.PlantMask;
		}
		Test->TestTrue(TEXT("real Chaos locomotion traverses the scene"), FVector::Dist2D(Start, Actor->GetActorLocation()) > Speed * 2.0);
		Test->TestTrue(TEXT("active animation contributes calibrated motion for most walking frames"), MovingFrames > 280);
		Test->TestTrue(TEXT("terrain probe observes all four feet on broad flat floor"), GroundedFrames > 280);
		Test->TestTrue(TEXT("measurement includes enough consecutive planted-foot samples"), Samples > 80);
		Test->TestTrue(TEXT("planted world-space paw sliding improves by at least 30 percent over this frame's original animation"),
			AuthoredSlide > 1.0 && SolvedSlide < AuthoredSlide * 0.7);
		Test->TestTrue(TEXT("stride stays within configured anatomical bounds"), MinStride >= 0.599 && MaxStride <= 1.601);
		CheckSkeletonContract(Test, Consumer->GetAnimationSource(), Consumer->GetVisualMesh());
		Test->AddInfo(FString::Printf(TEXT("Event=locomotion_sliding_measured Class=%s RequestedSpeedCmS=%.2f ActualSpeedCmS=%.2f AnimationSpeedCmS=%.2f StrideRange=%.3f,%.3f PlantSamples=%d AuthoredSlideCm=%.3f SolvedSlideCm=%.3f GroundedFrames=%d MovingFrames=%d"),
			*Actor->GetClass()->GetPathName(), Speed, Body->GetVelocity().Size2D(), Consumer->GetLocomotionObservation().AnimationSpeedCmS,
			MinStride, MaxStride, Samples, AuthoredSlide, SolvedSlide, GroundedFrames, MovingFrames));
		return !Test->HasAnyErrors();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionStrideTest,
	"Catfishing.Locomotion.Runtime.RealGaitReducesSlidingAtThreeSpeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLocomotionStrideTest::RunTest(const FString& Parameters)
{
	for (const double Speed : {25.0, 60.0, 100.0})
	{
		CatLocomotionTest::FScene Scene;
		if (!Scene.Initialize(this)) return false;
		auto* Pawn = Scene.Spawn();
		if (!Pawn) return false;
		Scene.Step(60);
		CatLocomotionTest::MeasureWalking(this, Scene, Pawn, Speed);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionFormalConsumerTest,
	"Catfishing.Locomotion.Runtime.FormalBlueprintKeepsAnimationAndPhysicsAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLocomotionFormalConsumerTest::RunTest(const FString& Parameters)
{
	CatLocomotionTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	UClass* Class = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	if (!TestNotNull(TEXT("production BP asset exists"), Class)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Cat = Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(Class,
		FVector(0,0,Class->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight()), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("production BP starts with physical integration"), Cat)) return false;
	Scene.Step(90);
	auto* Consumer = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	auto* Body = Cat->FindComponentByClass<UCatPhysicalBodyComponent>();
	if (!Consumer || !Body || !Cat->GetMesh()->GetAnimInstance()) return false;
	UAnimInstance* Original = Cat->GetMesh()->GetAnimInstance();
	TestEqual(TEXT("original formal ABP remains the actual animation source"), Consumer->GetAnimationSource(), Cat->GetMesh());
	TestTrue(TEXT("formal ABP is active"), Original->GetClass()->GetPathName().Contains(TEXT("ABP_Cat")));
	for (const double Speed : {100.0, 200.0, 300.0}) CatLocomotionTest::MeasureWalking(this, Scene, Cat, Speed);
	TestEqual(TEXT("stride correction keeps the existing animation instance"), Cat->GetMesh()->GetAnimInstance(), Original);
	TestTrue(TEXT("body retains authority CMC movement"), Body->UsesCharacterMovement() && !Body->GetBody()->IsSimulatingPhysics());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionTerrainTest,
	"Catfishing.Locomotion.Runtime.TerrainPosePreservesPhysicsAndBoneLengths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLocomotionTerrainTest::RunTest(const FString& Parameters)
{
	CatLocomotionTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	// Configure the query-only volume before advancing Chaos; response changes are deferred by the physics scene.
	auto* InteractionOnly = Scene.AddBox(FVector(0,0,2.0),FVector(40,40,2));
	if (!InteractionOnly) return false;
	InteractionOnly->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionOnly->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionOnly->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	auto* Pawn = Scene.Spawn();
	if (!Pawn) return false;
	Scene.Step(120);
	auto* Body = Pawn->FindComponentByClass<UCatPhysicalBodyComponent>();
	auto* Consumer = Pawn->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	auto* Source = Consumer->GetAnimationSource();
	auto* Visual = Consumer->GetVisualMesh();
	FCatQuadrupedLocomotion Solver;
	FCatQuadrupedLocomotionSettings Settings;
	const FTransform BodyBefore = Pawn->GetActorTransform();
	const FVector VelocityBefore = Body->GetVelocity();
	const TArray<FTransform> SourceBefore = Source->GetBoneSpaceTransforms();
	const auto ApplyPose = [&](float LeftReach = 0.0f)
	{
		Visual->CopyPoseFromSkeletalComponent(Source);
		Solver.Apply(Source, Visual, Body, LeftReach, 0.0f, 1.0f / 60.0f, Settings);
		Visual->RefreshBoneTransforms();
	};
	for (int32 Frame = 0; Frame < 90; ++Frame) ApplyPose();
	FVector Flat[4];
	for (int32 Foot = 0; Foot < 4; ++Foot) Flat[Foot] = Visual->GetBoneLocationByName(CatLocomotionTest::Ankles[Foot], EBoneSpaces::WorldSpace);
	FHitResult InteractionHit;
	FCollisionQueryParams InteractionQuery(SCENE_QUERY_STAT(LocomotionInteractionTest),false,Pawn);
	Scene.World.GetTestWorld()->LineTraceSingleByChannel(InteractionHit,FVector(0,0,10),FVector(0,0,-10),ECC_Visibility,InteractionQuery);
	TestEqual(TEXT("the volume remains hittable by the real interaction trace"),InteractionHit.GetActor(),static_cast<AActor*>(InteractionOnly));
	TestTrue(TEXT("visibility-only interaction volume cannot become a foot support"), Flat[0].Z > 0.5 && Flat[0].Z < 2.5);
	const FTransform PropPose(FVector(0, 0, 4));
	auto* LightProp = Scene.World.GetTestWorld()->SpawnActorDeferred<ACatPhysicsGrabProp>(ACatPhysicsGrabProp::StaticClass(), PropPose);
	if (!LightProp || !LightProp->ConfigureFromAuthority(FVector(80, 80, 4), true, .35f, FLinearColor::Green)) return false;
	LightProp->FinishSpawning(PropPose);
	LightProp->GetPhysicsMesh()->SetPhysicsLinearVelocity(FVector(100, 0, 0));
	for (int32 Frame = 0; Frame < 90; ++Frame) ApplyPose();
	TestTrue(TEXT("lightweight solid prop does not lift a visible foot"),
		FMath::Abs(Visual->GetBoneLocationByName(CatLocomotionTest::Ankles[0], EBoneSpaces::WorldSpace).Z - Flat[0].Z) < .1);
	TestEqual(TEXT("lightweight prop velocity is not inherited as platform movement"), Solver.GetObservation().SupportSpeedCmS, 0.0, .01);
	LightProp->Destroy();
	auto* Step = Scene.AddBox(FVector(Flat[0].X,Flat[0].Y,1.5), FVector(2.0,2.0,1.5));
	if (!Step) return false;
	for (int32 Frame = 0; Frame < 90; ++Frame) ApplyPose();
	const FVector Raised = Visual->GetBoneLocationByName(CatLocomotionTest::Ankles[0], EBoneSpaces::WorldSpace);
	TestTrue(TEXT("front paw adapts to a three-centimeter step"), Raised.Z - Flat[0].Z > 2.5 && Raised.Z - Flat[0].Z < 3.5);
	TestTrue(TEXT("opposite rear paw retains its floor height"), FMath::Abs(Visual->GetBoneLocationByName(CatLocomotionTest::Ankles[3], EBoneSpaces::WorldSpace).Z - Flat[3].Z) < 0.5);
	TestTrue(TEXT("pelvis movement remains bounded in mesh centimeters"), FMath::Abs(Solver.GetObservation().PelvisOffsetCm) <= Settings.MaxPelvisOffsetCm + 0.01);
	Step->AddActorWorldOffset(FVector(0,0,1.0));
	for (int32 Frame = 0; Frame < 30; ++Frame) ApplyPose();
	const FVector Moved = Visual->GetBoneLocationByName(CatLocomotionTest::Ankles[0], EBoneSpaces::WorldSpace);
	TestTrue(TEXT("planted paw follows its moving support in support-local space"), Moved.Z - Raised.Z > 0.7 && Moved.Z - Raised.Z < 1.3);
	Step->SetActorRotation(FRotator(12,0,0));
	for (int32 Frame = 0; Frame < 60; ++Frame) ApplyPose();
	const FReferenceSkeleton& Skeleton = CastChecked<USkeletalMesh>(Visual->GetSkinnedAsset())->GetRefSkeleton();
	TArray<FTransform> Reference = Skeleton.GetRefBonePose();
	for (int32 Bone = 1; Bone < Reference.Num(); ++Bone) Reference[Bone] *= Reference[Skeleton.GetParentIndex(Bone)];
	const int32 Ankle = Visual->GetBoneIndex(CatLocomotionTest::Ankles[0]);
	const FVector SoleLocal = Reference[Ankle].GetRotation().Inverse().RotateVector(FVector::UpVector);
	const FVector SoleWorld = Visual->GetBoneTransformByName(CatLocomotionTest::Ankles[0],EBoneSpaces::WorldSpace).GetRotation().RotateVector(SoleLocal);
	TestTrue(TEXT("planted paw sole follows the actual twelve-degree support normal"), FVector::DotProduct(SoleWorld,Step->GetActorUpVector()) > 0.995);
	CatLocomotionTest::CheckSkeletonContract(this, Source, Visual);
	for (int32 Frame = 0; Frame < 60; ++Frame) ApplyPose(1.0f);
	TestTrue(TEXT("reaching front paw is excluded from ground planting"), (Solver.GetObservation().ExcludedFootMask & 1) && !(Solver.GetObservation().PlantMask & 1));
	TestTrue(TEXT("IK pose calculation does not change the Chaos body transform"), BodyBefore.Equals(Pawn->GetActorTransform(), 0.00001));
	TestTrue(TEXT("IK pose calculation does not change physical velocity"), VelocityBefore.Equals(Body->GetVelocity(), 0.00001));
	bool bSourceUnchanged = SourceBefore.Num() == Source->GetBoneSpaceTransforms().Num();
	for (int32 Bone = 0; Bone < SourceBefore.Num(); ++Bone) bSourceUnchanged &= SourceBefore[Bone].Equals(Source->GetBoneSpaceTransforms()[Bone], 0.00001);
	TestTrue(TEXT("terrain IK leaves original ABP output untouched"), bSourceUnchanged);
	AddInfo(FString::Printf(TEXT("Event=locomotion_terrain_measured StepRiseCm=%.3f MovingSupportRiseCm=%.3f PelvisCm=%.3f"), Raised.Z - Flat[0].Z, Moved.Z - Raised.Z, Solver.GetObservation().PelvisOffsetCm));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionLifecycleTest,
	"Catfishing.Locomotion.Runtime.GrabJumpResetAndDisableReleaseFootControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLocomotionLifecycleTest::RunTest(const FString& Parameters)
{
	CatLocomotionTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Pawn = Scene.Spawn();
	if (!Pawn) return false;
	Scene.Step(120);
	auto* Body = Pawn->FindComponentByClass<UCatPhysicalBodyComponent>();
	auto* Consumer = Pawn->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	TestTrue(TEXT("standing feet acquire the actual floor"), Consumer->GetLocomotionObservation().PlantMask == 15);
	Pawn->SetGrabInput(true, true);
	Scene.Step(30);
	TestTrue(TEXT("ground solver yields only the reaching front paw"), Consumer->GetLocomotionObservation().ExcludedFootMask == 1
		&& !(Consumer->GetLocomotionObservation().PlantMask & 1) && (Consumer->GetLocomotionObservation().GroundMask & 12) == 12);
	Pawn->SetGrabInput(true, false);
	Scene.Step(30);
	TestTrue(TEXT("released hand resumes normal foot placement"), Consumer->GetLocomotionObservation().ExcludedFootMask == 0);
	Pawn->RequestJump();
	bool bSawAirborne = false, bSawReleased = false;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Scene.Step(1);
		const auto& Observation = Consumer->GetLocomotionObservation();
		bSawAirborne |= Observation.Mode == TEXT("Airborne");
		bSawReleased |= Observation.Mode == TEXT("Airborne") && Observation.PlantMask == 0 && Observation.Alpha < 0.1;
	}
	TestTrue(TEXT("actual physical jump is observed"), bSawAirborne);
	TestTrue(TEXT("airborne pose fades to the authored jump without old plant anchors"), bSawReleased);
	Pawn->RequestReset();
	Scene.Step(120);
	TestTrue(TEXT("reset reacquires floor with fresh anchors"), Consumer->GetLocomotionObservation().PlantMask == 15);
	Consumer->LocomotionSettings.bEnabled = false;
	Scene.Step(60);
	TestTrue(TEXT("disable setting removes all correction and plant state"), Consumer->GetLocomotionObservation().Mode == TEXT("Disabled")
		&& Consumer->GetLocomotionObservation().PlantMask == 0 && Consumer->GetLocomotionObservation().Alpha < 0.001);
	Body->SetLocomotionEnabledFromAuthority(false, TEXT("LocomotionTest"));
	Consumer->LocomotionSettings.bEnabled = true;
	Scene.Step(30);
	TestTrue(TEXT("downed/unavailable body cannot retain walking IK"), Consumer->GetLocomotionObservation().Mode == TEXT("BodyUnavailable")
		&& Consumer->GetLocomotionObservation().PlantMask == 0);
	return !HasAnyErrors();
}

#endif
