#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCharacterLifecycleConstructionTest,
	"Catfishing.Unit.Character.Lifecycle.DefaultBodyComponentsArePresentAndFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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
