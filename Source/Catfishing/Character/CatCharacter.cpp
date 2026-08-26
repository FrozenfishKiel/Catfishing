#include "Character/CatCharacter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySet.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Social/CatSocialService.h"

// 构造流程：一次创建 Character-owned ASC/AttributeSet、个人鱼护复制出口、离散身体状态、吃鱼成长和局内装备组件；只开启组件复制，ActorInfo、属性初值与 Ability 仍由显式 runtime gate 启动。
ACatCharacter::ACatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCatAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	SurvivalAttributes = CreateDefaultSubobject<UCatSurvivalAttributeSet>(TEXT("SurvivalAttributes"));
	// ASC 不会仅凭同 Actor 上存在 AttributeSet 就稳定纳入查询列表；构造期显式登记，保证占有时播种属性不会找不到 AttributeSet。
	AbilitySystemComponent->AddAttributeSetSubobject(SurvivalAttributes.Get());
	PersonalFishGuard = CreateDefaultSubobject<UCatContainerReplicationComponent>(TEXT("PersonalFishGuard"));
	ConditionComponent = CreateDefaultSubobject<UCatConditionComponent>(TEXT("ConditionComponent"));
	GrowthComponent = CreateDefaultSubobject<UCatGrowthComponent>(TEXT("GrowthComponent"));
	EquipmentComponent = CreateDefaultSubobject<UCatEquipmentComponent>(TEXT("EquipmentComponent"));
}

// ASC 查询流程：直接返回构造期唯一组件；不通过 Controller、PlayerState 或全局管理器寻找第二份身体能力真相。
UAbilitySystemComponent* ACatCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UCatAbilitySystemComponent* ACatCharacter::GetCatAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

// 鱼护 ID 读取流程：直接返回 authority 注册事实；客户端若尚未从容器 Snapshot 观察到 ID，不得用该值提交写命令。
FGuid ACatCharacter::GetPersonalFishGuardId() const
{
	return PersonalFishGuardId;
}

// Condition 读取流程：直接返回构造期唯一组件；外部命令不从 ASC 或 Controller 复制推导另一份 Wet/Downed 状态。
UCatConditionComponent* ACatCharacter::GetConditionComponent() const
{
	return ConditionComponent;
}

// Growth 读取流程：直接返回构造期唯一组件；吃鱼入口只调用这一处，避免 Items、Condition 或 UI 各自缓存经验槽。
UCatGrowthComponent* ACatCharacter::GetGrowthComponent() const
{
	return GrowthComponent;
}

// 一次性表现广播落地点：挥网仍由本地 Ability 先播，所以发起端跳过；Primary 输入有瞄准/提竿/收线
// 三种语义，已不再按下即播，因此提竿事件也必须让发起端收到服务器确认后的表现。
void ACatCharacter::Multicast_PlayCosmeticEvent_Implementation(const FGameplayTag EventTag)
{
	const bool bLocallyPredicted = EventTag != CatFishingAbilityTags::Cosmetic_Fishing_HookPull;
	if (!EventTag.IsValid() || (IsLocallyControlled() && bLocallyPredicted))
	{
		return;
	}
	BP_PlayCosmeticEvent(EventTag);
}

// BodyAction 表现开始流程：服务器只广播，真正播放发生在每台客户端；没有配置 Montage 时仍触发蓝图事件，保证正式资源接入点稳定。
void ACatCharacter::Multicast_PlayBodyActionPresentation_Implementation(
	const ECatBodyActionAbilityCommand Command, const FGameplayTag PresentationEventTag)
{
	if (GetNetMode() == NM_DedicatedServer || Command == ECatBodyActionAbilityCommand::Unknown
		|| !PresentationEventTag.IsValid())
	{
		return;
	}
	PlayBodyActionMontageFromPresentation(Command);
	BP_PlayBodyActionPresentation(Command, PresentationEventTag);
}

// BodyAction 表现停止流程：取消或拒绝提交时停止同一动作的可选 Montage，再通知蓝图清理非 Montage 表现。
void ACatCharacter::Multicast_StopBodyActionPresentation_Implementation(
	const ECatBodyActionAbilityCommand Command, const FGameplayTag PresentationEventTag)
{
	if (GetNetMode() == NM_DedicatedServer || Command == ECatBodyActionAbilityCommand::Unknown
		|| !PresentationEventTag.IsValid())
	{
		return;
	}
	StopBodyActionMontageFromPresentation(Command);
	BP_StopBodyActionPresentation(Command, PresentationEventTag);
}

bool ACatCharacter::PlayBodyActionMontageFromPresentation(const ECatBodyActionAbilityCommand Command)
{
	// Montage 播放流程：专服和 Unknown 动作直接拒绝；客户端读取共享表现设置并同步加载可选 Montage，返回值只表示本机是否实际播放成功。
	// 没配置正式 Montage 时返回 false，但上层 multicast 仍会继续触发 BP_PlayBodyActionPresentation，给蓝图音效、特效或后续正式资产保留入口。
	if (GetNetMode() == NM_DedicatedServer || Command == ECatBodyActionAbilityCommand::Unknown)
	{
		return false;
	}
	const UCatBodyActionPresentationSettings* Presentation = GetDefault<UCatBodyActionPresentationSettings>();
	UAnimMontage* Montage = Presentation ? Presentation->LoadMontageForCommand(Command) : nullptr;
	return Montage && PlayAnimMontage(Montage) > 0.0f;
}

