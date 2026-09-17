#include "Character/CatCharacter.h"
#include "Fishing/CatFishingService.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Interaction/Carry/CatCarryableActor.h"
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
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Attributes/CatGrowthAttributeSet.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/Animation/CatForceReactionComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Growth/CatGrowthComponent.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Net/UnrealNetwork.h"

// 构造流程：一次创建 Character-owned ASC/AttributeSet、离散身体状态、吃鱼成长、正式随身库存和局内装备组件；只开启组件复制，ActorInfo、属性初值与 Ability 仍由显式 runtime gate 启动。
ACatCharacter::ACatCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCatCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	AbilitySystemComponent = CreateDefaultSubobject<UCatAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	SurvivalAttributes = CreateDefaultSubobject<UCatSurvivalAttributeSet>(TEXT("SurvivalAttributes"));
	GrowthAttributes = CreateDefaultSubobject<UCatGrowthAttributeSet>(TEXT("GrowthAttributes"));
	AbilitySystemComponent->AddAttributeSetSubobject(GrowthAttributes.Get());
	ItemAbilities = CreateDefaultSubobject<UCatItemAbilityComponent>(TEXT("ItemAbilities"));
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
	SetReplicateMovement(true);
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
	CreateDefaultSubobject<UCatForceReactionComponent>(TEXT("ForceReaction"));

}

void ACatCharacter::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	// 相机裁决流程：先让钓鱼表现组件尝试提供持杆视角；没有活动钓鱼镜头或组件尚未就绪时，回到 ACharacter/蓝图相机，避免普通移动视角被 C++ 抢占。
	if (!FishingCameraComponent || !FishingCameraComponent->TryGetCameraView(DeltaTime, OutResult))
	{
		Super::CalcCamera(DeltaTime, OutResult);
		if (IsLocallyControlled() && !HasAuthority())
			if (const auto* Movement = Cast<UCatCharacterMovementComponent>(GetCharacterMovement()))
			{
				const FVector Offset = Movement->GetOwnerCorrectionVisualOffset();
				if (!Offset.IsNearlyZero())
				{
					FHitResult Hit;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(CatOwnerCorrectionCamera), false, this);
					const FVector Target = OutResult.Location+Offset;
					const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, OutResult.Location, Target,
						FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(10), Params);
					OutResult.Location = bBlocked ? Hit.Location : Target;
				}
			}
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

