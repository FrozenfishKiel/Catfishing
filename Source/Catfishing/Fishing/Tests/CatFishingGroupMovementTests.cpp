#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "GameFramework/PlayerState.h"
#include <limits>

namespace
{
	FCatExternalTractionInput GroupInput()
	{
		FCatExternalTractionInput Input;
		Input.SourceId = FGuid::NewGuid();
		Input.Direction = FVector::ForwardVector;
		Input.bActive = Input.bGroupDriven = true;
		Input.RosterVersion = Input.ControlEpoch = Input.MembershipEpoch = 1;
		Input.SpeedLimitCentimetersPerSecond = 100.0;
		return Input;
	}

	UCatCharacterMovementComponent* PrepareMovement(ACatCharacter* Cat)
	{
		UCatCharacterMovementComponent* Movement = Cat ? Cast<UCatCharacterMovementComponent>(Cat->GetCharacterMovement()) : nullptr;
		if (Movement)
		{
			Movement->bRunPhysicsWithNoController = true;
			Movement->SetMovementMode(MOVE_Flying);
			Movement->BrakingDecelerationFlying = 0.0f;
		}
		return Movement;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupMovementContinuityTest,
	"Catfishing.Unit.Fishing.Runtime.GroupFormationCorrectionDoesNotBecomeMomentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupMovementContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	ACatCharacter* Cat = Wrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
	UCatCharacterMovementComponent* Movement = PrepareMovement(Cat);
	if (!TestNotNull(TEXT("真实 CMC 可执行组移动"), Movement)) return false;
	FCatExternalTractionInput Input = GroupInput();
	Input.GroupDesiredVelocity = FVector(10.0, 0.0, 0.0);
	Input.FormationCorrectionVelocity = FVector(60.0, 0.0, 0.0);
	Movement->Velocity = FVector(10.0, 0.0, 0.0);
	Movement->SetExternalTraction(Cat, Input);
	const FVector Start = Cat->GetActorLocation();
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const FVector Before = Cat->GetActorLocation();
		Movement->PerformMovement(0.1f);
		TestEqual(TEXT("每次仍是动力 1cm 加独立修正 6cm，不随子步累加"), Cat->GetActorLocation().X - Before.X, 7.0, 0.01);
		TestEqual(TEXT("队形修正不写入下步动力速度"), Movement->Velocity.X, 10.0, 0.01);
	}
	TestEqual(TEXT("两秒固定修正有界且不加速"), Cat->GetActorLocation().X - Start.X, 140.0, 0.02);
	for (int32 Index = 0; Index < 20; ++Index) Movement->CalcVelocity(0.01f, 0.0f, false, 0.0f);
	TestEqual(TEXT("直接重复 CMC 子步也不累计修正速度"), Movement->Velocity.X, 10.0, 0.01);
	Input.FormationCorrectionVelocity = FVector::ZeroVector;
	Movement->SetExternalTraction(Cat, Input);
	const FVector BeforeClear = Cat->GetActorLocation();
	Movement->PerformMovement(0.1f);
	TestEqual(TEXT("消除站位误差后立即只剩动力位移"), Cat->GetActorLocation().X - BeforeClear.X, 1.0, 0.01);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupMovementEpochTest,
	"Catfishing.Unit.Fishing.Runtime.GroupSavedMovesRejectEveryStaleMembershipDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupMovementEpochTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	ACatCharacter* Cat = Wrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
	UCatCharacterMovementComponent* Movement = PrepareMovement(Cat);
	if (!TestNotNull(TEXT("真实预测移动组件"), Movement)) return false;
	FCatExternalTractionInput Original = GroupInput();
	Original.FormationCorrectionVelocity = FVector(60.0, 0.0, 0.0);
	Movement->SetExternalTraction(Cat, Original);
	auto* Prediction = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	FCatSavedMove Saved;
	Saved.SetMoveFor(Cat, 0.1f, FVector::ZeroVector, *Prediction);
	for (int32 Domain = 0; Domain < 5; ++Domain)
	{
		FCatExternalTractionInput Current = Original;
		Current.FormationCorrectionVelocity = FVector(0.0, 30.0, 0.0);
		if (Domain == 0) ++Current.RosterVersion;
		if (Domain == 1) ++Current.ControlEpoch;
		if (Domain == 2) ++Current.MembershipEpoch;
		if (Domain == 3) ++Current.AimInputEpoch;
		if (Domain == 4)
		{
			Current.bActive = false;
			Current.bUnloadedMovement = true;
		}
		Cat->SetActorLocation(FVector::ZeroVector);
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetExternalTraction(Cat, Current);
		Saved.PrepMoveFor(Cat);
		Movement->PerformMovement(0.1f);
		TestEqual(TEXT("旧成员域的 X 修正没有复活"), Cat->GetActorLocation().X, 0.0, 0.001);
		TestEqual(TEXT("改用当前有效域的 Y 修正"), Cat->GetActorLocation().Y, 3.0, 0.001);
	}
	Movement->ClearExternalTraction(Cat);
	Cat->SetActorLocation(FVector::ZeroVector);
	Movement->Velocity = FVector::ZeroVector;
	Saved.PrepMoveFor(Cat);
	Movement->PerformMovement(0.1f);
	TestTrue(TEXT("退出并清理后旧 SavedMove 不能再次牵引"), Cat->GetActorLocation().IsNearlyZero());
	AddExpectedError(TEXT("Event=fishing_group_traction_rejected"), EAutomationExpectedErrorFlags::Contains, 4);
	for (int32 VectorField = 0; VectorField < 4; ++VectorField)
	{
		Movement->SetExternalTraction(Cat, Original);
		FCatExternalTractionInput Invalid = Original;
		const FVector NonFinite(std::numeric_limits<double>::infinity(), 0.0, 0.0);
		if (VectorField == 0) Invalid.GroupDesiredVelocity = NonFinite;
		if (VectorField == 1) Invalid.GroupLateralAcceleration = NonFinite;
		if (VectorField == 2) Invalid.FormationCorrectionVelocity = NonFinite;
		if (VectorField == 3) Invalid.GroupUnloadedVelocity = NonFinite;
		Movement->SetExternalTraction(Cat, Invalid);
		TestFalse(TEXT("拒绝非有限向量并清理旧输入"), Movement->GetExternalTraction().bGroupDriven);
	}
	// 名单和控制权不变时，结束鱼载荷本身也必须使旧 SavedMove 失效。
	FCatExternalTractionInput Loaded = Original;
	Loaded.AccelerationCentimetersPerSecondSquared = 500.0;
	Loaded.GroupDesiredVelocity = FVector(100.0, 0.0, 0.0);
	Loaded.FormationCorrectionVelocity = FVector::ZeroVector;
	Movement->SetExternalTraction(Cat, Loaded);
	FCatSavedMove SavedLoaded;
	SavedLoaded.SetMoveFor(Cat, 0.1f, FVector::ZeroVector, *Prediction);
	FCatExternalTractionInput Unloaded = Loaded;
	Unloaded.bActive = false;
	Unloaded.bUnloadedMovement = true;
	Unloaded.AccelerationCentimetersPerSecondSquared = 0.0;
	Unloaded.GroupDesiredVelocity = FVector::ZeroVector;
	Unloaded.GroupUnloadedVelocity = FVector(0.0, 20.0, 0.0);
	Movement->SetExternalTraction(Cat, Unloaded);
	Cat->SetActorLocation(FVector::ZeroVector);
	Movement->Velocity = FVector::ZeroVector;
	SavedLoaded.PrepMoveFor(Cat);
	Movement->PerformMovement(0.1f);
	TestEqual(TEXT("无鱼力模式拒绝同成员域旧SavedMove的X鱼力"), Cat->GetActorLocation().X, 0.0, 0.001);
	TestTrue(TEXT("重放使用当前无鱼力共同速度"), Cat->GetActorLocation().Y > 1.0 && Movement->MovementTraction.bUnloadedMovement);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupMembershipContinuityTest,
	"Catfishing.Unit.Fishing.Runtime.GroupRosterKeepsAnchorAndRejectsStaleReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupMembershipContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	APlayerState* First = World->SpawnActor<APlayerState>();
	APlayerState* Second = World->SpawnActor<APlayerState>();
	ACatCharacter* FirstCat = World->SpawnActor<ACatCharacter>();
	ACatCharacter* SecondCat = World->SpawnActor<ACatCharacter>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!First || !Second || !FirstCat || !SecondCat || !Rod) return false;
	FirstCat->SetPlayerState(First);
	SecondCat->SetPlayerState(Second);
	FirstCat->SetActorLocation(FVector(0.0, 0.0, 0.0));
	SecondCat->SetActorLocation(FVector(0.0, 300.0, 0.0));
	UCatCharacterMovementComponent* FirstMovement = PrepareMovement(FirstCat);
	UCatCharacterMovementComponent* SecondMovement = PrepareMovement(SecondCat);
	if (!FirstMovement || !SecondMovement) return false;
	TestTrue(TEXT("初始化同时创建首位元数据"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("GroupRod"), NAME_None, First, First, true, false));
	TestTrue(TEXT("初始主位已经具备有效握持 epoch"), Rod->GetOperatorMembershipEpoch(First) > 0);
	const uint32 FirstRosterVersion = Rod->GetRosterVersion();
	const uint32 FirstControlEpoch = Rod->GetControlEpoch();
	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("辅助加入"), Rod->AddOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision, JoinedSlot));
	TestTrue(TEXT("辅助加入推进名单版本"), Rod->GetRosterVersion() > FirstRosterVersion);
	TestEqual(TEXT("辅助加入不改变主位控制权 epoch"), Rod->GetControlEpoch(), FirstControlEpoch);
	const uint32 SecondMembershipEpoch = Rod->GetOperatorMembershipEpoch(Second);
	TestTrue(TEXT("新成员有独立 epoch"), SecondMembershipEpoch != 0 && SecondMembershipEpoch != Rod->GetOperatorMembershipEpoch(First));
	TestTrue(TEXT("发布共同约束"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true));
	TestTrue(TEXT("发布共同移动"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("两个成员都收到共同运动"), FirstMovement->GetExternalTraction().bGroupDriven && SecondMovement->GetExternalTraction().bGroupDriven);
	const FCatFishingGroupMotionState OldMotion = Rod->GroupMotionState;
	const FCatFishingCarrierConstraintState OldConstraint = Rod->CarrierConstraintState;
	// 模拟成员碰撞产生不同的站位误差，在退出边界不能以剩余均值重置组根。
	FirstCat->SetActorLocation(FVector(100.0, 0.0, 0.0));
	SecondCat->SetActorLocation(FVector(20.0, 300.0, 0.0));
	Rod->RefreshGroupAnchorFromAuthority();
	const FVector PreviousAnchor = Rod->GetGroupAnchorWorld();
	APlayerState* Promoted = nullptr;
	TestTrue(TEXT("主位退出并自动接力"), Rod->RemoveOperatorFromAuthority(First, Rod->GetPresentationState().RodActorRevision, Promoted));
	TestEqual(TEXT("接力保留幸存者握持轮次"), Rod->GetOperatorMembershipEpoch(Second), SecondMembershipEpoch);
	TestTrue(TEXT("接力推进控制权"), Rod->GetControlEpoch() > FirstControlEpoch);
	Rod->RefreshGroupAnchorFromAuthority();
	TestTrue(TEXT("重定基后下一帧组根仍连续"), Rod->GetGroupAnchorWorld().Equals(PreviousAnchor, 0.001));
	TestFalse(TEXT("离队成员立即解除运动"), FirstMovement->GetExternalTraction().bGroupDriven);
	Rod->GroupMotionState = OldMotion;
	Rod->CarrierConstraintState = OldConstraint;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("旧主位/名单组合只保留当前成员等待，不附着旧受力"), SecondMovement->GetExternalTraction().bWaitingForGroupSolve);
	TestEqual(TEXT("等待输入使用当前名单版本"), SecondMovement->GetExternalTraction().RosterVersion, Rod->GetRosterVersion());
	TestTrue(TEXT("等待输入清空旧组速度"), SecondMovement->GetExternalTraction().GroupDesiredVelocity.IsNearlyZero());
	Rod->GroupMotionState = FCatFishingGroupMotionState{};
	TestTrue(TEXT("新版约束可以发布"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true));
	TestTrue(TEXT("新版组移动可以发布"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	const FCatFishingGroupMotionState CurrentMotion = Rod->GroupMotionState;
	TestTrue(TEXT("新版运动恢复到唯一幸存者"), SecondMovement->GetExternalTraction().bGroupDriven);
	Rod->ClearCarrierConstraintFromAuthority();
	TestFalse(TEXT("停止约束同步清空鱼载荷"), Rod->GetCarrierConstraintState().bFightActive);
	TestTrue(TEXT("仍握竿的成员切回无鱼力共同移动"), SecondMovement->GetExternalTraction().bUnloadedMovement);
	const FCatFishingGroupMotionState StoppedMotion = Rod->GroupMotionState;
	Rod->GroupMotionState = CurrentMotion;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("停止后旧搏斗快照只保留成员等待正确无鱼力快照"),
		SecondMovement->GetExternalTraction().bGroupDriven && SecondMovement->GetExternalTraction().bWaitingForGroupSolve);
	TestFalse(TEXT("停止后的旧组快照不能复活鱼力"), SecondMovement->GetExternalTraction().bActive);
	Rod->GroupMotionState = StoppedMotion;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("当前停止快照到齐后恢复无鱼力共同移动"), SecondMovement->GetExternalTraction().bUnloadedMovement
		&& !SecondMovement->GetExternalTraction().bWaitingForGroupSolve);
	TestFalse(TEXT("未开始新搏斗时禁止单独复活组运动"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupWaitingTest,
	"Catfishing.Unit.Fishing.Runtime.GroupSnapshotGapsHoldMembershipWithoutOldForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupWaitingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	APlayerState* First = World->SpawnActor<APlayerState>();
	APlayerState* Second = World->SpawnActor<APlayerState>();
	ACatCharacter* FirstCat = World->SpawnActor<ACatCharacter>();
	ACatCharacter* SecondCat = World->SpawnActor<ACatCharacter>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!First || !Second || !FirstCat || !SecondCat || !Rod) return false;
	FirstCat->SetPlayerState(First);
	SecondCat->SetPlayerState(Second);
	SecondCat->SetActorLocation(FVector(0.0, 300.0, 0.0));
	UCatCharacterMovementComponent* FirstMovement = PrepareMovement(FirstCat);
	UCatCharacterMovementComponent* SecondMovement = PrepareMovement(SecondCat);
	if (!FirstMovement || !SecondMovement) return false;
	if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("WaitingRod"), NAME_None, First, First, true, false)) return false;
	TestTrue(TEXT("首次载荷复制先到，成员即进入等待"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 100.0, 100.0, 0.0, 0.0, true));
	TestTrue(TEXT("尚无组解时也不回普通走路"), FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	TestTrue(TEXT("组解齐全开始受力"), Rod->SetGroupMotionFromAuthority(FVector(100.0, 0.0, 0.0), FVector::ZeroVector));
	const FCatFishingGroupMotionState OriginalMotion = Rod->GroupMotionState;
	auto* Prediction = static_cast<FNetworkPredictionData_Client_Character*>(FirstMovement->GetPredictionData_Client());
	FCatSavedMove Saved;
	Saved.SetMoveFor(FirstCat, 0.1f, FVector::ForwardVector * 1000.0, *Prediction);
	int32 Slot = INDEX_NONE;
	TestTrue(TEXT("中途加人"), Rod->AddOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision, Slot));
	TestTrue(TEXT("两人都等待新的共同求解"), FirstMovement->GetExternalTraction().bWaitingForGroupSolve && SecondMovement->GetExternalTraction().bWaitingForGroupSolve);
	Rod->GroupMotionState = {};
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("组属性尚未抵达仍保持当前成员"), FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 200.0, 100.0, 0.0, 0.0, true);
	Rod->GroupMotionState = OriginalMotion;
	Rod->OnRep_CarrierConstraintState();
	const FCatExternalTractionInput Waiting = FirstMovement->GetExternalTraction();
	TestTrue(TEXT("旧组和新载荷组合继续等待"), Waiting.bGroupDriven && Waiting.bWaitingForGroupSolve);
	TestEqual(TEXT("等待不继续用旧拉力"), Waiting.AccelerationCentimetersPerSecondSquared, 0.0);
	TestTrue(TEXT("等待不继续用旧站位修正"), Waiting.FormationCorrectionVelocity.IsNearlyZero());
	const FVector Before = FirstCat->GetActorLocation();
	FirstMovement->Velocity = FVector::ZeroVector;
	FirstMovement->Acceleration = FVector(1000.0, 0.0, 0.0);
	Saved.PrepMoveFor(FirstCat);
	FirstMovement->PerformMovement(0.1f);
	TestTrue(TEXT("真实CMC等待中不应用个人移动或重放旧外力"), FirstCat->GetActorLocation().Equals(Before, 0.001));
	ACatFishingRodActor* OtherRod = World->SpawnActor<ACatFishingRodActor>();
	if (!TestNotNull(TEXT("使用真实第二根竿验证来源冲突"), OtherRod)) return false;
	FCatExternalTractionInput OtherInput = GroupInput();
	AddExpectedError(TEXT("Event=fishing_group_traction_source_conflict"), EAutomationExpectedErrorFlags::Contains, 1);
	FirstMovement->SetExternalTraction(OtherRod, OtherInput);
	FirstMovement->ClearExternalTraction(OtherRod);
	TestEqual(TEXT("另一根竿不能夺取或清理当前等待归属"), FirstMovement->GetExternalTraction().SourceId, Rod->GetPresentationState().RodActorId);
	Rod->SetGroupMotionFromAuthority(FVector(100.0, 0.0, 0.0), FVector::ZeroVector);
	TestFalse(TEXT("同域快照齐全恢复组求解"), FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	FirstMovement->Acceleration = FVector::ZeroVector;
	FirstMovement->PerformMovement(0.1f);
	TestTrue(TEXT("真实CMC恢复共同加速"), FirstCat->GetActorLocation().X > Before.X);
	APlayerState* Promoted = nullptr;
	Rod->RemoveOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision, Promoted);
	TestFalse(TEXT("实际离队才释放移动归属"), SecondMovement->GetExternalTraction().bGroupDriven);
	Rod->ClearCarrierConstraintFromAuthority();
	TestTrue(TEXT("明确停止记录保留成员的无鱼力共同移动"), FirstMovement->GetExternalTraction().bUnloadedMovement);
	const FCatFishingGroupMotionState StoppedMotion = Rod->GroupMotionState;
	Rod->GroupMotionState = OriginalMotion;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("停止记录和旧组快照不匹配时保持成员等待"), FirstMovement->GetExternalTraction().bGroupDriven
		&& FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	TestFalse(TEXT("明确停止记录拒绝旧鱼力复活"), FirstMovement->GetExternalTraction().bActive);
	Rod->GroupMotionState = StoppedMotion;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("正确停止快照抵达后无鱼力共同移动恢复"), FirstMovement->GetExternalTraction().bUnloadedMovement
		&& !FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	Rod->GroupMotionState = OriginalMotion;
	Rod->GroupMotionState.RosterVersion = Rod->GetRosterVersion();
	Rod->GroupMotionState.ControlEpoch = Rod->GetControlEpoch();
	Rod->GroupMotionState.AimInputEpoch = Rod->CarrierConstraintState.AimInputEpoch + 1;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("下一场组解先到而载荷仍是停止记录时只等待"), FirstMovement->GetExternalTraction().bWaitingForGroupSolve);
	Rod->ClearCarrierConstraintFromAuthority();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupUnloadedMovementTest,
	"Catfishing.Unit.Fishing.Runtime.JoinedGroupMovesTogetherBeforeSessionWithoutFishCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupUnloadedMovementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!Rod) return false;
	TArray<APlayerState*> Players;
	TArray<ACatCharacter*> Cats;
	TArray<UCatCharacterMovementComponent*> Movements;
	TArray<float> StaminaBefore;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		APlayerState* Player = World->SpawnActor<APlayerState>();
		ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0.0, 200.0 * Index, 1000.0), FRotator::ZeroRotator, Spawn);
		if (!Player || !Cat) return false;
		Cat->SetPlayerState(Player);
		UCatCharacterMovementComponent* Movement = PrepareMovement(Cat);
		UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
		if (!Movement || !ASC) return false;
		ASC->InitAbilityActorInfo(Cat, Cat);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
		if (!TestTrue(TEXT("真实成员具有有效个人体力基线"), ASC->InitializeFishingStaminaForSession())) return false;
		Players.Add(Player);
		Cats.Add(Cat);
		Movements.Add(Movement);
		StaminaBefore.Add(ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()));
		// 加入时各人带着不同惯性，不能等自然制动后才形成共同移动。
		Movement->Velocity = FVector(Index % 2 == 0 ? -90.0 : 70.0, 0.0, 0.0);
		if (Index == 0)
		{
			if (!TestTrue(TEXT("无会话时直接初始化主位"), Rod->InitializeAuthoritativeIdentity(
				FGuid::NewGuid(), FGuid::NewGuid(), TEXT("UnloadedGroupRod"), NAME_None, Player, Player, true, false))) return false;
		}
		else
		{
			int32 Slot = INDEX_NONE;
			if (!TestTrue(TEXT("无会话时辅助直接加入"), Rod->AddOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision, Slot))) return false;
		}
		const FCatExternalTractionInput Bound = Movement->GetExternalTraction();
		TestTrue(TEXT("加入返回前已经绑定无鱼力共同移动"), Bound.bGroupDriven && Bound.bUnloadedMovement && !Bound.bWaitingForGroupSolve);
		TestFalse(TEXT("加入不会伪造搏斗阶段"), Rod->GetCarrierConstraintState().bFightActive);
		TestFalse(TEXT("无鱼力模式没有鱼端牵引"), Bound.bActive);
	}
	// Falling 使用新旧速度均值积分；入步前仍必须抹平各人的水平惯性，同时保留重力。
	TArray<FVector> BeforeFalling;
	for (int32 Index = 0; Index < Cats.Num(); ++Index)
	{
		BeforeFalling.Add(Cats[Index]->GetActorLocation());
		Movements[Index]->SetMovementMode(MOVE_Falling);
		Movements[Index]->Velocity = FVector(Index % 2 == 0 ? -90.0 : 70.0, 0.0, 0.0);
	}
	for (UCatCharacterMovementComponent* Movement : Movements) Movement->PerformMovement(0.05f);
	Rod->RefreshHeldTransformFromAuthority(0.05);
	for (int32 Index = 0; Index < Cats.Num(); ++Index)
	{
		TestTrue(TEXT("Falling 首子步不同水平初速不改变相对站位"),
			(Cats[Index]->GetActorLocation() - Cats[0]->GetActorLocation()).Equals(BeforeFalling[Index] - BeforeFalling[0], 0.1));
		TestTrue(TEXT("无鱼力共同移动保留重力下落"), Cats[Index]->GetActorLocation().Z < BeforeFalling[Index].Z
			&& Movements[Index]->MovementMode == MOVE_Falling);
		Movements[Index]->SetMovementMode(MOVE_Flying);
		Movements[Index]->Velocity = FVector(Index % 2 == 0 ? -90.0 : 70.0, 0.0, 0.0);
	}
	Rod->RefreshHeldTransformFromAuthority();
	const FVector InitialRodLocation = Rod->GetActorLocation();
	TArray<FVector> InitialPositions;
	for (ACatCharacter* Cat : Cats) InitialPositions.Add(Cat->GetActorLocation());
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		for (int32 Index = 0; Index < Movements.Num(); ++Index)
			Movements[Index]->Acceleration = (Index == 3 ? FVector::ForwardVector : -FVector::ForwardVector) * Movements[Index]->GetMaxAcceleration();
		Rod->Tick(1.0f / 20.0f);
		for (UCatCharacterMovementComponent* Movement : Movements) Movement->PerformMovement(1.0f / 20.0f);
		Rod->RefreshHeldTransformFromAuthority(1.0 / 20.0);
		for (int32 Index = 1; Index < Cats.Num(); ++Index)
		{
			const FVector RelativePosition = Cats[Index]->GetActorLocation() - Cats[0]->GetActorLocation();
			TestTrue(TEXT("不同初速和相反个人输入不会使同组成员散开"),
				RelativePosition.Equals(InitialPositions[Index] - InitialPositions[0], 0.1));
		}
	}
	for (int32 Index = 0; Index < Cats.Num(); ++Index)
	{
		TestTrue(TEXT("无Session的每名成员都由真实CMC共同移动"), Cats[Index]->GetActorLocation().X < InitialPositions[Index].X - 1.0);
		TestEqual(TEXT("战前共同移动不结算搏斗体力"),
			Cats[Index]->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), StaminaBefore[Index]);
		TestEqual(TEXT("共同移动保留CMC移动模式"), Movements[Index]->MovementMode.GetValue(), MOVE_Flying);
	}
	TestTrue(TEXT("鱼竿跟随共同位移而非单个辅助漂移"),
		(Rod->GetActorLocation() - InitialRodLocation).Equals(Cats[0]->GetActorLocation() - InitialPositions[0], 0.1));

	const FCatFishingGroupMotionState PreviousUnloaded = Rod->GroupMotionState;
	const FVector AnchorBeforeLeave = Rod->GetGroupAnchorWorld();
	const FVector SecondBeforeLeave = Cats[1]->GetActorLocation();
	APlayerState* Promoted = nullptr;
	if (!TestTrue(TEXT("战前主位离开并接力"), Rod->RemoveOperatorFromAuthority(Players[0], Rod->GetPresentationState().RodActorRevision, Promoted))) return false;
	TestEqual(TEXT("战前晋升最早辅助"), Promoted, Players[1]);
	TestFalse(TEXT("离队立即释放本竿共同运动"), Movements[0]->GetExternalTraction().bGroupDriven);
	TestFalse(TEXT("战前补位不等待不存在的搏斗Aim域"), Rod->bAwaitingNewHolderAim);
	TestTrue(TEXT("战前补位保留组根和幸存者身体位置"),
		Rod->GetGroupAnchorWorld().Equals(AnchorBeforeLeave, 0.001) && Cats[1]->GetActorLocation().Equals(SecondBeforeLeave, 0.001));
	Cats[1]->SetActorRotation(FRotator(0.0, 70.0, 0.0));
	Rod->RefreshHeldTransformFromAuthority();
	Rod->RefreshHeldTransformFromAuthority();
	FCatFishingRodRotationPrediction Prediction;
	TestTrue(TEXT("新主位战前正常瞄准，无需搏斗输入解锁"), Rod->GetRotationPredictionFromAuthority(0.05, Prediction)
		&& !Prediction.bHoldActualAim && Prediction.Input.RequestedAim.Equals(FRotator(0.0, 70.0, 0.0), 0.001));
	const FCatFishingGroupMotionState CurrentUnloaded = Rod->GroupMotionState;
	Rod->GroupMotionState = PreviousUnloaded;
	Rod->OnRep_CarrierConstraintState();
	const FCatExternalTractionInput Reconciled = Movements[1]->GetExternalTraction();
	TestTrue(TEXT("迟到战前快照不能恢复旧名单，当前成员等待完整新快照"), Reconciled.bGroupDriven && Reconciled.bWaitingForGroupSolve
		&& Reconciled.RosterVersion == Rod->GetRosterVersion() && Reconciled.ControlEpoch == Rod->GetControlEpoch());
	TestFalse(TEXT("迟到战前快照不能触发鱼力"), Reconciled.bActive);
	TestTrue(TEXT("等待不能继续使用旧共同速度"), Reconciled.GroupUnloadedVelocity.IsNearlyZero());
	Rod->GroupMotionState = CurrentUnloaded;
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("当前战前快照抵达后恢复无鱼力共同移动"), Movements[1]->GetExternalTraction().bUnloadedMovement
		&& !Movements[1]->GetExternalTraction().bWaitingForGroupSolve);

	for (int32 Index = 1; Index < Players.Num(); ++Index)
		if (!Rod->RemoveOperatorFromAuthority(Players[Index], Rod->GetPresentationState().RodActorRevision, Promoted)) return false;
	for (UCatCharacterMovementComponent* Movement : Movements)
		TestFalse(TEXT("最后一人离开后清理所有共同移动来源"), Movement->GetExternalTraction().bGroupDriven);
	const FVector AfterLeave = Cats[1]->GetActorLocation();
	Movements[1]->Velocity = FVector::ZeroVector;
	// 正常个人移动还需要输入入口计算 AnalogInputModifier，不能只直写 Acceleration。
	Movements[1]->MoveAutonomous(0.05f, 0.05f, 0, FVector::ForwardVector * Movements[1]->GetMaxAcceleration());
	TestTrue(TEXT("离队后真实CMC恢复个人移动"), Cats[1]->GetActorLocation().X > AfterLeave.X);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupHandoffAimTest,
	"Catfishing.Unit.Fishing.Runtime.GroupHandoffPreservesAimUntilNewDomainInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupHandoffAimTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	APlayerState* First = World->SpawnActor<APlayerState>();
	APlayerState* Second = World->SpawnActor<APlayerState>();
	ACatCharacter* FirstCat = World->SpawnActor<ACatCharacter>();
	ACatCharacter* SecondCat = World->SpawnActor<ACatCharacter>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!First || !Second || !FirstCat || !SecondCat || !Rod) return false;
	FirstCat->SetPlayerState(First);
	SecondCat->SetPlayerState(Second);
	FirstCat->SetActorRotation(FRotator(0.0, 20.0, 0.0));
	SecondCat->SetActorRotation(FRotator(0.0, 150.0, 0.0));
	SecondCat->SetActorLocation(FVector(0.0, 300.0, 0.0));
	if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("AimHandoffRod"), NAME_None, First, First, true, false)) return false;
	int32 Slot = INDEX_NONE;
	Rod->AddOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision, Slot);
	Rod->RefreshHeldTransformFromAuthority();
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 100.0);
	Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector);
	const FRotator PreviousAim = Rod->AuthoritativeHeldAimRotation;
	const FVector PreviousGrip = Rod->GetGripWorldTransform().GetLocation();
	const uint32 PreviousAimEpoch = Rod->CarrierConstraintState.AimInputEpoch;
	APlayerState* Promoted = nullptr;
	Rod->RemoveOperatorFromAuthority(First, Rod->GetPresentationState().RodActorRevision, Promoted);
	TestTrue(TEXT("接力进入等待新主输入状态"), Rod->bAwaitingNewHolderAim);
	for (int32 Tick = 0; Tick < 3; ++Tick) Rod->RefreshHeldTransformFromAuthority(0.02);
	TestTrue(TEXT("换主第二帧之后仍保持原实际竿向"), Rod->AuthoritativeHeldAimRotation.Equals(PreviousAim, 0.001));
	TestTrue(TEXT("换主握把位置连续"), Rod->GetGripWorldTransform().GetLocation().Equals(PreviousGrip, 0.001));
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 100.0);
	FCatFishingRodRotationPrediction Prediction;
	Rod->GetRotationPredictionFromAuthority(0.02, Prediction);
	TestTrue(TEXT("预测也明确锁住同一实际角度"), Prediction.bHoldActualAim && Prediction.Input.RequestedAim.Equals(PreviousAim, 0.001));
	FCatFishingRodAimSample Sample;
	Sample.RodActorId = Rod->GetPresentationState().RodActorId;
	Sample.InputEpoch = PreviousAimEpoch;
	Sample.Sequence = 1;
	Sample.bMouseActive = true;
	Sample.MouseStrokeSequence = 1;
	Sample.CumulativeLookDegrees = FVector2D(500.0, 0.0);
	Sample.MouseStrokeStartLookDegrees = Sample.CumulativeLookDegrees;
	TestFalse(TEXT("上一主位输入域不能接管"), Rod->AcceptHeldAimSampleFromAuthority(Second, Sample));
	Sample.InputEpoch = 0;
	TestFalse(TEXT("换主时未确认域的重设不能解锁竿向"), Rod->CanRebaseHeldAimFromAuthority(Second, Sample));
	Sample.InputEpoch = Rod->CarrierConstraintState.AimInputEpoch;
	TestTrue(TEXT("新域首次采样以当前实际角重定基"), Rod->AcceptHeldAimSampleFromAuthority(Second, Sample));
	TestFalse(TEXT("合法输入接管后解除等待"), Rod->bAwaitingNewHolderAim);
	Rod->RefreshHeldTransformFromAuthority(0.02);
	TestTrue(TEXT("第一份累计输入不产生换主跳转"), Rod->AuthoritativeHeldAimRotation.Equals(PreviousAim, 0.001));
	++Sample.Sequence;
	Sample.CumulativeLookDegrees.X += 10.0;
	TestTrue(TEXT("后续新输入生效"), Rod->AcceptHeldAimSampleFromAuthority(Second, Sample));
	Rod->RefreshHeldTransformFromAuthority(0.02);
	const double ActualChange = FMath::FindDeltaAngleDegrees(PreviousAim.Yaw, Rod->AuthoritativeHeldAimRotation.Yaw);
	TestTrue(TEXT("后续转向按受力模型连续响应"), ActualChange > 0.0 && ActualChange < 10.0);
	const FRotator BeforeReturn = Rod->AuthoritativeHeldAimRotation;
	TestTrue(TEXT("再次接力建立全新输入域"), Rod->SetOperatorFromAuthority(First, Rod->GetPresentationState().RodActorRevision));
	TestTrue(TEXT("权威为新主位重新发布搏斗约束"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 100.0));
	Sample.InputEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
	++Sample.Sequence;
	++Sample.MouseStrokeSequence;
	Sample.MouseStrokeStartLookDegrees = Sample.CumulativeLookDegrees;
	Sample.CumulativeLookDegrees.X += 5.0;
	TestTrue(TEXT("接力首包同时移动鼠标也被接收"), Rod->AcceptHeldAimSampleFromAuthority(First, Sample));
	Rod->GetRotationPredictionFromAuthority(0.02, Prediction);
	TestTrue(TEXT("首包从实际杆向保留本段5度增量且不瞬移姿态"), Prediction.Input.bCatDriveActive
		&& Prediction.Input.CurrentAim.Equals(BeforeReturn, 0.001));
	TestEqual(TEXT("接力初始化不吞掉首帧鼠标"),
		FMath::FindDeltaAngleDegrees(BeforeReturn.Yaw, Prediction.Input.RequestedAim.Yaw), 5.0, 1e-7);
	return !HasAnyErrors();
}

#endif
