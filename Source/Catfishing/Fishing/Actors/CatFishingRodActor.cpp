#include "Fishing/Actors/CatFishingRodActor.h"

#include "Components/SceneComponent.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSettings.h"
#include "Net/UnrealNetwork.h"

// 构造流程：创建表现根、权威锚点和默认复制姿态；这里只搭好场景骨架，真实 Actor/Item 身份稍后由服务器初始化。
ACatFishingRodActor::ACatFishingRodActor()
{
	// 表现 Actor 需要复制自身存在与 Transform，但玩法权威不在这里，Tick 全关以省性能。
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false; // 不强制全图相关性，交给引擎按距离/视锥裁剪
	bNetUseOwnerRelevancy = false;
	bOnlyRelevantToOwner = false; // 其他玩家也要能看到这根竿，不能只对 Owner 复制
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// SceneRoot 是根组件，其余锚点都挂在它下面，整体随 Actor Transform 移动
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// VisualRoot 承载美术表现（皮肤/特效），与权威判定用的锚点分层，便于蓝图独立驱动视觉
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	// RodTip/Stand/Grip 三个锚点分别对应竿尖(挂线)、插竿点、握持点，供表现和玩法逻辑取世界坐标
	RodTipAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RodTipAnchor"));
	RodTipAnchor->SetupAttachment(SceneRoot);
	StandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("StandAnchor"));
	StandAnchor->SetupAttachment(SceneRoot);
	RightStandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RightStandAnchor"));
	RightStandAnchor->SetupAttachment(SceneRoot);
	LeftStandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("LeftStandAnchor"));
	LeftStandAnchor->SetupAttachment(SceneRoot);
	GripAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("GripAnchor"));
	GripAnchor->SetupAttachment(SceneRoot);
	// 这些锚点的具体相对位置由权威在 ConfigureCanonicalAnchorsFromAuthority 中设置，不允许蓝图继承时手改
	SceneRoot->bEditableWhenInherited = false;
	RodTipAnchor->bEditableWhenInherited = false;
	StandAnchor->bEditableWhenInherited = false;
	RightStandAnchor->bEditableWhenInherited = false;
	LeftStandAnchor->bEditableWhenInherited = false;
	GripAnchor->bEditableWhenInherited = false;
}

void ACatFishingRodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 只复制这一个聚合结构体；所有客户端可见状态都收敛到 PresentationState，减少复制条目
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishingRodActor::InitializeAuthoritativeIdentity(const FGuid InRodActorId, const FGuid InItemInstanceId,
	const FName InRodDefinitionId, const FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState,
	APlayerState* InOperatorPlayerState, const bool bInDeployed, const bool bInBroken)
{
	// 权威身份初始化流程：
	// 1. 先拒绝非服务器、空 ActorId、空 ItemInstanceId、空定义和空 Owner，避免收杆时找不到应归还的实例。
	// 2. 如果身份已经写过，只允许不可变身份相同的重放成功，防止同一个场景 Actor 被复用成另一根竿。
	// 3. 首次写入时构造完整 PresentationState，并立即分发表现变化和请求复制。
	// 只有服务器能设身份；Actor 身份、物品实例身份和定义身份缺一不可，否则收杆时无法证明该还哪一件物品。
	if (!HasAuthority() || !InRodActorId.IsValid() || !InItemInstanceId.IsValid()
		|| InRodDefinitionId.IsNone() || !InOwnerPlayerState)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		// 幂等保护：身份已初始化过后只比较不可变身份；皮肤、操作位和部署状态由后续权威写口单独提交。
		// 这样既能防止同一个 Actor 承载第二根竿，也不会让旧测试夹具的表现字段重放破坏正式身份。
		return PresentationState.RodActorId == InRodActorId
			&& PresentationState.ItemInstanceId == InItemInstanceId
			&& PresentationState.RodDefinitionId == InRodDefinitionId
			&& PresentationState.OwnerPlayerState == InOwnerPlayerState;
	}

	// 记录变更前状态用于表现事件对比（Previous -> Current）
	const FCatFishingRodPresentationState Previous = PresentationState;
	FCatFishingRodPresentationState Next;
	Next.RodActorId = InRodActorId;
	Next.RodActorRevision = 1; // 首次初始化即为 Revision 1，后续每次权威变更递增
	Next.ItemInstanceId = InItemInstanceId;
	Next.RodDefinitionId = InRodDefinitionId;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	Next.OwnerPlayerState = InOwnerPlayerState;
	Next.OperatorPlayerState = InOperatorPlayerState;
	if (InOperatorPlayerState)
	{
		Next.OperatorPlayerStates.Add(InOperatorPlayerState);
	}
	Next.bDeployed = bInDeployed;
	Next.bBroken = bInBroken;
	PresentationState = Next;
	bIdentityInitialized = true;
	// 本地（服务器）立即广播表现变化事件；客户端则依赖下面的 OnRep 触发同样的事件
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate(); // 身份初始化是一次性关键事件，强制立即复制，不等下个 tick 窗口
	return true;
}