bool ACatCharacter::StopBodyActionMontageFromPresentation(const ECatBodyActionAbilityCommand Command)
{
	// Montage 停止流程：专服和 Unknown 动作直接拒绝；客户端按同一表现设置找到本动作 Montage，缺配置时不做动画副作用并返回 false。
	// 返回 false 不代表停止表现广播失败，上层仍会调用 BP_StopBodyActionPresentation，正式蓝图可用它清理非 Montage 表现或执行兜底恢复。
	if (GetNetMode() == NM_DedicatedServer || Command == ECatBodyActionAbilityCommand::Unknown)
	{
		return false;
	}
	const UCatBodyActionPresentationSettings* Presentation = GetDefault<UCatBodyActionPresentationSettings>();
	UAnimMontage* Montage = Presentation ? Presentation->LoadMontageForCommand(Command) : nullptr;
	if (!Montage)
	{
		return false;
	}
	StopAnimMontage(Montage);
	return true;
}

bool ACatCharacter::PlayFishingCastMontageFromPresentation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	UAnimMontage* Montage = Presentation ? Presentation->CastMontage.LoadSynchronous() : nullptr;
	return Montage && PlayAnimMontage(Montage) > 0.0f;
}

// Equipment 读取流程：直接返回构造期唯一一局组件；调用方不得从 Profile SaveGame 或 UI 建立第二个运行装配聚合。
UCatEquipmentComponent* ACatCharacter::GetEquipmentComponent() const
{
	return EquipmentComponent;
}

// BeginPlay 流程：先让 Actor 与组件完成注册（ASC 此时会按引擎默认临时建立 ActorInfo），再用项目 gate 幂等刷新或清除，避免未裁 runtime 偷跑。
void ACatCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeAbilityActorInfo();
}

// 服务端占有流程：父类先建立 Controller/Owner/PlayerState 关系，再幂等刷新 Character=this 的 ASC Owner/Avatar 并尝试整体应用一次初值；最后仅 authority 授予正式 AbilitySet，并用 PlayerState::UniqueId 注册个人鱼护。
void ACatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilityActorInfo();
	if (HasAuthority())
	{
		GrantDefaultAbilitySetOnce();
		RegisterPersonalFishGuard();
		ApplyStarterLoadoutIfConfigured();
	}
}

// Controller 复制流程：父类先修复 Pawn/Controller 双向关系；有效 Controller 刷新 ActorInfo，空 Controller 直接 Clear，使无占有期间不保留失效 Avatar。
void ACatCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	if (GetController())
	{
		InitializeAbilityActorInfo();
	}
	else
	{
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->ClearActorInfo();
		}
	}
}

// 本地重启流程：父类先重置移动预测和创建输入组件；随后只刷新 ASC ActorInfo，正式输入映射由 PlayerController 维护。
void ACatCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	InitializeAbilityActorInfo();
}

// 临时失去占有流程：Controller 尚有效时先通知 Fishing/Social 终止该身体的半场协议，再取消 Ability；父类断开占有后才 ClearActorInfo，保留正式 Ability Spec 供同 Actor 重占有。
void ACatCharacter::UnPossessed()
{
	NotifyFishingOwnerUnavailable();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
	}
	Super::UnPossessed();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->ClearActorInfo();
	}
}

// 最终清理流程：无论此前是否经历 UnPossessed，都先幂等终止 Fishing/Social，authority 再从 Items 解注册个人鱼护；随后撤销默认 AbilitySet、取消 Ability 并清 ActorInfo，最后才交还父类销毁组件。
void ACatCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	NotifyFishingOwnerUnavailable();
	if (HasAuthority())
	{
		if (UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr)
		{
			Items->UnregisterContainer(PersonalFishGuard);
		}
	}
	if (AbilitySystemComponent)
	{
		DefaultAbilitySetHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		bDefaultAbilitySetGranted = false;
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->ClearActorInfo();
	}
	Super::EndPlay(EndPlayReason);
}

// 个人鱼护注册流程：仅 authority 且继承 UniqueId 有效时生成一次容器 ID，读取显式容量并交给 Items；原始身份不写入组件快照。
void ACatCharacter::RegisterPersonalFishGuard()
{
	const APlayerState* OwningPlayerState = GetPlayerState();
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	if (!HasAuthority() || !Items || !PersonalFishGuard || !OwningPlayerState || !OwningPlayerState->GetUniqueId().IsValid())
	{
		return;
	}
	if (!PersonalFishGuardId.IsValid())
	{
		PersonalFishGuardId = FGuid::NewGuid();
	}
	const int32 Capacity = GetDefault<UCatItemsSettings>()->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard));
	Items->RegisterContainer(PersonalFishGuard, PersonalFishGuardId, ECatContainerKind::PersonalGuard,
		OwningPlayerState->GetUniqueId()->ToString(), Capacity);
}

