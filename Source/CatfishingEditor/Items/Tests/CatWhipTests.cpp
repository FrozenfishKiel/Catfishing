#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Camp/CatCampInventoryActor.h"
#include "Components/SphereComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "HAL/FileManager.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Items/Abilities/CatGA_UseWhip.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/Fragments/CatWhipUseFragment.h"
#include "../../Inventory/Tests/CatSelectedUseInputTestAccess.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Items/Whip/CatWhipActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWhipAssetTest,"Catfishing.Editor.Whip.Assets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatWhipAssetTest::RunTest(const FString&)
{
    const auto* Item=GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(44);
    if (!TestNotNull(TEXT("44号皮鞭正式目录"),Item)) return false;
    const auto* Config=Item->FindFragment<UCatWhipUseFragment>();
    if (!TestNotNull(TEXT("GAS来源配置"),Config)) return false;
    TestTrue(TEXT("使用配置有效"),Config->IsRuntimeReady());
    const auto* Actor=Config->SwingActorClass.GetDefaultObject();
    if (!TestNotNull(TEXT("世界使用Actor"),Actor)) return false;
    TestTrue(TEXT("骨架和命中窗口有效"),Actor->IsWhipConfigurationReady());
    TestEqual(TEXT("正式皮鞭水平击飞冲量"),Actor->ImpulseNewtonSeconds,18.f);
    TestEqual(TEXT("正式皮鞭向上击飞冲量"),Actor->UpwardImpulseNewtonSeconds,12.f);
    TestEqual(TEXT("持久原物不消耗"),Config->ConsumeCount,0);
    TestEqual(TEXT("拾取载体与使用Actor同类"),Item->WorldActorClass.LoadSynchronous(),Config->SwingActorClass.Get());
    const auto& Ref=Actor->GetWhipMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    FVector First,Last;
    for (const FName Name : {FName("lash_01"),FName("lash_16")})
    {
        FTransform T=FTransform::Identity;
        for (int32 I=Ref.FindBoneIndex(Name);I!=INDEX_NONE;I=Ref.GetParentIndex(I)) T*=Ref.GetRefBonePose()[I];
        if (Name==TEXT("lash_01")) First=T.GetLocation(); else Last=T.GetLocation();
    }
    const FVector Delta=Actor->MeshRotationOffset.RotateVector(Last-First);
    AddInfo(FString::Printf(TEXT("Event=whip_reference_pose Bones=%d LashSpan=%s Duration=%.3f"),Ref.GetNum(),*Delta.ToString(),Actor->GetSwingDuration()));
    TestTrue(TEXT("参考鞭身沿角色前方+X伸出，厘米单位"),Delta.X>90 && Delta.X<100 && FMath::Abs(Delta.Y)<1 && FMath::Abs(Delta.Z)<1);
    return true;
}

