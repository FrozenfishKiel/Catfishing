#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterDefinition.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

// 复制声明流程：先保留父类字段，再为当前仍是玩法真相的三项属性注册无条件、Always RepNotify；复制层不夹带阈值、成长或表现裁决。
void UCatSurvivalAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FishingStrength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FightStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Poison, COND_None, REPNOTIFY_Always);
}

// FishingStrength 复制通知流程：把旧基值交给 ASC，使客户端属性 delegate 与服务器最终力量收敛；不派生多人合力结论。
void UCatSurvivalAttributeSet::OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FishingStrength, OldFishingStrength);
}

// FightStamina 复制通知流程：使用标准 RepNotify 更新短周期搏斗体力；疲惫已经退为表现层，不在 ASC 里参与数值同步。
void UCatSurvivalAttributeSet::OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FightStamina, OldFightStamina);

	const float OldValue = OldFightStamina.GetCurrentValue();
	const float NewValue = FightStamina.GetCurrentValue();
	const bool bFirstSample = !bHasFightStaminaDiagnostic;
	if (!bFirstSample && FMath::IsNearlyEqual(OldValue, NewValue)) return;
	AActor* OwnerActor = GetOwningActor();
	const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor);
	const ACatCharacter* Character = Cast<ACatCharacter>(OwnerActor);
	const APlayerState* PlayerState = OwnerCharacter ? OwnerCharacter->GetPlayerState() : nullptr;
	const UWorld* World = OwnerActor ? OwnerActor->GetWorld() : GetWorld();
	const double WorldSeconds = World ? World->GetTimeSeconds() : 0.0;
	const bool bReachedZero = OldValue > UE_SMALL_NUMBER && NewValue <= UE_SMALL_NUMBER;
	const bool bRecoveredFromZero = OldValue <= UE_SMALL_NUMBER && NewValue > UE_SMALL_NUMBER;

	// 只借用已经驻留的正式配置识别满体边沿；诊断不得额外同步加载资产或缓存第二份玩法上限。
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	bool bDefinitionsLoaded = true;
	for (const auto& Definition : Settings->CharacterDefinitions)
	{
		if (!Definition.IsNull() && !Definition.IsValid())
		{
			bDefinitionsLoaded = false;
			break;
		}
	}
	float Baseline = 0.0f;
	const bool bBaselineKnown = bDefinitionsLoaded && Settings->TryGetFightStaminaBaselineForCharacter(
		Character ? Character->GetCatDefinitionId() : NAME_None, Baseline);
	const bool bRecovered = bBaselineKnown && OldValue < Baseline
		&& NewValue >= Baseline - UE_SMALL_NUMBER;
	if (!bFirstSample && !bReachedZero && !bRecoveredFromZero && !bRecovered
		&& WorldSeconds < NextFightStaminaDiagnosticWorldSeconds) return;

	// FishingService 的活动会话索引只在服务器存在；客户端用复制的玩家 ID 与 Actor 关联会话日志。
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fishing_cat_stamina_received OwnerActor=%s PlayerState=%s PlayerId=%d "
			"World=%s NetMode=%d Authority=%s LocalRole=%d Old=%.3f New=%.3f "
			"BaselineKnown=%s Baseline=%.3f Edge=%s"),
		*GetNameSafe(OwnerActor), *GetNameSafe(PlayerState), PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE,
		*GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : INDEX_NONE,
		OwnerActor && OwnerActor->HasAuthority() ? TEXT("true") : TEXT("false"),
		OwnerActor ? static_cast<int32>(OwnerActor->GetLocalRole()) : INDEX_NONE, OldValue, NewValue,
		bBaselineKnown ? TEXT("true") : TEXT("false"), Baseline,
		bFirstSample ? TEXT("Initial") : bReachedZero ? TEXT("Depleted")
			: bRecovered ? TEXT("Recovered") : bRecoveredFromZero ? TEXT("RecoveredFromZero") : TEXT("Changed"));
	NextFightStaminaDiagnosticWorldSeconds = WorldSeconds + 1.0;
	bHasFightStaminaDiagnostic = true;
}

// Poison 复制通知流程：使用标准 RepNotify 更新中毒累积的客户端读模型；客户端不自行判断倒地、恢复或死亡。
void UCatSurvivalAttributeSet::OnRep_Poison(const FGameplayAttributeData& OldPoison)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Poison, OldPoison);
}
