#include "AbilitySystem/Items/Abilities/CatGA_UseWhip.h"
#include "Inventory/Fragments/CatWhipUseFragment.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Items/Whip/CatWhipActor.h"
#include "Character/CatCharacter.h"
#include "Logging/CatLog.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"

bool UCatGA_UseWhip::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
    const auto* Config = Cast<UCatWhipUseFragment>(&Configuration);
    if (Config && Config->SwingActorClass && Config->ConsumeCount==0 && Config->CommitDelay==0
        && Config->Effects.IsEmpty() && Config->Magnitudes.IsEmpty() && Config->Montage) return true;
    OutError=NSLOCTEXT("CatWhip","InvalidConfig","皮鞭需专用片段、世界表现类和占位蒙太奇；消耗数量/通用前摇为零，不配置自用效果或效果参数。命中前摇由皮鞭窗口统一控制。");
    return false;
}

void UCatGA_UseWhip::CommitUse()
{
    if (!IsActive() || bUseCommitted || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
    IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
    auto* Character=Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
    const auto* Config=Cast<UCatWhipUseFragment>(GetUseConfiguration());
    if (!Character || !Config || !ValidateUse())
    { EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    FActorSpawnParameters Params; Params.Owner=Character; Params.Instigator=Character;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SwingActor=GetWorld()->SpawnActor<ACatWhipActor>(Config->SwingActorClass,Character->GetActorTransform(),Params);
    if (!SwingActor || !SwingActor->IsWhipConfigurationReady())
    {
        UE_LOG(LogCatSocial, Warning, TEXT("Event=whip_use_rejected RequestId=%s Item=%s Player=%s World=%s NetMode=%d Authority=1 Result=InvalidActorConfiguration"),
            *UseTarget.RequestId.ToString(),*UseTarget.ItemId.ToString(),*GetNameSafe(Character),*GetNameSafe(GetWorld()),GetWorld()->GetNetMode());
        EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return;
    }
    CommittedSource=ResolveSourceItem();
    if (!CommitAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo) || !bResourceCommitted
        || !SwingActor->StartSwingFromAuthority(Character,UseTarget.RequestId,UseTarget.ItemId))
    { EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    bUseCommitted=true;
    GetWorld()->GetTimerManager().SetTimer(FinishTimer,this,&ThisClass::FinishSwing,SwingActor->GetSwingDuration(),false);
}

void UCatGA_UseWhip::FinishSwing()
{
    EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}

void UCatGA_UseWhip::EndAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
    if (!IsActive()) return;
    if (ScopeLockCount>0) { Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled); return; }
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(FinishTimer);
    if (IsValid(SwingActor)) SwingActor->Destroy();
    SwingActor=nullptr;
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}