namespace CatWhipTests
{
class FRestore : public IAutomationLatentCommand
{
public:
    FRestore()
    {
        auto* S=GetDefault<ULevelEditorPlaySettings>(); S->GetPlayNetMode(Mode); S->GetPlayNumberOfClients(Count); S->GetRunUnderOneProcess(One);
        Drivers=GEngine->NetDriverDefinitions;
    }
    bool Update() override
    {
        if (GEditor->PlayWorld) return false;
        auto* S=GetMutableDefault<ULevelEditorPlaySettings>(); S->SetPlayNetMode(Mode); S->SetPlayNumberOfClients(Count); S->SetRunUnderOneProcess(One);
        GEngine->NetDriverDefinitions=Drivers; return true;
    }
    EPlayNetMode Mode=PIE_Standalone; int32 Count=1; bool One=true; TArray<FNetDriverDefinition> Drivers;
};

class FExercise : public IAutomationLatentCommand
{
public:
    explicit FExercise(FAutomationTestBase* In,bool InFacingRegression=false):Test(In),bFacingRegression(InFacingRegression){}
    bool Update() override
    {
        if (Start==0) Start=FPlatformTime::Seconds();
        if (FPlatformTime::Seconds()-Start>45)
        { Test->AddError(FString::Printf(TEXT("Whip network timeout Stage=%d MaxHits=%d ClientSeen=%d"),Stage,MaxHits,bClientSeen)); return true; }
        if (Stage==0) return Prepare();
        if (!Server.IsValid() || !Client.IsValid() || !Attacker.IsValid() || !Victim.IsValid() || !ClientPC.IsValid())
        { Test->AddError(TEXT("Whip fixture world or pawn lost")); return true; }
        if (Stage==1)
        {
            auto* LocalCat=Cast<ACatCharacter>(ClientPC->GetPawn());
            if (!LocalCat) return false;
            auto* Inv=LocalCat->GetInventoryComponent();
            const int32 Slot=Inv->FindInventorySlotIndexFromInstanceId(ItemId);
            if (Slot==INDEX_NONE) return false;
            if (!Test->TestTrue(TEXT("远端选择皮鞭格"),ClientPC->RequestSelectQuickbarSlotFromInput(Slot))) return true;
            Transition(2); return false;
        }
        if (Stage==2)
        {
            if (Age()<.5) return false;
            Press(); Transition(3); return false;
        }
        if (Stage==3 || Stage==5)
        {
            Observe();
            if (Age()<2.3) return false;
            Test->TestEqual(TEXT("同一次挥击最多结算一次"),MaxHits,Stage==3?1:0);
            if (Stage==3)
            {
                Test->TestTrue(TEXT("客户端看见服务器使用Actor"),bClientSeen);
                Test->TestTrue(TEXT("服务器实际播放当前骨架占位蒙太奇"),bServerMontage);
                Test->TestTrue(TEXT("拥有客户端实际播放当前骨架占位蒙太奇"),bClientMontage);
                Test->TestTrue(TEXT("受击猫实际腾空并恢复落地"),bVictimAirborne && Victim->GetCharacterMovement()->IsMovingOnGround());
                Test->TestTrue(TEXT("受击高度超过20厘米"),MaxVictimHeightCm>20);
                Test->TestTrue(TEXT("远端看见受击猫腾空超过20厘米"),MaxClientVictimHeightCm>20);
                Test->TestTrue(TEXT("抽击产生超过1米的水平击退"),FVector::Dist2D(VictimStart,Victim->GetActorLocation())>100);
                Test->AddInfo(FString::Printf(TEXT("Event=whip_launch_verified HeightCm=%.2f ClientHeightCm=%.2f TravelCm=%.2f Landed=%d"),
                    MaxVictimHeightCm,MaxClientVictimHeightCm,FVector::Dist2D(VictimStart,Victim->GetActorLocation()),Victim->GetCharacterMovement()->IsMovingOnGround()));
                Test->TestTrue(TEXT("权威受击猫产生水平位移"),FVector::Dist2D(VictimStart,Victim->GetActorLocation())>1);
                bool bVictimReplicated=false;
                for (TActorIterator<ACatCharacter> It(Client.Get());It;++It)
                    if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==Victim->GetPlayerState()->GetPlayerId())
                        bVictimReplicated=FVector::Dist2D(VictimStart,It->GetActorLocation())>1
                            && FVector::Dist2D(Victim->GetActorLocation(),It->GetActorLocation())<10;
                Test->TestTrue(TEXT("受击水平位移复制给远端观察者"),bVictimReplicated);
            }
            else Test->TestTrue(TEXT("墙体实际拦截原可命中目标"),MaxOccluded>0);
            Test->TestEqual(TEXT("动作结束服务器Actor已清理"),CountSwing(Server.Get()),0);
            Test->TestEqual(TEXT("动作结束客户端Actor已清理"),CountSwing(Client.Get()),0);
            const auto* Entry=Attacker->GetInventoryComponent()->GetInventoryEntryAtSlot(Attacker->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(ItemId));
            Test->TestTrue(TEXT("挥鞭后原实物仍为一件"),Entry && Entry->StackCount==1);
            if (Stage==3)
            {
                ResetPositions(); MakeWall(); MaxHits=MaxOccluded=0; Transition(4); return false;
            }
            if (Wall.IsValid()) Wall->Destroy();
            ResetPositions(); MaxHits=MaxOccluded=0; Transition(6); return false;
        }
        if (Stage==4)
        {
            if (Age()<.5) return false;
            Press(); Transition(5); return false;
        }
        if (Stage==6)
        {
            if (Age()<.5) return false;
            auto* LocalCat=Cast<ACatCharacter>(ClientPC->GetPawn());
            Test->TestTrue(TEXT("旧请求重放进入原GAS入口"),LocalCat->FindComponentByClass<UCatItemAbilityComponent>()->RequestUse(LocalCat->GetInventoryComponent(),ItemId,FirstRequest));
            Transition(7); return false;
        }
        if (Stage==7)
        {
            Observe();
            if (Age()<1) return false;
            Test->TestEqual(TEXT("旧RequestId不产生第二次命中"),MaxHits,0);
            Test->TestEqual(TEXT("重放拒绝不遗留Actor"),CountSwing(Server.Get()),0);
            Press(); Transition(8); return false;
        }
        if (Stage==8)
        {
            if (!CountSwing(Server.Get())) return false;
            const int32 Slot=Attacker->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(ItemId);
            Attacker->GetInventoryComponent()->RemoveItemInstanceFromIndex(Slot);
            Transition(9); return false;
        }
        if (Stage==9)
        {
            Observe();
            if (Age()<1.8) return false;
            Test->TestEqual(TEXT("来源移除立即清理使用Actor"),CountSwing(Server.Get()),0);
            Test->TestEqual(TEXT("来源移除客户端同步清理"),CountSwing(Client.Get()),0);
            Test->TestEqual(TEXT("前摇取消没有命中"),MaxHits,0);
            Test->AddInfo(TEXT("Event=whip_network_verified ServerObserved=1 ClientObserved=1 Scenarios=HitOnce,Wall,Replay,SourceRemoval"));
            return true;
        }
        return false;
    }
private:
    bool Prepare()
    {
        for (const auto& C:GEngine->GetWorldContexts())
            if (C.WorldType==EWorldType::PIE && C.World())
            { if(C.World()->GetNetMode()==NM_ListenServer) Server=C.World(); else if(C.World()->GetNetMode()==NM_Client) Client=C.World(); }
        if (!Server.IsValid() || !Client.IsValid()) return false;
        ClientPC=Cast<ACatfishingPlayerController>(Client->GetFirstPlayerController());
        if (!ClientPC.IsValid() || !ClientPC->PlayerState || !ClientPC->GetPawn()) return false;
        for (TActorIterator<ACatCharacter> It(Server.Get());It;++It)
            if (It->GetPlayerState())
            { if(It->GetPlayerState()->GetPlayerId()==ClientPC->PlayerState->GetPlayerId()) Attacker=*It;
              else if(It->IsLocallyControlled()) Victim=*It; }
        auto* Mode=Server->GetAuthGameMode<ACatfishingGameModeBase>();
        if (!Attacker.IsValid() || !Victim.IsValid() || !Mode || !Mode->CanAcceptGameplayCommand(Attacker->GetController())) return false;
        UClass* Cls=LoadClass<ACatWhipActor>(nullptr,TEXT("/Game/Catfishing/Items/Whip/BP_Whip.BP_Whip_C"));
        if (!Test->TestNotNull(TEXT("正式皮鞭Actor类"),Cls)) return true;
        auto* Inv=Attacker->GetInventoryComponent();
        for (int32 I=Inv->GetInventorySlotCount()-1;I>=0;--I) Inv->RemoveItemInstanceFromIndex(I);
        ResetPositions();
        if (bFacingRegression)
        {
            // Use the production camp interaction volume, not a synthetic collision profile.
            Camp=Server->SpawnActor<ACatCampInventoryActor>(FVector(45,70,40),FRotator::ZeroRotator);
            const auto* Sphere=Camp->FindComponentByClass<USphereComponent>();
            Test->TestEqual(TEXT("仓库交互球阻挡交互射线"),Sphere->GetCollisionResponseToChannel(ECC_Visibility),ECR_Block);
            Test->TestEqual(TEXT("仓库交互球不阻挡身体"),Sphere->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
        }
        auto* Pickup=Server->SpawnActor<ACatWhipActor>(Cls,Attacker->GetActorLocation()+FVector(30,0,20),FRotator::ZeroRotator);
        if (!Test->TestTrue(TEXT("正式拾取Actor发放同一实物"),Pickup && Pickup->Interact_Implementation(Attacker->GetController(),FGuid::NewGuid()))) return true;
        for (const auto& E:Inv->GetInventoryEntries()) if(E.Instance && E.Instance->GetItemId()==44) ItemId=E.Instance->GetItemInstanceId();
        if (!Test->TestTrue(TEXT("拾取形成有效实例GUID"),ItemId.IsValid())) return true;
        Transition(1); return false;
    }
    void ResetPositions()
    {
        auto* A=Attacker->GetPhysicalBodyComponent(); auto* B=Victim->GetPhysicalBodyComponent();
        A->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,FVector(0,0,A->GetStandRootHeightCm())),TEXT("WhipTest"));
        B->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,FVector(85,0,B->GetStandRootHeightCm())),TEXT("WhipTest"));
        const FRotator View(0,bFacingRegression?(Stage<3?90:180):0,0);
        Attacker->GetController()->SetControlRotation(View); ClientPC->SetControlRotation(View);
        VictimStart=Victim->GetActorLocation();
    }
    void MakeWall()
    {
        Wall=Server->SpawnActor<AActor>();
        auto* Box=NewObject<UBoxComponent>(Wall.Get()); Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(4,150,180)); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Wall->SetActorLocation(FVector(48,0,80));
    }
    void Press() { FCatSelectedUseInputTestAccess::Press(ClientPC.Get()); FCatSelectedUseInputTestAccess::Release(ClientPC.Get()); }
    int32 CountSwing(UWorld* World)
    {
        int32 N=0; for(TActorIterator<ACatWhipActor> It(World);It;++It) if(It->GetOwner() && !It->IsActorBeingDestroyed()) ++N;
        return N;
    }
    void Observe()
    {
        if (Stage==3)
        {
            MaxVictimHeightCm=FMath::Max(MaxVictimHeightCm,Victim->GetActorLocation().Z-VictimStart.Z);
            bVictimAirborne|=Victim->GetCharacterMovement()->IsFalling();
            for(TActorIterator<ACatCharacter> It(Client.Get());It;++It)
                if(It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==Victim->GetPlayerState()->GetPlayerId())
                    MaxClientVictimHeightCm=FMath::Max(MaxClientVictimHeightCm,It->GetActorLocation().Z-VictimStart.Z);
            auto Playing=[](ACatCharacter* Cat)
            {
                auto* Anim=Cat && Cat->GetMesh()?Cat->GetMesh()->GetAnimInstance():nullptr;
                const auto* Montage=Anim?Anim->GetCurrentActiveMontage():nullptr;
                return Montage && Montage->GetSkeleton()==Cat->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()
                    && Montage->GetName().Contains(TEXT("Attack_Agressive_Legs_01"));
            };
            bServerMontage|=Playing(Attacker.Get());
            bClientMontage|=Playing(Cast<ACatCharacter>(ClientPC->GetPawn()));
            if (!bCaptured && Age()>.65 && FParse::Param(FCommandLine::Get(),TEXT("WhipScreenshots")))
            { bCaptured=true; Capture(); }
        }
        for(TActorIterator<ACatWhipActor> It(Server.Get());It;++It)
            if(It->GetOwner()==Attacker.Get())
            {
                MaxHits=FMath::Max(MaxHits,It->GetHitCount()); MaxOccluded=FMath::Max(MaxOccluded,It->GetOccludedTargetCount());
                if (bFacingRegression && !bServerFacingChecked)
                {
                    bServerFacingChecked=true;
                    Test->TestTrue(TEXT("镜头偏转时服务器鞭子仍朝猫身体前方"),It->GetActorForwardVector().Dot(Attacker->GetActorForwardVector())>.99);
                    Test->TestTrue(TEXT("场景确实分离猫与镜头朝向"),FMath::Abs(FMath::FindDeltaAngleDegrees(Attacker->GetActorRotation().Yaw,Attacker->GetControlRotation().Yaw))>80);
                }
            }
        if (bFacingRegression && !bClientFacingChecked)
            for(TActorIterator<ACatWhipActor> It(Client.Get());It;++It)
                if(It->GetOwner()==ClientPC->GetPawn())
                {
                    bClientFacingChecked=true;
                    Test->TestTrue(TEXT("客户端鞭子同样朝猫身体前方"),It->GetActorForwardVector().Dot(ClientPC->GetPawn()->GetActorForwardVector())>.99);
                }
        bClientSeen|=CountSwing(Client.Get())>0;
        if(!FirstRequest.IsValid()) for(const auto& Spec:Attacker->GetCatAbilitySystemComponent()->GetActivatableAbilities())
            if(Spec.IsActive()) if(const auto* GA=Cast<UCatGA_UseWhip>(Spec.GetPrimaryInstance())) FirstRequest=GA->GetUseTarget().RequestId;
    }
    void Transition(int32 Next) {Stage=Next; At=Server->GetTimeSeconds(); bServerFacingChecked=bClientFacingChecked=false;}
    void Capture()
    {
        auto* Rig=Client->SpawnActor<AActor>();
        auto* Camera=NewObject<USceneCaptureComponent2D>(Rig);
        Rig->SetRootComponent(Camera); Camera->RegisterComponent();
        Camera->bCaptureEveryFrame=false; Camera->bCaptureOnMovement=false;
        Camera->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR; Camera->FOVAngle=50;
        const FVector Center=ClientPC->GetPawn()->GetActorLocation()+FVector(55,0,15);
        const FVector Position=Center+FVector(100,-360,180);
        Camera->SetWorldLocationAndRotation(Position,(Center-Position).Rotation());
        auto* Target=NewObject<UTextureRenderTarget2D>(Rig);
        Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
        Target->InitAutoFormat(1280,800); Target->UpdateResourceImmediate(true); Camera->TextureTarget=Target;
        for (const FVector Offset : {FVector(60,-100,180),FVector(-100,80,150)})
        {
            auto* Light=NewObject<UPointLightComponent>(Rig);
            Light->SetIntensity(22000); Light->SetCastShadows(false); Light->SetAttenuationRadius(1000);
            Light->RegisterComponent(); Light->SetWorldLocation(Center+Offset);
        }
        Client->SendAllEndOfFrameUpdates(); Camera->CaptureScene();
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("Art/Whip");
        IFileManager::Get().MakeDirectory(*Directory,true);
        UKismetRenderingLibrary::ExportRenderTarget(Client.Get(),Target,Directory,TEXT("UE_Whip_Swing.png"));
        Rig->Destroy();
    }
    double Age() const {return Server->GetTimeSeconds()-At;}
    FAutomationTestBase* Test; double Start=0,At=0; int32 Stage=0,MaxHits=0,MaxOccluded=0; bool bClientSeen=false;
    bool bServerMontage=false,bClientMontage=false,bCaptured=false;
    bool bVictimAirborne=false;
    double MaxVictimHeightCm=0,MaxClientVictimHeightCm=0;
    bool bFacingRegression=false,bServerFacingChecked=false,bClientFacingChecked=false;
    TWeakObjectPtr<UWorld> Server,Client;
    TWeakObjectPtr<ACatfishingPlayerController> ClientPC;
    TWeakObjectPtr<ACatCharacter> Attacker,Victim;
    TWeakObjectPtr<AActor> Wall;
    TWeakObjectPtr<ACatCampInventoryActor> Camp;
    FGuid ItemId,FirstRequest; FVector VictimStart;
};
}

static bool RunWhipNetworkTest(FAutomationTestBase* Test,bool FacingRegression)
{
    if(!Test->TestTrue(TEXT("idle editor"),GEditor && GEngine && !GEditor->PlayWorld)) return false;
    const auto Restore=MakeShared<CatWhipTests::FRestore>();
    auto* S=GetMutableDefault<ULevelEditorPlaySettings>(); S->SetPlayNetMode(PIE_ListenServer); S->SetPlayNumberOfClients(2); S->SetRunUnderOneProcess(true);
    for(auto& D:GEngine->NetDriverDefinitions) if(D.DefName==TEXT("GameNetDriver"))
    {D.DriverClassName=TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); D.DriverClassNameFallback=D.DriverClassName;}
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatWhipTests::FExercise>(Test,FacingRegression));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWhipNetworkTest,"Catfishing.Editor.Whip.RemoteUseAndCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatWhipNetworkTest::RunTest(const FString&) {return RunWhipNetworkTest(this,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWhipFacingTest,"Catfishing.Editor.Whip.BodyFacingAndCampInteraction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatWhipFacingTest::RunTest(const FString&) {return RunWhipNetworkTest(this,true);}
#endif