bool ACatFishingRodActor::InitializeAuthoritativeIdentity(const FGuid InRodActorId,
	const FName InRodDefinitionId, const FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState,
	APlayerState* InOperatorPlayerState, const bool bInDeployed, const bool bInBroken)
{
	// 兼容初始化流程：
	// 1. 旧自动化夹具只验证场景鱼竿身份和占位容器，不具备真实库存实例。
	// 2. 这里把 ActorId 作为稳定的临时 ItemInstanceId 交给正式入口，保持重放语义一致；正式 PlaceRod 仍必须传 Use 返回的物品实例。
	return InitializeAuthoritativeIdentity(InRodActorId, InRodActorId, InRodDefinitionId, InRodSkinDefinitionId,
		InOwnerPlayerState, InOperatorPlayerState, bInDeployed, bInBroken);
}

bool ACatFishingRodActor::ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip,
	const FTransform& InStand, const FTransform& InGrip)
{
	// 必须在身份初始化“之前”配置锚点（bIdentityInitialized 为真时拒绝），
	// 且三个 Transform 都不能是 NaN，否则后续世界坐标换算会污染整条竿的表现
	if (!HasAuthority() || bIdentityInitialized || InRodTip.ContainsNaN() || InStand.ContainsNaN() || InGrip.ContainsNaN())
	{
		return false;
	}
	// 保存规范的本地 Transform，供 GetXXXWorldTransform 与 Actor 世界 Transform 相乘得到世界坐标
	RodTipCanonicalLocalTransform = InRodTip;
	StandCanonicalLocalTransform = InStand;
	GripCanonicalLocalTransform = InGrip;
	// 同步应用到实际场景组件上，使编辑器/运行时可视化与权威数据一致
	RodTipAnchor->SetRelativeTransform(InRodTip);
	StandAnchor->SetRelativeTransform(InStand);
	RightStandAnchor->SetRelativeTransform(ResolveOperatorStandLocalTransform(0));
	LeftStandAnchor->SetRelativeTransform(ResolveOperatorStandLocalTransform(1));
	GripAnchor->SetRelativeTransform(InGrip);
	return true;
}

