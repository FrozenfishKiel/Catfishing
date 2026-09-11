#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/World.h"
#include "Character/CatCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// 初始随身库存容量迁移流程：角色创建正式库存时默认读 InventorySettings；旧 EquipmentSettings 被测试或诊断改值时保留一次兼容覆盖。
	int32 ResolveInitialPlayerInventorySlotCapacity()
	{
		return GetDefault<UCatInventorySettings>()->GetPlayerInventorySlotCapacity();
	}
}

// 构造流程：一次创建 Character-owned ASC/AttributeSet、离散身体状态、吃鱼成长、正式随身库存和局内装备组件；只开启组件复制，ActorInfo、属性初值与 Ability 仍由显式 runtime gate 启动。
ACatCharacter::ACatCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCatCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	AbilitySystemComponent = CreateDefaultSubobject<UCatAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	SurvivalAttributes = CreateDefaultSubobject<UCatSurvivalAttributeSet>(TEXT("SurvivalAttributes"));
	// ASC 不会仅凭同 Actor 上存在 AttributeSet 就稳定纳入查询列表；构造期显式登记，保证占有时播种属性不会找不到 AttributeSet。
	AbilitySystemComponent->AddAttributeSetSubobject(SurvivalAttributes.Get());
	ConditionComponent = CreateDefaultSubobject<UCatConditionComponent>(TEXT("ConditionComponent"));
	ConditionPresentation = CreateDefaultSubobject<UCatConditionPresentationComponent>(TEXT("ConditionPresentation"));
	GrowthComponent = CreateDefaultSubobject<UCatGrowthComponent>(TEXT("GrowthComponent"));
	InventoryComponent = CreateDefaultSubobject<UCatBackPackComponent>(TEXT("InventoryComponent"));
	EquipmentComponent = CreateDefaultSubobject<UCatEquipmentComponent>(TEXT("EquipmentComponent"));
	FishingCameraComponent = CreateDefaultSubobject<UCatFishingCameraComponent>(TEXT("FishingCameraComponent"));
	GetCharacterMovement()->MaxWalkSpeed=100.0f;
	PrimaryActorTick.bCanEverTick=true;
	PrimaryActorTick.TickGroup=TG_PostPhysics;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30);
	bUseControllerRotationYaw=false;
	PhysicalBody=CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicsBody"));
	SetRootComponent(PhysicalBody);
	GetCapsuleComponent()->SetupAttachment(PhysicalBody);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	LeftPhysicsHand=CreateDefaultSubobject<USphereComponent>(TEXT("LeftPhysicsHand"));
	RightPhysicsHand=CreateDefaultSubobject<USphereComponent>(TEXT("RightPhysicsHand"));
	LeftPhysicsHand->SetupAttachment(PhysicalBody);
	RightPhysicsHand->SetupAttachment(PhysicalBody);
	UCatPhysicalBodyComponent::ConfigureGeometry(PhysicalBody,LeftPhysicsHand,RightPhysicsHand);
	LeftPhysicsArm=CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LeftShoulder"));
	RightPhysicsArm=CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("RightShoulder"));
	LeftPhysicsArm->SetupAttachment(PhysicalBody);
	RightPhysicsArm->SetupAttachment(PhysicalBody);
	PhysicsGrab=CreateDefaultSubobject<UCatPhysicsGrabComponent>(TEXT("PhysicsGrab"));
	PhysicalBodyComponent=CreateDefaultSubobject<UCatPhysicalBodyComponent>(TEXT("PhysicalBody"));
	CreateDefaultSubobject<UCatPhysicalEffortComponent>(TEXT("PhysicalEffort"));
	PhysicalVisual=CreateDefaultSubobject<UCatPhysicsPrototypeVisualComponent>(TEXT("PhysicalVisual"));
	ModelContacts=CreateDefaultSubobject<UCatModelContactComponent>(TEXT("ModelContacts"));

}

void ACatCharacter::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	// 相机裁决流程：先让钓鱼表现组件尝试提供持杆视角；没有活动钓鱼镜头或组件尚未就绪时，回到 ACharacter/蓝图相机，避免普通移动视角被 C++ 抢占。
	if (!FishingCameraComponent || !FishingCameraComponent->TryGetCameraView(DeltaTime, OutResult))
	{
		Super::CalcCamera(DeltaTime, OutResult);
	}
}

