#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCMCStabilityTest,
    "Catfishing.Unit.Fishing.PhysicalRod.CMCHistoricalShortLineStabilityAndReadOnlyPrediction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingCMCStabilityTest::RunTest(const FString& Parameters)
{
    const auto* Definition = LoadObject<UCatEquipmentDefinition>(nullptr,
        TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
    if (!TestNotNull(TEXT("formal rod calibration"), Definition)) return false;
    for (double FishMass : {5.535, 15.0}) for (int32 Rate : {20, 60, 120})
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.World.CreateTestWorld(EWorldType::Game)) return false;
        Scene.World.ForwardErrorMessages(this);
        UWorld* World = Scene.World.GetTestWorld();
        FURL URL; URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
        if (!World->SetGameMode(URL)) return false;
        Scene.Floor = Scene.AddBox(FVector(0,0,-10), FVector(3000,3000,10));
        if (!Scene.World.BeginPlayInTestWorld()) return false;
        auto* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
        GameMode->bRunCommandsOpen = true;
        GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
        GameMode->RunPublicState.Phase.bFishingAllowed = true;
        auto* Cat = Scene.SpawnCat(FVector(0,0,20));
        auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
        auto* Player = World->SpawnActor<ACatfishingPlayerState>();
        Controller->PlayerState = Player; Cat->SetPlayerState(Player); Controller->Possess(Cat);
        Player->SetPlayerId(1);
        const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("CMCStability"), FName(TEXT("CAT_TEST")));
        Player->SetUniqueId(FUniqueNetIdRepl(NetId));
        ACatfishingGameModeBase::FAdmissionRecord Admission;
        Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active; Admission.Controller = Controller;
        GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
        auto* Body = Cat->GetPhysicalBodyComponent();
        auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
        auto* ASC = Cat->GetCatAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(),100);
        ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(),100);
        ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(),50);
        const FRotator InitialAim(-25.38,290.63,0);
        Controller->SetControlRotation(InitialAim); Body->SetViewIntent(InitialAim);
        const auto Tick = [&](float Dt) { Body->SetMoveIntent(FVector::ZeroVector); Scene.World.TickTestWorld(Dt); };
        for (int32 I=0; I<Rate/2; ++I) Tick(1.f/Rate);
        auto* Rod = World->SpawnActorDeferred<ACatFishingRodActor>(ACatFishingRodActor::StaticClass(),FTransform::Identity);
        if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(Definition->FindFragment<UCatEquipmentFragment_Rod>()->RodTipLocalTransform,
            Definition->FindFragment<UCatEquipmentFragment_Rod>()->StandLocalTransform,Definition->FindFragment<UCatEquipmentFragment_Rod>()->GripLocalTransform)
            || !Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(),FGuid::NewGuid(),TEXT("ShortCMC"),NAME_None,Player,nullptr,true,false)) return false;
        Rod->FinishSpawning(FTransform::Identity);
        if (!Rod->BeginPhysicalHoldFromAuthority(Player,true)
            || !Rod->SetPrimaryOperatorFromAuthority(Player,Rod->GetPresentationState().RodActorRevision)
            || !Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Player)) return false;
        auto* Session = World->SpawnActor<ACatFishingSession>();
        const FGuid SessionId = FGuid::NewGuid();
        Session->Snapshot.FishingSessionId = SessionId; Session->Snapshot.RodActor = Rod;
        auto* Service = World->GetSubsystem<UCatFishingService>(); Service->Sessions.Add(SessionId,Session);
        auto* Receiver = Rod->GetPhysicalRodComponent();
        Rod->SetFightConstraintObservationFromAuthority(FVector(0,-1,0),0,0,true,0,50);
        FCatFightSimulationConfig C;
        C.FixedStepSeconds=.05; C.PrimaryOperatorCatStrength=50; C.PrimaryOperatorMassKilograms=Movement->Mass;
        C.FishMassKilograms=FishMass; C.FishStrength=FishMass*10; C.CatStaminaMaximum=100;
        C.ReelSpeedCentimetersPerSecond=80; C.FishFullEffortSpeedCentimetersPerSecond=180;
        C.MaximumLineLengthCentimeters=1000; C.RodDurability=100000;
        FCatFightSimulationState S; S.CatStamina=S.FishStamina=100;
        S.MotionIntent=ECatFishMotionIntent::StrugglingOutward;
        S.FishWorldPosition=Rod->GetRodTipWorldTransform().GetLocation()+FVector(-55.8,-100,-148.88);
        S.LineLengthCentimeters=FVector::Distance(S.FishWorldPosition,Rod->GetRodTipWorldTransform().GetLocation());
        double MinLoad=DBL_MAX,MaxLoad=0,MinSpeed=DBL_MAX,MaxSpeed=0;
        int32 Unloaded=0; uint64 StepNumber=0;
        for (int32 Frame=0; Frame<Rate*6; ++Frame)
        {
            if (Frame % (Rate/20)==0)
            {
                FCatFightRodConstraintInput Input; Input.bRodHeld=true;
                Input.RodForwardWorld=Rod->GetAuthoritativeRodForwardVector();
                Input.CarrierVelocityCentimetersPerSecond=Body->GetVelocity();
                Receiver->PopulateEndpointResponse(Input);
                const FTransform BeforeCat=Cat->GetActorTransform(),BeforeRod=Rod->GetPhysicalRodBody()->GetComponentTransform();
                const auto BeforeEffort=Rod->GetAuthoritativeRotationEffortSnapshot();
                const FGuid BeforeGrip=Body->GetGrab()->GetGripState(true).GripId;
                const auto Step=FCatFishingFightSimulator::Step(C,S,Input,FVector(0,-1,0));
                if (!TestTrue(TEXT("historical short-line solve uses CMC candidate"),Step.bSucceeded && Step.Trace.bCMCEndpointPredicted)) return false;
                if (Frame==Rate*2)
                {
                    const auto Repeated=FCatFishingFightSimulator::Step(C,S,Input,FVector(0,-1,0));
                    const auto AfterEffort=Rod->GetAuthoritativeRotationEffortSnapshot();
                    TestTrue(TEXT("candidate loop is repeatable and never writes pose, grip, effort or stamina"),
                        Repeated.ProposedFishWorldPosition.Equals(Step.ProposedFishWorldPosition,1.e-8)
                        && BeforeCat.Equals(Cat->GetActorTransform()) && BeforeRod.Equals(Rod->GetPhysicalRodBody()->GetComponentTransform())
                        && BeforeGrip==Body->GetGrab()->GetGripState(true).GripId
                        && BeforeEffort.ExertionSquaredSeconds==AfterEffort.ExertionSquaredSeconds
                        && BeforeEffort.IntegratedSeconds==AfterEffort.IntegratedSeconds
                        && ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())==100);
                }
                S.FishWorldPosition=Step.ProposedFishWorldPosition; S.FishVelocityCentimetersPerSecond=Step.ResolvedFishVelocityCentimetersPerSecond;
                S.LineLengthCentimeters=Step.LineLengthCentimeters;
                Rod->SetFightConstraintObservationFromAuthority(Step.RodLineForceNewtons.GetSafeNormal(),Step.NormalizedTension,
                    Step.ConstraintErrorCentimeters,true,Step.LineTensionNewtons/C.ForcePerStrengthNewtons*C.RodPhysicsLengthCentimeters/100,C.PrimaryOperatorCatStrength,Step.RodLineForceNewtons.GetSafeNormal());
                Receiver->SetLineLoad(SessionId,++StepNumber,Step.RodLineForceNewtons,C.FixedStepSeconds,.15);
                if (Frame>=Rate*2) { MinLoad=FMath::Min(MinLoad,Step.LineTensionNewtons); MaxLoad=FMath::Max(MaxLoad,Step.LineTensionNewtons); Unloaded+=Step.LineTensionNewtons<.01; }
            }
            Tick(1.f/Rate);
            if (Frame>=Rate*2) { MinSpeed=FMath::Min(MinSpeed,Body->GetVelocity().Size2D()); MaxSpeed=FMath::Max(MaxSpeed,Body->GetVelocity().Size2D()); }
        }
        AddInfo(FString::Printf(TEXT("Event=fishing_cmc_history_verified MassKg=%.3f Hz=%d MinN=%.4f MaxN=%.4f UnloadedSteps=%d MinSpeedCmS=%.4f MaxSpeedCmS=%.4f"),FishMass,Rate,MinLoad,MaxLoad,Unloaded,MinSpeed,MaxSpeed));
        TestEqual(TEXT("steady outward fight has no periodic complete unloading"),Unloaded,0);
        TestTrue(TEXT("loaded body keeps moving"),MinSpeed>5);
        TestTrue(TEXT("steady load and speed settle without repeated kicks"),MaxLoad-MinLoad<5 && MaxSpeed-MinSpeed<10);
        FCatFightRodConstraintInput Input; Input.bRodHeld=true;
        Input.RodForwardWorld=Rod->GetAuthoritativeRodForwardVector();
        Input.CarrierVelocityCentimetersPerSecond=Body->GetVelocity();
        Receiver->PopulateEndpointResponse(Input);
        auto GeometryState=S;
        GeometryState.FishWorldPosition=Input.RodTipWorldPosition+FVector(300,0,0);
        GeometryState.FishVelocityCentimetersPerSecond=FVector::ZeroVector;
        GeometryState.LineLengthCentimeters=300;
        const auto Exact=FCatFishingFightSimulator::Step(C,GeometryState,Input,FVector::ForwardVector);
        GeometryState.FishWorldPosition.X+=20;
        const auto Repaired=FCatFishingFightSimulator::Step(C,GeometryState,Input,FVector::ForwardVector);
        TestTrue(TEXT("historical line error repairs geometry without injecting fish velocity"),Exact.bSucceeded && Repaired.bSucceeded
            && Repaired.Trace.FishPositionCorrectionCentimeters>0
            && Exact.ResolvedFishVelocityCentimetersPerSecond.Equals(Repaired.ResolvedFishVelocityCentimetersPerSecond,1.e-5));
        GeometryState.CatAction=ECatFightCatAction::Slack;
        const auto Slack=FCatFishingFightSimulator::Step(C,GeometryState,Input,FVector::ForwardVector);
        TestTrue(TEXT("genuine free spool still unloads the joint prediction"),Slack.bSucceeded && Slack.LineTensionNewtons==0);
        Receiver->ClearLineLoad(SessionId); Service->Sessions.Reset(); Session->Snapshot.Phase=ECatFishingPhase::Terminated;
    }
    return !HasAnyErrors();
}
#endif
