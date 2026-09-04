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
		Rod->GetGripWorldTransform().GetLocation() - View.Location).Equals(FVector(35, 16, -16), 0.01));
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
	TestTrue(TEXT("回转一帧内杆和镜头一起恢复"), View.Rotation.Yaw < LockedView.Yaw && View.Rotation.Yaw > 0.0);
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

#endif
