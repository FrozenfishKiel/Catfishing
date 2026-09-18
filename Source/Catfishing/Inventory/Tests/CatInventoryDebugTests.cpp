#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Items/CatIconItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "OnlineSubsystemTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryDebugGiveTest, "Catfishing.Contract.Inventory.DebugGive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 真实总表和实例回归：先检验名称与正式命令注册，再向独立角色给予普通物、装备、假鱼和真鱼；非法输入不能增加库存。
// 再经控制台检查严格参数解析及省略数量的默认发货，最后填满背包核对溢出实物数量；世界包装器负责清理，不修改正式资产。
bool FCatInventoryDebugGiveTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* World = Scene.GetTestWorld(); World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Scene.BeginPlayInTestWorld()) return false;
	auto* Pawn = World->SpawnActor<ACatCharacter>();
	auto* PC = World->SpawnActor<ACatfishingPlayerController>();
	auto* PS = World->SpawnActor<ACatfishingPlayerState>();
	const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("DebugGiveTest"), FName(TEXT("CAT_TEST")));
	PS->SetUniqueId(FUniqueNetIdRepl(NetId));
	PC->PlayerState = PS; Pawn->SetPlayerState(PS); PC->Possess(Pawn);
	TStrongObjectPtr<ULocalPlayer> Local(NewObject<ULocalPlayer>(GEngine)); PC->SetPlayer(Local.Get());
	auto* Inventory = Pawn->GetInventoryComponent();
	auto* Settings = GetDefault<UCatInventorySettings>();
	TArray<UCatInventoryItemDefinition*> Definitions; FString Error;
	if (!TestTrue(TEXT("正式目录有效"), Settings->GetItemDefinitions(Definitions, Error))) return false;
	TestEqual(TEXT("正式目录51项"), Definitions.Num(), 51);
	for (auto* Definition : Definitions)
		TestFalse(FString::Printf(TEXT("编号%d有策划名称"), Definition->ItemId), Definition->GetInventoryDisplayName().IsEmptyOrWhitespace());
	TestEqual(TEXT("未知定义不显示数字"), UCatInventoryItemDefinition::GetPlayerFacingName(nullptr).ToString(), FString(TEXT("未知物品")));
	auto* Unnamed = NewObject<UCatInventoryItemDefinition>();
	TestEqual(TEXT("空名不显示ID"), UCatInventoryItemDefinition::GetPlayerFacingName(Unnamed).ToString(), FString(TEXT("未命名物品")));
	TestNull(TEXT("旧给鱼指令已移除"), IConsoleManager::Get().FindConsoleObject(TEXT("cat.Fishing.Debug.GiveFish")));
	TestNotNull(TEXT("统一给予指令已注册"), IConsoleManager::Get().FindConsoleObject(TEXT("cat.Item.Give")));
	for (int32 Id : {44, 48, 37, 51})
	{
		for (auto* Item : Inventory->GetAllItems()) Inventory->RemoveItemInstance(Item);
		PC->ServerDebugGiveItem(FGuid::NewGuid(), Id, 1);
		TestEqual(FString::Printf(TEXT("给予编号%d"), Id), Inventory->CountVisibleInventoryQuantityByItemId(Id), 1);
	}
	for (auto* Item : Inventory->GetAllItems()) Inventory->RemoveItemInstance(Item);
	const auto* FishEntry = Definitions.FindByPredicate([](auto* D) { return D->IsA<UCatFishDefinition>(); });
	if (!TestNotNull(TEXT("正式目录包含真鱼"), FishEntry)) return false;
	auto* FishDefinition = CastChecked<UCatFishDefinition>(*FishEntry);
	PC->ServerDebugGiveItem(FGuid::NewGuid(), FishDefinition->ItemId, 2);
	TSet<FGuid> Ids;
	for (auto* Item : Inventory->GetAllItems())
	{
		auto* Fish = Cast<UCatFishInventoryItemInstance>(Item);
		if (!TestNotNull(TEXT("给予真鱼是完整鱼实例"), Fish)) return false;
		Ids.Add(Fish->GetItemInstanceId());
		TestEqual(TEXT("重量取定义中值"), Fish->GetFishWeightKilograms(), (FishDefinition->MinimumWeightKilograms + FishDefinition->MaximumWeightKilograms) * .5);
		TestTrue(TEXT("独立来源身份已初始化"), Fish->GetSourceFishingSessionId().IsValid());
	}
	TestEqual(TEXT("两条独立鱼"), Ids.Num(), 2);
	for (auto* Item : Inventory->GetAllItems()) Inventory->RemoveItemInstance(Item);
	for (const TCHAR* Invalid : {TEXT("cat.Item.Give"), TEXT("cat.Item.Give Fish_Blackfish"), TEXT("cat.Item.Give 44x"),
		TEXT("cat.Item.Give -1"), TEXT("cat.Item.Give 2147483648"), TEXT("cat.Item.Give 44 0"), TEXT("cat.Item.Give 44 1000"), TEXT("cat.Item.Give 44 1 extra")})
		IConsoleManager::Get().ProcessUserConsoleInput(Invalid, *GLog, World);
	Scene.TickTestWorld(.01f);
	TestTrue(TEXT("非法语法不发货"), Inventory->GetAllItems().IsEmpty());
	PC->ServerDebugGiveItem(FGuid::NewGuid(), 999999, 1);
	PC->ServerDebugGiveItem(FGuid::NewGuid(), 44, 1000);
	TestTrue(TEXT("服务器也拒绝坏编号与数量"), Inventory->GetAllItems().IsEmpty());
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("cat.Item.Give 44"), *GLog, World);
	Scene.TickTestWorld(.01f);
	TestEqual(TEXT("省略数量正式入口给一件"), Inventory->CountVisibleInventoryQuantityByItemId(44), 1);
	for (auto* Item : Inventory->GetAllItems()) Inventory->RemoveItemInstance(Item);
	for (int32 Index = 0; Index < Inventory->GetInventorySlotCount(); ++Index) Inventory->AddItemDefinition(Settings->FindRuntimeDefinition(47), 1);
	PC->ServerDebugGiveItem(FGuid::NewGuid(), 44, 5);
	int32 Overflow = 0;
	for (TActorIterator<ACatIconItem> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed()) for (const auto& Entry : It->GetPickupInventory().InstanceEntries) Overflow += Entry.Count;
	TestEqual(TEXT("满包五件在世界交付"), Overflow, 5);
	return !HasAnyErrors();
}
#endif
