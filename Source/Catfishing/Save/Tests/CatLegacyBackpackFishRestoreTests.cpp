#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Save/CatSaveSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLegacyBackpackFishRestoreTest,
	"Catfishing.Unit.Save.OldItemSchemaCannotReachRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 协调器版本边界回归：构造带鱼条目的 v8 内存载荷，验证版本检查先拒绝并提示新建存档，同时保留编号；不执行读盘或世界恢复。
bool FCatLegacyBackpackFishRestoreTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Game)) return false;
	auto* Instance = World.GetTestWorld()->GetGameInstance();
	auto* Save = Instance ? Instance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("正式存档协调器"), Save)) return false;
	auto* Legacy = NewObject<UCatRunSaveGame>();
	Legacy->FormatVersion = 8; Legacy->SlotId = FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Legacy->DisplayName = TEXT("Old item schema");
	Legacy->bHasWorldSnapshot = true; Legacy->bHasPlayerSnapshot = true;
	auto& Fish = Legacy->PlayerSnapshot.InventorySlots.AddDefaulted_GetRef();
	Fish.ItemId = 1183317; Fish.ItemInstanceId = FGuid::NewGuid(); Fish.Quantity = 1;
	Fish.FishSessionId = FGuid::NewGuid(); Fish.FishWeightKilograms = 1.0;
	FText Failure;
	TestFalse(TEXT("旧鱼库存载荷不能进入恢复"), Save->ValidateLoadedRunSaveGame(*Legacy, Legacy->SlotId, Failure));
	TestTrue(TEXT("明确提示创建新存档且旧文件保留"), Failure.ToString().Contains(TEXT("创建新存档")) && Failure.ToString().Contains(TEXT("旧文件已保留")));
	TestEqual(TEXT("拒绝不改写旧版本"), Legacy->FormatVersion, 8);
	TestEqual(TEXT("拒绝不重新编号物品"), Legacy->PlayerSnapshot.InventorySlots[0].ItemId, 1183317);
	return !HasAnyErrors();
}
#endif
