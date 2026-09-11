#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Fishing/InputAbilities/CatFishingPrimaryActionAbility.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "InputActionValue.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Engine/LocalPlayer.h"
#include "OnlineSubsystemTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/HUD/CatHUDModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalInputRouteTest,
	"Catfishing.PhysicalGrab.Runtime.OwnerRodHoldKeepsMouseFishingIndependentAndClearsLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalInputRouteTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!TestTrue(TEXT("建立真实输入物理 World"), Scene.CreateTestWorld(EWorldType::Game))) return false;
	Scene.ForwardErrorMessages(this);
	UWorld* World = Scene.GetTestWorld();
	FURL URL; URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Floor || !Cube) return false;
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	// Keep the water-facing edge at X=300, while providing real shore support behind and beside
	// the cat for the full randomized bite wait. The hand/rod remain free to move physically.
	Floor->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(-2350,0,-10), FVector(53,100,.2)));
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
	FName CatalogRegion;
	for (const auto& Reference : GetDefault<UCatFishCatalogSettings>()->Definitions)
	{
		const UCatFishDefinition* Definition = Reference.LoadSynchronous();
		if (Definition && !Definition->RegionIds.IsEmpty()) { CatalogRegion = Definition->RegionIds[0]; break; }
	}
	if (!TestFalse(TEXT("正式鱼种库提供真实水域身份"), CatalogRegion.IsNone())) return false;
	FCatWaterGeometryBuildInput WaterInput;
	WaterInput.RegionId = CatalogRegion;
	WaterInput.WaterPointVerticalToleranceCm = 100;
	// Match Scripts/create_stage_a_maps.py: the production shore admits the camera eye height
	// before AimLibrary selects the region and runs the real ray-to-water query.
	WaterInput.BankHeightToleranceCm = 250;
	WaterInput.BoundaryToleranceCm = 1;
	WaterInput.MaxLandingCorrectionCm = 100;
	WaterInput.MinimumWaterInsetCm = 1;
	auto& Boundary = WaterInput.Boundaries.AddDefaulted_GetRef();
	Boundary.BoundaryId = TEXT("PhysicalInputRetakeShore");
	Boundary.Vertices = {{400,-1200},{1800,-1200},{1800,1200},{400,1200}};
	const auto BakedWater = FCatWaterGeometry::Build(WaterInput);
	ACatWaterRegion* WaterRegion = World->SpawnActor<ACatWaterRegion>();
	if (!TestTrue(TEXT("抛竿回归创建真实烘焙水域"), BakedWater.bSucceeded && WaterRegion)) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*WaterRegion, BakedWater.Cache);
	if (!Scene.BeginPlayInTestWorld()) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0,0,20), FRotator::ZeroRotator, Spawn);
	ACatCharacter* Other = World->SpawnActor<ACatCharacter>(FVector(40,0,20), FRotator::ZeroRotator, Spawn);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* Helper = World->SpawnActor<ACatfishingPlayerState>();
	if (!Cat || !Other || !Controller || !Helper) return false;
	Controller->PlayerState = Helper;
	Cat->SetPlayerState(Helper);
	Controller->Possess(Cat);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("PhysicalInputOwner"), FName(TEXT("CAT_TEST")));
	Helper->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	auto* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	ACatfishingGameModeBase::FAdmissionRecord Admission;
	Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Admission.Controller = Controller;
	GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Helper->GetUniqueId()), Admission);
	GameMode->bRunCommandsOpen = true;
	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	GameMode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	Controller->SetPlayer(LocalPlayer.Get());
	Controller->SetViewTarget(Cat);
	UCatAbilityInputBindingComponent* Input = Controller->FindComponentByClass<UCatAbilityInputBindingComponent>();
	UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent();
	UCatPhysicsGrabComponent* Grab = Body ? Body->GetGrab() : nullptr;
	UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
	if (!Input || !Grab || !ASC) return false;
	ASC->InitAbilityActorInfo(Cat,Cat);
	ASC->ClearAllAbilities();
	FGameplayAbilitySpec PrimarySpec(UCatGA_FishingPrimaryAction::StaticClass());
	PrimarySpec.GetDynamicSpecSourceTags().AddTag(CatFishingAbilityTags::Input_Fishing_Primary);
	const auto Spec = ASC->GiveAbility(PrimarySpec);
	ASC->RegisterAbilityInput(Spec, CatFishingAbilityTags::Input_Fishing_Primary, ECatAbilityActivationPolicy::WhileInputActive);
	// This isolated authority fixture feeds accepted input through the production Move entry each frame.
	// Without a client heartbeat it must time out after 0.5 s; real network heartbeat is covered separately.
	const auto TickInputFrame = [&]()
	{
		Controller->Move(FInputActionValue(FVector2D::ZeroVector));
		Scene.TickTestWorld(1.0f/60.0f);
	};
	for (int32 Frame=0; Frame<30; ++Frame) TickInputFrame();
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	for (int32 Frame=0; Frame<60; ++Frame) TickInputFrame();
	if (!TestTrue(TEXT("空手左键实际约束另一只猫，而非仅伸手标记"), Grab->IsGripping(true) && Grab->GetGripTarget(true)==Other)) return false;
	TestEqual(TEXT("抓握按下不进入收线 ASC"), ASC->GetHeldInputCount(), 0);

	UCatEquipmentComponent* Equipment = Cat->GetEquipmentComponent();
	if (!TestTrue(TEXT("主控拥有正式鱼竿库存实例"),Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(),Equipment->GetSnapshot().Revision,TEXT("StarterRodT1")).bCommitted)) return false;
	UCatFishingCommandComponent* Commands = Controller->GetFishingCommandComponent();
	const FCatFishingInputEdge RodPress = Commands->SubmitRodInteract();
	FCatFishingCommandResult RodResult;
	if (!TestTrue(TEXT("实际 R 命令从库存取出本人鱼竿"),Commands->TryGetResult(RodPress.RequestId,RodResult)&&RodResult.bCommitted)) return false;
	UCatFishingService* Service = World->GetSubsystem<UCatFishingService>();
	ACatFishingRodActor* Rod = Service->FindRodOperatedBy(Helper);
	if (!TestTrue(TEXT("左手抓人时 R 用空闲右手真实持本人鱼竿"),Rod && Grab->IsGripping(false) && Grab->GetGripTarget(false)==Rod)) return false;
	const FGuid RodGripId=Grab->GetGripState(false).GripId;
	UCatHUDModel* HUDModel=NewObject<UCatHUDModel>();
	if (!TestTrue(TEXT("真实主控 HUD Model 绑定"),HUDModel->Bind(LocalPlayer.Get(),Controller,Cat))) return false;
	TestTrue(TEXT("右爪实际持竿提示 R 放竿而非鼠标松键"),HUDModel->GetViewState().PhysicalHandStateText.ToString().Contains(TEXT("右爪：持竿（R 放竿）")));
	TestTrue(TEXT("另一只抓猫手仍提示松键释放"),HUDModel->GetViewState().PhysicalHandStateText.ToString().Contains(TEXT("左爪：抓住（松键释放）")));
	HUDModel->Unbind();
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	TestFalse(TEXT("成为明确主控后松左键仍释放原抓猫约束"), Grab->IsGripping(true));
	TestFalse(TEXT("原抓握松键同时停止左手伸出"), Grab->IsReaching(true));
	TestEqual(TEXT("旧抓握释放不会误发主控 GAS"), ASC->GetReleasedInputCount(),0);
	TestTrue(TEXT("左键松开不会释放 R 建立的另一只手持竿"),Grab->IsGripping(false)&&Grab->GetGripState(false).GripId==RodGripId);

	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	TestTrue(TEXT("主位左键继续保留原生 Ability 路由"), ASC->GetHeldInputCount()>0);
	TestFalse(TEXT("主位按下不额外伸左爪"), Grab->IsReaching(true));
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Slack);
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Slack);
	TestTrue(TEXT("主控右键放线边沿不释放 R 的右手持竿"),Grab->IsGripping(false)&&Grab->GetGripState(false).GripId==RodGripId);
	Rod->SetPrimaryOperatorFromAuthority(nullptr,Rod->GetPresentationState().RodActorRevision);
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	TestEqual(TEXT("退主位后松键仍清旧 ASC 按住状态"),ASC->GetHeldInputCount(),0);
	TestTrue(TEXT("退主位后松键发往原 ASC"),ASC->GetReleasedInputCount()>0);
	ASC->ResetAbilityInput(); // 本用例不激活正式抛竿领域命令。
	Rod->SetPrimaryOperatorFromAuthority(Helper,Rod->GetPresentationState().RodActorRevision);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	bool bPrimaryHeld=false, bSlackHeld=false;
	int64 HeldSequence=0;
	TestTrue(TEXT("真实 GAS 激活进入服务器瞄准按住状态"),Commands->TryGetHeldFightInputStateFromAuthority(bPrimaryHeld,bSlackHeld,HeldSequence)&&bPrimaryHeld&&!bSlackHeld);
	TestTrue(TEXT("GAS 主控按下仍保留 R 的同一条持竿约束"),Grab->IsGripping(false)&&Grab->GetGripState(false).GripId==RodGripId);
	TestTrue(TEXT("正式 Primary Ability 保持活跃等待松键"),ASC->FindAbilitySpecFromHandle(Spec)->IsActive());
	const int64 EquipmentBeforeCancel=Equipment->GetSnapshot().Revision;
	const int64 SequenceBeforeCancel=HeldSequence;
	Controller->ClearPhysicalControlInput(TEXT("MenuOpened"));
	TestFalse(TEXT("菜单取消真实活跃 Ability，不能等待迟到 Release 抛竿"),ASC->FindAbilitySpecFromHandle(Spec)->IsActive());
	TestTrue(TEXT("菜单清服务器按住状态且序号前进"),Commands->TryGetHeldFightInputStateFromAuthority(bPrimaryHeld,bSlackHeld,HeldSequence)&&!bPrimaryHeld&&!bSlackHeld&&HeldSequence>SequenceBeforeCancel);
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	TestEqual(TEXT("瞄准中菜单取消及迟到松键未创建抛竿会话"),Service->GetTrackedSessionCountForDiagnostics(),0);
	TestEqual(TEXT("取消瞄准没有装备写入副作用"),Equipment->GetSnapshot().Revision,EquipmentBeforeCancel);
	TestEqual(TEXT("菜单清待处理的主位按下"),ASC->GetPressedInputCount(),0);
	TestEqual(TEXT("菜单取消不会发送可触发抛钩的正常 Released"),ASC->GetReleasedInputCount(),0);
	TestEqual(TEXT("菜单清主位持续按住状态"),ASC->GetHeldInputCount(),0);
	Rod->SetPrimaryOperatorFromAuthority(nullptr,Rod->GetPresentationState().RodActorRevision);

	// Parked rods reject mouse grabs; R retakes the same rod, and releasing the old mouse route must preserve its explicit hold.
	Controller->ClearPhysicalControlInput(TEXT("PrepareSameHandRetake"));
	Other->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(
		FTransform(FRotator::ZeroRotator,FVector(-200,300,20)),TEXT("RetakeOtherOutOfReach"));
	Body->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,FVector(0,0,Body->GetStandRootHeightCm())),TEXT("RetakeFixture"));
	Controller->SetControlRotation(FRotator::ZeroRotator);
	for (int32 Frame=0; Frame<30; ++Frame) TickInputFrame();
	UPrimitiveComponent* RodBody = Rod->GetPhysicalRodBody();
	const FVector GrabContact = Grab->GetShoulderWorldLocation(true) + FVector(14,0,0);
	RodBody->SetWorldLocation(RodBody->GetComponentLocation() + GrabContact - Rod->GetGripWorldTransform().GetLocation(),false,nullptr,ETeleportType::TeleportPhysics);
	RodBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
	RodBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	for (int32 Frame=0; Frame<60 && !Grab->IsGripping(true); ++Frame) TickInputFrame();
	if (!TestFalse(TEXT("架住的鱼竿即使贴手也拒绝鼠标抓取"), Grab->IsGripping(true))) return false;
	TestNull(TEXT("尝试物理抓取不授予钓鱼主控"), Service->FindRodOperatedBy(Helper));
	const auto RetakeEdge = Commands->SubmitRodInteract();
	FCatFishingCommandResult RetakeResult;
	if (!TestTrue(TEXT("R 提交本人原竿控制"), Commands->TryGetResult(RetakeEdge.RequestId,RetakeResult) && RetakeResult.bCommitted)) return false;
	if (!TestTrue(TEXT("R 直接拾回同一鱼竿并建立显式抓握"), Grab->IsGripping(true) && Grab->GetGripTarget(true) == Rod && Grab->GetGripState(true).bExplicitHold)) return false;
	const FGuid RetakeGripId = Grab->GetGripState(true).GripId;
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	for (int32 Frame=0; Frame<15; ++Frame) TickInputFrame();
	if (!TestTrue(TEXT("原鼠标松键及真实物理步后仍握同一条 R 约束"), Grab->IsGripping(true) && Grab->GetGripState(true).GripId==RetakeGripId)) return false;
	TestEqual(TEXT("交接后的旧 Grab release 不进入 GAS"), ASC->GetReleasedInputCount(),0);
	if (!TestTrue(TEXT("抛竿使用真实正式浮标装备"), Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(),Equipment->GetSnapshot().Revision,TEXT("FeatherFloat")).bCommitted)) return false;
	if (!TestTrue(TEXT("抛竿使用真实消耗品事务发放鱼饵"), Equipment->GrantInventoryQuantityFromAuthority(
		FGuid::NewGuid(),Equipment->GetSnapshot().Revision,TEXT("BugBait"),1).bCommitted)) return false;
	FVector InitialViewOrigin;
	FRotator InitialViewRotation;
	Controller->GetPlayerViewPoint(InitialViewOrigin,InitialViewRotation);
	Controller->SetControlRotation((FVector(900,0,0)-InitialViewOrigin).Rotation());
	Controller->PlayerCameraManager->UpdateCamera(1.0f/60.0f);
	FVector ViewOrigin,ViewDirection,Landing;
	FCatWaterRegionHandle AimWater;
	const bool bHasRay=UCatFishingAimLibrary::TryGetLocalCastViewRay(Controller,ViewOrigin,ViewDirection);
	const bool bHitsWater=bHasRay && UCatFishingAimLibrary::ResolveCastAimPoint(Controller,ViewOrigin,ViewDirection.Rotation(),AimWater,Landing);
	const auto* WaterQueries=World->GetSubsystem<UCatWaterQuerySubsystem>();
	const auto Nearest=WaterQueries->QueryNearestShoreForPreview(ViewOrigin);
	const auto DirectRay=WaterQueries->ResolveRayToWater(ViewOrigin,ViewDirection,WaterRegion->GetWaterRegionHandle());
	AddInfo(FString::Printf(TEXT("Event=physical_input_retake_cast_view_observed ViewTarget=%s Cat=%s MouseCursor=%d Origin=%s Direction=%s ControlRotation=%s HasRay=%d HitsWater=%d Landing=%s World=%s Authority=1"),
		*GetNameSafe(Controller->GetViewTarget()),*GetNameSafe(Cat),Controller->ShouldShowMouseCursor(),*ViewOrigin.ToString(),*ViewDirection.ToString(),
		*Controller->GetControlRotation().ToString(),bHasRay,bHitsWater,*Landing.ToString(),*World->GetName()));
	AddInfo(FString::Printf(TEXT("Event=physical_input_retake_water_query_observed BankHeightToleranceCm=%.3f NearestSucceeded=%d NearestError=%s DirectRaySucceeded=%d DirectRayError=%s RegionId=%s"),
		WaterInput.BankHeightToleranceCm,Nearest.bSucceeded,*UEnum::GetValueAsString(Nearest.Error),DirectRay.bSucceeded,
		*UEnum::GetValueAsString(DirectRay.Error),*CatalogRegion.ToString()));
	if (!TestTrue(TEXT("正式相机射线实际命中测试水域"), bHitsWater)) return false;
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	TestTrue(TEXT("抓回后新鼠标按下真实激活 Primary Ability"),ASC->FindAbilitySpecFromHandle(Spec)->IsActive());
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	FGuid CastSessionId;
	FCatFishingSessionSnapshot CastSnapshot;
	if (!TestTrue(TEXT("正常 GAS 松键创建真实抛竿会话"),Service->TryGetActiveSessionForController(Controller,CastSessionId,CastSnapshot))) return false;
	ACatFishingSession* CastSession = Service->FindSession(CastSessionId);
	if (!TestTrue(TEXT("抛钩与鱼饵预留未释放显式持竿"),CastSession && Equipment->IsFishingUseActive(CastSessionId)
		&& Grab->IsGripping(true) && Grab->GetGripState(true).GripId==RetakeGripId)) return false;
	const double WaitStarted=World->GetTimeSeconds();
	const FVector WaitBodyStart=Body->GetBody()->GetComponentLocation();
	double MinimumWaitingBodyZ=WaitBodyStart.Z;
	bool bWaitingGripSurvived=true;
	for (int32 Frame=0; Frame<3600 && !CastSession->IsTerminal() && bWaitingGripSurvived
		&& CastSession->GetSnapshot().Phase!=ECatFishingPhase::TrueBiteWindow; ++Frame)
	{
		TickInputFrame();
		MinimumWaitingBodyZ=FMath::Min(MinimumWaitingBodyZ,Body->GetBody()->GetComponentLocation().Z);
		bWaitingGripSurvived=Grab->IsGripping(true) && Grab->GetGripState(true).GripId==RetakeGripId;
	}
	const FVector WaitBodyEnd=Body->GetBody()->GetComponentLocation();
	FCollisionQueryParams ShoreParams(SCENE_QUERY_STAT(PhysicalInputRetakeShoreSupport),false);
	ShoreParams.AddIgnoredActor(Cat); ShoreParams.AddIgnoredActor(Other); ShoreParams.AddIgnoredActor(Rod);
	FHitResult ShoreHit;
	const bool bHasShoreSupport=World->LineTraceSingleByChannel(ShoreHit,
		WaitBodyEnd+FVector(0,0,100),WaitBodyEnd-FVector(0,0,100),ECC_Visibility,ShoreParams) && ShoreHit.GetActor()==Floor;
	AddInfo(FString::Printf(TEXT("Event=physical_input_retake_wait_observed WaitSeconds=%.6f BodyStart=%s BodyEnd=%s TravelCm=%.3f MinimumBodyZ=%.3f ShoreSupport=%d Grounded=%d GripSurvived=%d GripId=%s World=%s Authority=1"),
		World->GetTimeSeconds()-WaitStarted,*WaitBodyStart.ToString(),*WaitBodyEnd.ToString(),FVector::Dist2D(WaitBodyStart,WaitBodyEnd),
		MinimumWaitingBodyZ,bHasShoreSupport,Body->IsGrounded(),bWaitingGripSurvived,*RetakeGripId.ToString(),*World->GetName()));
	if (!TestTrue(TEXT("完整随机咬钩等待仍保持同一条显式抓握"),bWaitingGripSurvived)
		|| !TestTrue(TEXT("等待与提竿始终处于真实岸地支撑范围，没有掉出夹具"),bHasShoreSupport && MinimumWaitingBodyZ>-1)) return false;
	if (!TestEqual(TEXT("真实飞行和等口计时器推进到提竿窗口"),CastSession->GetSnapshot().Phase,ECatFishingPhase::TrueBiteWindow)) return false;
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	for (int32 Frame=0; Frame<10 && !CastSession->IsFightRunnerRunning(); ++Frame) TickInputFrame();
	if (!TestTrue(TEXT("鼠标提竿启动真实 FightRunner"),CastSession->IsFightRunnerRunning())) return false;
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	TestFalse(TEXT("正常松鼠标停止本人收线"),CastSession->GetSnapshot().bReeling);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->ProcessAbilityInput(1.0f/60.0f,false);
	for (int32 Frame=0; Frame<6; ++Frame) TickInputFrame();
	TestTrue(TEXT("再次按鼠标可以真实收线且同一 R 持竿仍承重"),CastSession->GetSnapshot().bReeling
		&& Grab->IsGripping(true) && Grab->GetGripState(true).GripId==RetakeGripId);
	AddInfo(FString::Printf(TEXT("Event=physical_input_same_hand_retake_cast_reel_verified GripId=%s SessionId=%s ExplicitHold=%d Phase=%s Reeling=%d World=%s Authority=1"),
		*RetakeGripId.ToString(),*CastSessionId.ToString(),Grab->GetGripState(true).bExplicitHold,
		*UEnum::GetValueAsString(CastSession->GetSnapshot().Phase),CastSession->GetSnapshot().bReeling,*World->GetName()));
	Controller->ClearPhysicalControlInput(TEXT("RetakeRegressionFinished"));
	TestFalse(TEXT("生命周期强清理仍拆显式来源的真实约束"),Grab->IsGripping(true));
	TestFalse(TEXT("强清理不遗留显式来源"),Grab->GetGripState(true).bExplicitHold);
	if (!CastSession->IsTerminal())
	{
		AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"),EAutomationExpectedErrorFlags::Contains,1);
		CastSession->CancelFromAuthority(FGuid::NewGuid());
	}
	Rod->SetPrimaryOperatorFromAuthority(nullptr,Rod->GetPresentationState().RodActorRevision);

	{
		UCharacterMovementComponent* DefaultMovement = Cat->GetClass()->GetDefaultObject<ACatCharacter>()->GetCharacterMovement();
		TGuardValue<float> WalkDefault(DefaultMovement->MaxWalkSpeed,137.0f);
		Controller->ApplySprintSpeed(Cat,false);
		TestEqual(TEXT("普通移动读取正式猫类CMC默认配置"),Body->MaxMovementSpeedCmS,137.0);
		Controller->ApplySprintSpeed(Cat,true);
		TestEqual(TEXT("保留正式350cm/s疾跑"),Body->MaxMovementSpeedCmS,350.0);
		Controller->ApplySprintSpeed(Cat,false);
		TestEqual(TEXT("结束疾跑恢复猫类普通速度而非上次疾跑实例值"),Body->MaxMovementSpeedCmS,137.0);
	}
	Controller->ApplySprintSpeed(Cat,false);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Slack);
	Controller->Move(FInputActionValue(FVector2D(0,1)));
	TestTrue(TEXT("正式 Move 提交单位前向意图"),Body->GetMoveIntent().Equals(FVector::ForwardVector,.001));
	Controller->StopMove();
	TestTrue(TEXT("Completed/Canceled 共用入口清移动而非延续最后一帧"),Body->GetMoveIntent().IsNearlyZero());
	Controller->Move(FInputActionValue(FVector2D(0,1)));
	Body->GetBody()->SetPhysicsLinearVelocity(FVector(42,3,1));
	const FVector VelocityBeforeClear=Body->GetVelocity();
	Controller->FlushPressedKeys();
	TestTrue(TEXT("失焦清双爪"),!Grab->IsReaching(true)&&!Grab->IsReaching(false));
	TestTrue(TEXT("失焦清移动意图"),Body->GetMoveIntent().IsNearlyZero());
	TestTrue(TEXT("清输入不清外部运动速度"),Body->GetVelocity().Equals(VelocityBeforeClear,.001));
	TestTrue(TEXT("失焦清按下接收方"),Input->PressedRoutes.IsEmpty());
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Slack);
	Controller->ClearPhysicalControlInput(TEXT("MenuOpened"));
	TestFalse(TEXT("菜单清右手"),Grab->IsReaching(false));
	Controller->SetIgnoreMoveInput(true);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	Controller->Move(FInputActionValue(FVector2D(0,1)));
	TestTrue(TEXT("菜单开启期间新按下不重新伸手或移动"),!Grab->IsReaching(true)&&Body->GetMoveIntent().IsNearlyZero());
	Controller->SetIgnoreMoveInput(false);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Slack);
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Cancel);
	TestFalse(TEXT("取消输入先收起已有右手"),Grab->IsReaching(false));
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Cancel);
	ASC->ResetAbilityInput();
	Input->HandleAbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Slack);
	Controller->Possess(Other);
	TestFalse(TEXT("切 Pawn 清旧身体右手"),Grab->IsReaching(false));
	Input->HandleAbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Slack);
	TestFalse(TEXT("迟到松键不会写新身体右手"),Other->GetPhysicalBodyComponent()->GetGrab()->IsReaching(false));
	return !HasAnyErrors();
}

#endif
