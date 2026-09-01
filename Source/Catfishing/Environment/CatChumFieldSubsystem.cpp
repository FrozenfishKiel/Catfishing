#include "Environment/CatChumFieldSubsystem.h"

#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"

namespace CatChumFieldSubsystemPrivate
{
	// 校验向量三分量都是有限数，防止 NaN/Inf 从网络输入或曲线求值里混进权威状态
	static bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	// FGuid 没有内建 operator<，这里按四个 32 位分量逐级比较，提供一个确定性的全序用于排序
	static bool GuidLess(const FGuid& Left, const FGuid& Right)
	{
		if (Left.A != Right.A) return Left.A < Right.A;
		if (Left.B != Right.B) return Left.B < Right.B;
		if (Left.C != Right.C) return Left.C < Right.C;
		return Left.D < Right.D;
	}
}

void UCatChumFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureCleanupTimer(); // World 还未必 BeginPlay，这里先尝试起一次；真正世界开始时 OnWorldBeginPlay 会再确认一次
}

void UCatChumFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)

{
	Super::OnWorldBeginPlay(InWorld);
	EnsureCleanupTimer(); // 世界正式开始后，此时 GetNetMode 等信息才可靠，重新确认定时器状态
}

// 幂等地确保过期清理定时器在授权端跑起来；客户端或功能未启用时不会创建定时器
void UCatChumFieldSubsystem::EnsureCleanupTimer()
{
	if (IsAuthorityRuntimeReady() && !GetWorld()->GetTimerManager().IsTimerActive(CleanupTimerHandle))
	{
		const double Interval = GetDefault<UCatChumFieldSettings>()->ExpiredCleanupIntervalSeconds;
		// 按策划配置的间隔循环触发过期清理；bLoop=true
		GetWorld()->GetTimerManager().SetTimer(CleanupTimerHandle, this,
			&UCatChumFieldSubsystem::HandleCleanupTimer, static_cast<float>(Interval), true);
	}
}

void UCatChumFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CleanupTimerHandle); // World 销毁前先停掉定时器，避免野指针回调
	}
	// 逐一清空所有运行期状态，防止 Subsystem 复用/PIE 结束后残留上一局数据
	PendingByToken.Reset();
	PendingTokenByRequest.Reset();
	FieldsById.Reset();
	FieldIdsByRegion.Reset();
	FieldSetRevisionByRegion.Reset();
	BudgetByRegion.Reset();
	TerminalByIdentityAndRequest.Reset();
	Super::Deinitialize();
}

// 判断当前端是否具备"作为打窝权威"的资格：必须是服务器（含监听服务器），且功能配置合法可用
bool UCatChumFieldSubsystem::IsAuthorityRuntimeReady() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && GetDefault<UCatChumFieldSettings>()->IsRuntimeReady();
}

// 统一构造一个"准备失败"结果；bPrepared 保持默认 false，只填错误码
FCatPrepareChumFieldResult UCatChumFieldSubsystem::MakePrepareError(const ECatChumFieldError Error)
{
	FCatPrepareChumFieldResult Result;
	Result.Error = Error;
	return Result;
}

// 把水域查询子系统的错误码翻译成打窝领域自己的错误码，隔离两个子系统的错误语义
ECatChumFieldError UCatChumFieldSubsystem::MapWaterError(const ECatWaterQueryError Error)
{
	return Error == ECatWaterQueryError::StaleGeometry ? ECatChumFieldError::StaleGeometry
		: ECatChumFieldError::InvalidWaterTarget;
}

