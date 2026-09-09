#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Character/CatCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UObject/UnrealType.h"

namespace
{
	// 只在测试中注入一份复制输入，后续走生产 Bridge -> Model -> Widget 消费链。
	FCatFishingSessionSnapshot* GetTestSnapshot(ACatFishingSession* Session)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(ACatFishingSession::StaticClass(), TEXT("Snapshot"));
		return Property && Session ? Property->ContainerPtrToValuePtr<FCatFishingSessionSnapshot>(Session) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDCooperativeStaminaTest,
	"Catfishing.Unit.UI.HUD.CooperativeStaminaKeepsPersonalBalanceAndRendersTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDCooperativeStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 HUD 消费链测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	ACatFishingSession* Session = WorldWrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	FCatFishingSessionSnapshot* Snapshot = GetTestSnapshot(Session);
	if (!TestNotNull(TEXT("测试复制快照可写"), Snapshot)) return false;
	Snapshot->FishingSessionId = FGuid::NewGuid();
	Snapshot->Phase = ECatFishingPhase::HookedFight;
	Snapshot->FightParticipantCount = 4;
	Snapshot->CombinedFightStamina = 250.0;
	Snapshot->CombinedFightStaminaMaximum = 400.0;
	Snapshot->CombinedFishingStrength = 400.0;
	Snapshot->ActiveCombinedFishingStrength = 250.0;

