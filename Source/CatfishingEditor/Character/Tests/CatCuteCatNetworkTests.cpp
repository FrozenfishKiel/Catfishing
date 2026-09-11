#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Character/Animation/CatForceReactionComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace CatCuteNetwork
{
	const TCHAR* ClassPath = TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C");
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Count); Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
			WorldHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues) {
				if (World && World->WorldType == EWorldType::PIE) World->bIsNameStableForNetworking = true;
			});
			ModeHandle = FGameModeEvents::OnGameModeInitializedEvent().AddLambda([](AGameModeBase* Mode) {
				if (!Mode || Mode->GetWorld()->WorldType != EWorldType::PIE || Mode->GetClass()!=AGameModeBase::StaticClass()) return;
				Mode->DefaultPawnClass = LoadClass<ACatCharacter>(nullptr, ClassPath);
				Mode->PlayerControllerClass = APlayerController::StaticClass(); Mode->PlayerStateClass = APlayerState::StaticClass();
			});
		}
		~FRestore() override { Restore(); }
		bool Update() override { if (GEditor->PlayWorld) return false; Restore(); return true; }
	private:
		void Restore()
		{
			if (bRestored) return;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Count); Settings->SetRunUnderOneProcess(OneProcess);
			GEngine->NetDriverDefinitions = Drivers;
			FWorldDelegates::OnPreWorldInitialization.Remove(WorldHandle); FGameModeEvents::OnGameModeInitializedEvent().Remove(ModeHandle);
			bRestored=true;
		}
		EPlayNetMode Mode=PIE_Standalone;
		int32 Count=1;
		bool OneProcess=true, bRestored=false;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle WorldHandle, ModeHandle;
	};
	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds()-Started > 60) { Test->AddError(FString::Printf(TEXT("CuteCat network timeout Stage=%d"), Stage)); return true; }
			UWorld* Server=nullptr; UWorld* Client=nullptr;
			for (const auto& Context : GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::PIE && Context.World()) {
				if (Context.World()->GetNetMode()==NM_ListenServer) Server=Context.World();
				if (Context.World()->GetNetMode()==NM_Client) Client=Context.World();
			}
			if (!Server || !Client) return false;
			APlayerController* Local=Client->GetFirstPlayerController();
			ACatCharacter* ClientCat=Local ? Cast<ACatCharacter>(Local->GetPawn()) : nullptr;
			if (!ClientCat || !Local->PlayerState || Local->AcknowledgedPawn != ClientCat) return false;
			ACatCharacter* ServerCat=nullptr;
			for (TActorIterator<ACatCharacter> It(Server); It; ++It)
				if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==Local->PlayerState->GetPlayerId()) ServerCat=*It;
			if (!ServerCat) return false;
			auto* SB=ServerCat->GetPhysicalBodyComponent(); auto* CB=ClientCat->GetPhysicalBodyComponent();
			auto* SV=ServerCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			auto* CV=ClientCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			if (!CV->GetVisualMesh() || !SV->GetVisualMesh() || !ClientCat->GetMesh()->GetAnimInstance()) return false;
			const double Now=Server->GetTimeSeconds();
			if (Stage==0) {
				Local->SetActorTickEnabled(false);
				for (auto It=Server->GetPlayerControllerIterator(); It; ++It) if (It->Get()) It->Get()->SetActorTickEnabled(false);
				// Free locomotion fixture: the simple GameMode may reuse an unobstructed PlayerStart
				// because this character's legacy capsule has collision disabled. Separate real model bodies.
				int32 Placement=0;
				for (TActorIterator<ACatCharacter> It(Server);It;++It)
					It->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,
						FVector(-300,Placement++*600-300,It->GetDefaultHalfHeight())),TEXT("VariantTestArrange"));
				for (ACatCharacter* Cat : {ServerCat,ClientCat}) {
					Test->TestEqual(TEXT("network spawns selected CuteCat class"),Cat->GetClass()->GetPathName(),FString(ClassPath));
					Test->TestTrue(TEXT("network animation instance is the CuteCat template child"),Cat->GetMesh()->GetAnimInstance()->GetClass()->GetName()==TEXT("ABP_CuteCat_C"));
				}
				Stage=1; StageAt=Now;
			}
			CB->SetMoveIntent(Stage==2 ? FVector::ForwardVector : FVector::ZeroVector);
			CB->SetViewIntent(FRotator::ZeroRotator);
			if (Stage==1 && Now-StageAt>3 && SB->IsGrounded() && CB->IsGrounded()) {
				Test->TestTrue(TEXT("both endpoints initialize four-foot IK"),SV->GetLocomotionObservation().GroundMask==15 && CV->GetLocomotionObservation().GroundMask==15);
				StartLocation=ServerCat->GetActorLocation(); Stage=2; StageAt=Now;
				StandingHead=CV->GetVisualMesh()->GetBoneLocationByName(TEXT("Head_001"),EBoneSpaces::WorldSpace).Z;
			}
			else if (Stage==2 && Now-StageAt>1.5) {
				Test->TestTrue(TEXT("owning client input moves authoritative selected character"),FVector::Dist2D(StartLocation,ServerCat->GetActorLocation())>70);
				Test->TestTrue(TEXT("both endpoints consume calibrated native walk"),SV->GetLocomotionObservation().AnimationSpeedCmS>1 && CV->GetLocomotionObservation().AnimationSpeedCmS>1);
				CB->SetMoveIntent(FVector::ZeroVector); CB->RequestJump(); Stage=3; StageAt=Now;
			}
			else if (Stage==3) {
				int32 Index=0;
				for (ACatCharacter* Cat : {ServerCat,ClientCat}) {
					UAnimInstance* Anim=Cat->GetMesh()->GetAnimInstance(); const int32 Machine=Anim->GetStateMachineIndex(TEXT("Main States"));
					const FName State=Machine==INDEX_NONE ? NAME_None : Anim->GetCurrentStateName(Machine);
					Phases[Index++] |= State==TEXT("Jump") ? 1 : State==TEXT("Fall Loop") ? 2 : State==TEXT("Land") ? 4 : 0;
				}
				if (Now-StageAt<2.5 || !SB->IsGrounded() || !CB->IsGrounded()) return false;
				Test->TestEqual(TEXT("server shared template plays all jump phases"),Phases[0],7);
				Test->TestEqual(TEXT("owning client shared template plays all jump phases"),Phases[1],7);
				ServerCat->Multicast_PlayCosmeticEvent(FGameplayTag::RequestGameplayTag(TEXT("Cat.Cosmetic.Fishing.HookPull"))); Stage=4; StageAt=Now;
			}
			else if (Stage==4 && Now-StageAt>0.1) {
				UAnimMontage* Source=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/Animalia/Cat/AM_Attack_Agressive_Legs_01-IP_Montage"));
				for (ACatCharacter* Cat : {ServerCat,ClientCat}) Test->TestTrue(TEXT("multicast plays mapped CuteCat montage on both endpoints"),
					Cat->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Cast<UAnimMontage>(Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->ResolveAnimationAsset(Source))));
				ServerCat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(),115);
				Test->AddExpectedMessage(TEXT("Event=character_downed"),ELogVerbosity::Warning);
				Test->TestTrue(TEXT("real condition authority accepts downed transition"),CatIsAcceptedDomainCommandResult(ServerCat->GetConditionComponent()->RequestFieldSelfRecovery(ServerCat->GetController(),FGuid::NewGuid())));
				Stage=5; StageAt=Now;
			}
			else if (Stage==5 && Now-StageAt>8) {
				Test->TestTrue(TEXT("downed state reaches client"),ClientCat->GetConditionComponent()->GetSnapshot().bDowned);
				for (ACatCharacter* Cat : {ServerCat,ClientCat}) Test->TestEqual(TEXT("both consumers reach lying pose"),Cat->FindComponentByClass<UCatConditionPresentationComponent>()->GetObservedPosePhase(),FName(TEXT("DownedPose")));
				Test->TestTrue(TEXT("retargeted lying pose visibly lowers the client head"),CV->GetVisualMesh()->GetBoneLocationByName(TEXT("Head_001"),EBoneSpaces::WorldSpace).Z<StandingHead-3);
				Test->TestTrue(TEXT("real condition authority accepts recovery"),CatIsAcceptedDomainCommandResult(ServerCat->GetConditionComponent()->RequestFieldSelfRecovery(ServerCat->GetController(),FGuid::NewGuid())));
				Stage=6; StageAt=Now;
			}
			else if (Stage==6 && Now-StageAt>8) {
				for (ACatCharacter* Cat : {ServerCat,ClientCat}) Test->TestEqual(TEXT("both consumers complete get-up"),Cat->FindComponentByClass<UCatConditionPresentationComponent>()->GetObservedPosePhase(),FName(TEXT("Locomotion")));
				Test->TestTrue(TEXT("client recovers movement and standing pose"),!ClientCat->GetConditionComponent()->GetSnapshot().bDowned && CB->IsLocomotionEnabled()
					&& FMath::Abs(CV->GetVisualMesh()->GetBoneLocationByName(TEXT("Head_001"),EBoneSpaces::WorldSpace).Z-StandingHead)<5);
				Test->AddInfo(TEXT("Event=cute_cat_network_verified Scope=OwningClientInput,JumpStates,MulticastMontage,Downed,Recovery")); return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		double Started, StageAt=0, StandingHead=0;
		int32 Stage=0, Phases[2]={0,0};
		FVector StartLocation;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCuteCatNetworkTest,"Catfishing.CharacterVariants.Network.CuteCatReplicatesSharedAnimationAndCondition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCuteCatNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle validation editor"),GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore=MakeShared<CatCuteNetwork::FRestore>();
	UWorld* Map=FAutomationEditorCommonUtils::CreateNewMap();
	Map->bIsNameStableForNetworking=true;
	Map->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
	auto* Floor=Map->SpawnActor<AStaticMeshActor>();
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube")));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,-10),FVector(40,40,0.2)));
	UClass* Class=LoadClass<ACatCharacter>(nullptr,CatCuteNetwork::ClassPath);
	if (!TestNotNull(TEXT("selectable CuteCat"),Class)) return false;
	const double Height=Class->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight();
	Map->SpawnActor<APlayerStart>(FVector(-300,300,Height),FRotator::ZeroRotator);
	Map->SpawnActor<APlayerStart>(FVector(0,-150,Height),FRotator::ZeroRotator);
	auto* Settings=GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	for (auto& Driver:GEngine->NetDriverDefinitions) if (Driver.DefName==TEXT("GameNetDriver"))
		Driver.DriverClassName=Driver.DriverClassNameFallback=TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatCuteNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