// 打窝的"两阶段提交"第一阶段：校验 + 预占容量配额，产出一个 CommitToken，尚不写入公开可见状态。
// 之所以拆成 Prepare/Activate 两步，是为了让上层（装备/背包扣费等）能在真正落子前后做原子性协调。
FCatPrepareChumFieldResult UCatChumFieldSubsystem::PrepareField(const FCatPrepareChumFieldRequest& Request)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		// 打窝权威逻辑只能在服务器/单机端跑，客户端调用直接拒绝
		return MakePrepareError(ECatChumFieldError::DependencyUnavailable);
	}
	const UCatChumFieldSettings* Settings = GetDefault<UCatChumFieldSettings>();
	if (!Settings || !Settings->IsRuntimeReady())
	{
		// 功能总开关关闭或配置非法
		return MakePrepareError(ECatChumFieldError::FeatureDisabled);
	}
	if (Request.StableNetId.IsEmpty())
	{
		// 请求方身份缺失，无法做去重/归属判断
		return MakePrepareError(ECatChumFieldError::InvalidIdentity);
	}
	if (!Request.Command.RequestId.IsValid() || !Request.Command.ExpectedWaterRegionHandle.IsValid()
		|| Request.Command.ChumDefinitionId.IsNone() || Request.Command.Quantity <= 0
		|| !FMath::IsFinite(Request.ServerTime)
		|| !CatChumFieldSubsystemPrivate::IsFiniteVector(Request.ServerCorrectedCenter))
	{
		// 载荷本身（请求ID/水域句柄/窝料定义/数量/服务器时间/落点坐标）任一非法都直接拒绝
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}
	CleanupExpiredFields(Request.ServerTime); // 先顺手清理过期窝料场，释放容量配额，避免陈旧数据挡住新请求
	const FCatChumRequestKey RequestKey{Request.StableNetId, Request.Command.RequestId};
	if (TerminalByIdentityAndRequest.Contains(RequestKey))
	{
		// 这个 (身份, RequestId) 组合已经有终态结果了，说明是重复提交（如重连重放），拒绝重新处理
		return MakePrepareError(ECatChumFieldError::AlreadyResolved);
	}
	if (const FGuid* ExistingTokenId = PendingTokenByRequest.Find(RequestKey))
	{
		if (const FCatPendingChumField* Existing = PendingByToken.Find(*ExistingTokenId))
		{
			// 同一个请求已经 Prepare 过还没 Activate/Abort：直接把之前算好的结果原样重放，
			// 保证幂等（比如 RPC 因丢包被客户端重发）而不会重复占用配额
			FCatPrepareChumFieldResult Replay;
			Replay.bPrepared = true; Replay.Error = ECatChumFieldError::None;
			Replay.CommitToken.Value = *ExistingTokenId; Replay.FieldId = Existing->State.FieldId;
			Replay.WaterRegion = Existing->State.WaterRegion; Replay.CorrectedCenter = Existing->State.CenterWorldPoint;
			Replay.StartServerTime = Existing->State.StartServerTime; Replay.ExpireServerTime = Existing->State.ExpireServerTime;
			return Replay;
		}
	}

	const UCatWaterQuerySubsystem* WaterQuery = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery)
	{
		return MakePrepareError(ECatChumFieldError::DependencyUnavailable);
	}
	// 用服务器修正过的落点重新查询水域：既验证目标点确实在合法水域内，也拿到贴合水面的真实坐标
	const FCatWaterSpatialResult Water = WaterQuery->QueryWaterPoint(
		Request.ServerCorrectedCenter, Request.Command.ExpectedWaterRegionHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		// 查询失败或落点其实在水域外（客户端预测的落点被服务器判定无效）
		return MakePrepareError(MapWaterError(Water.Error));
	}
	FCatChumRuntimeInfluence RuntimeInfluence;
	if (!Request.Influence.BuildRuntimeInfluence(Request.Command.Quantity, RuntimeInfluence))
	{
		// 窝料定义配置本身非法，或本次数量超出该定义允许的单次投放上限
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}
	const double DefinitionRadiusCentimeters = RuntimeInfluence.RadiusCentimeters;
	double InfluenceRadiusScale = 0.0;
	if (!Settings->TryGetInfluenceRadiusScale(InfluenceRadiusScale))
	{
		return MakePrepareError(ECatChumFieldError::FeatureDisabled);
	}
	RuntimeInfluence.RadiusCentimeters *= InfluenceRadiusScale;
	if (!FMath::IsFinite(RuntimeInfluence.RadiusCentimeters)
		|| RuntimeInfluence.RadiusCentimeters <= 0.0)
	{
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}
	// 三种诱鱼因子（腥/香/发酵）之和作为这份窝料对"预算"的原始占用量，用来限制单水域窝料浓度上限
	const double RawContribution = RuntimeInfluence.BaseContribution.Fishy
		+ RuntimeInfluence.BaseContribution.Fragrant + RuntimeInfluence.BaseContribution.Fermented;
	if (!FMath::IsFinite(RawContribution) || RawContribution <= 0.0)
	{
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}

	// 按水域 RegionId 各自维护独立的配额账本，找不到则新建一个全零的
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Water.WaterRegion.RegionId);
	if (Budget.ActiveCount + Budget.PendingCount >= Settings->MaxActiveFieldsPerRegion
		|| Budget.ReservedRawContribution + RawContribution > Settings->MaxRawContributionPerRegion)
	{
		// 已生效 + 正在准备中的窝料场数量，或者累计浓度贡献，超过该水域的策划上限
		return MakePrepareError(ECatChumFieldError::FieldCapacityExceeded);
	}

	FGuid TokenId = FGuid::NewGuid();
	FGuid FieldId = FGuid::NewGuid();
	while (PendingByToken.Contains(TokenId)) TokenId = FGuid::NewGuid(); // 极小概率碰撞时重新生成，保证 Token 唯一
	while (FieldsById.Contains(FieldId)) FieldId = FGuid::NewGuid(); // 同理保证 FieldId 全局唯一
	FCatPendingChumField Pending;
	Pending.RequestKey = RequestKey;
	Pending.RawContribution = RawContribution; // 记住占用量，Abort 时需要按这个数值回滚配额
	Pending.State.FieldId = FieldId;
	Pending.State.WaterRegion = Water.WaterRegion;
	Pending.State.ChumDefinitionId = Request.Command.ChumDefinitionId;
	Pending.State.CenterWorldPoint = Water.WaterSurfaceWorldPoint; // 用服务器查询到的贴水面坐标，而非客户端原始落点
	Pending.State.Influence = MoveTemp(RuntimeInfluence);
	Pending.State.StartServerTime = Request.ServerTime;
	Pending.State.ExpireServerTime = Request.ServerTime + Pending.State.Influence.DurationSeconds; // 到期时间 = 起效时间 + 持续时长
	Pending.State.Source = Request.Source;
	Pending.State.OwnerStableNetId = Request.StableNetId;
	PendingByToken.Add(TokenId, Pending); // 存入待激活表，等 ActivatePreparedFieldDeferred 真正落子
	PendingTokenByRequest.Add(RequestKey, TokenId); // 反向索引：请求 -> Token，用于幂等重放
	++Budget.PendingCount; // 先占坑，防止 Prepare 和 Activate 之间又有新请求把配额挤爆
	Budget.ReservedRawContribution += RawContribution;

	FCatPrepareChumFieldResult Result;
	Result.bPrepared = true; Result.Error = ECatChumFieldError::None; Result.CommitToken.Value = TokenId;
	Result.FieldId = FieldId; Result.WaterRegion = Water.WaterRegion; Result.CorrectedCenter = Water.WaterSurfaceWorldPoint;
	Result.StartServerTime = Request.ServerTime; Result.ExpireServerTime = Pending.State.ExpireServerTime;
	UE_LOG(LogCatEnvironment, Log,
		TEXT("Event=chum_field_prepared RequestId=%s FieldId=%s Region=%s Definition=%s Quantity=%d BaseRadiusCm=%.2f EffectiveRadiusCm=%.2f AreaMultiplier=%.3f StartServerTime=%.3f ExpireServerTime=%.3f"),
		*Request.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*FieldId.ToString(EGuidFormats::DigitsWithHyphensLower), *Water.WaterRegion.RegionId.ToString(),
		*Request.Command.ChumDefinitionId.ToString(), Request.Command.Quantity,
		DefinitionRadiusCentimeters, Pending.State.Influence.RadiusCentimeters,
		Settings->InfluenceAreaMultiplier, Pending.State.StartServerTime, Pending.State.ExpireServerTime);
	return Result;
}

