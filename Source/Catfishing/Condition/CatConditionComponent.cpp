#include "Condition/CatConditionComponent.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "GameFramework/Controller.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"

// 构造流程：关闭独立状态复制和 Tick；状态通过 ASC 的活动效果复制。
UCatConditionComponent::UCatConditionComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = false;
}

// 观察流程：绑定身体 ASC 的 Tag 变化并立即投影；状态可先于组件 BeginPlay 到达，首次读取也不会漏掉。
void UCatConditionComponent::BeginPlay()
{
	Super::BeginPlay();
	ObservedASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	if (auto* ASC = ObservedASC.Get()) StateTagHandle = ASC->RegisterGenericGameplayTagEvent().AddUObject(this, &ThisClass::HandleStateTagChanged);
	RefreshSnapshot();
}

// 退出流程：从原 ASC 移除监听；组件不拥有状态 GE，最终回收归 ASC。
void UCatConditionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ASC = ObservedASC.Get()) ASC->RegisterGenericGameplayTagEvent().Remove(StateTagHandle);
	Super::EndPlay(Reason);
}

// 查询流程：只读角色 ASC；缺少 ASC 时没有可声明的状态，不创建本地替代状态。
bool UCatConditionComponent::HasState(FGameplayTag Tag) const
{
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	return ASC && ASC->HasMatchingGameplayTag(Tag);
}

// 投影流程：从 GAS 同时读取倒地、湿毛和水域；旧水域枚举只为钓鱼消费者映射，不参与权威存储。
void UCatConditionComponent::RefreshSnapshot() const
{
	Snapshot.bWet = HasState(CatStateTags::Wet);
	Snapshot.bDowned = HasState(CatStateTags::Downed);
	Snapshot.WaterExposure = HasState(CatStateTags::WaterDangerous) ? ECatWaterExposureState::Dangerous
		: HasState(CatStateTags::WaterShallow) ? ECatWaterExposureState::Shallow : ECatWaterExposureState::Dry;
}

// 读取流程：按需刷新只读投影，保证无 BeginPlay 的领域测试和服务器早期查询也观察同一 ASC。
const FCatConditionSnapshot& UCatConditionComponent::GetSnapshot() const
{
	RefreshSnapshot();
	return Snapshot;
}

// 状态通知流程：只处理角色状态分支，刷新兼容投影并递增本机观察计数；消费者自己订阅 ASC，此处不再转发通知或回写效果。
void UCatConditionComponent::HandleStateTagChanged(FGameplayTag Tag, int32 Count)
{
	if (!Tag.MatchesTag(CatStateTags::State)) return;
	RefreshSnapshot();
	++Snapshot.Revision;
}

// 湿毛写入流程：只更新该环境来源的 GE；清除后其他独立来源仍可保持湿毛，属性不受影响。
void UCatConditionComponent::SetWetFromAuthority(bool bNewWet)
{
	if (auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>())
		ASC->SetStateTagsFromAuthority(TEXT("Condition.Wet"), bNewWet ? FGameplayTagContainer(CatStateTags::Wet) : FGameplayTagContainer());
}

