#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"

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
#endif