// 两阶段提交第二阶段：把 Prepare 阶段暂存的窝料场真正落子为"活跃"状态（此时尚未广播给客户端，见 PublishActivatedField）。
// 分离 Activate 和 Publish 是为了让上层能先在同一事务里扣完装备/背包消耗，再统一对外可见。
FCatPlaceChumResult UCatChumFieldSubsystem::ActivatePreparedFieldDeferred(
	const FCatChumFieldCommitToken Token, const int64 EquipmentRevision)
{
	FCatPlaceChumResult Result;
	Result.EquipmentRevision = EquipmentRevision;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Token.IsValid())
	{
		Result.Error = ECatChumFieldError::DependencyUnavailable;
		return Result;
	}
	FCatPendingChumField* Pending = PendingByToken.Find(Token.Value);
	if (!Pending)
	{
		// Token 找不到：要么已经被 Activate/Abort 过，要么从未 Prepare 成功；统一按"已处理过"返回
		Result.Error = ECatChumFieldError::AlreadyResolved;
		return Result;
	}
	const FCatPendingChumField Frozen = *Pending; // 先拷贝一份快照，后面移除 map 条目后原指针会失效
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId);
	--Budget.PendingCount; // 从"预占"转为"已生效"，配额记账相应转移
	++Budget.ActiveCount;
	FieldsById.Add(Frozen.State.FieldId, Frozen.State); // 正式写入权威活跃窝料场表
	FieldIdsByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId).Add(Frozen.State.FieldId); // 同步维护按水域分组的索引，供采样/清理快速遍历
	PendingTokenByRequest.Remove(Frozen.RequestKey); // 清掉待处理索引，避免和终态结果表混淆
	PendingByToken.Remove(Token.Value);
	// 该水域的窝料场集合发生了变化，递增版本号；客户端/其他系统可以用这个 Revision 判断是否需要重新采样
	const int64 Revision = ++FieldSetRevisionByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId);

	Result.RequestId = Frozen.RequestKey.RequestId;
	Result.bCommitted = true; Result.Error = ECatChumFieldError::None; Result.FieldId = Frozen.State.FieldId;
	Result.WaterRegion = Frozen.State.WaterRegion; Result.ServerCorrectedCenter = Frozen.State.CenterWorldPoint;
	Result.StartServerTime = Frozen.State.StartServerTime; Result.ExpireServerTime = Frozen.State.ExpireServerTime;
	Result.EquipmentRevision = EquipmentRevision; Result.ChumFieldSetRevision = Revision;
	return Result;
}