// Starter 兜底流程：先拒绝非 authority、无组件或未显式打开的情况，避免正式默认路径绕过商店/团队库/Profile Grant。
// 只有 Loadout 仍没有鱼竿时才用当前 Equipment Revision 写入配置的基础三件套；装配成功后再按新 Revision 发放可选窝料，任一步失败只记日志不重试。
void ACatCharacter::ApplyStarterLoadoutIfConfigured()
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	if (!HasAuthority() || !EquipmentComponent || !Settings || !Settings->bAutoConfigureStarterLoadout)
	{
		return;
	}
	const FCatEquipmentLoadoutSnapshot& Snapshot = EquipmentComponent->GetSnapshot();
	if (!Snapshot.RodDefinitionId.IsNone())
	{
		return;
	}
	const FCatDomainCommandResult Configure = EquipmentComponent->ConfigureLoadoutFromAuthority(FGuid::NewGuid(),
		Snapshot.Revision, Settings->StarterRodDefinitionId, Settings->StarterBaitDefinitionId,
		Settings->StarterFloatDefinitionId, Settings->StarterScoopNetDefinitionId);
	UE_LOG(LogCatCharacter, Log, TEXT("Event=starter_loadout_configure Committed=%s Error=%s Revision=%lld"),
		Configure.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Configure.Error), Configure.Revision);
	if (!Configure.bCommitted || Settings->StarterChumDefinitionId.IsNone() || Settings->StarterChumQuantity <= 0)
	{
		return;
	}
	const FCatDomainCommandResult Grant = EquipmentComponent->GrantRunConsumableFromAuthority(FGuid::NewGuid(),
		EquipmentComponent->GetSnapshot().Revision, Settings->StarterChumDefinitionId, Settings->StarterChumQuantity);
	UE_LOG(LogCatCharacter, Log, TEXT("Event=starter_chum_grant Committed=%s Error=%s Revision=%lld Definition=%s Quantity=%d"),
		Grant.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Grant.Error), Grant.Revision,
		*Settings->StarterChumDefinitionId.ToString(), Settings->StarterChumQuantity);
}

// 会话中断通知流程：向当前 authority World 的 Fishing 与 Social 服务报告身体失效；前者终止半场搏斗，后者返还仍在追回窗口的鱼，二者都不跨 World 保存协议。
void ACatCharacter::NotifyFishingOwnerUnavailable()
{
	if (!HasAuthority())
	{
		return;
	}
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->TerminateSessionsForCharacter(this);
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->CancelTheftsForCharacter(this);
	}
}

// ActorInfo 初始化流程：未配置组件或 runtime 时清除引擎自动信息；显式 Full 策略成立时设置复制模式，并以 this/this 幂等刷新 Owner 与 Avatar。
void ACatCharacter::InitializeAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}
	if (!GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled())
	{
		AbilitySystemComponent->ClearActorInfo();
		return;
	}
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Full);
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	ApplyInitialAttributesOnce();
}

// 初始属性流程：只在 authority、ASC 已就绪且尚未应用时整体读取配置；三项全部有效才写基值并设置一次性标记，配置不完整时保留重试机会。
void ACatCharacter::ApplyInitialAttributesOnce()
{
	if (!HasAuthority() || !AbilitySystemComponent || bInitialAttributesApplied)
	{
		return;
	}
	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	if (!GetDefault<UCatAbilitySettings>()->TryGetInitialAttributesForCharacter(CatDefinitionId,
		Poison, FishingStrength, FightStamina))
	{
		if (!CatDefinitionId.IsNone())
		{
			UE_LOG(LogCatCharacter, Warning,
				TEXT("Event=initial_attributes_unresolved CatDefinitionId=%s Reason=DefinitionMissingOrNotReady"),
				*CatDefinitionId.ToString());
		}
		return;
	}
	AbilitySystemComponent->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), Poison);
	AbilitySystemComponent->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), FishingStrength);
	if (!AbilitySystemComponent->InitializeFishingStaminaForSession())
	{
		return;
	}
	bInitialAttributesApplied = true;
}

// 默认 AbilitySet 授予流程：只接受 authority、有效 ASC、尚未授予和完整正式资产配置；失败不创建临时代用品。
void ACatCharacter::GrantDefaultAbilitySetOnce()
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!HasAuthority() || !AbilitySystemComponent || bDefaultAbilitySetGranted
		|| !Settings || !Settings->IsFishingRuntimeReady())
	{
		return;
	}
	const UCatAbilitySet* AbilitySet = Settings->DefaultAbilitySet.LoadSynchronous();
	bDefaultAbilitySetGranted = AbilitySet
		&& AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, DefaultAbilitySetHandles);
}
