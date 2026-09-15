#include "Interaction/Carry/CatCarryableActor.h"

#include "Components/SceneComponent.h"

// 携带物共同复制诊断；按 Actor 串联各物体自己的服务器拾取与释放日志，不把同步规则归到某一种物品。
DEFINE_LOG_CATEGORY_STATIC(LogCatCarry, Log, All);

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