// 把已激活但还未对外广播的窝料场同步进复制组件，驱动客户端生成表现；只有确认终态结果已落盘才允许发布，
// 防止服务器在 Activate 和"记录终态结果"之间崩溃/回滚导致客户端看到一个逻辑上还未提交成功的窝料场。
void UCatChumFieldSubsystem::PublishActivatedField(const FGuid FieldId)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	FCatChumFieldState* Field = FieldsById.Find(FieldId);
	if (!Field || Field->bPublicationFlushed) return; // 找不到或已经发布过，幂等直接返回
	bool bTerminalStored = false;
	// 遍历终态结果表，确认确实存在一条"属于该窝料场 Owner、已提交成功、FieldId 匹配"的终态记录
	for (const TPair<FCatChumRequestKey, FCatPlaceChumResult>& Pair : TerminalByIdentityAndRequest)
	{
		if (Pair.Key.StableNetId == Field->OwnerStableNetId && Pair.Value.bCommitted && Pair.Value.FieldId == FieldId)
		{
			bTerminalStored = true;
			break;
		}
	}
	if (!bTerminalStored) return; // 还没有对应终态记录，说明上层事务尚未完成，先不广播
	Field->bPublicationFlushed = true; // 打上已发布标记，防止重复广播
	if (ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>())
	{
		if (UCatChumFieldReplicationComponent* Replication = GameState->GetChumFieldReplicationFromAuthority())
		{
			Replication->ReconcileFieldFromAuthority(*Field); // 真正写入 FastArray，触发对客户端的网络复制
		}
	}
	OnFieldActivated.Broadcast(FieldId); // 服务器本地事件，供其他系统（如成就/日志）订阅
}

// 两阶段提交的回滚路径：上层事务在 Prepare 之后判定失败（如背包扣费失败），需要撤销预占的配额
void UCatChumFieldSubsystem::AbortPreparedField(const FCatChumFieldCommitToken Token)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Token.IsValid()) return;
	const FCatPendingChumField* Pending = PendingByToken.Find(Token.Value);
	if (!Pending) return; // 找不到说明已经被处理过（Activate 或重复 Abort），幂等忽略
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Pending->State.WaterRegion.RegionId);
	// 用 Max(0, ...) 兜底，防止并发/重复调用把计数减成负数
	Budget.PendingCount = FMath::Max(0, Budget.PendingCount - 1);
	Budget.ReservedRawContribution = FMath::Max(0.0, Budget.ReservedRawContribution - Pending->RawContribution);
	PendingTokenByRequest.Remove(Pending->RequestKey);
	PendingByToken.Remove(Token.Value);
}

