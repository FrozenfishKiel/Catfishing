#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Save/CatSaveSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalCharacterSaveRestoreConsumerTest,
	"Catfishing.PhysicalGrab.Runtime.SaveRestoreMovesAllBodiesOnceAndPreservesSavedInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalCharacterSaveRestoreConsumerTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	UWorld* World = Scene.World.GetTestWorld();
	auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
	auto* Player = World->SpawnActor<ACatfishingPlayerState>();
	auto* Cat = Scene.SpawnCat(FVector(0, 0, 20));
	UGameInstance* Instance = World->GetGameInstance();
	if (!Controller || !Player || !Cat || !Instance) return false;
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("PhysicalSaveConsumer"), TEXT("TEST"));
	Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = Player;
	Controller->Possess(Cat);
	Scene.Step(5);
	UCatSaveSubsystem* Save = Instance->GetSubsystem<UCatSaveSubsystem>();
	if (!TestNotNull(TEXT("真实GameInstance的存档子系统"), Save)) return false;
	// 只准备已读取的存档载荷，不在回归中覆盖玩家的任何磁盘槽。
	Save->PendingRestoreSaveGame = NewObject<UCatRunSaveGame>(Save);
	Save->bWorldRestoreApplied = true;
	FCatSavedPlayerRunState& Saved = Save->PendingRestoreSaveGame->Players.AddDefaulted_GetRef();
	Saved.StableNetId = UniqueId->ToString();
	Saved.CharacterTransform = FTransform(FRotator(0, 30, 0), FVector(400, 100, 20));
	FCatEquipmentLoadoutSnapshot Before;
	FText Failure;
	if (!TestTrue(TEXT("存档夹具读取实际库存"), Cat->GetEquipmentComponent()->ExportSnapshotFromAuthority(Before, Failure))) return false;
	// 此夹具未选择钓具；保留已发放的真实库存实例及其数量。
	for (const FCatRunInventorySlot& Slot : Before.InventorySlots)
	{
		FCatSavedRunInventorySlot& SavedSlot = Saved.EquipmentSnapshot.InventorySlots.AddDefaulted_GetRef();
		SavedSlot.DefinitionId = Slot.DefinitionId;
		SavedSlot.ItemInstanceId = Slot.ItemInstanceId;
		SavedSlot.Quantity = Slot.Quantity;
		SavedSlot.RodDurability = Slot.RodDurability;
		SavedSlot.bRodBroken = Slot.bRodBroken;
	}
	auto* Physical = Cat->GetPhysicalBodyComponent();
	const uint32 PreviousEpoch = Physical->GetResetEpoch();
	if (!TestTrue(TEXT("GameMode使用的恢复入口成功"), Save->RestorePlayerAfterSpawn(*Controller, *Cat))) return false;
	TestTrue(TEXT("恢复推进身体失效域"), Physical->GetResetEpoch() > PreviousEpoch);
	TestTrue(TEXT("恢复指定身体Transform"), Physical->GetBody()->GetComponentTransform().Equals(Saved.CharacterTransform, .01));
	for (const bool bLeft : {true, false})
		TestTrue(TEXT("恢复同时移动两只独立手刚体"), Physical->GetHand(bLeft)->GetComponentLocation().Equals(
			Saved.CharacterTransform.TransformPosition(UCatPhysicsGrabComponent::RestHandLocal(bLeft)), .01));
	FCatEquipmentLoadoutSnapshot After;
	if (!TestTrue(TEXT("恢复后仍能导出正式库存"), Cat->GetEquipmentComponent()->ExportSnapshotFromAuthority(After, Failure))) return false;
	TestEqual(TEXT("恢复保留库存槽数"), After.InventorySlots.Num(), Before.InventorySlots.Num());
	for (int32 Index = 0; Index < FMath::Min(After.InventorySlots.Num(), Before.InventorySlots.Num()); ++Index)
	{
		TestEqual(TEXT("物品实例不重生"), After.InventorySlots[Index].ItemInstanceId, Before.InventorySlots[Index].ItemInstanceId);
		TestEqual(TEXT("物品数量不变化"), After.InventorySlots[Index].Quantity, Before.InventorySlots[Index].Quantity);
	}
	const FTransform Later(FVector(700, 100, 20));
	if (!Physical->TeleportBodyFromAuthority(Later, TEXT("AfterRestoreMovement"))) return false;
	const uint32 RestoredEpoch = Physical->GetResetEpoch();
	TestTrue(TEXT("重复恢复请求幂等成功"), Save->RestorePlayerAfterSpawn(*Controller, *Cat));
	TestEqual(TEXT("重复恢复不重置身体"), Physical->GetResetEpoch(), RestoredEpoch);
	TestTrue(TEXT("重复恢复不覆盖玩家新位置"), Cat->GetActorTransform().Equals(Later, .01));
	Save->PendingRestoreSaveGame = nullptr;
	Save->bWorldRestoreApplied = false;
	Save->RestoredPlayerStableNetIds.Reset();
	return !HasAnyErrors();
}

#endif
