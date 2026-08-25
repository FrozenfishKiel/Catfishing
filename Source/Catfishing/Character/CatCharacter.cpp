#include "Character/CatCharacter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySet.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "AbilitySystem/CatStageCTestAbility.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Condition/CatConditionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Logging/CatLog.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
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

// 服务端占有流程：父类先建立 Controller/Owner/PlayerState 关系，再幂等刷新 Character=this 的 ASC Owner/Avatar 并尝试整体应用一次初值；最后仅 authority 按 SpecHandle 授予一次诊断 Ability，并用 PlayerState::UniqueId 注册个人鱼护。
void ACatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilityActorInfo();
	if (HasAuthority())
	{
		GrantDefaultAbilitySetOnce();
		GrantStageCTestAbility();
		RegisterPersonalFishGuard();
		ApplyStarterLoadoutIfConfigured();
	}
}

// Controller 复制流程：父类先修复 Pawn/Controller 双向关系；有效 Controller 刷新 ActorInfo，空 Controller 则先移除自有输入再 Clear，使无占有期间不保留失效 Avatar。
void ACatCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	if (GetController())
	{
		InitializeAbilityActorInfo();
	}
	else
	{
		RemoveProvisionalMappingContext();
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->ClearActorInfo();
		}
	}
}

// 本地重启流程：父类先重置移动预测和创建输入组件；再刷新 ASC，最后 remove-own/add-own 重装临时 MappingContext，重复重启不会叠加上下文。
void ACatCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	InitializeAbilityActorInfo();
	RefreshProvisionalMappingContext();
}

// 输入绑定流程：保留父类输入初始化后只接受 EnhancedInputComponent；runtime 或软 IA 缺失时安全返回，存在时同步加载该显式临时资产并绑定 Triggered。
void ACatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Settings->IsRuntimeEnabled() || !Settings->bEnableDiagnosticAbility || !EnhancedInput || Settings->DiagnosticInputAction.IsNull())
	{
		return;
	}
	if (const UInputAction* InputAction = Settings->DiagnosticInputAction.LoadSynchronous())
	{
		EnhancedInput->BindAction(InputAction, ETriggerEvent::Triggered, this, &ThisClass::HandleDiagnosticAbilityInput);
	}
}

// 临时失去占有流程：Controller 尚有效时先通知 Fishing/Social 终止该身体的半场协议，再精确移除自有 MappingContext 并取消 Ability；父类断开占有后才 ClearActorInfo，保留 Ability Spec 供同 Actor 重占有。
void ACatCharacter::UnPossessed()
{
	NotifyFishingOwnerUnavailable();
	RemoveProvisionalMappingContext();
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

// 最终清理流程：无论此前是否经历 UnPossessed，都先幂等终止 Fishing/Social，authority 再从 Items 解注册个人鱼护；随后移除自有输入、取消 Ability 并清 ActorInfo，最后才交还父类销毁组件。
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
	RemoveProvisionalMappingContext();
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

// 开发便利装配流程：只在 authority、开关打开、Loadout 仍为空时执行一次；装配与发窝料都走正式权威写口并留结构化日志，失败不重试。
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

// Ability 授予流程：只接受 authority、已开启 runtime、有效 ASC 且尚无句柄的组合；GiveAbility 返回的句柄立即成为后续 PossessedBy 的唯一去重依据。
void ACatCharacter::GrantStageCTestAbility()
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!HasAuthority() || !AbilitySystemComponent || StageCTestAbilityHandle.IsValid()
		|| !Settings->IsRuntimeEnabled() || !Settings->bEnableDiagnosticAbility)
	{
		return;
	}
	StageCTestAbilityHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UCatStageCTestAbility::StaticClass(), 1));
}

// MappingContext 刷新流程：先移除本 Character 上一次添加的上下文；只有本地拥有、runtime 开启、软 IMC 可解析时才向对应 LocalPlayer 子系统以中性优先级 0 添加并保存配对引用。
void ACatCharacter::RefreshProvisionalMappingContext()
{
	RemoveProvisionalMappingContext();
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Settings->IsRuntimeEnabled() || !Settings->bEnableDiagnosticAbility || !IsLocallyControlled()
		|| !InputSubsystem || Settings->DiagnosticMappingContext.IsNull())
	{
		return;
	}
	UInputMappingContext* MappingContext = Settings->DiagnosticMappingContext.LoadSynchronous();
	if (!MappingContext)
	{
		return;
	}
	InputSubsystem->AddMappingContext(MappingContext, 0);
	AppliedMappingContext = MappingContext;
	AppliedInputSubsystem = InputSubsystem;
}

// MappingContext 移除流程：只有保存的子系统和上下文同时存活才执行精确 Remove；随后无条件清引用，使 UnPossessed/EndPlay/重启的重复调用保持幂等且不影响别的系统上下文。
void ACatCharacter::RemoveProvisionalMappingContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = AppliedInputSubsystem.Get(); InputSubsystem && AppliedMappingContext)
	{
		InputSubsystem->RemoveMappingContext(AppliedMappingContext);
	}
	AppliedMappingContext = nullptr;
	AppliedInputSubsystem.Reset();
}

// 诊断输入流程：只在本地拥有且 ASC 存活时按类请求激活；允许 GAS 把 ServerOnly 请求路由到 authority，客户端从不授予 Ability 或直接改 Attribute。
void ACatCharacter::HandleDiagnosticAbilityInput()
{
	if (IsLocallyControlled() && AbilitySystemComponent)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(UCatStageCTestAbility::StaticClass(), true);
	}
}
