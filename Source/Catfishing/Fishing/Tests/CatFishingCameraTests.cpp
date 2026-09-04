#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camera/CameraComponent.h"
#include "Character/CatCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFirstPersonCameraTest,
	"Catfishing.Unit.Fishing.Camera.FirstPersonFollowsLoadedRodAndRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFirstPersonCameraTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建相机测试世界"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	Wrapper.BeginPlayInTestWorld();
	UWorld* World = Wrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* Player = World->SpawnActor<ACatfishingPlayerState>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
	if (!Controller || !Player || !Character || !Rod || !Fishing) return false;
	Controller->PlayerState = Player;
	Character->SetPlayerState(Player);
	Controller->Possess(Character);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	Controller->SetPlayer(LocalPlayer.Get());
	Controller->SetViewTarget(Character);
	TestTrue(TEXT("本地控制器"), Controller->IsLocalController());

	UCameraComponent* OriginalCamera = NewObject<UCameraComponent>(Character);
	OriginalCamera->SetupAttachment(Character->GetRootComponent());
	OriginalCamera->RegisterComponent();
	OriginalCamera->SetRelativeLocation(FVector(-240, 0, 100));
	OriginalCamera->FieldOfView = 75.0f;
	OriginalCamera->Activate();
	Character->GetMesh()->SetOwnerNoSee(false);
	// 非零握把平移/旋转能揭露错误地用 Rod Actor 原点或本地默认锚点的实现。
	TestTrue(TEXT("配置非单位握把"), Rod->ConfigureCanonicalAnchorsFromAuthority(
		FTransform(FVector(200, 0, 0)), FTransform::Identity,
		FTransform(FRotator(5, 20, 0), FVector(13, 7, 4))));
	TestTrue(TEXT("初始化杆"), Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(),
		TEXT("CameraRod"), TEXT("Skin"), Player, Player, true, false));
	TestTrue(TEXT("登记杆"), Fishing->RegisterDeployedRod(Player, Rod));
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Rod->RefreshHeldTransformFromAuthority();
	FMinimalViewInfo View;
	Character->CalcCamera(0.016f, View);
	TestEqual(TEXT("等鱼不切镜头"), View.FOV, 75.0f);
	const float OriginalNearClipPlane = View.PerspectiveNearClipPlane;
	TestFalse(TEXT("等鱼不隐藏身体"), Character->GetMesh()->bOwnerNoSee);

	const FProperty* GripProperty = FindFProperty<FProperty>(Rod->GetClass(), TEXT("GripCanonicalLocalTransform"));
	TestTrue(TEXT("握把标定进入复制布局"), GripProperty && GripProperty->HasAnyPropertyFlags(CPF_Net));
	TArray<FLifetimeProperty> Lifetime;
	Rod->GetClass()->SetUpRuntimeReplicationData();
	Rod->GetLifetimeReplicatedProps(Lifetime);
	TestTrue(TEXT("不变握把随初始状态发送"), GripProperty && Lifetime.ContainsByPredicate(
		[GripProperty](const FLifetimeProperty& P) { return P.RepIndex == GripProperty->RepIndex && P.Condition == COND_InitialOnly; }));

	TestTrue(TEXT("进入角力"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector,
		0, 0, 1, 1, 0, true, 100, 50));
	Controller->SetControlRotation(FRotator(0, 120, 0));
	for (int32 I = 0; I < 180; ++I) Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0);
	Character->CalcCamera(0.016f, View);
	TestEqual(TEXT("转矩平衡后杆停在30度"), View.Rotation.Yaw, 30.0, 0.1);
	TestTrue(TEXT("镜头使用实际握把"), View.Location.Equals(Rod->GetGripWorldTransform().TransformPositionNoScale(
		GetDefault<UCatFishingPresentationSettings>()->FightCameraGripOffsetCentimeters), 0.01));
	TestTrue(TEXT("镜头在握把后方且杆位于右下"), View.Rotation.UnrotateVector(
		Rod->GetGripWorldTransform().GetLocation() - View.Location).Equals(
		-GetDefault<UCatFishingPresentationSettings>()->FightCameraGripOffsetCentimeters, 0.01));
	TestTrue(TEXT("只有本人看不到头部遮挡"), Character->GetMesh()->bOwnerNoSee);
	const FRotator LockedView = View.Rotation;
	Controller->SetControlRotation(FRotator(0, 150, 0));
	for (int32 I = 0; I < 60; ++I) Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0);
	Controller->UpdateRotation(0.016f);
	Character->CalcCamera(0.016f, View);
	TestTrue(TEXT("继续向外施力不会绕过杆自由转镜头"), View.Rotation.Equals(LockedView, 0.1));
	TestEqual(TEXT("猫身跟随实际杆"), Character->GetActorRotation().Yaw, View.Rotation.Yaw, 0.1);
	Character->ConsumeMovementInputVector();
	Controller->Move(FInputActionValue(FVector2D(0, 1)));
	TestTrue(TEXT("前进沿可见镜头方向"), Character->ConsumeMovementInputVector().GetSafeNormal().Equals(
		FRotator(0, View.Rotation.Yaw, 0).Vector(), 0.01));
	TestEqual(TEXT("鼠标意图仍可继续驱动角力"), Controller->GetControlRotation().Yaw, 150.0);

	Controller->SetControlRotation(FRotator::ZeroRotator);
	Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0);
	Character->CalcCamera(0.016f, View);
	TestTrue(TEXT("回转首帧镜头开始平滑跟随杆"), View.Rotation.Yaw < LockedView.Yaw && View.Rotation.Yaw > 0.0);
	const FRotator LastVisible = View.Rotation;
	Rod->ClearCarrierConstraintFromAuthority();
	Character->CalcCamera(0.016f, View);
	TestEqual(TEXT("搏斗结束恢复原相机FOV"), View.FOV, 75.0f);
	TestEqual(TEXT("搏斗不改变原近裁剪面"), View.PerspectiveNearClipPlane, OriginalNearClipPlane);
	TestTrue(TEXT("恢复原相机位置"), View.Location.Equals(OriginalCamera->GetComponentLocation(), 0.01));
	TestFalse(TEXT("恢复身体可见性"), Character->GetMesh()->bOwnerNoSee);
	TestTrue(TEXT("退出从最后看见的方向继续"), Controller->GetControlRotation().Equals(LastVisible, 0.01));

	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0, 0, 1, 1, 0, true, 100, 50);
	Character->CalcCamera(0.016f, View);
	AActor* SpectatorTarget = World->SpawnActor<AActor>();
	Controller->SetViewTarget(SpectatorTarget);
	UCatFishingCameraComponent* Camera = Character->FindComponentByClass<UCatFishingCameraComponent>();
	Camera->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("切观战目标即归还可见性"), Character->GetMesh()->bOwnerNoSee);
	Controller->SetViewTarget(Character);
	Character->CalcCamera(0.016f, View);
	APlayerState* Promotion = nullptr;
	Rod->RemoveOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision, Promotion);
	Character->CalcCamera(0.016f, View);
	TestEqual(TEXT("离杆恢复原相机"), View.FOV, 75.0f);
	TestFalse(TEXT("离杆恢复身体"), Character->GetMesh()->bOwnerNoSee);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCameraContinuousFollowTest,
	"Catfishing.Unit.Fishing.Camera.SteppedRodMotionRemainsContinuousAcrossFrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCameraContinuousFollowTest::RunTest(const FString& Parameters)
{
	for (const int32 Rate : {30, 60, 120})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		Wrapper.BeginPlayInTestWorld();
		UWorld* World = Wrapper.GetTestWorld();
		auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
		auto* Player = World->SpawnActor<ACatfishingPlayerState>();
		auto* Character = World->SpawnActor<ACatCharacter>();
		auto* Rod = World->SpawnActor<ACatFishingRodActor>();
		auto* Fishing = World->GetSubsystem<UCatFishingService>();
		if (!Controller || !Player || !Character || !Rod || !Fishing) return false;
		Controller->PlayerState = Player;
		Character->SetPlayerState(Player);
		Controller->Possess(Character);
		TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
		Controller->SetPlayer(LocalPlayer.Get());
		Controller->SetViewTarget(Character);
		if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(),
			TEXT("SteppedCameraRod"), TEXT("Skin"), Player, Player, true, false)
			|| !Fishing->RegisterDeployedRod(Player, Rod)) return false;
		Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0, 0, 1, 1, 0, true, 100, 50);
		Controller->SetControlRotation(FRotator(0, 120, 0));
		const auto EffortBefore = Rod->GetAuthoritativeRotationEffortSnapshot();
		const float DeltaTime = 1.0f / Rate;
		// 模拟慢转时每 50ms 才变化的握把快照，并跨过 Yaw 的 +/-180 度接缝。
		Rod->SetActorTransform(FTransform(FRotator(5, 170, 0), FVector(100, 0, 80)));
		FMinimalViewInfo View;
		Character->CalcCamera(DeltaTime, View);
		FQuat PreviousViewRotation = View.Rotation.Quaternion();
		FVector PreviousViewLocation = View.Location;
		double MaximumViewStep = 0.0;
		int32 HeldSnapshotFrames = 0;
		int32 ContinuousFrames = 0;
		int32 PreviousSnapshot = 0;
		for (int32 Frame = 1; Frame <= Rate * 2; ++Frame)
		{
			const int32 Snapshot = Frame * 20 / Rate;
			const FTransform RawPose(FRotator(5 + Snapshot * 0.1, 170 + Snapshot * 1.40625, 0),
				FVector(100 + Snapshot, Snapshot * 0.5, 80));
			Rod->SetActorTransform(RawPose);
			const FTransform AppliedPose = Rod->GetActorTransform();
			Character->CalcCamera(DeltaTime, View);
			const double ViewStep = FMath::RadiansToDegrees(PreviousViewRotation.AngularDistance(View.Rotation.Quaternion()));
			MaximumViewStep = FMath::Max(MaximumViewStep, ViewStep);
			if (Frame > Rate && Snapshot == PreviousSnapshot)
			{
				++HeldSnapshotFrames;
				if (ViewStep > 0.001 && FVector::Distance(View.Location, PreviousViewLocation) > 0.001) ++ContinuousFrames;
			}
			TestTrue(TEXT("镜头插值不改写实际杆姿态"), Rod->GetActorTransform().Equals(AppliedPose));
			PreviousSnapshot = Snapshot;
			PreviousViewRotation = View.Rotation.Quaternion();
			PreviousViewLocation = View.Location;
		}
		TestTrue(TEXT("确实覆盖无新姿态的渲染帧"), HeldSnapshotFrames > 0);
		TestEqual(TEXT("无新快照时位置和朝向仍连续移动"), ContinuousFrames, HeldSnapshotFrames);
		TestTrue(TEXT("不直接跳过一个完整姿态台阶或绕长弧"), MaximumViewStep < 1.40625 * 0.85);
		TestEqual(TEXT("平滑不会覆盖玩家施力意图"), Controller->GetControlRotation().Yaw, 120.0);
		TestEqual(TEXT("平滑不会增加权威转杆做功"),
			Rod->GetAuthoritativeRotationEffortSnapshot().PositiveWorkRadians, EffortBefore.PositiveWorkRadians);
		AddInfo(FString::Printf(TEXT("Rate=%d HeldSnapshotFrames=%d ContinuousFrames=%d MaximumViewStepDegrees=%.4f"),
			Rate, HeldSnapshotFrames, ContinuousFrames, MaximumViewStep));

		const FRotator BeforePause = View.Rotation;
		Rod->SetActorRotation(FRotator(0, -30, 0));
		Character->CalcCamera(0.0f, View);
		TestTrue(TEXT("暂停不推进视角"), View.Rotation.Equals(BeforePause, 0.001));
		for (int32 Frame = 0; Frame < Rate; ++Frame) Character->CalcCamera(DeltaTime, View);
		TestTrue(TEXT("静止目标最终对齐实际杆"), View.Rotation.Equals(Rod->GetGripWorldTransform().Rotator(), 0.01));
		Rod->ClearCarrierConstraintFromAuthority();
		Character->CalcCamera(DeltaTime, View);
		Rod->SetActorRotation(FRotator(0, 60, 0));
		Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0, 0, 1, 1, 0, true, 100, 50);
		Character->CalcCamera(DeltaTime, View);
		TestTrue(TEXT("重新上鱼首帧不沿用上场平滑历史"), View.Rotation.Equals(Rod->GetGripWorldTransform().Rotator(), 0.001));
	}
	return !HasAnyErrors();
}

#endif
