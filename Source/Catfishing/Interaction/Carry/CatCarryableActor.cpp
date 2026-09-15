#include "Interaction/Carry/CatCarryableActor.h"

#include "Components/SceneComponent.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "Inventory/CatWorldDropProtectionComponent.h"

// 携带物共同复制诊断；按 Actor 串联各物体自己的服务器拾取与释放日志，不把同步规则归到某一种物品。
DEFINE_LOG_CATEGORY_STATIC(LogCatCarry, Log, All);

// 父类登记后仅追加携带代次，客户端的附件收敛仍由引擎通知驱动。
void ACatCarryableActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CarryRevision);
}

// 服务器每次取得嘴部占用后推进代次；即使原 Actor 被重新叼起，旧输入也无法匹配新携带。
void ACatCarryableActor::BeginCarryRevisionFromAuthority()
{
	if (HasAuthority()) { ++CarryRevision; ForceNetUpdate(); }
}

// 没有库存身份的世界物无需制造库存条目，默认返回空。
UCatInventoryItemInstance* ACatCarryableActor::GetCarriedInventoryItem() const { return nullptr; }

// 默认形状已由共同落点求解核验；无附加姿态需求的物品原样接受候选。
bool ACatCarryableActor::PrepareCarryRelease(ACatCharacter* Character, FTransform& Transform, bool bThrow) const { return true; }

// 无专属表现或效果时共同提交已经完成全部工作。
void ACatCarryableActor::OnCarryReleased(ACatCharacter* Character, bool bThrow) {}

