#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishDefinition.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Items/CatFishGuardActor.h"
#include "Items/CatItemsService.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatFishPickupActor.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishPickupVisualScaleTest,
	"Catfishing.Unit.Items.FishPickup.ConsumesFrozenWeightVisualScaleWithoutScalingGameplayRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishPickupMouthCarryAndGuardStoreTest,
	"Catfishing.Unit.Items.FishPickup.MouthCarryStoresOnlyInInteractedGroundGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishPickupActorTest
{
	static UCatFishDefinition* MakeReadyFish()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = TEXT("PickupScaleFish");
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RarityTierId = TEXT("Common");
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 0.5;
		Definition->MaximumWeightKilograms = 8.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = TEXT("Nibble");
		Definition->FightPersonalityId = TEXT("Steady");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = 1.0;
		return Definition;
	}
}

bool FCatFishPickupVisualScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates pickup scale world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishPickupActor* Pickup = World ? World->SpawnActor<ACatFishPickupActor>() : nullptr;
	UCatFishDefinition* Definition = CatFishPickupActorTest::MakeReadyFish();
	if (!TestNotNull(TEXT("spawns pickup"), Pickup)
		|| !TestNotNull(TEXT("creates ready fish definition"), Definition))
	{
		return false;
	}

	const FGuid SessionId = FGuid::NewGuid();
	const FGuid FishInstanceId = FGuid::NewGuid();
	TestTrue(TEXT("authority initializes pickup with frozen scale"), Pickup->InitializeFromAuthority(
		SessionId, FishInstanceId, Definition, 8.0, 1.5, TEXT("LakeA"), {}));
	TestEqual(TEXT("pickup publishes frozen weight"), Pickup->GetPresentationState().WeightKilograms, 8.0);
	TestEqual(TEXT("pickup publishes frozen scale"), Pickup->GetPresentationState().VisualScale, 1.5);

	const USkeletalMeshComponent* Mesh = Pickup->FindComponentByClass<USkeletalMeshComponent>();
	if (TestNotNull(TEXT("pickup owns fish mesh"), Mesh))
	{
		const FVector BaseScale = GetDefault<UCatWorldItemSettings>()->LandedFishMeshRelativeTransform.GetScale3D();
		TestTrue(TEXT("pickup scales only its visual mesh"),
			Mesh->GetRelativeScale3D().Equals(BaseScale * 1.5, UE_KINDA_SMALL_NUMBER));
	}
	TestTrue(TEXT("pickup gameplay root remains unit scale"),
		Pickup->GetActorScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER));

	ACatFishPickupActor* InvalidScalePickup = World->SpawnActor<ACatFishPickupActor>();
	TestFalse(TEXT("pickup rejects non-positive frozen scale"), InvalidScalePickup
		&& InvalidScalePickup->InitializeFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), Definition,
			1.0, 0.0, TEXT("LakeA"), {}));
	return !HasAnyErrors();
}

