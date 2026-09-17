#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Engine/LocalPlayer.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystem/BodyAction/Social/CatGA_BodyActionRequestManualHelp.h"
#include "AbilitySystem/BodyAction/Social/CatGA_BodyActionPlaceProtectionSign.h"
#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBodyActionLifecycleTest,
	"Catfishing.Contract.AbilitySystem.BodyActionDownedAdmissionAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 在真实 ASC 上提交 GameplayEvent，验证前摇被倒地打断而求助仍允许；不调用提交函数冒充完整激活。
bool FCatBodyActionLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* Character = Scene.GetTestWorld()->SpawnActor<ACatCharacter>();
	auto* Controller = Scene.GetTestWorld()->SpawnActor<ACatfishingPlayerController>();
	if (!Character || !Controller) return false;
	// 单机拥有者必须有 LocalPlayer；否则 GAS 会把无网络连接的测试 Controller 当远端，并在本机回调 Client RPC。
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	Controller->SetPlayer(LocalPlayer.Get());
	Controller->Possess(Character);
	auto* ASC = Character->GetCatAbilitySystemComponent();
	if (!ASC->InitializeCharacterOwnerAvatar(Character)) return false;
	ASC->RevokeConfiguredDefaultAbilitySet();

	// 本用例验证生命周期；临时去掉 Montage 并延长前摇至 10 秒，确保同步断言落在提交前；离开作用域恢复正式配置。
	auto* Settings = GetMutableDefault<UCatBodyActionPresentationSettings>();
	const auto SavedConfigs = Settings->ActionPresentationConfigs;
	ON_SCOPE_EXIT { Settings->ActionPresentationConfigs = SavedConfigs; };
	for (auto& Config : Settings->ActionPresentationConfigs) { Config.Montage.Reset(); Config.LeadInSeconds = 10.f; }
	const auto SignHandle = ASC->GiveAbility(FGameplayAbilitySpec(UCatGA_BodyActionPlaceProtectionSign::StaticClass(), 1));
	const auto HelpHandle = ASC->GiveAbility(FGameplayAbilitySpec(UCatGA_BodyActionRequestManualHelp::StaticClass(), 1));
	FGameplayEventData SignEvent;
	SignEvent.EventTag = CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign;
	auto* Sign = new FCatBodyActionRequestPlaceProtectionSignTargetData;
	Sign->RequestId = FGuid::NewGuid();
	SignEvent.TargetData.Add(Sign);
	TestEqual(TEXT("正常时放牌事件进入前摇"), ASC->HandleGameplayEvent(SignEvent.EventTag, &SignEvent), 1);
	TestTrue(TEXT("提交前能力保持活跃"), ASC->FindAbilitySpecFromHandle(SignHandle)->IsActive());
	ASC->SetStateTagsFromAuthority(TEXT("Test.Downed"), FGameplayTagContainer(CatStateTags::Downed));
	TestFalse(TEXT("任意来源倒地立即中断放牌"), ASC->FindAbilitySpecFromHandle(SignHandle)->IsActive());
	TestEqual(TEXT("倒地时禁止重新启动放牌"), ASC->HandleGameplayEvent(SignEvent.EventTag, &SignEvent), 0);
	FGameplayEventData HelpEvent;
	HelpEvent.EventTag = CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp;
	auto* Help = new FCatBodyActionRequestManualHelpTargetData;
	Help->RequestId = FGuid::NewGuid();
	Help->HelpKind = ECatHelpSignalKind::ManualDowned;
	HelpEvent.TargetData.Add(Help);
	TestEqual(TEXT("倒地仍可请求救援"), ASC->HandleGameplayEvent(HelpEvent.EventTag, &HelpEvent), 1);
	TestTrue(TEXT("求救前摇保持活跃"), ASC->FindAbilitySpecFromHandle(HelpHandle)->IsActive());
	ASC->CancelBodyActionAbilitiesFromAuthority();
	TestFalse(TEXT("主动取消可以结束求救"), ASC->FindAbilitySpecFromHandle(HelpHandle)->IsActive());
	ASC->ClearAbility(SignHandle);
	ASC->ClearAbility(HelpHandle);
	ASC->ClearStateSourcesFromAuthority();
	return true;
}

#endif