// 主动丢弃流程：
// 1. 读取 Controller 当前 Pawn、根刚体和掉落配置，先确认服务器、本人嘴部占用、身体状态、可移动刚体和速度配置都有效。
// 2. 再用统一世界释放查询求落点，并交给派生物品补充姿态、形状或水面等专属预检。
// 3. 任一条件失败只记录拒绝并继续叼着原 Actor；全部通过才把 RPC RequestId 交给提交层，旧测试入口未传 ID 时才补一个新号。
bool ACatCarryableActor::DropFromAuthority(AController* Controller, FGuid RequestId)
{
	ACatCharacter* Character = IsValid(Controller) ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetRootComponent());
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	FTransform Transform;
	if (!HasAuthority() || IsActorBeingDestroyed() || !Character || Character->GetWorld() != GetWorld()
		|| Character->GetController() != Controller || Character->GetMouthCarriedActor() != this
		|| GetAttachParentActor() != Character || !Character->GetConditionComponent()
		|| Character->GetConditionComponent()->GetSnapshot().bDowned || !Body || !Body->IsRegistered()
		|| Body->Mobility != EComponentMobility::Movable || !Body->GetBodySetup()
		|| Body->GetBodySetup()->AggGeom.GetElementCount() == 0
		|| Body->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple || !Settings
		|| !FMath::IsFinite(Settings->DropForwardSpeed) || Settings->DropForwardSpeed < 0
		|| !FMath::IsFinite(Settings->DropUpwardSpeed) || Settings->DropUpwardSpeed < 0
		|| !UCatInventoryStatics::FindWorldReleaseTransform(Character, this, ECatInventoryWorldAction::Drop, *Settings, Transform)
		|| !PrepareCarryRelease(Character, Transform, true))
	{
		UE_LOG(LogCatCarry, Warning, TEXT("Event=carry_drop_rejected Actor=%s Player=%s Reason=IdentityPhysicsOrSpace World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*GetName(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
		return false;
	}
	return CommitCarryRelease(Character, Transform, true, RequestId.IsValid() ? RequestId : FGuid::NewGuid());
}

// 强制释放流程：不检查玩家输入资格，只确认服务器仍由该角色嘴部持有；保留世界缩放，以给定位置和物品落地策略生成姿态，再用独立 RequestId 走同一个提交。
void ACatCarryableActor::ReleaseMouthCarryFromAuthority(const FVector& DropLocation)
{
	ACatCharacter* Character = Cast<ACatCharacter>(GetAttachParentActor());
	if (!HasAuthority() || !Character || Character->GetMouthCarriedActor() != this) return;
	FTransform Transform(GetActorRotation(), DropLocation, GetActorScale3D());
	if (PrepareCarryRelease(Character, Transform, false)) CommitCarryRelease(Character, Transform, false, FGuid::NewGuid());
}

// 释放提交：先用同一个 RequestId 独占实际库存条目（若存在），再清嘴部、解绑原 Actor、更新实例宿主和物品表现。
// 主动 Q 必须证明实例仍在角色原槽，强制生命周期释放允许原槽已被宿主清理；最后共同恢复根碰撞与运动，发布库存和两端复制，容器内容不迁移。
bool ACatCarryableActor::CommitCarryRelease(ACatCharacter* Character, const FTransform& Transform, bool bThrow, FGuid RequestId)
{
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetRootComponent());
	if (!Character || !Body || Character->GetMouthCarriedActor() != this) return false;
	UCatInventoryItemInstance* Item = GetCarriedInventoryItem();
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	const int32 Slot = Item && Inventory ? Inventory->FindInventorySlotIndexFromInstanceId(Item->GetItemInstanceId()) : INDEX_NONE;
	const FGuid Transaction = RequestId;
	if (Slot != INDEX_NONE && (!Inventory->GetInventoryEntryAtSlot(Slot)
		|| Inventory->GetInventoryEntryAtSlot(Slot)->Instance != Item
		|| !Inventory->PrepareRemovalFromAuthority(Transaction, {Item->GetItemInstanceId()}))) return false;
	// 主动 Q 必须证明本体还归旧宿主且能从正确槽预留；宿主退出时原槽可能已先清理，强制释放仍要解除残留嘴部占用。
	if (bThrow && Item && Item->GetRuntimeOwnerActor() == Character && Slot == INDEX_NONE) return false;
	if (Slot != INDEX_NONE) Inventory->FinishRemovalFromAuthority(Transaction, true, false);
	Character->ReleaseMouthCarriedActorFromAuthority(this);
	Body->SetSimulatePhysics(false);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	if (Item) Item->SetRuntimeOwnerActor(this);
	OnCarryReleased(Character, bThrow);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	Body->SetPhysicsLinearVelocity(bThrow
		? Character->GetActorForwardVector().GetSafeNormal2D() * Settings->DropForwardSpeed + FVector(0, 0, Settings->DropUpwardSpeed)
		: FVector::ZeroVector);
	if (bThrow) UCatWorldDropProtectionComponent::ArmFromAuthority(this);
	ForceNetUpdate();
	Character->ForceNetUpdate();
	if (Slot != INDEX_NONE) Inventory->BroadcastInventoryChange(Slot);
	UE_LOG(LogCatCarry, Log, TEXT("Event=carry_released RequestId=%s Actor=%s Player=%s CarryRevision=%u Throw=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*RequestId.ToString(), *GetName(), *GetNameSafe(Character), CarryRevision, bThrow, *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
	return true;
}

// 附件通知接收流程：保留服务器写入的 AttachmentReplication，只拦截父类立即附着的副作用。
// 引擎会在整批 RepNotify 结束及未解析引用补齐后调用 PostRepNotifies，因此这里保持空实现，不建立定时重试或第二份 pending 状态。
void ACatCarryableActor::OnRep_AttachmentReplication()
{
}

// 共同收敛流程：
// 1. authority 不消费自己的复制通知；客户端先按服务器 bRepPhysics 解除旧附件或关闭刚体，Sync 同时移除过期物理复制目标。
// 2. 物理包到达时不应用旧嘴部附件；非物理附件缺少组件引用时只暂时解绑，等引用映射后自然重入。
// 3. 原样应用服务器的附件和相对变换；新副本的初始缩放可能已是相对比例，不能用客户端暂时的世界尺寸反算并覆盖它。
// 4. 最后通知派生类刷新网格姿态、碰撞和焦点表现；重复通知只刷新表现，不写第二份携带状态。
void ACatCarryableActor::PostRepNotifies()
{
	Super::PostRepNotifies();
	if (HasAuthority() || !RootComponent) return;
	AActor* PreviousParent = GetAttachParentActor();
	const bool bPreviouslySimulating = RootComponent->IsSimulatingPhysics();
	if (GetReplicatedMovement().bRepPhysics)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	SyncReplicatedPhysicsSimulation();
	if (!GetReplicatedMovement().bRepPhysics)
	{
		// 服务器发送实际父组件。引用尚未映射时不能退回角色胶囊；映射完成会再次进入此批末入口。
		if (AttachmentReplication.AttachParent && !AttachmentReplication.AttachComponent)
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			RefreshCarryPresentation(false);
			return;
		}
		Super::OnRep_AttachmentReplication();
	}
	RefreshCarryPresentation(false);
	if (PreviousParent != GetAttachParentActor() || bPreviouslySimulating != RootComponent->IsSimulatingPhysics())
	{
		UE_LOG(LogCatCarry, Log,
			TEXT("Event=carry_replication_reconciled Actor=%s Class=%s Owner=%s PreviousParent=%s Parent=%s Socket=%s RepPhysics=%d Simulating=%d World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*GetName(), *GetClass()->GetName(), *GetNameSafe(GetOwner()), *GetNameSafe(PreviousParent), *GetNameSafe(GetAttachParentActor()),
			*RootComponent->GetAttachSocketName().ToString(), GetReplicatedMovement().bRepPhysics, RootComponent->IsSimulatingPhysics(),
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	}
}

// 默认表现刷新流程：没有附着专用外观的实物不需要额外处理；派生类只在这里更新网格姿态、碰撞或焦点表现。
// bForceRefresh 表示资源或身份刚变化，需要忽略碰撞状态的快速返回；共同基类仍是根附件和物理模拟的唯一客户端收口。
void ACatCarryableActor::RefreshCarryPresentation(bool bForceRefresh)
{
}