namespace CatCuteNetwork
{
    class FVerifyForceReaction final : public IAutomationLatentCommand
    {
    public:
        explicit FVerifyForceReaction(FAutomationTestBase* InTest):Test(InTest),Started(FPlatformTime::Seconds()) {}
        bool Update() override
        {
            if (FPlatformTime::Seconds()-Started>45) { Test->AddError(FString::Printf(TEXT("Reaction network timeout Stage=%d"),Stage)); return true; }
            UWorld* Server=nullptr; TArray<UWorld*> Clients;
            for (const auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::PIE && Context.World())
            {
                if (Context.World()->GetNetMode()==NM_ListenServer) Server=Context.World();
                if (Context.World()->GetNetMode()==NM_Client) Clients.Add(Context.World());
            }
            if (!Server || Clients.Num()!=2) return false;
            auto* PC=Clients[0]->GetFirstPlayerController();
            auto* OwnerCat=PC ? Cast<ACatCharacter>(PC->GetPawn()) : nullptr;
            if (!OwnerCat || !PC->PlayerState) return false;
            const int32 PlayerId=PC->PlayerState->GetPlayerId();
            TArray<ACatCharacter*> Cats;
            for (auto* World:{Server,Clients[0],Clients[1]})
            {
                ACatCharacter* Found=nullptr;
                for (TActorIterator<ACatCharacter> It(World);It;++It)
                    if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==PlayerId) Found=*It;
                if (!Found || !Found->GetMesh()->GetAnimInstance()) return false;
                Cats.Add(Found);
            }
            const double Now=Server->GetTimeSeconds();
            auto* Body=Cats[0]->GetPhysicalBodyComponent();
            if (Stage==0)
            {
                for (auto* World : Clients)
                {
                    const auto* Local = World->GetFirstPlayerController();
                    if (!Local || !Local->GetPawn() || Local->AcknowledgedPawn != Local->GetPawn()) return false;
                }
                for (auto* World:{Server,Clients[0],Clients[1]})
                    for (auto It=World->GetPlayerControllerIterator();It;++It) if (It->Get()) It->Get()->SetActorTickEnabled(false);
                int32 Placement=0;
                for (TActorIterator<ACatCharacter> It(Server);It;++It)
                    It->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,
                        FVector(-500,Placement++*600-600,It->GetDefaultHalfHeight())),TEXT("ReactionTestArrange"));
                Stage=1; At=Now; return false;
            }
            if (Stage==1 && Now-At>3)
            {
                for (auto* Cat:Cats)
                {
                    Baseline.Add(Cat->FindComponentByClass<UCatForceReactionComponent>()->GetPlayedCount());
                    auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
                    HeadBefore.Add(Cat->GetActorTransform().InverseTransformPosition(Visual->GetVisualMesh()->GetBoneLocationByName(TEXT("Head_001"),EBoneSpaces::WorldSpace)));
                }
                Force=Cats[0]->GetActorForwardVector()*1000;
                Body->SetExternalForceFromAuthority(Cats[0],Force,false,false,true);
                Stage=2; At=Now;
            }
            else if (Stage==2 && Now-At>.25)
            {
                int32 Index=0;
                for (auto* Cat:Cats)
                {
                    auto* Reaction=Cat->FindComponentByClass<UCatForceReactionComponent>();
                    Test->TestEqual(TEXT("server, owning client, observer each plays onset once"),Reaction->GetPlayedCount(),Baseline[Index]+1);
                    Test->TestEqual(TEXT("all endpoints choose actual forward force"),Reaction->GetObservedDirection(),ECatForceReactionDirection::Forward);
                    auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
                    auto* Montage=Cast<UAnimMontage>(Visual->ResolveAnimationAsset(Reaction->DirectionalMontages[0]));
                    Test->TestTrue(TEXT("mapped montage is actually playing"),Cat->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Montage));
                    const FVector Head=Cat->GetActorTransform().InverseTransformPosition(Visual->GetVisualMesh()->GetBoneLocationByName(TEXT("Head_001"),EBoneSpaces::WorldSpace));
                    Test->TestTrue(TEXT("visible post-IK mesh actually consumes reaction"),FVector::Dist(Head,HeadBefore[Index++])>.25);
                }
                Stage=3;
            }
            else if (Stage==3 && Now-At>3)
            {
                int32 Index=0;
                for (auto* Cat:Cats)
                {
                    Test->TestEqual(TEXT("sustained force never replays after montage ends"),Cat->FindComponentByClass<UCatForceReactionComponent>()->GetPlayedCount(),Baseline[Index++]+1);
                    Test->TestFalse(TEXT("one-shot naturally finishes"),Cat->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying());
                }
                Body->SetExternalForceFromAuthority(Cats[0],-Force,false,false,true);
                Stage=4; At=Now;
            }
            else if (Stage==4 && Now-At>.5)
            {
                int32 Index=0;
                for (auto* Cat:Cats) Test->TestEqual(TEXT("force reversal without unloading cannot replay"),Cat->FindComponentByClass<UCatForceReactionComponent>()->GetPlayedCount(),Baseline[Index++]+1);
                Body->ClearExternalForce(Cats[0]); Stage=5; At=Now;
            }
            else if (Stage==5 && Now-At>.35)
            {
                Body->SetExternalForceFromAuthority(Cats[0],-Force,false,false,true); Stage=6; At=Now;
            }
            else if (Stage==6 && Now-At>.25)
            {
                int32 Index=0;
                for (auto* Cat:Cats)
                {
                    auto* Reaction=Cat->FindComponentByClass<UCatForceReactionComponent>();
                    Test->TestEqual(TEXT("stable unload permits one new playback"),Reaction->GetPlayedCount(),Baseline[Index++]+2);
                    Test->TestEqual(TEXT("push now plays opposite to earlier pull on every endpoint"),Reaction->GetObservedDirection(),ECatForceReactionDirection::Backward);
                }
                Body->ClearExternalForce(Cats[0]);
                Test->AddInfo(TEXT("Event=force_reaction_network_verified Endpoints=ListenServer,OwningClient,Observer Scope=Once,Sustain,Reverse,Rearm,VisiblePose"));
                return true;
            }
            return false;
        }
    private:
        FAutomationTestBase* Test;
        double Started,At=0; int32 Stage=0; FVector Force; TArray<FVector> HeadBefore; TArray<uint32> Baseline;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatForceReactionNetworkTest,"Catfishing.ForceReaction.Network.OnsetOnlyAcrossThreeEndpoints",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatForceReactionNetworkTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("idle validation editor"),GEditor && GEngine && !GEditor->PlayWorld)) return false;
    const auto Restore=MakeShared<CatCuteNetwork::FRestore>();
    UWorld* Map=FAutomationEditorCommonUtils::CreateNewMap(); Map->bIsNameStableForNetworking=true;
    Map->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    auto* Floor=Map->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube")));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,-10),FVector(60,60,.2)));
    const double Height=LoadClass<ACatCharacter>(nullptr,CatCuteNetwork::ClassPath)->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight();
    for (int32 I=0;I<3;++I) Map->SpawnActor<APlayerStart>(FVector(-500,I*500-500,Height),FRotator::ZeroRotator);
    auto* Settings=GetMutableDefault<ULevelEditorPlaySettings>();
    Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(3); Settings->SetRunUnderOneProcess(true);
    for (auto& Driver:GEngine->NetDriverDefinitions) if (Driver.DefName==TEXT("GameNetDriver"))
        Driver.DriverClassName=Driver.DriverClassNameFallback=TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatCuteNetwork::FVerifyForceReaction>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