// Gameplay keeps its original montage identity; each skin resolves playback to its own skeleton.
float ACatCharacter::PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName)
{
	UAnimMontage* Resolved = PhysicalVisual ? Cast<UAnimMontage>(PhysicalVisual->ResolveAnimationAsset(AnimMontage)) : AnimMontage;
	return Resolved ? Super::PlayAnimMontage(Resolved, InPlayRate, StartSectionName) : 0.0f;
}

void ACatCharacter::StopAnimMontage(UAnimMontage* AnimMontage)
{
	UAnimMontage* Resolved = PhysicalVisual ? Cast<UAnimMontage>(PhysicalVisual->ResolveAnimationAsset(AnimMontage)) : AnimMontage;
	if (!AnimMontage || Resolved) Super::StopAnimMontage(Resolved);
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
	const FGameplayTag BodyActionEventTag, const FGameplayTag PresentationEventTag)
{
	if (GetNetMode() == NM_DedicatedServer || !BodyActionEventTag.IsValid()
		|| !PresentationEventTag.IsValid())
	{
		return;
	}
	PlayBodyActionMontageFromPresentation(BodyActionEventTag);
	BP_PlayBodyActionPresentation(BodyActionEventTag, PresentationEventTag);
}

// BodyAction 表现停止流程：取消或拒绝提交时停止同一动作的可选 Montage，再通知蓝图清理非 Montage 表现。
void ACatCharacter::Multicast_StopBodyActionPresentation_Implementation(
	const FGameplayTag BodyActionEventTag, const FGameplayTag PresentationEventTag)
{
	if (GetNetMode() == NM_DedicatedServer || !BodyActionEventTag.IsValid()
		|| !PresentationEventTag.IsValid())
	{
		return;
	}
	StopBodyActionMontageFromPresentation(BodyActionEventTag);
	BP_StopBodyActionPresentation(BodyActionEventTag, PresentationEventTag);
}

bool ACatCharacter::PlayBodyActionMontageFromPresentation(const FGameplayTag BodyActionEventTag)
{
	// Montage 播放流程：专服和空动作标签直接拒绝；客户端读取共享表现设置并同步加载可选 Montage，返回值只表示本机是否实际播放成功。
	// 没配置正式 Montage 时返回 false，但上层 multicast 仍会继续触发 BP_PlayBodyActionPresentation，给蓝图音效、特效或后续正式资产保留入口。
	if (GetNetMode() == NM_DedicatedServer || !BodyActionEventTag.IsValid())
	{
		return false;
	}
	const UCatBodyActionPresentationSettings* Presentation = GetDefault<UCatBodyActionPresentationSettings>();
	UAnimMontage* Montage = Presentation ? Presentation->LoadMontage(BodyActionEventTag) : nullptr;
	return Montage && PlayAnimMontage(Montage) > 0.0f;
}