// 一次性表现广播落地点：抄网由统一物品 Use 提交，挥网与提竿等事件都由服务器确认后送达发起端和旁观端。
void ACatCharacter::Multicast_PlayCosmeticEvent_Implementation(const FGameplayTag EventTag)
{
	// 挥网不再使用旧 Ability 的本地预测；两种角色均由 Blueprint 调用角色 Montage 映射。
	const bool bServerConfirmed = EventTag == CatFishingAbilityTags::Cosmetic_Fishing_ScoopSwing
		|| EventTag == CatFishingAbilityTags::Cosmetic_Fishing_HookPull
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
// 3. 若对象支持共同携带，立即推进携带代次，使旧 Q 请求不能作用于重新叼起的同一 Actor。
// 4. 绑定销毁回调并强制复制，让附件表现只消费这份已建立的权威事实。
bool ACatCharacter::TryClaimMouthCarriedActorFromAuthority(AActor* ExpectedActor)
{
	if (!HasAuthority() || !IsValid(ExpectedActor) || ExpectedActor->IsActorBeingDestroyed() || ExpectedActor->GetWorld() != GetWorld()
		|| MouthCarriedActor != nullptr)
	{
		return false;
	}
	MouthCarriedActor = ExpectedActor;
	// 每次成功认领推进携带代次，旧输入不能作用于重新叼起的同一实物。
	if (ACatCarryableActor* Carryable = Cast<ACatCarryableActor>(ExpectedActor)) Carryable->BeginCarryRevisionFromAuthority();
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
	CacheInitialMeshOffset(GetMesh()->GetRelativeLocation(), GetMesh()->GetRelativeRotation());
	ConfigureCharacterMovementAuthority();
	PhysicalBodyComponent->UseCharacterMovement(CastChecked<UCatCharacterMovementComponent>(GetCharacterMovement()));
	PhysicalBodyComponent->ConfigureMovementDefaults(GetCharacterMovement()->JumpZVelocity,
		GetCharacterMovement()->GravityScale, GetCharacterMovement()->MaxWalkSpeed);
	PhysicalBodyComponent->Initialize(PhysicalBody,LeftPhysicsHand,RightPhysicsHand,LeftPhysicsArm,RightPhysicsArm,PhysicsGrab,MeshGeometryScale);
	GetCharacterMovement()->AddTickPrerequisiteComponent(PhysicalBodyComponent);
	PrimaryActorTick.AddPrerequisite(GetCharacterMovement(), GetCharacterMovement()->PrimaryComponentTick);
	if (GetMesh()->GetSkeletalMeshAsset())
	{
		GetMesh()->PrimaryComponentTick.TickGroup=TG_PostPhysics;
		GetMesh()->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
		PhysicalVisual->InitializeVisual(PhysicalBody,LeftPhysicsHand,RightPhysicsHand,GetMesh());
		ModelContacts->Initialize(PhysicalVisual->GetVisualMesh());
	}
	AbilitySystemComponent->RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ACatCharacter::RefreshPhysicalCondition);
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
			// T02：首次和重新占有共用背包的基础值＋本局成长，不能覆盖已扩出的格数。
			CastChecked<UCatBackPackComponent>(InventoryComponent)->InitializePlayerInventorySlotCapacityFromAuthority();
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
	if (ACatCarryableActor* Item = Cast<ACatCarryableActor>(MouthCarriedActor)) Item->ReleaseMouthCarryFromAuthority(GetActorLocation());
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
	if (ACatCarryableActor* Item = Cast<ACatCarryableActor>(MouthCarriedActor)) Item->ReleaseMouthCarryFromAuthority(GetActorLocation());
	AbilitySystemComponent->RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
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
	SetReplicateMovement(true);
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
	GetCharacterMovement()->PrimaryComponentTick.TickGroup=TG_PostPhysics;
	GetCharacterMovement()->SetComponentTickEnabled(true);
	GetMesh()->bOnlyAllowAutonomousTickPose=false;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
}
// 身体条件刷新流程：服务器按最新 Downed 快照更新物理移动；进入倒地时只读取角色当前单嘴引用，分别调用鱼或鱼护的落地释放，随后由各 Actor 清理同一 expected-actor 引用。
// 非服务器只接收复制结果，不在客户端改移动或世界物归属。
//
// 倒地 ≠ 不能动（猫册 §3.1.5「倒地者可缓慢爬行」）。
// 墓碑（2026-09-12）：这里原本是 SetLocomotionEnabledFromAuthority(!bDowned)——倒地即关掉移动意图与地面支撑，
// 身体只剩被推被拖，和设计写的「可缓慢爬行」正好相反，单人玩家因此没有任何自救位移。
// 现在移动始终开着，倒地只把速度压到爬行倍率并禁止跳跃。
void ACatCharacter::RefreshPhysicalCondition(FGameplayTag Tag, int32 Count)
{
	if (HasAuthority())
	{
		PhysicalBodyComponent->SetLocomotionEnabledFromAuthority(true, TEXT("ConditionChanged"));
		RefreshLocomotionSpeedScale();
		if (AbilitySystemComponent->HasMatchingGameplayTag(CatStateTags::Downed))
		{
			// 保留钓鱼负责人现有退出入口：任何来源的倒地 Tag 都释放操作者，不另存倒地副本。
			if (auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>()) Fishing->ReleaseFishingOperatorForCharacter(this);
			if (ACatCarryableActor* Item = Cast<ACatCarryableActor>(MouthCarriedActor)) Item->ReleaseMouthCarryFromAuthority(GetActorLocation());
		}
	}
}

// 速度缩放合成流程：爬行倍率（倒地）与三选一「移动速度 +10%」（加算、上限 +30%）在这里相乘后一次写进物理身体。
// 只有服务器合成；客户端读复制值，不自己算加成。
void ACatCharacter::RefreshLocomotionSpeedScale()
{
	if (!HasAuthority() || !PhysicalBodyComponent || !ConditionComponent)
	{
		return;
	}
	const bool bDowned = AbilitySystemComponent->HasMatchingGameplayTag(CatStateTags::Downed);
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	const double CrawlScale = bDowned && Settings && FMath::IsFinite(Settings->DownedCrawlSpeedScale)
		? FMath::Max(0.0, Settings->DownedCrawlSpeedScale) : 1.0;
	const double GrowthBonus = GrowthComponent
		? GrowthComponent->GetTotalMagnitude(ECatGrowthOptionId::MoveSpeed) : 0.0;
	const double GrowthScale = FMath::IsFinite(GrowthBonus) ? FMath::Max(0.0, 1.0 + GrowthBonus) : 1.0;
	PhysicalBodyComponent->SetLocomotionSpeedScaleFromAuthority(CrawlScale * GrowthScale, bDowned,
		bDowned ? TEXT("DownedCrawl") : TEXT("ConditionOrGrowthChanged"));
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
