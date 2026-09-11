#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDPersonalStaminaTest,
	"Catfishing.Unit.UI.HUD.OperatorHUDUsesOnlyPersonalASCStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDPersonalStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 HUD 消费链测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	ACatFishingSession* Session = WorldWrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	FCatFishingSessionSnapshot* Snapshot = GetTestSnapshot(Session);
	if (!TestNotNull(TEXT("测试复制快照可写"), Snapshot)) return false;
	Snapshot->FishingSessionId = FGuid::NewGuid();
	Snapshot->Phase = ECatFishingPhase::HookedFight;

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
	TestEqual(TEXT("主控体力读取本人 ASC"), State.FightStamina, 35.0f);
	TestEqual(TEXT("主控上限读取本人 ASC"), State.FightStaminaMaximum, 100.0f);
	TestEqual(TEXT("个人体力比例不合并物理协助者"), State.NormalizedFightStamina, 0.35f);
	TestEqual(TEXT("会话中仍显示个人体力"), State.CatStaminaText.ToString(), FString(TEXT("玩家体力 35 / 100")));

	UCatHUDWidget* Widget = NewObject<UCatHUDWidget>();
	Widget->CatStaminaProgressBar = NewObject<UProgressBar>(Widget);
	Widget->CatStaminaTextBlock = NewObject<UTextBlock>(Widget);
	Widget->RenderHUD(State);
	TestEqual(TEXT("真实 ProgressBar 消费本人余额"), Widget->CatStaminaProgressBar->GetPercent(), 0.35f);
	TestEqual(TEXT("真实 TextBlock 消费个人体力文本"), Widget->CatStaminaTextBlock->GetText().ToString(), State.CatStaminaText.ToString());
	TestEqual(TEXT("搏斗中体力条可见"), Widget->CatStaminaProgressBar->GetVisibility(), ESlateVisibility::HitTestInvisible);

	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 20.0f);
	Session->OnSnapshotChanged.Broadcast();
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestEqual(TEXT("本人余额变化更新同一条进度条"), Widget->CatStaminaProgressBar->GetPercent(), 0.2f);
	Model->FishingViewBridge->UnbindSession();
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestFalse(TEXT("离竿后清会话标记"), Model->GetViewState().bHasFishingSession);
	TestEqual(TEXT("没有会话仍显示个人消耗与恢复中的体力"), Widget->CatStaminaProgressBar->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("个人体力可见不会赋予鱼信息或钓鱼会话"), Model->GetViewState().bShowFightMeters || Model->GetViewState().bHasFishingSession);
	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 100.0f);
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestEqual(TEXT("恢复至上限且无会话后收起体力条"), Widget->CatStaminaProgressBar->GetVisibility(), ESlateVisibility::Collapsed);
	Model->Unbind();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDFishingOwnerBindingTest,
	"Catfishing.Unit.UI.HUD.OnlyOperatorBindsOwnedSessionAndReconcilesDeparture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDFishingOwnerBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建会话归属 UI 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!WorldWrapper.BeginPlayInTestWorld()) return false;
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
	TestTrue(TEXT("发布唯一鱼竿所有者操作位"), Rod->SetPrimaryOperatorFromAuthority(Primary,
		Rod->GetPresentationState().RodActorRevision));
	Snapshot->FishingSessionId = FGuid::NewGuid();
	Snapshot->RodActor = Rod;
	Snapshot->FisherPlayerState = Primary;
	Snapshot->Phase = ECatFishingPhase::HookedFight;
	TestEqual(TEXT("主位能查询会话"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Primary), Session);
	TestNull(TEXT("旁人不能查询主控会话"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Helper));

	UCatHUDModel* Model = NewObject<UCatHUDModel>();
	Model->BoundPlayerController = Controller;
	Model->FishingViewBridge = NewObject<UCatFishingViewBridge>(Model);
	Model->RefreshFishingSessionBinding();
	Model->Refresh();
	Model->ScheduleFishingSessionBindingReconcile();
	TestNull(TEXT("旁人 Model 不绑定别人的钓鱼界面"), Model->FishingViewBridge->GetBoundSession());
	TestFalse(TEXT("非主控没有鱼体力和会话提示"), Model->GetViewState().bHasFishingSession || Model->GetViewState().bShowFightMeters);
	TestTrue(TEXT("会话归属对账定时器已启动"), World->GetTimerManager().IsTimerActive(Model->FishingSessionBindingReconcileTimerHandle));
	Controller->PlayerState = Primary;
	Model->RefreshFishingSessionBinding();
	TestEqual(TEXT("真正主控能绑定本人会话"), Model->FishingViewBridge->GetBoundSession(), Session);
	Snapshot->FisherPlayerState = Helper;
	Model->RefreshFishingSessionBinding();
	TestNull(TEXT("即便仍占竿也不得绑定不属于自己的 Session"), Model->FishingViewBridge->GetBoundSession());
	Snapshot->FisherPlayerState = Primary;
	Model->RefreshFishingSessionBinding();
	TestEqual(TEXT("会话身份恢复后重新绑定"), Model->FishingViewBridge->GetBoundSession(), Session);
	TestTrue(TEXT("清除主控操作位"), Rod->SetPrimaryOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision));
	Model->RefreshFishingSessionBinding();
	TestNull(TEXT("主控离竿后没有命令回执也能清会话"), Model->FishingViewBridge->GetBoundSession());
	TestFalse(TEXT("解绑立即清掉 HUD 会话标记"), Model->GetViewState().bHasFishingSession);
	TestNull(TEXT("主控离竿不会自动将旁人升级为 Session 用户"), UCatFishingViewBridge::FindFishingSessionForPlayerState(Controller, Helper));
	const FTimerHandle TimerHandle = Model->FishingSessionBindingReconcileTimerHandle;
	Model->Unbind();
	TestFalse(TEXT("HUD 退出清理会话对账定时器"), World->GetTimerManager().IsTimerActive(TimerHandle));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDFormalPersonalMeterTest,
	"Catfishing.Unit.UI.HUD.FormalWidgetRendersPersonalStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDFormalPersonalMeterTest::RunTest(const FString& Parameters)
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
	State.NormalizedFightStamina = 0.35f;
	State.CatStaminaText = FText::FromString(TEXT("玩家体力 35 / 100"));
	Widget->RenderHUD(State);
	TestEqual(TEXT("正式 WBP 的 Native BindWidget 消费个人比例"), Bar->GetPercent(), 0.35f);
	TestEqual(TEXT("正式 WBP 的体力文字已应用"), Text->GetText().ToString(), State.CatStaminaText.ToString());
	TestEqual(TEXT("正式体力控件解除初始隐藏"), Bar->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestNotNull(TEXT("保留正式背包按钮"), Widget->GetWidgetFromName(TEXT("InventoryButton")));
	TestNotNull(TEXT("保留正式设置按钮"), Widget->GetWidgetFromName(TEXT("MainMenuButton")));
	State.bHasFishingSession = false;
	State.bShowFightMeters = false;
	State.bShowPersonalStamina = true;
	Widget->RenderHUD(State);
	TestEqual(TEXT("正式辅助者无会话仍显示本人耗体"), Bar->GetVisibility(), ESlateVisibility::HitTestInvisible);
	if (auto* FishBar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("FishStaminaProgressBar"))))
		TestEqual(TEXT("辅助者体力条不泄漏鱼端会话表现"), FishBar->GetVisibility(), ESlateVisibility::Collapsed);
	Widget->RenderHUD(FCatHUDViewState());
	TestEqual(TEXT("退出钓鱼后正式体力条隐藏"), Bar->GetVisibility(), ESlateVisibility::Collapsed);
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDPhysicalGrabProjectionTest,
	"Catfishing.PhysicalGrab.Contract.HUDReadsHandsWithoutCreatingHelperMembership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDPhysicalGrabProjectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World=Scene.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
	if (!Scene.BeginPlayInTestWorld()) return false;
	ACatCharacter* Cat=World->SpawnActor<ACatCharacter>();
	APlayerController* Controller=World->SpawnActor<APlayerController>();
	APlayerState* Primary=World->SpawnActor<APlayerState>();
	APlayerState* Helper=World->SpawnActor<APlayerState>();
	if (!Cat || !Controller || !Primary || !Helper) return false;
	Controller->PlayerState=Helper;
	Controller->Possess(Cat);
	UCatPhysicsGrabComponent* Grab=Cat->GetPhysicalBodyComponent()->GetGrab();
	if (!TestNotNull(TEXT("正式身体抓握组件已初始化"),Grab)) return false;
	UCatHUDModel* Model=NewObject<UCatHUDModel>();
	Model->BoundPlayerController=Controller;
	Model->FishingViewBridge=NewObject<UCatFishingViewBridge>(Model);
	Model->Refresh();
	TestTrue(TEXT("空手提示来自正式 Model"),Model->GetViewState().bShowPhysicalControls);
	TestTrue(TEXT("空手说明按住左右键抓握"),Model->GetViewState().PhysicalControlText.ToString().Contains(TEXT("按住左 / 右键")));
	Grab->SetGrabInput(false,true);
	Model->RefreshFishingSessionBinding();
	TestTrue(TEXT("没有钓鱼会话时调和仍刷新右手观察"),Model->GetViewState().bRightHandReaching);
	TestFalse(TEXT("伸手不冒充接触成功"),Model->GetViewState().bRightHandGripped);
	TestTrue(TEXT("右爪伸手文本进入 View"),Model->GetViewState().PhysicalHandStateText.ToString().Contains(TEXT("右爪：伸手中")));
	ACatFishingRodActor* Rod=World->SpawnActor<ACatFishingRodActor>();
	if (!Rod || !Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(),FGuid::NewGuid(),TEXT("HUDTestRod"),NAME_None,Primary,Primary,true,false)) return false;
	if (UCatFishingService* Service=World->GetSubsystem<UCatFishingService>())
		if (!Service->RegisterDeployedRod(Primary,Rod)) return false;
	Rod->SetPrimaryOperatorFromAuthority(Primary,Rod->GetPresentationState().RodActorRevision);
	Model->Refresh();
	TestFalse(TEXT("旁人不会获得主控身份"),Model->GetViewState().bPrimaryRodOperator);
	TestFalse(TEXT("普通抓握提示没有会话辅助身份"),Model->GetViewState().PhysicalControlText.ToString().Contains(TEXT("辅助")));
	TestTrue(TEXT("普通抓握说明任意方向移动拉动"),Model->GetViewState().PhysicalControlText.ToString().Contains(TEXT("WASD 拉动")));
	Controller->PlayerState=Primary;
	Model->Refresh();
	TestTrue(TEXT("明确主控保留原钓鱼输入"),Model->GetViewState().bPrimaryRodOperator);
	TestTrue(TEXT("主控提示原收放线输入"),Model->GetViewState().PhysicalControlText.ToString().Contains(TEXT("右键放线")));
	Grab->SetGrabInput(false,false);
	Rod->SetPrimaryOperatorFromAuthority(nullptr,Rod->GetPresentationState().RodActorRevision);
	Model->RefreshFishingSessionBinding();
	TestFalse(TEXT("释放后清右手投影"),Model->GetViewState().bRightHandReaching);
	TestFalse(TEXT("离竿后清主位投影"),Model->GetViewState().bPrimaryRodOperator);
	Model->Unbind();
	TestFalse(TEXT("解绑后不显示旧手状态"),Model->GetViewState().bShowPhysicalControls);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDFormalPhysicalGrabTest,
	"Catfishing.PhysicalGrab.Presentation.FormalHUDRendersGripInstructionsAndHandRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHUDFormalPhysicalGrabTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	UClass* FormalClass=LoadClass<UCatHUDWidget>(nullptr,TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C"));
	if (!TestNotNull(TEXT("实际正式 HUD 类可加载"),FormalClass)) return false;
	UCatHUDWidget* Widget=CreateWidget<UCatHUDWidget>(Scene.GetTestWorld(),FormalClass);
	if (!Widget) return false;
	UTextBlock* Controls=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("PhysicalControlTextBlock")));
	UTextBlock* Hands=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("PhysicalHandStateTextBlock")));
	if (!TestNotNull(TEXT("正式 WBP 包含玩法提示控件"),Controls)||!TestNotNull(TEXT("正式 WBP 包含左右爪控件"),Hands)) return false;
	FCatHUDViewState State;
	State.bShowPhysicalControls=true;
	State.bLeftHandGripped=true;
	State.PhysicalControlText=FText::FromString(TEXT("按住左 / 右键抓人或抓竿 · WASD 拉动 · 松键释放"));
	State.PhysicalHandStateText=FText::FromString(TEXT("左爪：抓住（松键释放）    右爪：收回"));
	Widget->RenderHUD(State);
	TestEqual(TEXT("实际 BindWidget 显示普通抓握控制文本"),Controls->GetText().ToString(),State.PhysicalControlText.ToString());
	TestEqual(TEXT("实际 BindWidget 显示抓住和释放文本"),Hands->GetText().ToString(),State.PhysicalHandStateText.ToString());
	TestEqual(TEXT("普通抓握时提示可见"),Controls->GetVisibility(),ESlateVisibility::HitTestInvisible);
	TestNotNull(TEXT("保留原正式背包按钮"),Widget->GetWidgetFromName(TEXT("InventoryButton")));
	TestNotNull(TEXT("保留原正式个人体力条"),Widget->GetWidgetFromName(TEXT("CatStaminaProgressBar")));
	Widget->RenderHUD(FCatHUDViewState());
	TestEqual(TEXT("退出身体后清两项正式控件"),Controls->GetVisibility(),ESlateVisibility::Collapsed);
	TestEqual(TEXT("退出身体后清手状态控件"),Hands->GetVisibility(),ESlateVisibility::Collapsed);
	return !HasAnyErrors();
}

#endif