bool ACatCharacter::StopBodyActionMontageFromPresentation(const FGameplayTag BodyActionEventTag)
{
	// Montage 停止流程：专服和空动作标签直接拒绝；客户端按同一表现设置找到本动作 Montage，缺配置时不做动画副作用并返回 false。
	// 返回 false 不代表停止表现广播失败，上层仍会调用 BP_StopBodyActionPresentation，正式蓝图可用它清理非 Montage 表现或执行兜底恢复。
	if (GetNetMode() == NM_DedicatedServer || !BodyActionEventTag.IsValid())
	{
		return false;
	}
	const UCatBodyActionPresentationSettings* Presentation = GetDefault<UCatBodyActionPresentationSettings>();
	UAnimMontage* Montage = Presentation ? Presentation->LoadMontage(BodyActionEventTag) : nullptr;
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

// Inventory 读取流程：直接返回构造期正式库存组件；后续商店、拾取和营地迁移都应从这个组件进入统一收货。
UCatInventoryComponent* ACatCharacter::GetInventoryComponent() const
{
	return InventoryComponent;
}

// 嘴部读取流程：直接返回服务器复制的唯一引用；调用方若需要鱼或鱼护类型只做 Cast，禁止再从 Attachments 搜索第二条真相。
AActor* ACatCharacter::GetMouthCarriedActor() const
{
	return MouthCarriedActor;
}

// 嘴部认领流程：
// 1. 只接受本 World 内仍有效的 Actor，避免跨世界或销毁中的对象写入复制状态。
// 2. 只在当前为空时写入，因此并发拾取和库存回调最多有一个提交者获胜。
// 3. 立即强制复制，让附件表现只消费这份已建立的权威事实。
bool ACatCharacter::TryClaimMouthCarriedActorFromAuthority(AActor* ExpectedActor)
{
	if (!HasAuthority() || !IsValid(ExpectedActor) || ExpectedActor->IsActorBeingDestroyed() || ExpectedActor->GetWorld() != GetWorld()
		|| MouthCarriedActor != nullptr)
	{
		return false;
	}
	MouthCarriedActor = ExpectedActor;
	ExpectedActor->OnDestroyed.AddDynamic(this, &ThisClass::HandleMouthCarriedActorDestroyed);
	ForceNetUpdate();
	return true;
}

// 嘴部释放流程：只清除仍然属于 ExpectedActor 的引用；旧 Actor 的销毁回调无法误删新一轮携带状态。
bool ACatCharacter::ReleaseMouthCarriedActorFromAuthority(AActor* ExpectedActor)
{
	if (!HasAuthority() || !ExpectedActor || MouthCarriedActor != ExpectedActor)
	{
		return false;
	}
	ExpectedActor->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleMouthCarriedActorDestroyed);
	MouthCarriedActor = nullptr;
	ForceNetUpdate();
	return true;
}

// 通用销毁回调流程：只把回调中的旧 Actor 作为 expected actor 释放；若角色已经认领新对象，迟到销毁不会影响新嘴部状态。
void ACatCharacter::HandleMouthCarriedActorDestroyed(AActor* DestroyedActor)
{
	ReleaseMouthCarriedActorFromAuthority(DestroyedActor);
}

// 复制声明流程：把嘴部占用作为角色唯一的网络事实下发；Actor 本身仍各自复制位置、附着和表现状态。
void ACatCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MouthCarriedActor);
}

// BeginPlay 流程：先让 Actor 与组件完成注册（ASC 此时会按引擎默认临时建立 ActorInfo），再用项目 gate 幂等刷新或清除，避免未裁 runtime 偷跑。
void ACatCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Preserve the authored mesh/camera world transforms while restoring the capsule root.
	const FTransform ActorPose = GetActorTransform();
	const double MeshGeometryScale = GetMesh()->GetSkeletalMeshAsset()
		? GetMesh()->GetComponentTransform().GetRelativeTransform(PhysicalBody->GetComponentTransform()).GetScale3D().GetAbsMax() : 1.0;
	TArray<USceneComponent*> AuthoredChildren;
	GetCapsuleComponent()->GetChildrenComponents(true, AuthoredChildren);
	TArray<FTransform> ChildTransforms;
	for (const auto* Child : AuthoredChildren) ChildTransforms.Add(Child->GetComponentTransform());
	GetCapsuleComponent()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(GetCapsuleComponent());
	GetCapsuleComponent()->SetWorldTransform(ActorPose);
	PhysicalBody->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepWorldTransform);
	for (int32 Index=0; Index<AuthoredChildren.Num(); ++Index) AuthoredChildren[Index]->SetWorldTransform(ChildTransforms[Index]);
	GetCapsuleComponent()->SetCapsuleSize(13.0 * MeshGeometryScale, 20.0 * MeshGeometryScale);
	ConfigureCharacterMovementAuthority();
	PhysicalBodyComponent->UseCharacterMovement(CastChecked<UCatCharacterMovementComponent>(GetCharacterMovement()));
	PhysicalBodyComponent->ConfigureMovementDefaults(GetCharacterMovement()->JumpZVelocity,
		GetCharacterMovement()->GravityScale, GetCharacterMovement()->MaxWalkSpeed);
	PhysicalBodyComponent->Initialize(PhysicalBody,LeftPhysicsHand,RightPhysicsHand,LeftPhysicsArm,RightPhysicsArm,PhysicsGrab,MeshGeometryScale);
	PrimaryActorTick.AddPrerequisite(PhysicalBodyComponent, PhysicalBodyComponent->GetPostMovementTick());
	if (GetMesh()->GetSkeletalMeshAsset())
	{
		GetMesh()->PrimaryComponentTick.TickGroup=TG_PostPhysics;
		GetMesh()->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
		PhysicalVisual->InitializeVisual(PhysicalBody,LeftPhysicsHand,RightPhysicsHand,GetMesh());
		ModelContacts->Initialize(PhysicalVisual->GetVisualMesh());
	}
	ConditionComponent->OnSnapshotChanged.AddUObject(this,&ACatCharacter::RefreshPhysicalCondition);
	RefreshPhysicalCondition();

	InitializeAbilityActorInfo();
}

