#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Framework/Game/CatGameplayTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopNetworkContractTest,
	"Catfishing.Unit.Framework.ShopNetwork.RpcsAreReliableAndSnapshotsReplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatShopNetworkContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UClass* ControllerClass = ACatfishingPlayerController::StaticClass();
	for (const FName FunctionName : {
		FName(TEXT("ServerSubmitShopPurchase")),
		FName(TEXT("ServerClaimFreeShopEntry")),
		FName(TEXT("ServerSellFish"))})
	{
		const UFunction* Function = ControllerClass->FindFunctionByName(FunctionName);
		TestNotNull(*FString::Printf(TEXT("%s RPC 已反射"), *FunctionName.ToString()), Function);
		if (Function)
		{
			TestTrue(*FString::Printf(TEXT("%s 只在服务器执行"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetServer));
			TestTrue(*FString::Printf(TEXT("%s 使用 Reliable"), *FunctionName.ToString()),
				Function->HasAnyFunctionFlags(FUNC_NetReliable));
		}
	}

	const UClass* GameStateClass = ACatfishingGameState::StaticClass();
	const FProperty* ShopSnapshot = FindFProperty<FProperty>(GameStateClass, TEXT("ShopEconomySnapshot"));
	TestNotNull(TEXT("团队经济快照属性已反射"), ShopSnapshot);
	if (ShopSnapshot)
	{
		TestTrue(TEXT("团队经济快照参与网络复制"), ShopSnapshot->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("团队经济快照使用完整 RepNotify"), ShopSnapshot->RepNotifyFunc,
			FName(TEXT("OnRep_ShopEconomySnapshot")));
	}

	return !HasAnyErrors();
}

#endif
