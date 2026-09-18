#include "AbilitySystem/Items/Abilities/CatGA_WaterSpray.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Logging/CatLog.h"
#include "Misc/ScopeExit.h"

// 配置流程：喷水不消费本体，以独立资源支付；缺容量、距离或目标 GE 都拒绝进入运行。
bool UCatGA_WaterSpray::ValidateUseConfiguration(const UCatItemUseFragment& Config, FText& Error) const
{
 const bool Valid = Config.ConsumeCount == 0 && Config.ResourceCapacity > 0 && !Config.bConsumeWhenEmpty
  && !Config.Effects.IsEmpty() && FMath::IsFinite(SprayRange) && SprayRange > 0
  && FMath::IsFinite(RefillRange) && RefillRange > 0;
 if (!Valid) Error = NSLOCTEXT("CatItem","SprayConfig","喷水需配置水量、零本体消耗、正射程及目标表现效果。");
 return Valid;
}
// 预检流程：共用身体/来源校验后复核视线；喷水要求足够水，补水要求未满、近水且水面前无遮挡。
bool UCatGA_WaterSpray::ValidateUse() const
{
 if (!Super::ValidateUse()) return false;
 const auto* Item = ResolveSourceItem(); const auto* Config = GetUseConfiguration();
 FVector Origin, Direction;
 if (!Item || !Config || !ResolveUseRay(Origin, Direction)) return false;
 if (!UseTarget.bSecondaryInput) return Item->GetRemainingResource() >= Config->ResourceCost;
 if (Item->GetRemainingResource() >= Config->ResourceCapacity) return false;
 auto* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
 const auto Region = UCatFishingAimLibrary::FindNearestWaterRegion(GetAvatarActorFromActorInfo(), Origin);
 const auto Point = Water ? Water->ResolveRayToWater(Origin, Direction, Region) : FCatWaterSpatialResult();
 const FVector BodyOrigin = CastChecked<APawn>(GetAvatarActorFromActorInfo())->GetPawnViewLocation();
 if (!Point.bSucceeded || Point.Containment == ECatWaterContainment::Outside
  || FVector::Dist(BodyOrigin, Point.WaterSurfaceWorldPoint) > RefillRange) return false;
 FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ItemRefill), false, GetAvatarActorFromActorInfo());
 return !GetWorld()->LineTraceSingleByChannel(Hit, BodyOrigin, Point.WaterSurfaceWorldPoint, ECC_Visibility, Query)
  || FVector::DistSquared(Hit.ImpactPoint, Point.WaterSurfaceWorldPoint) < FMath::Square(10.0);
}
// 提交流程：服务器再次校验，冻结源实例并一次提交成本；补水改原余额，喷水重新追踪并给命中 ASC 施加配置 GE。
// 喷空仍是成功使用；效果只交给 GAS，不在这里中断目标能力或修改属性。作用域锁保护最后一件成本导致的重入取消。
void UCatGA_WaterSpray::CommitUse()
{
 if (!IsActive() || bUseCommitted || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
 IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
 if (!ValidateUse()) { EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
 CommittedSource = ResolveSourceItem();
 const auto* Config = GetUseConfiguration();
 FVector Origin,Direction; ResolveUseRay(Origin,Direction);
 if (!CommitAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo) || !bResourceCommitted)
 { EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
 bUseCommitted = true;
 AActor* Target = nullptr;
 if (UseTarget.bSecondaryInput) CommittedSource->SetRemainingResourceFromAuthority(Config->ResourceCapacity);
 else
 {
  FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ItemSpray),false,GetAvatarActorFromActorInfo());
  const FVector BodyOrigin = CastChecked<APawn>(GetAvatarActorFromActorInfo())->GetPawnViewLocation();
  GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*(SprayRange+FVector::Dist(BodyOrigin,Origin)),ECC_Visibility,Query);
  if (Hit.bBlockingHit && FVector::Dist(BodyOrigin, Hit.ImpactPoint) <= SprayRange)
  {
   // 相机只负责准星；从身体重新追踪，防止第三人称绕墙视点隔墙喷水。
   FHitResult BodyHit;
   if (GetWorld()->LineTraceSingleByChannel(BodyHit,BodyOrigin,Hit.ImpactPoint+Direction,ECC_Visibility,Query)) Hit = BodyHit;
  }
  else Hit = FHitResult();
  Target = Hit.GetActor();
  if (auto* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
   for (const auto& Class : Config->Effects)
   {
    auto Context = MakeEffectContext(CurrentSpecHandle,CurrentActorInfo); Context.AddSourceObject(CommittedSource);
    auto Spec = CurrentActorInfo->AbilitySystemComponent->MakeOutgoingSpec(Class,GetAbilityLevel(),Context);
    if (Spec.IsValid())
    {
     for (const auto& Parameter : Config->Magnitudes) Spec.Data->SetSetByCallerMagnitude(Parameter.Key, Parameter.Value);
     TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }
   }
 }
 UseTarget.Inventory->BroadcastInventoryChange();
 UE_LOG(LogCatCharacter,Log,TEXT("Event=item_water_use RequestId=%s Item=%s Target=%s Refill=%d Remaining=%d World=%s NetMode=%d Authority=1"),
  *UseTarget.RequestId.ToString(),*UseTarget.ItemId.ToString(),*GetNameSafe(Target),UseTarget.bSecondaryInput,
  CommittedSource->GetRemainingResource(),*GetNameSafe(GetWorld()),int32(GetWorld()->GetNetMode()));
 EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}
