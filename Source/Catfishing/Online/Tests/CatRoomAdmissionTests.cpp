#if WITH_DEV_AUTOMATION_TESTS
#include "Online/CatRoomAdmission.h"
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomAdmissionContractTest, "Catfishing.Online.Rooms.AdmissionContract",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatRoomAdmissionContractTest::RunTest(const FString& Parameters)
{
 UCatRoomAdmission* Admission = NewObject<UCatRoomAdmission>(NewObject<UGameInstance>());
 Admission->HostLobby = TEXT("fixture"); Admission->HostOwner = TEXT("host");
 Admission->Code = TEXT("ABC234"); Admission->Password = TEXT("room secret"); Admission->MaxPlayers = 3;
 TestTrue(TEXT("Transport without a verified connection cannot claim a peer"), UCatRoomAdmission::PeerIdentity(nullptr).IsEmpty());
 TestEqual(TEXT("Ordinary entry requires password"), Admission->AuthorizePeer(TEXT("alice"), TEXT(""), false), ECatOnlineError::PasswordRequired);
 TestEqual(TEXT("Wrong password rejected"), Admission->AuthorizePeer(TEXT("alice"), TEXT("wrong"), false), ECatOnlineError::PasswordIncorrect);
 TestEqual(TEXT("Partial route does not authorize"), Admission->AuthorizePeer(TEXT("alice"), TEXT("ABC"), true), ECatOnlineError::InvalidInviteCode);
 TestFalse(TEXT("Rejected attempt never consumes a seat"), Admission->IsAuthorized(TEXT("alice")));
 TestEqual(TEXT("Full code bypasses password, case insensitive"), Admission->AuthorizePeer(TEXT("alice"), TEXT("abc234"), true), ECatOnlineError::None);
 Admission->Invite(TEXT("bob"));
 TestEqual(TEXT("Host-directed invite bypasses password"), Admission->AuthorizePeer(TEXT("bob"), TEXT(""), false), ECatOnlineError::None);
 TestEqual(TEXT("Concurrent final-seat reservation is authoritative"), Admission->AuthorizePeer(TEXT("eve"), TEXT("room secret"), false), ECatOnlineError::SessionFull);
 TestFalse(TEXT("A full-room refusal never issues a grant"), Admission->IsAuthorized(TEXT("eve")));
 Admission->Grants.FindChecked(TEXT("alice")).Expires = FPlatformTime::Seconds() - 1;
 TestFalse(TEXT("Expired reservation is not authorization"), Admission->IsAuthorized(TEXT("alice")));
 TestEqual(TEXT("Expired reservation releases capacity"), Admission->AuthorizePeer(TEXT("eve"), TEXT("room secret"), false), ECatOnlineError::None);
 for (int32 Index = 0; Index < 5; ++Index) { Admission->AuthorizePeer(TEXT("attacker"), TEXT("wrong"), false); }
 TestEqual(TEXT("Per-peer limit covers repeated guessing"), Admission->AuthorizePeer(TEXT("attacker"), TEXT("room secret"), false), ECatOnlineError::AdmissionRateLimited);
 TestEqual(TEXT("Cannot reduce capacity below reservations"), Admission->ValidateSettings(TEXT("Room"), 2, TEXT("")), ECatOnlineError::RoomSettingsInvalid);
 Admission->StopHost();
 TestFalse(TEXT("A new room cannot inherit grants"), Admission->IsAuthorized(TEXT("bob")));
 TestTrue(TEXT("Closing clears code and password"), Admission->GetCode().IsEmpty() && !Admission->HasPassword());
 FString Normalized;
 TestFalse(TEXT("Ambiguous symbols rejected"), UCatRoomAdmission::NormalizeCode(TEXT("AB01IO"), Normalized));
 TestFalse(TEXT("Long input rejected"), UCatRoomAdmission::NormalizeCode(FString::ChrN(1000, TEXT('A')), Normalized));
 for (int32 Index = 0; Index < 64; ++Index)
 { TestTrue(TEXT("Generated code satisfies six-character input contract"), UCatRoomAdmission::NormalizeCode(UCatRoomAdmission::GenerateCode(), Normalized)); }
 return true;
}
#endif
