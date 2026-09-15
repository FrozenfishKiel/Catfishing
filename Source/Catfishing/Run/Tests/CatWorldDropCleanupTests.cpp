#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Items/CatItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/CatFishInventoryItemInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldDropDayEntryTest,
	"Catfishing.Unit.Run.FormalDayEntryPurgesOnlyUnclaimedWorldDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatWorldDropDayEntryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	// 在正式BeginPlay/StateTree第一天之前放入关卡物，验证初次开局不会执行昨日清理。
	auto* Initial = World->SpawnActor<ACatItem>();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!TestTrue(TEXT("StateTree经正式入口进入第一天"), Mode && Mode->GetRunPublicState().Phase.DayIndex == 1
		&& Mode->GetRunPublicState().Phase.Phase == ECatRunPhase::DayActive)) return false;
	TestFalse(TEXT("初次开局不清关卡初始物"), Initial->IsActorBeingDestroyed());
	auto* Retained = World->SpawnActor<ACatItem>();
	Retained->SetActorHiddenInGame(true);
	auto* GroundFish = World->SpawnActor<ACatFishPickupActor>();
	auto* HiddenFish = World->SpawnActor<ACatFishPickupActor>();
	HiddenFish->SetActorHiddenInGame(true);
	auto* Carried = World->SpawnActor<ACatFishPickupActor>();
	auto* Cat = World->SpawnActor<ACatCharacter>();
	auto* State = World->SpawnActor<APlayerState>();
	Cat->SetPlayerState(State);
	UClass* CatClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	auto* Definition = LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
	if (!CatClass || !Definition) return false;
	Cat->GetMesh()->SetSkeletalMeshAsset(CatClass->GetDefaultObject<ACatCharacter>()->GetMesh()->GetSkeletalMeshAsset());
	auto* Fish = NewObject<UCatFishInventoryItemInstance>(Carried);
	Fish->SetItemDefinition(Definition);
	if (!Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 2.5)
		|| !Carried->InitializeFromInventoryFromAuthority(Fish, 1)
		|| !Carried->BeginMouthCarryFromAuthority(Cat, State)) return false;
	TestTrue(TEXT("普通入夜"), Mode->EnterRunPhaseFromStateTree(ECatRunPhase::NormalNight, ECatRunTransitionReason::None).bApplied);
	TestFalse(TEXT("普通入夜仍可捡"), Initial->IsActorBeingDestroyed());
	TestTrue(TEXT("正式翻天入口"), Mode->EnterRunPhaseFromStateTree(ECatRunPhase::DayActive, ECatRunTransitionReason::None).bApplied);
	TestTrue(TEXT("翻天删除未拾ACatItem"), Initial->IsActorBeingDestroyed());
	TestTrue(TEXT("翻天删除Available地面鱼"), GroundFish->IsActorBeingDestroyed());
	TestFalse(TEXT("背包隐藏载体保留"), Retained->IsActorBeingDestroyed());
	TestFalse(TEXT("库存隐藏鱼载体保留"), HiddenFish->IsActorBeingDestroyed());
	TestTrue(TEXT("嘴叼鱼与角色引用保留"), !Carried->IsActorBeingDestroyed() && Cat->GetMouthCarriedActor() == Carried);
	return !HasAnyErrors();
}
#endif
