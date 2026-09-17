#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPredictionResetClockTest,
    "Catfishing.CMC.Contract.ControlResetDropsOldMovesWithoutRewindingClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPredictionResetClockTest::RunTest(const FString& Parameters)
{
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* Cat = Scene.SpawnCat(FVector(0,0,20));
    if (!Cat) return false;
    auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
    TestTrue(TEXT("rod and reaction consumers wait for native movement rather than the retired body motor"),
        &Cat->GetPhysicalBodyComponent()->GetPostMovementTick() == &Movement->PrimaryComponentTick);
    Cat->SetRole(ROLE_AutonomousProxy);
    auto* Data = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
    Data->CurrentTimeStamp = 47.5f;
    Data->SavedMoves.Add(Data->AllocateNewMove());
    Data->PendingMove = Data->SavedMoves[0];
    Data->LastAckedMove = Data->SavedMoves[0];
    Data->bUpdatePosition = true;
    Movement->ResetControlPrediction();
    TestEqual(TEXT("teleport keeps the network timestamp accepted by the server"), Data->CurrentTimeStamp, 47.5f);
    TestTrue(TEXT("no previous control input can be resent or replayed"), Data->SavedMoves.IsEmpty()
        && !Data->PendingMove.IsValid() && !Data->LastAckedMove.IsValid() && !Data->bUpdatePosition);
    Cat->SetRole(ROLE_Authority);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPredictionSavedSprintTest,
    "Catfishing.CMC.Contract.SavedMovePreservesSprintAndControlEpoch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPredictionSavedSprintTest::RunTest(const FString& Parameters)
{
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* Cat = Scene.SpawnCat(FVector(0,0,20));
    if (!Cat) return false;
    auto* Body = Cat->GetPhysicalBodyComponent();
    auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
    auto* Data = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
    Movement->SetSprintIntent(true); Body->SetMovementSpeed(350);
    FSavedMovePtr Move = Data->AllocateNewMove();
    Move->SetMoveFor(Cat, 1.0f/60, FVector(6000,0,0), *Data);
    Movement->SetSprintIntent(false); Body->SetMovementSpeed(100);
    Move->PrepMoveFor(Cat);
    TestTrue(TEXT("replay restores the historical sprint flag"), Movement->WantsSprint());
    TestEqual(TEXT("replay restores the historical cm/s speed"), Body->MaxMovementSpeedCmS, 350.0);
    TestTrue(TEXT("sprint travels in CMC compressed input"), (Move->GetCompressedFlags() & FSavedMove_Character::FLAG_Custom_0) != 0);
    FCatNetworkMoveData Packet;
    Packet.ClientFillNetworkMoveData(*Move, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
    TestEqual(TEXT("packet binds movement to the saved control epoch"), Packet.ControlEpoch, Body->GetControlEpoch());
    Body->BeginControlEpochFromAuthority();
    TestNotEqual(TEXT("a late packet remains distinguishable after control reset"), Packet.ControlEpoch, Body->GetControlEpoch());
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPredictionLoadResponseTest,
    "Catfishing.CMC.Contract.MoveResponseRebasesOldLoadWithoutRewindingNewSamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPredictionLoadResponseTest::RunTest(const FString&)
{
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* Cat = Scene.SpawnCat(FVector(0,0,20));
    if (!Cat) return false;
    auto* Body = Cat->GetPhysicalBodyComponent();
    auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
    Cat->SetRole(ROLE_AutonomousProxy);
    auto* Data = Movement->GetPredictionData_Client_Character();
    for (int32 Index=0; Index<3; ++Index)
    {
        FSavedMovePtr Move = Data->AllocateNewMove();
        Move->SetMoveFor(Cat, 1.0f/60, FVector(6000,0,0), *Data);
        Move->TimeStamp = float(Index+1);
        auto& Saved = static_cast<FCatSavedMove&>(*Move);
        Saved.PolicyServerSeconds = Index==2 ? 10 : 0;
        Saved.ExternalForce = FVector(900,0,0);
        Data->SavedMoves.Add(Move);
    }
    Data->CurrentTimeStamp = 3;
    FCatMoveResponseDataContainer Response;
    Response.ClientAdjustment.bAckGoodMove = true;
    Response.ClientAdjustment.TimeStamp = 1;
    Response.bHasPolicy = true;
    Response.Policy.ControlEpoch = Body->GetControlEpoch();
    Response.Policy.ServerSeconds = 5;
    Response.Policy.ExternalForce = FVector(-2500,0,0);
    Response.Policy.Drive.bLocomotion = Response.Policy.Drive.bCooperative = Response.Policy.Drive.bConnected = true;
    Response.Policy.Drive.bPassiveBodyContact = Response.Policy.Drive.bBodyContactDriven = true;
    Response.Policy.Drive.MaxForce = 5000; Response.Policy.Drive.MaxSpeed = 100;
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    TestTrue(TEXT("authority load response serializes"), Response.Serialize(*Movement, Writer, nullptr));
    FCatMoveResponseDataContainer Received;
    FMemoryReader Reader(Bytes);
    TestTrue(TEXT("load response deserializes"), Received.Serialize(*Movement, Reader, nullptr));
    TestEqual(TEXT("policy binds to the response move timestamp"), Received.Policy.MoveTime, 1.0f);
    Movement->ClientHandleMoveResponse(Received);
    TestEqual(TEXT("native CMC acknowledges only the completed move"), Data->SavedMoves.Num(), 2);
    if (Data->SavedMoves.Num()==2)
    {
        const auto& Old = static_cast<const FCatSavedMove&>(*Data->SavedMoves[0]);
        const auto& New = static_cast<const FCatSavedMove&>(*Data->SavedMoves[1]);
        TestEqual(TEXT("unacknowledged old policy uses corrected reciprocal load"), Old.ExternalForce, FVector(-2500,0,0));
        TestTrue(TEXT("contact motor flags travel with the same load"), Old.Drive.bPassiveBodyContact && Old.Drive.bBodyContactDriven && Old.Drive.bCooperative);
        TestEqual(TEXT("newer observed load is never overwritten by an older response"), New.ExternalForce, FVector(900,0,0));
        Received.ClientAdjustment.TimeStamp = Received.Policy.MoveTime = 2;
        ++Received.Policy.ControlEpoch;
        Movement->ClientHandleMoveResponse(Received);
        TestEqual(TEXT("a different control epoch cannot acknowledge or replay old movement"), Data->SavedMoves.Num(), 2);
    }
    Movement->ResetControlPrediction();
    FCatBodyDriveSample ResetDrive; FVector ResetForce; double ResetTime;
    Movement->GetPredictionPolicy(ResetDrive, ResetForce, ResetTime);
    TestEqual(TEXT("control reset drops the previous response load"), ResetForce, Body->GetReplicatedExternalForce());
    TestTrue(TEXT("control reset clears cosmetic correction"), Movement->GetOwnerCorrectionVisualOffset().IsZero());
    Cat->SetRole(ROLE_Authority);
    return !HasAnyErrors();
}

#endif
