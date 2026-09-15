#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "ShopEconomy/CatShopKioskActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopKioskInteractionSurfaceTest,
	"Catfishing.Editor.ShopEconomy.Kiosk.FormallyUsesVisibleModelAndDoesNotRejectOpenPageByDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 商店摊位交互回归流程：
// 1. 建立 authority World，让相距很远的 Pawn 仍向同世界、启用中的摊位提交资格检查，证明订单不再复用已删除的距离门槛。
// 2. 加载正式蓝图并找出已绑定资源的可见模型；无模型时失败，避免球体代理被误当作正式迁移完成。
// 3. 从模型包围盒外朝模型中心发出项目交互射线，要求命中的正是实际模型组件。
// 4. 再沿模型外侧的空白路径射线，要求不命中摊位，证明没有隐形大范围查询体占据空白空间。
bool FCatShopKioskInteractionSurfaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建商店交互回归 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("启动商店交互回归 World"), WorldWrapper.BeginPlayInTestWorld())) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatShopKioskActor* AuthorityKiosk = World ? World->SpawnActor<ACatShopKioskActor>(FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
	APlayerController* Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
	APawn* FarPawn = World ? World->SpawnActor<APawn>(FVector(100000.0, 0.0, 0.0), FRotator::ZeroRotator) : nullptr;
	if (!TestTrue(TEXT("创建权威摊位与远处请求者"), AuthorityKiosk && Controller && FarPawn)) return false;
	Controller->Possess(FarPawn);
	TestTrue(TEXT("同世界远处玩家的已打开商店页面不会被第二套距离规则拒绝"),
		AuthorityKiosk->CanServeOrderFromAuthority(Controller));

	UClass* FormalKioskClass = LoadClass<ACatShopKioskActor>(nullptr,
		TEXT("/Game/UI/Shop/BP_CatShopKiosk.BP_CatShopKiosk_C"));
	if (!TestNotNull(TEXT("加载正式商店摊位蓝图"), FormalKioskClass)) return false;
	ACatShopKioskActor* FormalKiosk = World->SpawnActor<ACatShopKioskActor>(FormalKioskClass, FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成正式商店摊位蓝图"), FormalKiosk)) return false;

	// 只接受携带真实资源且可见的网格，球体、空 SceneComponent 和隐藏外观均不能满足正式交互面的资产合同。
	UPrimitiveComponent* ModelComponent = nullptr;
	TInlineComponentArray<UPrimitiveComponent*> Components(FormalKiosk);
	for (UPrimitiveComponent* Component : Components)
	{
		const bool bVisible = Component && Component->GetVisibleFlag();
		const bool bStaticMesh = bVisible && Cast<UStaticMeshComponent>(Component)
			&& Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
		const bool bSkeletalMesh = bVisible && Cast<USkeletalMeshComponent>(Component)
			&& Cast<USkeletalMeshComponent>(Component)->GetSkeletalMeshAsset();
		if (bStaticMesh || bSkeletalMesh)
		{
			ModelComponent = Component;
			break;
		}
	}
	if (!TestNotNull(TEXT("正式蓝图绑定实际可见模型"), ModelComponent)) return false;
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	const ECollisionChannel TraceChannel = Settings->TargetingTraceChannel.GetValue();
	TestTrue(TEXT("实际模型阻挡项目交互射线"),
		ModelComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision
		&& ModelComponent->GetCollisionResponseToChannel(TraceChannel) == ECR_Block);

	const FBoxSphereBounds Bounds = ModelComponent->Bounds;
	const FVector ModelRayStart = Bounds.Origin - FVector(Bounds.BoxExtent.X + 100.0, 0.0, 0.0);
	FHitResult ModelHit;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatShopKioskSurface), true);
	if (!TestTrue(TEXT("朝实际模型发出的交互射线命中"),
		World->LineTraceSingleByChannel(ModelHit, ModelRayStart, Bounds.Origin, TraceChannel, QueryParams))) return false;
	TestEqual(TEXT("交互射线命中的组件就是正式可见模型"), ModelHit.GetComponent(), ModelComponent);

	const FVector EmptyRayStart = Bounds.Origin + FVector(0.0, Bounds.BoxExtent.Y + 500.0, 0.0);
	const FVector EmptyRayEnd = EmptyRayStart + FVector(1000.0, 0.0, 0.0);
	FHitResult EmptyHit;
	TestFalse(TEXT("无遮挡空白射线不会命中商店的隐形交互体"),
		World->LineTraceSingleByChannel(EmptyHit, EmptyRayStart, EmptyRayEnd, TraceChannel, QueryParams));
	return !HasAnyErrors();
}

#endif