bool FCatFishPickupMouthCarryAndGuardStoreTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建嘴叼鱼测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	ACatfishingPlayerState* PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishGuardActor* FirstGuard = World ? World->SpawnActor<ACatFishGuardActor>(FVector(100.0, 0.0, 0.0),
		FRotator::ZeroRotator) : nullptr;
	ACatFishGuardActor* OtherGuard = World ? World->SpawnActor<ACatFishGuardActor>(FVector(150.0, 100.0, 0.0),
		FRotator::ZeroRotator) : nullptr;
	ACatFishPickupActor* Pickup = World ? World->SpawnActor<ACatFishPickupActor>(FVector(50.0, 0.0, 0.0),
		FRotator::ZeroRotator) : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatFishDefinition* Definition = CatFishPickupActorTest::MakeReadyFish();
	if (!TestNotNull(TEXT("生成项目 Controller"), Controller)
		|| !TestNotNull(TEXT("生成项目 PlayerState"), PlayerState)
		|| !TestNotNull(TEXT("生成嘴叼角色"), Character)
		|| !TestNotNull(TEXT("生成目标地面鱼护"), FirstGuard)
		|| !TestNotNull(TEXT("生成另一个地面鱼护"), OtherGuard)
		|| !TestNotNull(TEXT("生成死鱼 Actor"), Pickup)
		|| !TestNotNull(TEXT("取得 Items 服务"), Items)
		|| !TestNotNull(TEXT("创建正式鱼定义"), Definition))
	{
		return false;
	}

	const FString StableNetId(TEXT("MouthCarryPlayer"));
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId, FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	Character->SetActorLocation(FVector::ZeroVector);
	for (AActor* RuntimeActor : TArray<AActor*>{Character, FirstGuard, OtherGuard, Pickup})
	{
		if (RuntimeActor && !RuntimeActor->HasActorBegunPlay())
		{
			RuntimeActor->DispatchBeginPlay();
		}
	}
	const FGuid SessionId = FGuid::NewGuid();
	const FGuid FishInstanceId = FGuid::NewGuid();
	TestTrue(TEXT("初始化可拾取死鱼"), Pickup->InitializeFromAuthority(SessionId, FishInstanceId,
		Definition, 2.5, 1.0, TEXT("LakeA"), {StableNetId}));
	USkeletalMeshComponent* PickupMesh = Pickup->FindComponentByClass<USkeletalMeshComponent>();
	const UCatWorldItemSettings* WorldItemSettings = GetDefault<UCatWorldItemSettings>();
	if (TestNotNull(TEXT("嘴叼鱼拥有视觉 Mesh"), PickupMesh)
		&& TestNotNull(TEXT("可读取世界物品设置"), WorldItemSettings))
	{
		TestTrue(TEXT("拾取前使用落地 Mesh 位置"), PickupMesh->GetRelativeLocation().Equals(
			WorldItemSettings->LandedFishMeshRelativeTransform.GetTranslation(), UE_KINDA_SMALL_NUMBER));
		TestTrue(TEXT("拾取前使用落地 Mesh 旋转"), PickupMesh->GetRelativeRotation().Quaternion().Equals(
			WorldItemSettings->LandedFishMeshRelativeTransform.GetRotation(), UE_KINDA_SMALL_NUMBER));
	}

	TestTrue(TEXT("对死鱼按 E 后进入嘴叼状态"), Pickup->Interact_Implementation(Controller, FGuid::NewGuid()));
	TestEqual(TEXT("死鱼状态为 Carried"), Pickup->GetPresentationState().State, ECatFishPickupState::Carried);
	TestEqual(TEXT("嘴叼鱼仍附着在角色 Actor 下"), Pickup->GetAttachParentActor(), static_cast<AActor*>(Character));
	TestEqual(TEXT("角色只能找到这一条嘴叼鱼"), ACatFishPickupActor::FindCarriedFish(Character), Pickup);
	if (PickupMesh)
	{
		TestTrue(TEXT("嘴叼状态清除落地 Mesh 位置"), PickupMesh->GetRelativeLocation().IsNearlyZero());
		TestTrue(TEXT("嘴叼状态清除落地 Mesh 旋转"), PickupMesh->GetRelativeRotation().IsNearlyZero());
		TestTrue(TEXT("嘴叼状态保留冻结视觉缩放"), PickupMesh->GetRelativeScale3D().Equals(
			WorldItemSettings ? WorldItemSettings->LandedFishMeshRelativeTransform.GetScale3D() : FVector::OneVector,
			UE_KINDA_SMALL_NUMBER));
	}

	FCatContainerSnapshot OtherBefore;
	TestTrue(TEXT("可读取未交互鱼护"), Items->TryGetContainerSnapshot(OtherGuard->GetGuardContainerId(), OtherBefore));
	TestEqual(TEXT("未交互鱼护初始为空"), CatItems::GetContainedObjectCount(OtherBefore), 0);
	TestTrue(TEXT("对目标鱼护按 E 可处理嘴叼鱼"), FirstGuard->Interact_Implementation(Controller, FGuid::NewGuid()));

	FCatContainerSnapshot FirstAfter;
	FCatContainerSnapshot OtherAfter;
	TestTrue(TEXT("可读取目标鱼护提交后快照"), Items->TryGetContainerSnapshot(FirstGuard->GetGuardContainerId(), FirstAfter));
	TestTrue(TEXT("可读取未交互鱼护提交后快照"), Items->TryGetContainerSnapshot(OtherGuard->GetGuardContainerId(), OtherAfter));
	TestEqual(TEXT("命中的鱼护收到嘴叼鱼"), CatItems::GetContainedObjectCount(FirstAfter), 1);
	TestEqual(TEXT("另一个鱼护没有被自动选择"), CatItems::GetContainedObjectCount(OtherAfter), 0);
	TestTrue(TEXT("目标鱼护保存原鱼实例"), FirstAfter.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == FishInstanceId;
	}));
	TestNull(TEXT("提交成功后角色不再叼鱼"), ACatFishPickupActor::FindCarriedFish(Character));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
