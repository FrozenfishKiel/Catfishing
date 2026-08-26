#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishDefinition.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatFishPickupActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishPickupVisualScaleTest,
	"Catfishing.Unit.Items.FishPickup.ConsumesFrozenWeightVisualScaleWithoutScalingGameplayRoot",
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
		Definition->HungerRelief = 1.0;
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

#endif // WITH_DEV_AUTOMATION_TESTS