// 服务端占有流程：父类先建立 Controller/Owner/PlayerState 关系，再把 Character=this 的 Owner/Avatar 建立时机交给 ASC。
// authority 先准备正式库存容量，再分别调用 ASC 默认授予、Equipment starter 选择和抄网补给；角色不读取具体定义或库存格。
void ACatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ConfigureCharacterMovementAuthority();
	PhysicalBodyComponent->BeginControlEpochFromAuthority();
	InitializeAbilityActorInfo();
	if (HasAuthority())
	{
		if (InventoryComponent)
		{
			InventoryComponent->SetInventorySlotCountFromAuthority(ResolveInitialPlayerInventorySlotCapacity());
		}
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->GrantConfiguredDefaultAbilitySetFromAuthority();
		}
		if (EquipmentComponent)
		{
		}
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
	ConfigureCharacterMovementAuthority();
	InitializeAbilityActorInfo();
}

// 失去占有流程：身份和 ASC 尚有效时先进入 GameMode 协调入口释放操作位并托管资源；随后取消身体 Ability 并断开占有。父类返回后清 ActorInfo，存档捕获由 Controller 的后置 Pawn 通知执行。
void ACatCharacter::UnPossessed()
{
	// 生命周期释放流程：失去控制不能让嘴叼物跟随一个无主 Pawn 停留；对象自己的释放方法负责恢复地面状态，随后按 expected actor 清空引用。
	if (ACatFishPickupActor* Fish = Cast<ACatFishPickupActor>(MouthCarriedActor)) Fish->ReleaseMouthCarryFromAuthority(GetActorLocation());
	else if (ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(MouthCarriedActor)) Guard->ReleaseMouthCarryFromAuthority(GetActorLocation());
	PhysicalBodyComponent->ReleaseConnectionsFromAuthority(TEXT("Unpossessed"));
	PhysicalBodyComponent->BeginControlEpochFromAuthority();
	ACatfishingGameModeBase::HandleCharacterUnavailable(this);
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

// 最终清理流程：直接 Destroy 或无占有的身体也先经过同一 GameMode 幂等协调入口；随后撤销默认授予、取消 Ability 并清 ActorInfo，最后交父类销毁组件。
void ACatCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 销毁路径复用同一 expected-actor 释放，覆盖直接 Destroy 而没有先走 UnPossessed 的服务器清理。
	if (ACatFishPickupActor* Fish = Cast<ACatFishPickupActor>(MouthCarriedActor)) Fish->ReleaseMouthCarryFromAuthority(GetActorLocation());
	else if (ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(MouthCarriedActor)) Guard->ReleaseMouthCarryFromAuthority(GetActorLocation());
	ConditionComponent->OnSnapshotChanged.RemoveAll(this);
	PhysicalBodyComponent->ReleaseConnectionsFromAuthority(TEXT("EndPlay"));
	ACatfishingGameModeBase::HandleCharacterUnavailable(this);
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RevokeConfiguredDefaultAbilitySet();
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->ClearActorInfo();
	}
	Super::EndPlay(EndPlayReason);
}

// ActorInfo 初始化流程：角色只把自身交给项目 ASC；ASC 负责 runtime gate、复制策略和一次性身体属性播种，失败时保留生命周期重试机会。
void ACatCharacter::InitializeAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}
	if (AbilitySystemComponent->InitializeCharacterOwnerAvatar(this))
	{
		AbilitySystemComponent->InitializeCharacterAttributesFromDefinition(CatDefinitionId);
	}
}

