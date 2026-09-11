#include "UI/WorldInfo/CatFishTankWorldInfoComponent.h"

#include "Engine/World.h"
#include "FishContainers/CatFishTankActor.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Run/CatRunSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatFishTankWorldInfo, Log, All);

// 构造流程：沿用父类的无 Tick 锚点与正式 WBP，指定附近摘要距离，再启用组件属性复制。
UCatFishTankWorldInfoComponent::UCatFishTankWorldInfoComponent()
{
	DisplayPolicy = ECatWorldInfoPolicy::NearbySummary;
	DisplayDistanceCentimeters = 500.0f;
	SetIsReplicatedByDefault(true);
}

// 复制登记流程：保留父类字段，再注册整份摘要；点数与就绪资格一起交给 RepNotify 消费。
void UCatFishTankWorldInfoComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OfferingSummary);
}

// 入场流程：先注册公共锚点；仅服务器订阅正式库存，再监听本机 GameState 建立事件并接入已存在的宿主。
// 此时 Actor 可能尚未写入鱼缸容量，因此不主动发布初值，由 Actor 的显式刷新收口。
void UCatFishTankWorldInfoComponent::BeginPlay()
{
	Super::BeginPlay();
	ACatFishTankActor* Tank = Cast<ACatFishTankActor>(GetOwner());
	if (Tank && Tank->HasAuthority())
	{
		ObservedInventory = Tank->GetFishInventoryComponent();
		if (UCatFishOnlyInventoryComponent* Inventory = ObservedInventory.Get())
		{
			InventoryChangedHandle = Inventory->OnInventoryObservedChanged.AddUObject(
				this, &ThisClass::RefreshSummary);
		}
	}
	if (UWorld* World = GetWorld())
	{
		GameStateSetHandle = World->GameStateSetEvent.AddUObject(this, &ThisClass::HandleGameStateSet);
		HandleGameStateSet(World->GetGameState());
	}
}

// 退出流程：用保存的事件源和凭据移除三种订阅，清空弱引用及句柄，最后让父类注销锚点；已销毁的源无需解引用。
void UCatFishTankWorldInfoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCatFishOnlyInventoryComponent* Inventory = ObservedInventory.Get())
	{
		Inventory->OnInventoryObservedChanged.Remove(InventoryChangedHandle);
	}
	if (ACatfishingGameState* GameState = ObservedGameState.Get())
	{
		GameState->OnRunPublicStateChanged.Remove(RunChangedHandle);
	}
	if (UWorld* World = GetWorld())
	{
		World->GameStateSetEvent.Remove(GameStateSetHandle);
	}
	ObservedInventory.Reset();
	ObservedGameState.Reset();
	InventoryChangedHandle.Reset();
	RunChangedHandle.Reset();
	GameStateSetHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

// 宿主切换流程：先移除旧 Run 通知，再保存当前正式 GameState 并订阅；无宿主也通知重读，让旧目标及时变为未知。
void UCatFishTankWorldInfoComponent::HandleGameStateSet(AGameStateBase* GameState)
{
	if (ACatfishingGameState* Previous = ObservedGameState.Get())
	{
		Previous->OnRunPublicStateChanged.Remove(RunChangedHandle);
	}
	RunChangedHandle.Reset();
	ObservedGameState = Cast<ACatfishingGameState>(GameState);
	if (ACatfishingGameState* Current = ObservedGameState.Get())
	{
		RunChangedHandle = Current->OnRunPublicStateChanged.AddUObject(this, &UCatWorldInfoComponent::NotifyInfoChanged);
	}
	NotifyInfoChanged();
}

// 汇总流程：
// 1. 客户端及 Actor 尚未完成父类入场时不计算，避免早到的库存委托发布初始化中间值。
// 2. 从正式库存取得实例快照与实际容量，逐条用真实千克重量调用 Run 分类；无效实例、分类失败或整数溢出使整份点数未知。
// 3. 只有摘要发生变化才替换复制投影、通知本地读者并请求网络更新；不写库存、不提交供品、不调用 RPC。
void UCatFishTankWorldInfoComponent::RefreshSummary()
{
	const ACatFishTankActor* Tank = Cast<ACatFishTankActor>(GetOwner());
	if (!Tank || !Tank->HasAuthority() || !Tank->HasActorBegunPlay()) return;

	FCatFishTankOfferingSummary Next;
	const UCatFishOnlyInventoryComponent* Inventory = Tank->GetFishInventoryComponent();
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	if (Inventory && Settings)
	{
		const TArray<UCatInventoryItemInstance*> Items = Inventory->GetAllItems();
		Next.Capacity = Inventory->GetInventorySlotCount();
		Next.Count = Items.Num();
		Next.Points = 0;
		Next.bReady = Next.Count <= Next.Capacity;
		for (const UCatInventoryItemInstance* Item : Items)
		{
			const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Item);
			ECatOfferingWeightClass WeightClass;
			int32 FishPoints = 0;
			if (!IsValid(Fish) || !Settings->TryClassifyOfferingWeight(Fish->GetFishWeightKilograms(), WeightClass, FishPoints)
				|| FishPoints <= 0 || Next.Points > MAX_int32 - FishPoints)
			{
				Next.bReady = false;
				break;
			}
			Next.Points += FishPoints;
		}
	}
	if (!Next.bReady) Next.Points = INDEX_NONE;
	if (OfferingSummary.bReady == Next.bReady && OfferingSummary.Points == Next.Points
		&& OfferingSummary.Count == Next.Count && OfferingSummary.Capacity == Next.Capacity) return;

	OfferingSummary = Next;
	if (!Next.bReady)
	{
		UE_LOG(LogCatFishTankWorldInfo, Warning,
			TEXT("Event=fish_tank_summary_unavailable World=%s NetMode=%d Authority=%d Role=%d Actor=%s Points=%d Count=%d Capacity=%d Result=InvalidInventoryOrWeight"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), Tank->HasAuthority(), static_cast<int32>(Tank->GetLocalRole()),
			*Tank->GetPathName(), Next.Points, Next.Count, Next.Capacity);
	}
	OnRep_OfferingSummary();
	GetOwner()->ForceNetUpdate();
}

