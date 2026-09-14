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
	Scene.Step(60);
	TestTrue(TEXT("unloaded recovery begins immediately at five points per second"), FMath::IsNearlyEqual(Stamina(Weak), 5.0, 0.02));
	const double BeforeReload = Stamina(Weak);
	Weak->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(0,0,-1000), true);
	Scene.Step(60);
	TestEqual(TEXT("renewed load pauses recovery"), Stamina(Weak), BeforeReload);
	Weak->GetPhysicalBodyComponent()->ClearExternalForce(Scene.Floor);
	Scene.Step(60);
	TestTrue(TEXT("unloading resumes without a second waiting gate"), FMath::IsNearlyEqual(Stamina(Weak), BeforeReload + 5.0, 0.03));
	TestTrue(TEXT("any positive balance restores force without a twenty-percent gate"), Effort->GetMaximumForceKgCmS2() > 0);
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
	"Catfishing.PhysicalEffort.Runtime.RealGripOppositionAndZeroStamina", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
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
	TestEqual(TEXT("the last tiny positive balance is fully charged"), Stamina(Puller), 0.0);
	TestTrue(TEXT("zero stamina keeps the real grip but supplies no motor"),
		First->GetGrab()->IsGripping(true) && First->CaptureDriveSample().MaxForce == 0.0);
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
	A->GetPhysicalBodyComponent()->SetExternalForceFromAuthority(Scene.Floor, FVector(0,0,-1000), true);
	Scene.Step(60);
	const FVector ZeroStart=B->GetActorLocation();
	A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
	Scene.Step(60);
	TestTrue(TEXT("a zero-stamina key press cannot manufacture a contact motor"),FVector::Dist2D(ZeroStart,B->GetActorLocation())<5);
	return !HasAnyErrors();
}
#endif
