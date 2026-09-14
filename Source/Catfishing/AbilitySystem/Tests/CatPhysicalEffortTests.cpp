#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"

namespace CatPhysicalEffortTest
{
void Seed(ACatCharacter* Cat, float Strength, float Stamina = 60)
{
	auto* ASC = Cat->GetCatAbilitySystemComponent();
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), Strength);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), Stamina);
}
double Stamina(ACatCharacter* Cat)
{
	return Cat->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalSupportEffortTest,
	"Catfishing.PhysicalEffort.Runtime.SupportStrengthAndImmediateRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalSupportEffortTest::RunTest(const FString&)
{
	using namespace CatPhysicalEffortTest;
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Weak = Scene.SpawnCat(FVector(0,0,20));
	auto* Strong = Scene.SpawnCat(FVector(0,200,20));
	if (!Weak || !Strong) return false;
	Seed(Weak, 20); Seed(Strong, 80);
	Scene.Step(60);
	const FVector WeakStart = Weak->GetActorLocation(), StrongStart = Strong->GetActorLocation();
	for (auto* Cat : {Weak,Strong}) Cat->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(1000,0,0));
	Scene.Step(60);
	TestTrue(TEXT("both finite budgets can support a 10 N horizontal load without a position lock"),
		FVector::Dist2D(WeakStart,Weak->GetActorLocation()) < 1 && FVector::Dist2D(StrongStart,Strong->GetActorLocation()) < 1);
	const double WeakPaid = 60-Stamina(Weak), StrongPaid=60-Stamina(Strong);
	TestTrue(TEXT("a stationary loaded helper spends its own stamina"), WeakPaid > .5 && StrongPaid > .1);
	TestTrue(TEXT("the stronger cat needs less relative support effort for the same load"), WeakPaid > StrongPaid*2.5);
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(4000,0,0));
	Scene.Step(30);
	TestTrue(TEXT("an overloaded stance yields to physical travel"), Weak->GetActorLocation().X > WeakStart.X+5);
	Seed(Weak,20,.001f);
	Scene.Step(1);
	auto* Effort = Weak->FindComponentByClass<UCatPhysicalEffortComponent>();
	TestTrue(TEXT("actual GAS payment exhausts the helper and closes its active force budget"),
		Stamina(Weak)==0 && Weak->GetPhysicalBodyComponent()->CaptureDriveSample().MaxForce==0);
	const FVector ExhaustedStart = Weak->GetActorLocation();
	Scene.Step(15);
	TestTrue(TEXT("an exhausted stationary cat yields instead of supplying its old strength"),
		Weak->GetActorLocation().X>ExhaustedStart.X+5 && Stamina(Weak)==0 && Effort->GetMaximumForceKgCmS2()==0);
	const double ExhaustedTravel = Weak->GetActorLocation().X - ExhaustedStart.X;
	// Start the following long recovery cases at rest on the floor. Do not let the preceding
	// deliberate overload carry the fixture off its finite platform during the waiting periods.
	Weak->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(WeakStart), TEXT("LoadedRecoveryFixture"));
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(1000,0,0));
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Strong, FVector(-1000,0,0));
	Scene.Step(180);
	TestTrue(TEXT("opposite loads cannot restore stamina or strength even when their sum is zero"),
		Weak->GetPhysicalBodyComponent()->GetExternalForceFromAuthority().IsNearlyZero()
		&& Weak->GetPhysicalBodyComponent()->HasExternalLoadFromAuthority() && Stamina(Weak)==0 && Effort->GetMaximumForceKgCmS2()==0);
	Weak->GetPhysicalBodyComponent()->ClearExternalForce(Strong);
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(0,0,-1000), true);
	Scene.Step(180);
	TestEqual(TEXT("pure vertical traction also blocks recovery after exhaustion"), Stamina(Weak), 0.0);
	Weak->GetPhysicalBodyComponent()->ClearExternalForce(Scene.Floor);
	// 墓碑（2026-09-14）：裁决⑥删除延迟恢复与 20% 再入的旧断言；负载阻断仍须验证。
	Scene.Step(30);
	TestTrue(TEXT("unloaded recovery starts within one period without a reentry threshold"), Stamina(Weak)>0 && Stamina(Weak)<12 && Effort->GetMaximumForceKgCmS2()>0);
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(0,0,-1000), true);
	const double LoadedBalance = Stamina(Weak);
	Scene.Step(60);
	TestEqual(TEXT("a renewed load blocks the running recovery channel"), Stamina(Weak), LoadedBalance);
	Weak->GetPhysicalBodyComponent()->ClearExternalForce(Scene.Floor);
	Scene.Step(30);
	TestTrue(TEXT("removing load resumes recovery without a new delay"), Stamina(Weak)>LoadedBalance);
	const double BeforeImpulse = Stamina(Weak);
	for (int32 Frame=0; Frame<180; ++Frame)
	{
		Weak->GetPhysicalBodyComponent()->AddExternalImpulseFromAuthority(FVector(10,0,0));
		Weak->GetPhysicalBodyComponent()->AddExternalImpulseFromAuthority(FVector(-10,0,0));
		Scene.Step(1);
	}
	TestEqual(TEXT("cancelling queued impulses also block an already recovering cat"), Stamina(Weak), BeforeImpulse);
	TestTrue(TEXT("the recovery cases remain on their actual supporting floor"), Weak->GetPhysicalBodyComponent()->IsGrounded());
	AddInfo(FString::Printf(TEXT("Event=physical_effort_support_verified WeakPaid=%.5f StrongPaid=%.5f ExhaustedTravelCm=%.3f Recovered=%.5f Evidence=runtime_behavior"),
		WeakPaid,StrongPaid,ExhaustedTravel,Stamina(Weak)));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalGripEffortTest,
	"Catfishing.PhysicalEffort.Runtime.RealGripOppositionAndRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalGripEffortTest::RunTest(const FString&)
{
	using namespace CatPhysicalEffortTest;
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Puller=Scene.SpawnCat(FVector(0,0,20));
	auto* Partner=Scene.SpawnCat(FVector(35,0,20));
	if (!Puller || !Partner) return false;
	Seed(Puller,60); Seed(Partner,20);
	Scene.Step(60);
	auto* First=Puller->GetPhysicalBodyComponent();
	auto* Second=Partner->GetPhysicalBodyComponent();
	First->SetViewIntent(FRotator::ZeroRotator);
	First->GetGrab()->SetGrabInput(true,true);
	First->GetGrab()->SetGrabInput(false,true);
	Scene.Step(60);
	if (!TestTrue(TEXT("two real hands establish contact with the partner"),
		First->GetGrab()->IsGripping(true) && First->GetGrab()->IsGripping(false))) return false;
	const double OneBodyBudget=First->CaptureDriveSample().MaxForce;
	TestEqual(TEXT("two hands do not double the cat's one motor budget"),OneBodyBudget,6000.0);
	First->SetMoveIntent(-FVector::ForwardVector);
	Second->SetMoveIntent(FVector::ForwardVector);
	const FVector Start=Partner->GetActorLocation();
	const double Before=Stamina(Puller), PartnerBefore=Stamina(Partner);
	int32 Writes=0, Frames=0;
	const auto Handle=Puller->GetCatAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda(
		[&](const FOnAttributeChangeData&){++Writes;});
	for (;Frames<90;++Frames) Scene.Step(1);
	Puller->GetCatAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(Handle);
	TestTrue(TEXT("the stronger cat drags the resisting partner through the actual grip"),Partner->GetActorLocation().X<Start.X-10);
	TestTrue(TEXT("each active cat pays its own failed intention"),Stamina(Puller)<Before && Stamina(Partner)<PartnerBefore);
	TestTrue(TEXT("two hands never create two ASC charges in one movement frame"),Writes>0 && Writes<=Frames);
	First->GetGrab()->ReleaseHandFromAuthority(false,TEXT("EffortTestOneHand"));
	TestEqual(TEXT("one hand retains the same cat strength budget"),First->CaptureDriveSample().MaxForce,OneBodyBudget);
	Seed(Puller,60,.00001f);
	Scene.Step(2);
	// 墓碑（2026-09-14）：裁决⑥删除耗尽自动松手/禁止重抓的断言；抓握与出力分离。
	TestTrue(TEXT("empty stamina keeps the existing grip but closes its motor budget"),
		Stamina(Puller)==0 && First->GetGrab()->IsGripping(true)
		&& Puller->FindComponentByClass<UCatPhysicalEffortComponent>()->GetMaximumForceKgCmS2()==0);
	First->SetMoveIntent(FVector::ZeroVector);
	Second->SetMoveIntent(FVector::ZeroVector);
	First->GetGrab()->ReleaseAllFromAuthority(TEXT("ZeroStaminaRegripFixture"));
	First->TeleportBodyFromAuthority(FTransform(FVector(0,0,20)), TEXT("ZeroGripFixture"));
	Second->TeleportBodyFromAuthority(FTransform(FVector(35,0,20)), TEXT("ZeroTargetFixture"));
	First->SetViewIntent(FRotator::ZeroRotator);
	Seed(Puller,60,0);
	// 让正式伸手扫描找到表面；不能把目标中心冒充已经与手相贴的接触点。
	First->SetExternalForceFromAuthority(Scene.Floor,FVector(0,0,-100),true);
	First->GetGrab()->SetGrabInput(true,true);
	Scene.Step(60);
	TestEqual(TEXT("real load keeps the regrip fixture at zero stamina"),Stamina(Puller),0.0);
	TestTrue(TEXT("zero stamina can establish a new real grip through reach input"),First->GetGrab()->IsGripping(true));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalYellowRecoveryTest,
	"Catfishing.PhysicalEffort.Runtime.YellowPoolAndUnarmedRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalYellowRecoveryTest::RunTest(const FString&)
{
	using namespace CatPhysicalEffortTest;
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Cat = Scene.SpawnCat(FVector(0,0,20));
	if (!Cat) return false;
	auto* ASC = Cat->GetCatAbilitySystemComponent();
	auto* Effort = Cat->FindComponentByClass<UCatPhysicalEffortComponent>();
	Seed(Cat,20,0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(),100);
	Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
	Scene.Step(60);
	TestTrue(TEXT("walking without prior effort restores five points in the first second"), Stamina(Cat)>=4.0 && Stamina(Cat)<=5.1);
	Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
	Scene.Step(1155);
	TestEqual(TEXT("a never-armed empty 100-point green pool fills in approximately twenty seconds"), Stamina(Cat),100.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(),1);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute(),80);
	FCatBodyDriveSample Drive;
	Drive.bCooperative=Drive.bLocomotion=true;
	Drive.MaxForce=Effort->GetMaximumForceKgCmS2();
	Effort->SettleMovementFromAuthority(Drive,FVector(100,0,0),FVector::ZeroVector,1,true);
	TestEqual(TEXT("physical effort spends green first"),Stamina(Cat),0.0);
	TestEqual(TEXT("physical effort overflow reaches yellow"),ASC->GetYellowFightStamina(),79.0f);
	TestEqual(TEXT("payment receipt includes both pools"),Effort->GetLastPaid(),2.0);
	TestEqual(TEXT("yellow-only balance still provides full strength"),Effort->GetMaximumForceKgCmS2(),2000.0);
	Drive = {};
	Effort->SettleMovementFromAuthority(Drive,FVector::ZeroVector,FVector::ZeroVector,1,false);
	Scene.Step(60);
	TestTrue(TEXT("recovery does not need a locomotion pose at its entry"),Stamina(Cat)>0);
	TestEqual(TEXT("natural recovery never refills yellow"),ASC->GetYellowFightStamina(),79.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalIntentProjectionTest,
	"Catfishing.PhysicalEffort.Contract.SignedProjectionAndSubdivision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalIntentProjectionTest::RunTest(const FString&)
{
	FCatIntentMotionInput Input;
	Input.IntendedDisplacementCentimeters=FVector(10,0,0);
	Input.StaminaPerUnfulfilledMeter=2;
	FCatIntentMotionResult Result;
	for (double Progress : {-3.0,0.0,6.0,10.0,15.0})
	{
		Input.ActualDisplacementCentimeters=FVector(Progress,80,0);
		TestTrue(TEXT("directional model accepts sideways and reverse movement"),FCatIntentMotionModel::ComputeDrain(Input,Result));
		TestEqual(TEXT("sideways travel never pays off forward intention"),Result.UnfulfilledDistanceCentimeters,FMath::Max(0.0,10-Progress));
		const double Whole=Result.StaminaDrain;
		Input.IntendedDisplacementCentimeters/=2;Input.ActualDisplacementCentimeters/=2;
		FCatIntentMotionModel::ComputeDrain(Input,Result);
		TestEqual(TEXT("subdivision does not multiply time or price twice"),2*Result.StaminaDrain,Whole,1e-10);
		Input.IntendedDisplacementCentimeters*=2;
	}
	Input.IntendedDisplacementCentimeters=FVector::ZeroVector;
	FCatIntentMotionModel::ComputeDrain(Input,Result);
	TestEqual(TEXT("pure passive movement creates no active effort"),Result.StaminaDrain,0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalPeerStalemateTest,
	"Catfishing.PhysicalEffort.Runtime.EqualStrengthContactAndZeroForce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalPeerStalemateTest::RunTest(const FString&)
{
	using namespace CatPhysicalEffortTest;
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* A=Scene.SpawnCat(FVector(0,0,20)); auto* B=Scene.SpawnCat(FVector(28,0,20));
	if (!A || !B) return false;
	Seed(A,20); Seed(B,20);
	Scene.Step(60);
	const FVector Before=B->GetActorLocation();
	A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
	B->GetPhysicalBodyComponent()->SetMoveIntent(-FVector::ForwardVector);
	Scene.Step(120);
	TestTrue(TEXT("equal actively opposing strength can hold a real peer contact without a position lock"),FVector::Dist2D(Before,B->GetActorLocation())<5);
	TestTrue(TEXT("both actively opposing movers spend personal stamina"),Stamina(A)<59 && Stamina(B)<59);
	A->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FVector(0,0,20)),TEXT("ZeroPushFixture"));
	B->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FVector(28,0,20)),TEXT("ZeroPeerFixture"));
	Seed(A,20,0); Seed(B,20);
	A->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(0,0,-100), true);
	Scene.Step(60);
	const FVector ZeroStart=B->GetActorLocation();
	A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
	Scene.Step(60);
	TestTrue(TEXT("a zero-stamina key press cannot manufacture a contact motor"),FVector::Dist2D(ZeroStart,B->GetActorLocation())<5);
	return !HasAnyErrors();
}
#endif
