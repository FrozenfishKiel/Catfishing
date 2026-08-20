#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCharacterLifecycleConstructionTest,
	"Catfishing.Unit.Character.Lifecycle.DefaultBodyComponentsArePresentAndFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCharacterPresentationAttachmentTest,
	"Catfishing.Unit.Character.Presentation.CameraAndPlaceholderMeshAttachToBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 Game World 生成项目 Character；核对相机挂在相机臂末端、相机臂与占位网格都附着胶囊、占位网格有真实
// StaticMesh 且无碰撞，以及“鼠标转相机、移动转身体”的旋转默认值。
bool FCatCharacterPresentationAttachmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Character 表现测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("Character 测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("可生成项目 Character"), Character);
	if (!Character)
	{
		return false;
	}

	UCameraComponent* Camera = Character->GetFollowCamera();
	UStaticMeshComponent* PlaceholderMesh = Character->GetPlaceholderMesh();
	TestNotNull(TEXT("Character 暴露跟随相机"), Camera);
	TestNotNull(TEXT("Character 暴露占位网格"), PlaceholderMesh);
	if (!Camera || !PlaceholderMesh)
	{
		Character->Destroy();
		return false;
	}

	USpringArmComponent* CameraBoom = Cast<USpringArmComponent>(Camera->GetAttachParent());
	TestNotNull(TEXT("相机附着在 SpringArm 上"), CameraBoom);
	if (CameraBoom)
	{
		TestEqual(TEXT("相机挂在 SpringArm 末端 socket"), Camera->GetAttachSocketName(), USpringArmComponent::SocketName);
		TestTrue(TEXT("SpringArm 附着在胶囊上"), CameraBoom->GetAttachParent() == Character->GetCapsuleComponent());
		TestTrue(TEXT("SpringArm 跟随 Controller 旋转"), CameraBoom->bUsePawnControlRotation);
	}
	TestTrue(TEXT("占位网格附着在胶囊上"), PlaceholderMesh->GetAttachParent() == Character->GetCapsuleComponent());
	TestNotNull(TEXT("占位网格有真实 StaticMesh 资产"), PlaceholderMesh->GetStaticMesh().Get());
	TestEqual(TEXT("占位网格不参与碰撞"), PlaceholderMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Controller 偏航不直接带动身体"), Character->bUseControllerRotationYaw);
	TestTrue(TEXT("身体朝移动方向旋转"), Character->GetCharacterMovement()->bOrientRotationToMovement);

	Character->Destroy();
	return !HasAnyErrors();
}

// 测试流程：在真实 Game World 生成项目 Character 和 Controller；只通过角色公开接口核对 ASC、Condition、Equipment 与个人鱼护 ID，不读取私有字段。
bool FCatCharacterLifecycleConstructionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Character 生命周期测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("Character 测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	TestNotNull(TEXT("可生成项目 Character"), Character);
	TestNotNull(TEXT("可生成测试 Controller"), Controller);
	if (!Character || !Controller)
	{
		return false;
	}

	UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
	TestNotNull(TEXT("Character 暴露唯一 ASC"), AbilitySystem);
	TestNotNull(TEXT("Character 暴露唯一 Condition 组件"), Character->GetConditionComponent());
	TestNotNull(TEXT("Character 暴露唯一 Equipment 组件"), Character->GetEquipmentComponent());
	TestFalse(TEXT("没有有效 PlayerState 身份时个人鱼护 ID 保持无效"), Character->GetPersonalFishGuardId().IsValid());

	Controller->Possess(Character);
	TestTrue(TEXT("Controller 可以占有项目 Character"), Character->GetController() == Controller);
	TestFalse(TEXT("默认 Ability runtime 关闭时占有不会注册个人鱼护"), Character->GetPersonalFishGuardId().IsValid());

	Controller->UnPossess();
	TestNull(TEXT("失去占有后 Character 不再持有 Controller"), Character->GetController());
	Character->Destroy();
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