void ACatCharacter::ConfigureCharacterMovementAuthority()
{
	SetReplicateMovement(false);
	bUseControllerRotationYaw=false;
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->CanCharacterStepUpOn=ECB_No;
	GetCharacterMovement()->SetUpdatedComponent(GetCapsuleComponent());
	GetCharacterMovement()->bRunPhysicsWithNoController=true;
	GetCharacterMovement()->bEnablePhysicsInteraction=false;
	GetCharacterMovement()->Mass=4.0f;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	GetCharacterMovement()->SetComponentTickEnabled(false);
	GetMesh()->bOnlyAllowAutonomousTickPose=false;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
}
// 身体条件刷新流程：服务器先按最新 Downed 快照更新物理移动；进入倒地时只读取角色当前单嘴引用，分别调用鱼或鱼护的落地释放，随后由各 Actor 清理同一 expected-actor 引用。
// 非服务器只接收复制结果，不在客户端改移动或世界物归属。
void ACatCharacter::RefreshPhysicalCondition()
{
	if (HasAuthority())
	{
		PhysicalBodyComponent->SetLocomotionEnabledFromAuthority(!ConditionComponent->GetSnapshot().bDowned,TEXT("ConditionChanged"));
		if (ConditionComponent->GetSnapshot().bDowned)
		{
			if (ACatFishPickupActor* Fish = Cast<ACatFishPickupActor>(MouthCarriedActor)) Fish->ReleaseMouthCarryFromAuthority(GetActorLocation());
			else if (ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(MouthCarriedActor)) Guard->ReleaseMouthCarryFromAuthority(GetActorLocation());
		}
	}
}
void ACatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhysicalVisual->SetHandReachState(PhysicsGrab->IsReaching(true),PhysicsGrab->IsReaching(false));
}
FVector ACatCharacter::GetVelocity() const { return PhysicalBodyComponent ? PhysicalBodyComponent->GetVelocity() : Super::GetVelocity(); }
float ACatCharacter::GetDefaultHalfHeight() const
{
	return PhysicalBodyComponent && PhysicalBodyComponent->GetBody() ? PhysicalBodyComponent->GetStandRootHeightCm()
		: 20.0f * GetCapsuleComponent()->GetRelativeScale3D().GetAbs().Z;
}
FVector ACatCharacter::GetBodyFootPointWorld() const { return PhysicalBodyComponent->GetSupportFootPointWorld(); }
double ACatCharacter::GetBodyStandRootHeightCm() const { return GetDefaultHalfHeight(); }
UMeshComponent* ACatCharacter::GetBodyVisualMesh() const
{
	return PhysicalVisual && PhysicalVisual->GetVisualMesh() ? static_cast<UMeshComponent*>(PhysicalVisual->GetVisualMesh()) : GetMesh();
}
void ACatCharacter::FaceRotation(FRotator NewControlRotation,float DeltaTime)
{
	if (PhysicalBodyComponent) PhysicalBodyComponent->SetViewIntent(NewControlRotation);
	else Super::FaceRotation(NewControlRotation,DeltaTime);
}
bool ACatCharacter::TeleportTo(const FVector& DestLocation,const FRotator& DestRotation,bool bIsATest,bool bNoCheck)
{
	if (!PhysicalBodyComponent || !PhysicalBodyComponent->GetBody()) return Super::TeleportTo(DestLocation,DestRotation,bIsATest,bNoCheck);
	if (!HasAuthority() || DestLocation.ContainsNaN() || DestRotation.ContainsNaN()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicalTeleport),false,this);
	const FTransform Destination(DestRotation,DestLocation,GetActorScale3D());
	PhysicalBodyComponent->AppendSupportQueryIgnores(Params);
	if (!bNoCheck && GetWorld()->OverlapBlockingTestByChannel(DestLocation, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius() * .99,
			GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .99), Params)) return false;
	return bIsATest || PhysicalBodyComponent->TeleportBodyFromAuthority(Destination,TEXT("CharacterTeleport"));
}