// 水域暴露更新流程：
// 1. 先确认 Character、authority、阈值配置、水域子系统和固定步时长齐全，缺任一项都不猜湿身结果。
// 2. 再用脚点查询指定 WaterRegion 的浸没深度，并按湿润阈值、危险进入阈值和退出滞回维护离散状态。
// 3. 状态没有变化时只返回 Unchanged；危险首次进入用 Warning 记录并交给 Fishing 终局入口处理。
ECatWaterExposureUpdate UCatConditionComponent::UpdateWaterExposureFromAuthority(
	const FCatWaterRegionHandle& WaterRegion, const double DeltaSeconds,
	double& OutImmersionDepthCentimeters)
{
	OutImmersionDepthCentimeters = 0.0;
	ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Character || !Character->HasAuthority() || !Settings || !Settings->HasWaterExposureThresholds()
		|| !Water || !WaterRegion.IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	// 身体组件提供真实支撑脚点；停用的 Character 胶囊不再代表物理猫的身高。
	const FVector FootPoint = Character->GetBodyFootPointWorld();
	const FCatWaterImmersionResult Immersion = Water->QueryImmersionAtWorldPoint(FootPoint, WaterRegion);
	if (!Immersion.bSucceeded)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	OutImmersionDepthCentimeters = Immersion.ImmersionDepthCentimeters;
	const bool bWet = Immersion.Containment != ECatWaterContainment::Outside
		&& OutImmersionDepthCentimeters >= Settings->WetWaterDepthCentimeters;
	RefreshSnapshot();
	ECatWaterExposureState NewExposure = bWet ? ECatWaterExposureState::Shallow : ECatWaterExposureState::Dry;
	if (Snapshot.WaterExposure == ECatWaterExposureState::Dangerous
		&& bWet && OutImmersionDepthCentimeters > Settings->DangerousWaterExitDepthCentimeters)
	{
		NewExposure = ECatWaterExposureState::Dangerous;
	}
	else if (bWet && OutImmersionDepthCentimeters >= Settings->DangerousWaterDepthCentimeters)
	{
		DangerousWaterBuildUpSeconds += DeltaSeconds;
		// 危险水域按 World 秒累计；极小容差只吸收浮点边界，避免正好到确认阈值的那帧被漏判。
		if (DangerousWaterBuildUpSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER
			>= Settings->DangerousWaterConfirmationSeconds)
		{
			NewExposure = ECatWaterExposureState::Dangerous;
		}
	}
	else
	{
		DangerousWaterBuildUpSeconds = 0.0;
	}
	const bool bDangerousEntered = Snapshot.WaterExposure != ECatWaterExposureState::Dangerous
		&& NewExposure == ECatWaterExposureState::Dangerous;
	const bool bObservationChanged = Snapshot.WaterExposure != NewExposure;
	auto* ASC = Character->FindComponentByClass<UCatAbilitySystemComponent>();
	if (!ASC) return ECatWaterExposureUpdate::Unavailable;
	FGameplayTagContainer WaterTags;
	if (bWet) WaterTags.AddTag(CatStateTags::Wet);
	if (NewExposure == ECatWaterExposureState::Dangerous) WaterTags.AddTag(CatStateTags::WaterDangerous);
	else if (NewExposure == ECatWaterExposureState::Shallow) WaterTags.AddTag(CatStateTags::WaterShallow);
	if (!ASC->SetStateTagsFromAuthority(TEXT("Condition.Water"), WaterTags)) return ECatWaterExposureUpdate::Unavailable;
	// 即使聚合状态相同也更新本来源；否则其他来源撤销后可能丢失仍在水中的湿毛。
	RefreshSnapshot();
	if (!bObservationChanged) return ECatWaterExposureUpdate::Unchanged;
	const FString ControllerFields = CatLogContext::BuildControllerFields(Character->GetController());
	if (bDangerousEntered)
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=character_water_exposure_changed Character=%s Region=%s Exposure=%s DepthCm=%.2f Revision=%lld Authority=true %s"),
			*Character->GetName(), *WaterRegion.RegionId.ToString(),
			*UEnum::GetValueAsString(NewExposure), OutImmersionDepthCentimeters, Snapshot.Revision,
			*ControllerFields);
	}
	else
	{
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=character_water_exposure_changed Character=%s Region=%s Exposure=%s DepthCm=%.2f Revision=%lld Authority=true %s"),
			*Character->GetName(), *WaterRegion.RegionId.ToString(),
			*UEnum::GetValueAsString(NewExposure), OutImmersionDepthCentimeters, Snapshot.Revision,
			*ControllerFields);
	}
	return bDangerousEntered ? ECatWaterExposureUpdate::DangerousEntered
		: ECatWaterExposureUpdate::Changed;
}

// 倒地写入流程：服务器将本来源的倒地意图交给 ASC，失败不改旧效果；成功后读取聚合状态，仅真实变化输出日志。角色和钓鱼退出由 Character 订阅 ASC 处理。
bool UCatConditionComponent::SetDownedFromAuthority(const bool bNewDowned)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}
	const bool bWasDowned = HasState(CatStateTags::Downed);
	auto* ASC = Owner->FindComponentByClass<UCatAbilitySystemComponent>();
	if (!ASC || !ASC->SetStateTagsFromAuthority(TEXT("Condition.Downed"), bNewDowned ? FGameplayTagContainer(CatStateTags::Downed) : FGameplayTagContainer())) return false;
	RefreshSnapshot();
	if (bWasDowned == Snapshot.bDowned) return true;
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=character_downed_changed Character=%s Downed=%s Revision=%lld World=%s NetMode=%d Authority=true LocalRole=%d"),
		*Owner->GetName(), Snapshot.bDowned ? TEXT("true") : TEXT("false"), Snapshot.Revision,
		*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
		static_cast<int32>(Owner->GetLocalRole()));

	return true;
}