	UCatHUDModel* Model = NewObject<UCatHUDModel>();
	ACatCharacter* Cat = WorldWrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
	UAbilitySystemComponent* AbilitySystem = Cat ? Cat->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("使用真实角色拥有的 ASC"), AbilitySystem)) return false;
	AbilitySystem->InitAbilityActorInfo(Cat, Cat);
	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 35.0f);
	Model->BoundAbilitySystem = AbilitySystem;
	Model->FishingViewBridge = NewObject<UCatFishingViewBridge>(Model);
	Model->FishingViewBridge->BindSession(Session);
	Model->Refresh();
	const FCatHUDViewState& State = Model->GetViewState();
	TestEqual(TEXT("个人体力不被总体力覆盖"), State.FightStamina, 35.0f);
	TestEqual(TEXT("四人总体力来自会话快照"), State.TotalFightStamina, 250.0);
	TestEqual(TEXT("团队上限来自个人上限合计"), State.TotalFightStaminaMaximum, 400.0);
	TestEqual(TEXT("总体力比例使用团队上限"), State.NormalizedTotalFightStamina, 0.625f);
	TestEqual(TEXT("力量只读投影不自行按人数重算"), State.Fishing.ActiveCombinedFishingStrength, 250.0);
	TestTrue(TEXT("体力文案包含人数"), State.CatStaminaText.ToString().Contains(TEXT("4 人")));

	UCatHUDWidget* Widget = NewObject<UCatHUDWidget>();
	Widget->CatStaminaProgressBar = NewObject<UProgressBar>(Widget);
	Widget->CatStaminaTextBlock = NewObject<UTextBlock>(Widget);
	Widget->RenderHUD(State);
	TestEqual(TEXT("真实 ProgressBar 消费总体力而不是本人余额"), Widget->CatStaminaProgressBar->GetPercent(), 0.625f);
	TestEqual(TEXT("真实 TextBlock 消费总体力文本"), Widget->CatStaminaTextBlock->GetText().ToString(), State.CatStaminaText.ToString());
	TestEqual(TEXT("搏斗中体力条可见"), Widget->CatStaminaProgressBar->GetVisibility(), ESlateVisibility::HitTestInvisible);

	Snapshot->FightParticipantCount = 1;
	Snapshot->CombinedFightStamina = 35.0;
	Snapshot->CombinedFightStaminaMaximum = 100.0;
	Session->OnSnapshotChanged.Broadcast();
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestEqual(TEXT("离队后的单人仍走同一条计算投影"), Widget->CatStaminaProgressBar->GetPercent(), 0.35f);
	TestEqual(TEXT("加入退出不改本人 ASC 余额"), Model->GetViewState().FightStamina, 35.0f);
	Model->FishingViewBridge->UnbindSession();
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestEqual(TEXT("离竿后清掉旧会话总体力"), Model->GetViewState().TotalFightStamina, 0.0);
	TestEqual(TEXT("离竿后隐藏钓鱼体力条"), Widget->CatStaminaProgressBar->GetVisibility(), ESlateVisibility::Collapsed);
	Model->Unbind();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDFishingMembershipBindingTest,
	"Catfishing.Unit.UI.HUD.HelpersBindSameSessionAndReconcileDeparture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDFishingMembershipBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建成员 UI 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	APlayerState* Primary = World->SpawnActor<APlayerState>();
	APlayerState* Helper = World->SpawnActor<APlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	FCatFishingSessionSnapshot* Snapshot = GetTestSnapshot(Session);
	if (!Rod || !Snapshot || !Primary || !Helper || !Controller) return false;
	Controller->PlayerState = Helper;
	TestTrue(TEXT("初始化正式鱼竿身份"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("TestRod"), NAME_None, Primary, Primary, true, false));
	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("辅助加入同一根竿"), Rod->AddOperatorFromAuthority(Helper,
		Rod->GetPresentationState().RodActorRevision, JoinedSlot));
	Snapshot->FishingSessionId = FGuid::NewGuid();
	Snapshot->RodActor = Rod;
	Snapshot->FisherPlayerState = Primary;
	Snapshot->Phase = ECatFishingPhase::HookedFight;
	TestEqual(TEXT("主位能查询会话"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Primary), Session);
	TestEqual(TEXT("辅助查询相同会话"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Helper), Session);

	UCatHUDModel* Model = NewObject<UCatHUDModel>();
	Model->BoundPlayerController = Controller;
	Model->FishingViewBridge = NewObject<UCatFishingViewBridge>(Model);
	Model->RefreshFishingSessionBinding();
	Model->Refresh();
	Model->ScheduleFishingSessionBindingReconcile();
	TestEqual(TEXT("辅助 Model 已绑定同一会话"), Model->FishingViewBridge->GetBoundSession(), Session);
	TestTrue(TEXT("成员复制对账定时器已启动"), World->GetTimerManager().IsTimerActive(Model->FishingSessionBindingReconcileTimerHandle));
	APlayerState* Promoted = nullptr;
	TestTrue(TEXT("主位离开并接力"), Rod->RemoveOperatorFromAuthority(Primary,
		Rod->GetPresentationState().RodActorRevision, Promoted));
	Model->RefreshFishingSessionBinding();
	TestEqual(TEXT("控制权补位不换 UI 会话"), Model->FishingViewBridge->GetBoundSession(), Session);
	TestNull(TEXT("离队主位不再查询旧会话"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Primary));
	TestTrue(TEXT("接力者离队"), Rod->RemoveOperatorFromAuthority(Helper,
		Rod->GetPresentationState().RodActorRevision, Promoted));
	Model->RefreshFishingSessionBinding();
	TestNull(TEXT("无命令回执也能对账解除会话"), Model->FishingViewBridge->GetBoundSession());
	TestFalse(TEXT("解绑立即清掉 HUD 会话标记"), Model->GetViewState().bHasFishingSession);
	const FTimerHandle TimerHandle = Model->FishingSessionBindingReconcileTimerHandle;
	Model->Unbind();
	TestFalse(TEXT("HUD 退出消费成员对账定时器"), World->GetTimerManager().IsTimerActive(TimerHandle));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDFormalCooperativeMeterTest,
	"Catfishing.Unit.UI.HUD.FormalWidgetRendersCooperativeStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDFormalCooperativeMeterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建正式 HUD 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UClass* FormalClass = LoadClass<UCatHUDWidget>(nullptr, TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C"));
	if (!TestNotNull(TEXT("正式 WBP 可加载"), FormalClass)) return false;
	UCatHUDWidget* Widget = CreateWidget<UCatHUDWidget>(WorldWrapper.GetTestWorld(), FormalClass);
	if (!TestNotNull(TEXT("创建真实正式 WBP 实例"), Widget)) return false;
	UProgressBar* Bar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("CatStaminaProgressBar")));
	UTextBlock* Text = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CatStaminaTextBlock")));
	if (!TestNotNull(TEXT("正式实例已包含体力条"), Bar) || !TestNotNull(TEXT("正式实例已包含体力文字"), Text)) return false;
	FCatHUDViewState State;
	State.bHasFishingSession = true;
	State.bShowFightMeters = true;
	State.Fishing.FishingSessionId = FGuid::NewGuid();
	State.Fishing.FightParticipantCount = 4;
	State.TotalFightStamina = 250.0;
	State.TotalFightStaminaMaximum = 400.0;
	State.NormalizedTotalFightStamina = 0.625f;
	State.NormalizedFightStamina = 0.1f;
	State.CatStaminaText = FText::FromString(TEXT("总体力 250 / 400（4 人）"));
	Widget->RenderHUD(State);
	TestEqual(TEXT("正式 WBP 的 Native BindWidget 消费总体力比例"), Bar->GetPercent(), 0.625f);
	TestEqual(TEXT("正式 WBP 的体力文字已应用"), Text->GetText().ToString(), State.CatStaminaText.ToString());
	TestEqual(TEXT("正式体力控件解除初始隐藏"), Bar->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestNotNull(TEXT("保留正式背包按钮"), Widget->GetWidgetFromName(TEXT("InventoryButton")));
	TestNotNull(TEXT("保留正式设置按钮"), Widget->GetWidgetFromName(TEXT("MainMenuButton")));
	Widget->RenderHUD(FCatHUDViewState());
	TestEqual(TEXT("退出钓鱼后正式体力条隐藏"), Bar->GetVisibility(), ESlateVisibility::Collapsed);
	return !HasAnyErrors();
}

#endif