// 按 (身份, RequestId) 查询这次打窝请求是否已经有终态结果；用于幂等重放（RPC 重发/重连）
bool UCatChumFieldSubsystem::TryGetTerminalResult(const FString& StableNetId, const FGuid RequestId,
	FCatPlaceChumResult& OutResult) const
{
	OutResult = FCatPlaceChumResult();
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || StableNetId.IsEmpty() || !RequestId.IsValid()) return false;
	if (const FCatPlaceChumResult* Found = TerminalByIdentityAndRequest.Find({StableNetId, RequestId}))
	{
		OutResult = *Found;
		return true;
	}
	return false;
}

// 记录一次请求的终态结果；只在第一次调用时真正写入，后续同 Key 的调用不会覆盖——保证终态"写一次即不可变"
void UCatChumFieldSubsystem::StoreTerminalResult(const FString& StableNetId, const FCatPlaceChumResult& Result)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || StableNetId.IsEmpty() || !Result.RequestId.IsValid()) return;
	const FCatChumRequestKey Key{StableNetId, Result.RequestId};
	if (!TerminalByIdentityAndRequest.Contains(Key))
	{
		TerminalByIdentityAndRequest.Add(Key, Result);
	}
}

// 核心查询接口：在给定世界坐标、给定服务器时间点，把所有仍然生效且覆盖该点的窝料场按距离/时间衰减叠加，
// 得出该点当前的"综合诱鱼向量"（供试探/咬钩概率等玩法逻辑消费）。
FCatChumSample UCatChumFieldSubsystem::SampleChumAtPoint(const FVector& WorldPoint,
	const FCatWaterRegionHandle& ExpectedHandle, const double ServerTime) const
{
	FCatChumSample Result;
	Result.SampleServerTime = ServerTime;
	if (!IsAuthorityRuntimeReady() || !FMath::IsFinite(ServerTime)) return Result; // 非权威端或时间非法，返回空采样
	const UCatWaterQuerySubsystem* WaterQuery = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery) return Result;
	const FCatWaterSpatialResult Water = WaterQuery->QueryWaterPoint(WorldPoint, ExpectedHandle); // 先确认这个点确实落在预期水域内
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		Result.Error = MapWaterError(Water.Error);
		return Result;
	}
	Result.bSucceeded = true; Result.Error = ECatChumFieldError::None; Result.WaterRegion = Water.WaterRegion;
	// 把该水域当前的窝料集合版本号带出去，调用方可以据此判断结果是否需要缓存失效
	Result.ChumFieldSetRevision = FieldSetRevisionByRegion.FindRef(Water.WaterRegion.RegionId);
	if (const TArray<FGuid>* RegionFields = FieldIdsByRegion.Find(Water.WaterRegion.RegionId))
	{
		Result.ContributingFieldIds = *RegionFields; // 先取该水域全部窝料场 ID 作为候选
		// 过滤掉：已找不到状态的、还没生效或已过期的、以及采样点超出其影响半径的窝料场
		Result.ContributingFieldIds.RemoveAll([this, &WorldPoint, ServerTime](const FGuid& Id)
		{
			const FCatChumFieldState* Field = FieldsById.Find(Id);
			if (!Field || ServerTime < Field->StartServerTime || ServerTime >= Field->ExpireServerTime) return true;
			return FVector2D::Distance(FVector2D(Field->CenterWorldPoint), FVector2D(WorldPoint))
				> Field->Influence.RadiusCentimeters;
		});
		// 按 GUID 排序保证多个窝料场的叠加顺序在同一帧内是确定性的（便于复现/调试，浮点加法不满足结合律）
		Result.ContributingFieldIds.Sort(CatChumFieldSubsystemPrivate::GuidLess);
		for (const FGuid& Id : Result.ContributingFieldIds)
		{
			const FCatChumFieldState& Field = FieldsById.FindChecked(Id);
			// 归一化距离：0=窝料中心，1=影响半径边界，用于喂给距离衰减曲线
			const double Distance = FVector2D::Distance(FVector2D(Field.CenterWorldPoint), FVector2D(WorldPoint))
				/ Field.Influence.RadiusCentimeters;
			// 归一化时间：0=刚投放，1=即将过期，用于喂给时间衰减曲线（模拟窝料随时间变淡）
			const double Time = (ServerTime - Field.StartServerTime) / (Field.ExpireServerTime - Field.StartServerTime);
			// 两条衰减曲线相乘得到综合权重：离得越远、放得越久，权重越低
			const double Weight = Field.Influence.DistanceFalloff.Evaluate(Distance)
				* Field.Influence.TimeFalloff.Evaluate(Time);
			// 把这个窝料场按权重缩放后的贡献叠加进总的诱鱼向量
			Result.EffectiveChumVector.Accumulate(Field.Influence.BaseContribution.ScaledBy(Weight));
		}
	}
	Result.ContributingFieldCount = Result.ContributingFieldIds.Num();
	return Result;
}

