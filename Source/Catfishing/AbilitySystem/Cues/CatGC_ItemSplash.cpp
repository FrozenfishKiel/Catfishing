#include "AbilitySystem/Cues/CatGC_ItemSplash.h"
#include "AbilitySystem/Effects/CatItemEffects.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Logging/CatLog.h"

// 构造流程：GAS 根据该标签寻找资源类，移除自动回收；重复喷水可以重新播爆发但不重复挂持续组件。
ACatGC_ItemSplash::ACatGC_ItemSplash()
{ GameplayCueTag = CatItemEffectTags::Splash; bAutoDestroyOnRemove = true; bAllowMultipleOnActiveEvents = true; }
// 爆发流程：仅有真实目标才播配置资源；未配置美术时仍完成 GC 消费并记录一次网络观察。
bool ACatGC_ItemSplash::OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	if (!Target) return false;
	if (BurstEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BurstEffect, Target->GetActorTransform());
	if (SplashSound) UGameplayStatics::PlaySoundAtLocation(this, SplashSound, Target->GetActorLocation());
	UE_LOG(LogCatCharacter, Log, TEXT("Event=item_splash_cue Target=%s BurstConfigured=%d WetConfigured=%d World=%s NetMode=%d"),
		*GetNameSafe(Target), BurstEffect != nullptr, WetEffect != nullptr, *GetNameSafe(GetWorld()), GetNetMode());
	return true;
}
// 持续流程：晚加入可见范围也从 GE 当前状态恢复，已有组件不重复创建。
bool ACatGC_ItemSplash::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	if (!Target || !Target->GetRootComponent()) return false;
	if (WetEffect && !ActiveWetEffect) ActiveWetEffect = UGameplayStatics::SpawnEmitterAttached(WetEffect, Target->GetRootComponent());
	return true;
}
// 清理流程：只销毁自己生成的粒子，不擦除目标其他 GC、天气或水体效果。
bool ACatGC_ItemSplash::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{ if (ActiveWetEffect) ActiveWetEffect->DestroyComponent(); ActiveWetEffect = nullptr; return true; }