// 本端接收流程：摘要已整体更新，先递增本地信息序号，再记录本端观察；只由发布或复制变化触发，不逐帧输出。
void UCatFishTankWorldInfoComponent::OnRep_OfferingSummary()
{
	NotifyInfoChanged();
	const AActor* Owner = GetOwner();
	UE_LOG(LogCatFishTankWorldInfo, Display,
		TEXT("Event=fish_tank_summary_changed World=%s NetMode=%d Authority=%d Role=%d Actor=%s Ready=%d Points=%d Count=%d Capacity=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), Owner && Owner->HasAuthority(),
		Owner ? static_cast<int32>(Owner->GetLocalRole()) : -1, *GetPathNameSafe(Owner),
		OfferingSummary.bReady, OfferingSummary.Points, OfferingSummary.Count, OfferingSummary.Capacity);
}

// 查询流程：先标记所有输出未知；只有整份摘要就绪才复制三个数值并返回成功，客户端不重算尚未到齐的鱼实例。
bool UCatFishTankWorldInfoComponent::TryGetOfferingSummary(int32& OutPoints, int32& OutCount, int32& OutCapacity) const
{
	OutPoints = OutCount = OutCapacity = INDEX_NONE;
	if (!OfferingSummary.bReady) return false;
	OutPoints = OfferingSummary.Points;
	OutCount = OfferingSummary.Count;
	OutCapacity = OfferingSummary.Capacity;
	return true;
}

// 视图构建流程：清空上一对象内容并过滤隐藏请求；读取复制摘要和本机正式 Run 目标，分别判断就绪。
// 摘要始终给出储备与任务两行，详情才增加数量/容量和非负缺口；缺任一计算前提时保留待同步文本，不改变玩法状态。
bool UCatFishTankWorldInfoComponent::BuildInfo_Implementation(APlayerController* Viewer, const ECatWorldInfoDetail Detail,
	FCatWorldInfoViewData& OutData) const
{
	OutData = FCatWorldInfoViewData();
	if (!Viewer || Detail == ECatWorldInfoDetail::Hidden) return false;
	OutData.Title = NSLOCTEXT("CatWorldInfo", "FishTankTitle", "共享鱼缸");
	const FText Pending = NSLOCTEXT("CatWorldInfo", "FishTankPending", "待同步");
	int32 Points, Count, Capacity;
	const bool bSummaryReady = TryGetOfferingSummary(Points, Count, Capacity);
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	const FCatRunPublicState* Run = GameState ? &GameState->GetRunPublicState() : nullptr;
	const bool bTargetReady = Run && Run->Revision > 0 && Run->Phase.RunId.IsValid()
		&& Run->Phase.DayIndex > 0 && Run->DailyOfferingTarget > 0;
	if (!bSummaryReady || !bTargetReady) OutData.Status = Pending;

	FCatWorldInfoRow& Reserve = OutData.Rows.AddDefaulted_GetRef();
	Reserve.Id = TEXT("OfferingReserve");
	Reserve.Label = NSLOCTEXT("CatWorldInfo", "FishTankReserve", "储备点数");
	Reserve.Value = bSummaryReady ? FText::Format(NSLOCTEXT("CatWorldInfo", "FishTankPoints", "{0} 点"), FText::AsNumber(Points)) : Pending;
	FCatWorldInfoRow& Target = OutData.Rows.AddDefaulted_GetRef();
	Target.Id = TEXT("DailyOfferingTarget");
	Target.Label = NSLOCTEXT("CatWorldInfo", "FishTankTarget", "今日任务");
	Target.Value = bTargetReady ? FText::Format(NSLOCTEXT("CatWorldInfo", "FishTankPoints", "{0} 点"), FText::AsNumber(Run->DailyOfferingTarget)) : Pending;
	if (Detail == ECatWorldInfoDetail::Full)
	{
		FCatWorldInfoRow& FishCount = OutData.Rows.AddDefaulted_GetRef();
		FishCount.Id = TEXT("FishCountCapacity");
		FishCount.Label = NSLOCTEXT("CatWorldInfo", "FishTankCount", "鱼数量 / 容量");
		FishCount.bSummary = false;
		FishCount.Value = bSummaryReady ? FText::Format(NSLOCTEXT("CatWorldInfo", "FishTankCapacity", "{0} / {1}"),
			FText::AsNumber(Count), FText::AsNumber(Capacity)) : Pending;
		FCatWorldInfoRow& Shortfall = OutData.Rows.AddDefaulted_GetRef();
		Shortfall.Id = TEXT("OfferingShortfall");
		Shortfall.Label = NSLOCTEXT("CatWorldInfo", "FishTankShortfall", "缺口");
		Shortfall.bSummary = false;
		Shortfall.Value = bSummaryReady && bTargetReady ? FText::Format(NSLOCTEXT("CatWorldInfo", "FishTankPoints", "{0} 点"),
			FText::AsNumber(FMath::Max(0, Run->DailyOfferingTarget - Points))) : Pending;
	}
	return true;
}
