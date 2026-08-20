#include "Environment/CatChumFieldReplicationComponent.h"

#include "Engine/World.h"
#include "Environment/Presentation/CatChumFieldPresentationActor.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// FastArraySerializer 回调：客户端上一个元素即将从复制数组中被移除时触发，转发给组件做表现清理
void FCatChumFieldPublicItem::PreReplicatedRemove(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedRemove(FieldId);
}

// FastArraySerializer 回调：客户端新增一个元素后触发，转发给组件生成表现 Actor
void FCatChumFieldPublicItem::PostReplicatedAdd(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedAdd(*this);
}

// FastArraySerializer 回调：客户端某个已存在元素的字段发生变化后触发（如半径/中心点被服务器更新）
void FCatChumFieldPublicItem::PostReplicatedChange(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedChange(*this);
}

UCatChumFieldReplicationComponent::UCatChumFieldReplicationComponent()
{
	SetIsReplicatedByDefault(true); // 该组件默认参与网络复制
	PublicFields.Owner = this; // FastArray 需要反向持有 Owner 才能在回调里找回组件
}

void UCatChumFieldReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	// 编辑器热重载/客户端连接时机可能早于首次 OnRep，这里主动补一次 Owner 绑定和表现同步
	PublicFields.Owner = this;
	OnRep_PublicFields();
}

void UCatChumFieldReplicationComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 只复制这一个 FastArray 属性：公开窝料场列表
	DOREPLIFETIME(ThisClass, PublicFields);
}

// authority 唯一写口：把窝料场私有权威状态（Subsystem 里的 FCatChumFieldState）投影成客户端可见的公开精简字段
void UCatChumFieldReplicationComponent::ReconcileFieldFromAuthority(const FCatChumFieldState& Field)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Field.FieldId.IsValid()) return; // 只允许服务器调用，且必须是合法 FieldId
	// 按 FieldId 在已复制列表里查找是否已存在这个窝料场（新增 vs 更新）
	FCatChumFieldPublicItem* Item = PublicFields.Items.FindByPredicate(
		[&Field](const FCatChumFieldPublicItem& Candidate) { return Candidate.FieldId == Field.FieldId; });
	const bool bAdded = Item == nullptr; // 没找到说明是首次广播这个窝料场
	if (bAdded) Item = &PublicFields.Items.AddDefaulted_GetRef(); // 新增一个默认项并拿到引用以便填充
	// 逐字段从权威状态拷贝到公开精简结构；只暴露客户端表现需要的最小信息
	Item->FieldId = Field.FieldId;
	Item->WaterRegion = Field.WaterRegion;
	Item->ChumDefinitionId = Field.ChumDefinitionId;
	Item->CenterWorldPoint = Field.CenterWorldPoint;
	Item->RadiusCentimeters = Field.Influence.RadiusCentimeters;
	Item->StartServerTime = Field.StartServerTime;
	Item->ExpireServerTime = Field.ExpireServerTime;
	Item->Source = Field.Source;
	Item->PresentationId = Field.Influence.PresentationId;
	PublicFields.MarkItemDirty(*Item); // 标记该 FastArray 元素为脏，驱动增量复制
	ReconcilePresentation(*Item, bAdded); // 服务器本地（含监听服务器）也要同步生成/刷新表现 Actor
	if (bAdded) OnFieldAdded.Broadcast(*Item); else OnFieldChanged.Broadcast(*Item); // 新增和更新走不同的本地事件
	GetOwner()->ForceNetUpdate(); // 打窝是低频离散事件，主动请求一次网络更新以降低客户端可见延迟
}

