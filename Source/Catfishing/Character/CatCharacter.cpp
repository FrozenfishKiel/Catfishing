#include "Character/CatCharacter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Logging/CatLog.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Social/CatSocialService.h"

// 构造流程：一次创建 Character-owned ASC/AttributeSet、离散身体状态、吃鱼成长和局内装备组件；只开启组件复制，ActorInfo、属性初值与 Ability 仍由显式 runtime gate 启动。
ACatCharacter::ACatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCatAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	SurvivalAttributes = CreateDefaultSubobject<UCatSurvivalAttributeSet>(TEXT("SurvivalAttributes"));
	// ASC 不会仅凭同 Actor 上存在 AttributeSet 就稳定纳入查询列表；构造期显式登记，保证占有时播种属性不会找不到 AttributeSet。
	AbilitySystemComponent->AddAttributeSetSubobject(SurvivalAttributes.Get());
	ConditionComponent = CreateDefaultSubobject<UCatConditionComponent>(TEXT("ConditionComponent"));
	GrowthComponent = CreateDefaultSubobject<UCatGrowthComponent>(TEXT("GrowthComponent"));
	EquipmentComponent = CreateDefaultSubobject<UCatEquipmentComponent>(TEXT("EquipmentComponent"));
	FishingCameraComponent = CreateDefaultSubobject<UCatFishingCameraComponent>(TEXT("FishingCameraComponent"));
}

void ACatCharacter::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (!FishingCameraComponent->TryGetCameraView(OutResult))
	{
		Super::CalcCamera(DeltaTime, OutResult);
	}
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
	// 提竿、断线、主动切线和落水都是服务器裁决后才知道的结果，本机玩家也必须收到；只有挥网等预测动作跳过本机重播。
	const bool bServerConfirmed = EventTag == CatFishingAbilityTags::Cosmetic_Fishing_HookPull
		|| EventTag == CatFishingAbilityTags::Cosmetic_Fishing_LineBroken
		|| EventTag == CatFishingAbilityTags::Cosmetic_Fishing_LineCut
		|| EventTag == CatFishingAbilityTags::Cosmetic_Fishing_CatInWater;
	const bool bLocallyPredicted = !bServerConfirmed;
	if (!EventTag.IsValid() || (IsLocallyControlled() && bLocallyPredicted))
	{
		return;
	}
	PlayFishingOutcomeMontageFromPresentation(EventTag);
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

bool ACatCharacter::PlayFishingOutcomeMontageFromPresentation(const FGameplayTag OutcomeEventTag)
{
	if (GetNetMode() == NM_DedicatedServer || !OutcomeEventTag.IsValid())
	{
		return false;
	}
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	if (!Presentation)
	{
		return false;
	}
	UAnimMontage* Montage = nullptr;
	if (OutcomeEventTag == CatFishingAbilityTags::Cosmetic_Fishing_LineBroken)
	{
		Montage = Presentation->LineBrokenMontage.LoadSynchronous();
	}
	else if (OutcomeEventTag == CatFishingAbilityTags::Cosmetic_Fishing_CatInWater)
	{
		Montage = Presentation->CatInWaterMontage.LoadSynchronous();
	}
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

// 服务端占有流程：父类先建立 Controller/Owner/PlayerState 关系，再幂等刷新 Character=this 的 ASC Owner/Avatar 并尝试整体应用一次初值；最后仅 authority 授予正式 AbilitySet 并应用可选 starter 选择。
void ACatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilityActorInfo();
	if (HasAuthority())
	{
		GrantDefaultAbilitySetOnce();
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

// 最终清理流程：无论此前是否经历 UnPossessed，都先幂等终止 Fishing/Social；随后撤销默认 AbilitySet、取消 Ability 并清 ActorInfo，最后才交还父类销毁组件。
void ACatCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	NotifyFishingOwnerUnavailable();
	if (AbilitySystemComponent)
	{
		DefaultAbilitySetHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		bDefaultAbilitySetGranted = false;
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->ClearActorInfo();
	}
	Super::EndPlay(EndPlayReason);
}

// Starter 兜底流程：
// 1. 先要求服务器、EquipmentComponent 和设置存在；兜底开关关闭时返回，不读取或写入随身库存。
// 2. 读取当前 Equipment 快照后，如果已经有鱼竿选择，就保留玩家/Profile 已建立的选择，不再覆盖。
// 3. 选择为空时才请求 Equipment 正式配置入口；该入口按目录、解锁、Revision 和随身库存已有物品校验，不会因为配置 ID 创建装备或占用第一格。
// 4. 只有装配提交成功后才可能通过正式库存命令补发配置窝料；失败、未配置窝料或数量为 0 都保持库存不变。
void ACatCharacter::ApplyStarterLoadoutIfConfigured()
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	if (!HasAuthority() || !EquipmentComponent || !Settings)
	{
		return;
	}
	if (!Settings->bAutoConfigureStarterLoadout)
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
	const FCatDomainCommandResult Grant = EquipmentComponent->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(),
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