bool ACatFishingRodActor::CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next,
	const int64 ExpectedRevision)
{
	// 乐观并发控制：调用方必须带上它读到的旧 Revision，若与当前不一致说明状态已被其他写者改过，拒绝本次提交
	if (!HasAuthority() || !bIdentityInitialized || ExpectedRevision != PresentationState.RodActorRevision)
	{
		return false;
	}
	// 身份类字段（Id/DefinitionId/Owner）不可被这条“可变状态”写口覆盖，只保留传入 Next 里的可变部分
	FCatFishingRodPresentationState Committed = Next;
	Committed.RodActorId = PresentationState.RodActorId;
	Committed.ItemInstanceId = PresentationState.ItemInstanceId;
	Committed.RodDefinitionId = PresentationState.RodDefinitionId;
	Committed.OwnerPlayerState = PresentationState.OwnerPlayerState;
	// 主位只是有序数组首项的兼容镜像，不单独保存模式位；人数从数组实时推导，避免 2→1 后残留协作状态。
	Committed.OperatorPlayerState = Committed.OperatorPlayerStates.IsEmpty()
		? nullptr : Committed.OperatorPlayerStates[0];
	Committed.RodActorRevision = PresentationState.RodActorRevision + 1; // 每次成功提交 Revision 自增一
	const FCatFishingRodPresentationState Previous = PresentationState;
	PresentationState = Committed;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

bool ACatFishingRodActor::SetOperatorFromAuthority(APlayerState* InOperatorPlayerState, const int64 ExpectedRevision)
{
	// 旧接口语义保持为“整组替换”，避免老调用方只清快捷字段却留下数组里的幽灵占位。
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.Reset();
	if (InOperatorPlayerState)
	{
		Next.OperatorPlayerStates.Add(InOperatorPlayerState);
	}
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::AddOperatorFromAuthority(APlayerState* InOperatorPlayerState,
	const int64 ExpectedRevision, int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;
	const int32 FreeSlotIndex = GetFirstFreeOperatorSlotIndex();
	if (!InOperatorPlayerState || FreeSlotIndex == INDEX_NONE
		|| PresentationState.OperatorPlayerStates.Contains(InOperatorPlayerState)
		|| !PresentationState.bDeployed || PresentationState.bBroken)
	{
		return false;
	}
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.Add(InOperatorPlayerState);
	if (!CommitAuthoritativeMutation(Next, ExpectedRevision))
	{
		return false;
	}
	OutSlotIndex = FreeSlotIndex;
	return true;
}

bool ACatFishingRodActor::RemoveOperatorFromAuthority(APlayerState* InOperatorPlayerState,
	const int64 ExpectedRevision, APlayerState*& OutPromotedPrimaryPlayerState)
{
	OutPromotedPrimaryPlayerState = nullptr;
	const int32 ExistingSlotIndex = GetOperatorSlotIndex(InOperatorPlayerState);
	if (ExistingSlotIndex == INDEX_NONE)
	{
		return false;
	}
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.RemoveAt(ExistingSlotIndex);
	if (!CommitAuthoritativeMutation(Next, ExpectedRevision))
	{
		return false;
	}
	if (ExistingSlotIndex == 0 && !PresentationState.OperatorPlayerStates.IsEmpty())
	{
		OutPromotedPrimaryPlayerState = PresentationState.OperatorPlayerStates[0];
	}
	return true;
}

bool ACatFishingRodActor::SetRodSkinFromAuthority(const FName InRodSkinDefinitionId, const int64 ExpectedRevision)
{
	if (InRodSkinDefinitionId.IsNone()) return false; // 空皮肤 ID 视为非法调用，直接拒绝
	FCatFishingRodPresentationState Next = PresentationState;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetBrokenFromAuthority(const bool bInBroken, const int64 ExpectedRevision)
{
	// 断竿是搏斗失败的惩罚结果之一，这里只负责把状态写进表现层，不涉及耐久扣减本身
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bBroken = bInBroken;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetDeployedFromAuthority(const bool bInDeployed, const int64 ExpectedRevision)
{
	// 插竿/收竿切换，驱动蓝图切换竿的摆放动画与碰撞表现
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bDeployed = bInDeployed;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

const FCatFishingRodPresentationState& ACatFishingRodActor::GetPresentationState() const { return PresentationState; }

// 三个世界 Transform 都是“本地规范 Transform 叠乘 Actor 当前世界 Transform”，随 Actor 移动/旋转自动更新
FTransform ACatFishingRodActor::GetRodTipWorldTransform() const { return RodTipCanonicalLocalTransform * GetActorTransform(); }
FTransform ACatFishingRodActor::GetStandWorldTransform() const { return GetOperatorStandWorldTransform(0); }
FTransform ACatFishingRodActor::GetOperatorInteractionWorldTransform() const
{
	return StandCanonicalLocalTransform * GetActorTransform();
}
FTransform ACatFishingRodActor::GetGripWorldTransform() const { return GripCanonicalLocalTransform * GetActorTransform(); }

FTransform ACatFishingRodActor::ResolveOperatorStandLocalTransform(const int32 SlotIndex) const
{
	int32 MaximumSlots = 0;
	double Spacing = 0.0;
	if (SlotIndex < 0 || !GetDefault<UCatFishingSettings>()->TryGetRodOperatorLayout(MaximumSlots, Spacing)
		|| SlotIndex >= MaximumSlots)
	{
		return StandCanonicalLocalTransform;
	}
	// 0/1 是最靠近中心的右/左；2/3 是外侧第二对。数组扩容时无需改变复制结构和占位算法。
	const double PairDistance = (static_cast<double>(SlotIndex / 2) + 0.5) * Spacing;
	const double LateralOffset = SlotIndex % 2 == 0 ? PairDistance : -PairDistance;
	FTransform SlotTransform = StandCanonicalLocalTransform;
	SlotTransform.AddToTranslation(FVector(0.0, LateralOffset, 0.0));
	return SlotTransform;
}

FTransform ACatFishingRodActor::GetOperatorStandWorldTransform(const int32 SlotIndex) const
{
	return ResolveOperatorStandLocalTransform(SlotIndex) * GetActorTransform();
}

int32 ACatFishingRodActor::GetOperatorCount() const
{
	return PresentationState.OperatorPlayerStates.Num();
}

int32 ACatFishingRodActor::GetOperatorSlotIndex(APlayerState* PlayerState) const
{
	return PlayerState ? PresentationState.OperatorPlayerStates.IndexOfByKey(PlayerState) : INDEX_NONE;
}

bool ACatFishingRodActor::IsPrimaryOperator(APlayerState* PlayerState) const
{
	return PlayerState && PresentationState.OperatorPlayerState == PlayerState;
}

int32 ACatFishingRodActor::GetFirstFreeOperatorSlotIndex() const
{
	int32 MaximumSlots = 0;
	double Spacing = 0.0;
	return GetDefault<UCatFishingSettings>()->TryGetRodOperatorLayout(MaximumSlots, Spacing)
		&& PresentationState.OperatorPlayerStates.Num() < MaximumSlots
		? PresentationState.OperatorPlayerStates.Num() : INDEX_NONE;
}

// BeginPlay 流程：先完成 Actor 自身进入 World 的初始化，再补发权威身份可能提前排队的表现变化；没有积压时不触发蓝图事件。
void ACatFishingRodActor::BeginPlay()
{
	Super::BeginPlay();
	// 身份可能在 Actor BeginPlay 之前就由权威初始化完毕（生成时序问题），
	// 那时事件被推迟到这里；BeginPlay 后再把积压的“上一次变化”补发一次。
	if (bHasPendingPresentationNotification)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

// EndPlay 流程：权威端先从 FishingService 注销这根已部署鱼竿，再交还给父类清理；客户端或无 Owner 的临时 Actor 不写服务登记。
void ACatFishingRodActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 只有权威端且已绑定 Owner 时才需要清理服务里的“已部署鱼竿”登记，避免野指针残留。
	if (HasAuthority() && PresentationState.OwnerPlayerState)
	{
		if (UWorld* World = GetWorld())
		{
			if (UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>())
			{
				Fishing->UnregisterDeployedRod(PresentationState.OwnerPlayerState, this);
			}
		}
	}
	// 服务登记已经清掉后再调用父类 EndPlay，避免注销路径读到组件或 World 进入半清理状态。
	Super::EndPlay(EndPlayReason);
}

// 复制回调流程：客户端收到 PresentationState 后只把前后状态交给表现分发层；它不修改权威身份、库存实例或操作位数组。
void ACatFishingRodActor::OnRep_PresentationState(const FCatFishingRodPresentationState& Previous)
{
	// Previous 由引擎在应用新值前自动传入旧值，蓝图可以据此区分皮肤、部署或操作位变化。
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishingRodActor::QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	if (!HasActorBegunPlay())
	{
		// BeginPlay 前不能安全触发蓝图事件（组件/资源可能还没就绪），先把变化排队
		if (!bHasPendingPresentationNotification)
		{
			// 只在“第一次”排队时记录 Previous，保证积压期间多次变化最终仍呈现“最早前值 -> 最新值”的单次跳变
			PendingPreviousPresentationState = Previous;
			bHasPendingPresentationNotification = true;
		}
		PendingCurrentPresentationState = Current;
		return;
	}
	DispatchPresentationChanged(Previous, Current);
}

void ACatFishingRodActor::DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	// 收竿后 Actor 还要活满一个终态复制窗（见 UCatFishingService::PackRod）才销毁，
	// 期间必须立刻从视觉和碰撞上消失，否则玩家会看到一根杵着不走、还挡路的幽灵竿。
	// 放在分发路径而不是权威写口：服务器与每个客户端各自在"得知"这次变化的那一刻本地执行；
	// bHidden 虽是复制属性，但 bActorEnableCollision 不是，只有本地各自执行才能保证两者一致。
	// 判据用 Current 的绝对状态而非 Previous->Current 跃迁：中途加入的客户端首帧 Previous 是默认结构体，
	// 跃迁判据会漏掉正处于死亡窗口里的竿。bDeployed 在竿的正常生命周期里恒为 true，故不会误伤。
	if (Current.RodActorId.IsValid() && !Current.bDeployed)
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
	// 先应用皮肤（视觉资源切换），再广播通用状态变化事件给蓝图做其余表现响应
	BP_ApplyRodSkin(Current.RodSkinDefinitionId);
	BP_OnRodPresentationChanged(Previous, Current);
}