// 扫描全部活跃窝料场，把已过期的整体下线：释放配额、更新版本号、驱动复制移除、广播事件。
// 既被 PrepareField 在处理新请求前主动调用（惰性清理），也被定时器周期性调用（兜底清理无人触发的过期）。
int32 UCatChumFieldSubsystem::CleanupExpiredFields(const double ServerTime)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !FMath::IsFinite(ServerTime)) return 0;
	TArray<FGuid> Expired;
	TSet<FName> AffectedRegions; // 记录哪些水域因本次清理而发生了集合变化，稍后统一递增一次版本号
	for (const TPair<FGuid, FCatChumFieldState>& Pair : FieldsById)
	{
		if (ServerTime >= Pair.Value.ExpireServerTime)
		{
			Expired.Add(Pair.Key);
			AffectedRegions.Add(Pair.Value.WaterRegion.RegionId);
		}
	}
	Expired.Sort(CatChumFieldSubsystemPrivate::GuidLess); // 排序保证多次清理时移除顺序确定，便于日志/调试复现
	for (const FGuid& Id : Expired)
	{
		const FCatChumFieldState Field = FieldsById.FindChecked(Id); // 移除前先拷贝一份，避免用悬空引用
		if (TArray<FGuid>* RegionIds = FieldIdsByRegion.Find(Field.WaterRegion.RegionId))
		{
			RegionIds->Remove(Id);
			if (RegionIds->IsEmpty()) FieldIdsByRegion.Remove(Field.WaterRegion.RegionId); // 清空的水域索引直接整体移除，避免留空数组
		}
		FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Field.WaterRegion.RegionId);
		Budget.ActiveCount = FMath::Max(0, Budget.ActiveCount - 1); // 释放"活跃数量"配额
		const double Raw = Field.Influence.BaseContribution.Fishy + Field.Influence.BaseContribution.Fragrant
			+ Field.Influence.BaseContribution.Fermented;
		Budget.ReservedRawContribution = FMath::Max(0.0, Budget.ReservedRawContribution - Raw); // 释放"浓度贡献"配额
		FieldsById.Remove(Id); // 从权威表里彻底删除
	}
	for (const FName RegionId : AffectedRegions)
	{
		++FieldSetRevisionByRegion.FindOrAdd(RegionId); // 每个受影响水域各递增一次，通知消费者集合已变化
	}
	for (const FGuid& Id : Expired)
	{
		// 逐个驱动复制组件把窝料场从客户端可见列表移除（触发表现层销毁特效）
		if (ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>())
		{
			if (UCatChumFieldReplicationComponent* Replication = GameState->GetChumFieldReplicationFromAuthority())
			{
				Replication->RemoveFieldFromAuthority(Id);
			}
		}
		OnFieldRemoved.Broadcast(Id);
	}
	return Expired.Num();
}

// 定时器回调：用世界当前时间驱动一次过期清理，兜底那些长时间没有新打窝请求触发惰性清理的场景
void UCatChumFieldSubsystem::HandleCleanupTimer()
{
	if (const UWorld* World = GetWorld()) CleanupExpiredFields(World->GetTimeSeconds());
}
