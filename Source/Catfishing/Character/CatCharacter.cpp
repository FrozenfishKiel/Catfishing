#include "Character/CatCharacter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatStageCTestAbility.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Condition/CatConditionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/CatInputSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Social/CatSocialService.h"

// 构造流程：先创建 Character-owned ASC 并开启组件复制，再创建 Survival AttributeSet 并立即交给 ASC 持有；个人鱼护、
// Condition 和 Equipment 随后作为同一局内身体的唯一组件出口建立。
// 之后挂表现层：相机臂附着胶囊并跟随 Controller 旋转，相机挂臂末端；占位圆柱按引擎 Cylinder（高 100、半径 50）缩放到
// 34/88 胶囊尺寸，朝向锥体俯仰 -90° 把 +Z 锥尖转到 +X，两者都关闭碰撞。
// 旋转默认值：Controller 偏航不直接带动身体（bUseControllerRotationYaw=false），身体以 500°/s 转向移动方向，于是鼠标只转相机、WASD 才转身。
ACatCharacter::ACatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	SurvivalAttributes = CreateDefaultSubobject<UCatSurvivalAttributeSet>(TEXT("SurvivalAttributes"));
	AbilitySystemComponent->AddAttributeSetSubobject(SurvivalAttributes.Get());
	PersonalFishGuard = CreateDefaultSubobject<UCatContainerReplicationComponent>(TEXT("PersonalFishGuard"));
	ConditionComponent = CreateDefaultSubobject<UCatConditionComponent>(TEXT("ConditionComponent"));
	EquipmentComponent = CreateDefaultSubobject<UCatEquipmentComponent>(TEXT("EquipmentComponent"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
	PlaceholderMesh->SetupAttachment(GetCapsuleComponent());
	PlaceholderMesh->SetStaticMesh(CylinderFinder.Object);
	PlaceholderMesh->SetRelativeScale3D(FVector(0.68f, 0.68f, 1.76f));
	PlaceholderMesh->SetCollisionProfileName(TEXT("NoCollision"));
	PlaceholderFacingCone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderFacingCone"));
	PlaceholderFacingCone->SetupAttachment(GetCapsuleComponent());
	PlaceholderFacingCone->SetStaticMesh(ConeFinder.Object);
	PlaceholderFacingCone->SetRelativeLocationAndRotation(FVector(45.0f, 0.0f, 40.0f), FRotator(-90.0f, 0.0f, 0.0f));
	PlaceholderFacingCone->SetRelativeScale3D(FVector(0.3f));
	PlaceholderFacingCone->SetCollisionProfileName(TEXT("NoCollision"));

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
}

// ASC 查询流程：直接返回构造期唯一组件；不通过 Controller、PlayerState 或全局管理器寻找第二份身体能力真相。
UAbilitySystemComponent* ACatCharacter::GetAbilitySystemComponent() const
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

// Equipment 读取流程：直接返回构造期唯一一局组件；调用方不得从 Profile SaveGame 或 UI 建立第二个运行装配聚合。
UCatEquipmentComponent* ACatCharacter::GetEquipmentComponent() const
{
	return EquipmentComponent;
}

// 相机读取流程：直接返回构造期组件。
UCameraComponent* ACatCharacter::GetFollowCamera() const
{
	return FollowCamera;
}

// 占位网格读取流程：直接返回构造期组件。
UStaticMeshComponent* ACatCharacter::GetPlaceholderMesh() const
{
	return PlaceholderMesh;
}

// BeginPlay 流程：先让 Actor 与组件完成注册（ASC 此时会按引擎默认临时建立 ActorInfo），再用项目 gate 幂等刷新或清除，避免未裁 runtime 偷跑。
void ACatCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeAbilityActorInfo();
}

// 服务端占有流程：父类先建立 Controller/Owner/PlayerState 关系，再幂等刷新 Character=this 的 ASC Owner/Avatar 并尝试
// 整体应用一次初值；最后仅 authority 按 SpecHandle 授予一次诊断 Ability，注册个人鱼护，并把 starter 装配尝试交给
// EquipmentComponent。
void ACatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilityActorInfo();
	if (HasAuthority())
	{
		GrantStageCTestAbility();
		RegisterPersonalFishGuard();
		ApplyStarterEquipmentOnce();
	}
}

// Controller 复制流程：父类先修复 Pawn/Controller 双向关系；有效 Controller 刷新 ActorInfo，空 Controller 则先移除自
// 有输入再 Clear，使无占有期间不保留失效 Avatar。
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

// 输入绑定流程：父类初始化后只接受 EnhancedInputComponent。先按 CatInputSettings 绑定角色输入：IsCharacterInputReady
// 成立时同步加载 Move/Look 并绑定 Triggered，Jump 非空时额外绑定 Started→Jump、Completed→StopJumping，遛鱼拖/松两个动
// 作非空时各绑 Started/Completed 记录按住状态；
// 再保留原有诊断分支：Ability runtime、诊断 gate 与诊断 IA 齐全时绑定 Triggered。两条链各自独立判空，任一缺失都不影响
// 另一条。MappingContext 由 UCatLocalPlayerInputSubsystem 装配，这里不加。
void ACatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	const UCatInputSettings* InputSettings = GetDefault<UCatInputSettings>();
	if (InputSettings->IsCharacterInputReady())
	{
		if (const UInputAction* MoveAction = InputSettings->MoveAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::HandleMoveInput);
		}
		if (const UInputAction* LookAction = InputSettings->LookAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::HandleLookInput);
		}
		if (const UInputAction* JumpAction = InputSettings->JumpAction.IsNull() ? nullptr : InputSettings->JumpAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}
		if (const UInputAction* PullAction = InputSettings->FishingPullAction.IsNull() ? nullptr : InputSettings->FishingPullAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(PullAction, ETriggerEvent::Started, this, &ThisClass::HandleFishingPullStarted);
			EnhancedInput->BindAction(PullAction, ETriggerEvent::Completed, this, &ThisClass::HandleFishingPullCompleted);
		}
		if (const UInputAction* ReleaseAction = InputSettings->FishingReleaseAction.IsNull() ? nullptr : InputSettings->FishingReleaseAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(ReleaseAction, ETriggerEvent::Started, this, &ThisClass::HandleFishingReleaseStarted);
			EnhancedInput->BindAction(ReleaseAction, ETriggerEvent::Completed, this, &ThisClass::HandleFishingReleaseCompleted);
		}
	}

	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!Settings->IsRuntimeEnabled() || !Settings->bEnableDiagnosticAbility || Settings->DiagnosticInputAction.IsNull())
	{
		return;
	}
	if (const UInputAction* InputAction = Settings->DiagnosticInputAction.LoadSynchronous())
	{
		EnhancedInput->BindAction(InputAction, ETriggerEvent::Triggered, this, &ThisClass::HandleDiagnosticAbilityInput);
	}
}

