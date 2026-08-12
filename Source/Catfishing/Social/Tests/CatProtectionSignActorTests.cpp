#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "GameFramework/PlayerState.h"
#include "Social/CatProtectionSignActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProtectionSignActorRadiusTest,
	"Catfishing.Unit.Social.ProtectionSignActor.ProtectsOnlyConfiguredPlayerInsideRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 Game World 中配置防骚扰牌，分别用目标玩家、其他玩家、范围内外位置查询；保护只绑定精确 PlayerState 与显式半径。
bool FCatProtectionSignActorRadiusTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建防骚扰牌测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建防骚扰牌测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatProtectionSignActor* SignActor = World->SpawnActor<ACatProtectionSignActor>();
	APlayerState* ProtectedPlayer = World->SpawnActor<APlayerState>();
	APlayerState* OtherPlayer = World->SpawnActor<APlayerState>();
	TestNotNull(TEXT("可生成防骚扰牌"), SignActor);
	TestNotNull(TEXT("可生成受保护 PlayerState"), ProtectedPlayer);
	TestNotNull(TEXT("可生成其他 PlayerState"), OtherPlayer);
	if (!SignActor || !ProtectedPlayer || !OtherPlayer)
	{
		return false;
	}

	SignActor->SetActorLocation(FVector::ZeroVector);
	TestFalse(TEXT("未配置前不保护目标"), SignActor->ProtectsMischiefAgainst(ProtectedPlayer, FVector::ZeroVector));
	TestFalse(TEXT("无效半径配置失败"), SignActor->ConfigureProtection(ProtectedPlayer, 0.0));
	TestTrue(TEXT("有效 PlayerState 与半径配置成功"), SignActor->ConfigureProtection(ProtectedPlayer, 100.0));

	TestTrue(TEXT("受保护玩家在半径内被保护"),
		SignActor->ProtectsMischiefAgainst(ProtectedPlayer, FVector(50.0, 0.0, 0.0)));
	TestFalse(TEXT("其他玩家即使在半径内也不被保护"),
		SignActor->ProtectsMischiefAgainst(OtherPlayer, FVector(50.0, 0.0, 0.0)));
	TestFalse(TEXT("受保护玩家在半径外不被保护"),
		SignActor->ProtectsMischiefAgainst(ProtectedPlayer, FVector(150.0, 0.0, 0.0)));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
