#include "Inventory/Fragments/CatConsumableEffectFragment.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "AbilitySystem/Effects/CatItemEffectApplication.h"
#include "GameFramework/Pawn.h"
#include "UObject/StrongObjectPtr.h"
#include "Logging/CatLog.h"

// 配置预检流程：当前唯一已定义消耗效果是经验；限制为单项即时元属性，防止失败后遗留不可回滚的其他效果。
bool UCatConsumableEffectFragment::IsRuntimeReady() const
{
	const UGameplayEffect* Effect = EffectClass ? EffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
	return ConsumeCount > 0 && Effect && FMath::IsFinite(EffectLevel) && EffectLevel > 0.0f
		&& Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Effect->Executions.IsEmpty()
		&& Effect->Modifiers.Num() == 1
		&& Effect->Modifiers[0].Attribute == UCatSurvivalAttributeSet::GetIncomingFishExperienceAttribute();
}
// 使用预检流程：不申请效果，先确认服务器和已有稳定属性集可用，客户端仅用于显示可用性。
bool UCatConsumableEffectFragment::ValidateForUser(APawn* User) const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(User);
	return User && IsRuntimeReady() && ASC && ASC->GetSet<UCatSurvivalAttributeSet>();
}
// 效果提交流程：创建短寿命回执来源，写入实例提供的参数，申请 GE 后只接受属性回调的提交事实。
FCatDomainCommandResult UCatConsumableEffectFragment::ApplyFromAuthority(APawn* User, FGuid RequestId,
	UObject* SourceObject, const TMap<FGameplayTag, float>& SetByCallerMagnitudes) const
{
	FCatDomainCommandResult Failure;
	Failure.RequestId = RequestId;
	Failure.Error = ECatDomainCommandError::DependencyUnavailable;
	if (!ValidateForUser(User) || !User->HasAuthority() || !RequestId.IsValid()) return Failure;
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(User);
	TStrongObjectPtr<UCatItemEffectApplication> Application(NewObject<UCatItemEffectApplication>());
	Application->RequestId = RequestId;
	Application->ItemSource = SourceObject;
	Application->Result = Failure;
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Application.Get());
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, Context);
	if (!Spec.IsValid()) return Failure;
	for (const auto& Magnitude : SetByCallerMagnitudes)
	{
		if (!FMath::IsFinite(Magnitude.Value) || Magnitude.Value < 0.0f) return Failure;
		Spec.Data->SetSetByCallerMagnitude(Magnitude.Key, Magnitude.Value);
	}
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	UE_LOG(LogCatCharacter, Log, TEXT("Event=item_effect_terminal RequestId=%s Source=%s Actor=%s World=%s NetMode=%d Authority=1 Committed=%d Error=%s"),
		*RequestId.ToString(), *GetNameSafe(SourceObject), *GetNameSafe(User), *GetNameSafe(User->GetWorld()),
		User->GetNetMode(), Application->Result.bCommitted, *UEnum::GetValueAsString(Application->Result.Error));
	return Application->Result;
}