// authority 唯一写口：窝料场过期或被清理时，从复制列表移除并驱动客户端销毁表现
void UCatChumFieldReplicationComponent::RemoveFieldFromAuthority(const FGuid FieldId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FieldId.IsValid()) return;
	const int32 Index = PublicFields.Items.IndexOfByPredicate(
		[FieldId](const FCatChumFieldPublicItem& Item) { return Item.FieldId == FieldId; });
	if (Index == INDEX_NONE) return; // 找不到说明已经被移除过，幂等直接返回
	PublicFields.Items.RemoveAt(Index);
	PublicFields.MarkArrayDirty(); // 标记整个数组结构变化（元素数量变化用 MarkArrayDirty 而非 MarkItemDirty）
	HandleReplicatedRemove(FieldId); // 服务器本地也走一遍和客户端相同的移除处理，保证表现一致
	GetOwner()->ForceNetUpdate();
}

// 客户端 RepNotify：整个 FastArray 复制到达后触发一次；这里只需要把 Owner 指针补上供后续回调使用
void UCatChumFieldReplicationComponent::OnRep_PublicFields()
{
	PublicFields.Owner = this;
}

// 客户端收到新增元素时：先按需生成/刷新表现 Actor，再广播事件给上层（UI/音效等）订阅
void UCatChumFieldReplicationComponent::HandleReplicatedAdd(const FCatChumFieldPublicItem& Item)
{
	ReconcilePresentation(Item, true);
	OnFieldAdded.Broadcast(Item);
}

// 客户端收到已存在元素的字段更新时：刷新表现 Actor 状态并广播变化事件
void UCatChumFieldReplicationComponent::HandleReplicatedChange(const FCatChumFieldPublicItem& Item)
{
	ReconcilePresentation(Item, false);
	OnFieldChanged.Broadcast(Item);
}

// 服务器和客户端共用的移除处理：销毁对应的表现 Actor 并广播移除事件
void UCatChumFieldReplicationComponent::HandleReplicatedRemove(const FGuid FieldId)
{
	if (TObjectPtr<ACatChumFieldPresentationActor>* Actor = PresentationActors.Find(FieldId))
	{
		if (*Actor)
		{
			(*Actor)->NotifyFieldRemoved(); // 先通知表现 Actor 播放消失效果/停止逻辑
			(*Actor)->Destroy(); // 再实际销毁 Actor
		}
		PresentationActors.Remove(FieldId); // 从缓存表里摘除，避免野指针残留
	}
	OnFieldRemoved.Broadcast(FieldId);
}

// 按当前公开状态生成或刷新窝料场的表现 Actor；只在有视觉表现意义的端（非专用服务器）执行
void UCatChumFieldReplicationComponent::ReconcilePresentation(
	const FCatChumFieldPublicItem& Item, const bool bAdded)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return; // 专用服务器没有渲染需求，跳过表现层
	ACatChumFieldPresentationActor* Actor = PresentationActors.FindRef(Item.FieldId); // 先看是否已经生成过表现 Actor
	if (!IsValid(Actor))
	{
		// 优先使用项目配置的蓝图子类，缺失或类型不符时回退原生基类。
		const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
		UClass* PresentationClass = Presentation ? Presentation->ChumFieldPresentationClass.LoadSynchronous() : nullptr;
		if (!PresentationClass || !PresentationClass->IsChildOf(ACatChumFieldPresentationActor::StaticClass()))
		{
			PresentationClass = ACatChumFieldPresentationActor::StaticClass();
		}
		// 在窝料场中心点生成表现 Actor（旋转固定为零，视觉朝向由蓝图/特效自行处理）
		Actor = World->SpawnActor<ACatChumFieldPresentationActor>(
			PresentationClass, Item.CenterWorldPoint, FRotator::ZeroRotator);
		if (!Actor) return; // 生成失败（例如关卡正在卸载）直接放弃，等待下次状态变化再重试
		PresentationActors.Add(Item.FieldId, Actor); // 记入缓存表，后续更新/移除靠 FieldId 找回
		Actor->ApplyPublicState(Item, true); // 首次生成按"新增"语义应用一次完整状态
		return;
	}
	// 已存在则直接按最新公开状态刷新（bAdded 透传给 Actor 决定是否需要重播"出现"效果）
	Actor->ApplyPublicState(Item, bAdded);
}