// 临时失去占有流程：Controller 尚有效时先通知 Fishing/Social 终止该身体的半场协议，再精确移除自有 MappingContext 并取
// 消 Ability；父类断开占有后才 ClearActorInfo，保留 Ability Spec 供同 Actor 重占有。
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

// 最终清理流程：无论此前是否经历 UnPossessed，都先幂等终止 Fishing/Social，authority 再从 Items 解注册个人鱼护；随后
// 移除自有输入、取消 Ability 并清 ActorInfo，最后才交还父类销毁组件。
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

// 会话中断通知流程：向当前 authority World 的 Fishing 与 Social 服务报告身体失效；前者终止半场搏斗，后者返还仍在追回
// 窗口的鱼，二者都不跨 World 保存协议。
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
	if (!GetDefault<UCatAbilitySettings>()->TryGetInitialAttributes(Poison, FishingStrength, FightStamina))
	{
		return;
	}
	AbilitySystemComponent->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), Poison);
	AbilitySystemComponent->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), FishingStrength);
	AbilitySystemComponent->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), FightStamina);
	bInitialAttributesApplied = true;
}

// Starter 装配触发流程：只在 authority、EquipmentComponent 存活且当前三槽为空时发起；实际目录、授权、耐久写入都由 EquipmentComponent 裁决。
void ACatCharacter::ApplyStarterEquipmentOnce()
{
	if (!HasAuthority() || !EquipmentComponent)
	{
		return;
	}
	const FCatEquipmentLoadoutSnapshot& EquipmentSnapshot = EquipmentComponent->GetSnapshot();
	if (!EquipmentSnapshot.RodDefinitionId.IsNone() || !EquipmentSnapshot.BaitDefinitionId.IsNone()
		|| !EquipmentSnapshot.FloatDefinitionId.IsNone())
	{
		return;
	}
	EquipmentComponent->ConfigureStarterLoadoutFromAuthority(FGuid::NewGuid());
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

// MappingContext 刷新流程：先移除本 Character 上一次添加的上下文；只有本地拥有、runtime 开启、软 IMC 可解析时才向对应
// LocalPlayer 子系统以中性优先级 0 添加并保存配对引用。
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

// MappingContext 移除流程：只有保存的子系统和上下文同时存活才执行精确 Remove；随后无条件清引用，使
// UnPossessed/EndPlay/重启的重复调用保持幂等且不影响别的系统上下文。
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

// 移动输入流程：没有 Controller 时忽略；否则只取 Controller 偏航构造水平旋转，用它的前向/右向单位向量分别乘以 Axis2D
// 的 Y/X 喂给 AddMovementInput，俯仰不影响行走方向。
void ACatCharacter::HandleMoveInput(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

// 遛鱼按键入口：四个入口只改对应的按住标记，再统一走 SendFishingFightIntent 合成上报。
void ACatCharacter::HandleFishingPullStarted() { bFishingPullHeld = true; SendFishingFightIntent(); }
void ACatCharacter::HandleFishingPullCompleted() { bFishingPullHeld = false; SendFishingFightIntent(); }
void ACatCharacter::HandleFishingReleaseStarted() { bFishingReleaseHeld = true; SendFishingFightIntent(); }
void ACatCharacter::HandleFishingReleaseCompleted() { bFishingReleaseHeld = false; SendFishingFightIntent(); }

// 遛鱼意图上报流程：左键按着就是拖（两键同时按时拖优先——飞书表里拖是更危险的那一行，宁可按玩家"在用力"处理），否则右键按着是松，都没按是 None；
// 合成结果和上次发出的相同就不发，避免按键抖动刷 RPC；只有本地控制且 Controller 是项目 PlayerController 时才发。
void ACatCharacter::SendFishingFightIntent()
{
	const ECatFishingFightIntent Intent = bFishingPullHeld ? ECatFishingFightIntent::Pull
		: bFishingReleaseHeld ? ECatFishingFightIntent::Release : ECatFishingFightIntent::None;
	ACatfishingPlayerController* PC = Cast<ACatfishingPlayerController>(Controller);
	if (!IsLocallyControlled() || !PC || Intent == LastSentFishingIntent)
	{
		return;
	}
	LastSentFishingIntent = Intent;
	PC->ServerSetFishingFightIntent(Intent);
}

// 视角输入流程：没有 Controller 时忽略；否则 X 累加到 Controller 偏航、Y 累加到俯仰。资产侧已对 Y 取反，配合
// bEnableLegacyInputScales 下引擎内部的负俯仰比例得到“上推=抬头”。
void ACatCharacter::HandleLookInput(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}
