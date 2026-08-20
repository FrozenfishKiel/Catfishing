#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "Items/CatItemTypes.h"
#include "Framework/Core/CatFishingBoundaryContracts.h"
#include "Fishing/CatFishingFightModel.h"
#include "CatFishingTypes.generated.h"

/** 单次钓鱼长流程的公开阶段；转移拓扑只由 ST_FishingSession 资产编排。 */
UENUM(BlueprintType)
enum class ECatFishingPhase : uint8
{
	/** 会话对象已建立但 StateTree 尚未进入试探期。 */
	Created,
	/** 鱼只给试探信号；此阶段提竿不能直接形成捕获。 */
	Probe,
	/** 真咬响应窗口；具体时长与输入规则由未裁配置和资产决定。 */
	TrueBiteWindow,
	/** Hooked 后唯一允许多人协作的搏斗阶段。 */
	HookedFight,
	/** 鱼已到近岸并等待首个合法抢抄 Compare-and-Commit。 */
	NearShore,
	/** 捕获事务已提交且唯一鱼实例已经进入胜者鱼护。 */
	Resolved,
	/** 掉线、倒地、局末或依赖失效后终止；旧半场不会重连恢复。 */
	Terminated
};

/** FishingSession 对客户端公开的最小只读事实；不复制 FishDefinition 对象、StableNetId 或容器写模型。 */
USTRUCT(BlueprintType)
struct FCatFishingSessionSnapshot
{
	GENERATED_BODY()

	/** 本次钓鱼会话的一局稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 每次阶段进入、协作者集合变化或终态提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前由服务器 StateTree 进入的公开阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase Phase = ECatFishingPhase::Created;

	/** 当前鱼种稳定 ID；表现和图鉴候选据此查询 Data，不持资产指针。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 当前鱼是否属于 Giant 档；只影响 HookedFight 是否接收协作者，近岸仍抢抄。 */
	UPROPERTY(BlueprintReadOnly)
	bool bGiant = false;

	/** 当前搏斗参与者数量；不公开平台身份数组。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FightParticipantCount = 0;

	/** 当前服务器计算的参与者 FishingStrength 合计；只在 HookedFight 更新，不能由客户端写入。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFishingStrength = 0.0;

	/** 当前服务器认定的合法参与者 FightStamina 合计；与人数和力量一起描述当下协作可达性。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFightStamina = 0.0;

	/**
	 * 当前鱼短周期体力剩余；进入会话时从 EncounterSpec 冻结，完美中鱼会先削减一次，之后只由 HookedFight 的逐帧搏斗推
	 * 进消耗（僵持时 -= 猫力×0.08/秒）。
	 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStaminaRemaining = 0.0;

	/** 本局鱼体力的起点值（完美中鱼削减后的值），和 Remaining 一起给 HUD 算"鱼累了几成"；0 表示搏斗还没开始。 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStaminaMax = 0.0;

	/** 鱼猫距离 D，单位米；HookedFight 内由服务器按飞书 D/L 模型逐帧推进，翻肚或碾压时归零。搏斗外保持上一次的值，不是实时鱼位置。 */
	UPROPERTY(BlueprintReadOnly)
	double FishDistanceMeters = 0.0;

	/** 已放线长 L，单位米；恒有 L ≥ D，到达 LineLengthMaxMeters 后右键放线失效只能拖。 */
	UPROPERTY(BlueprintReadOnly)
	double LineLengthMeters = 0.0;

	/** 本局钓手鱼竿的放线上限 L_max，单位米；进入搏斗时从竿定义冻结，HUD 用它显示"线快放完了"。 */
	UPROPERTY(BlueprintReadOnly)
	double LineLengthMaxMeters = 0.0;

	/** 鱼当前处于向外游（发力）还是向内游（休息）；上钩瞬间为向外游，按段推进，段内不换向。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishSwimState FishSwimState = ECatFishSwimState::None;

	/** 鱼是否正在垂死挣扎（含 0.5 秒前摇）；true 时鱼力量 ×1.5，HUD 可据此预警。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishDeathStruggle = false;

	/** 钓手当前按住的遛鱼操作（拖/松/无）；服务器收到意图 RPC 后写入，客户端只读它确认自己的输入已到达服务器。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingFightIntent FisherIntent = ECatFishingFightIntent::None;

	/** 本次搏斗的终局；None 表示还没分出结果。两种失败（断竿/拖下水）之后会话进 Terminated，三种成功进 NearShore。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingFightOutcome FightOutcome = ECatFishingFightOutcome::None;

	/** 是否完美中鱼（真咬后 1 秒内提竿，服务器时间戳判定）；true 表示鱼的力量与体力已按飞书 §4 削减。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPerfectHook = false;
};

/** StateTree 阶段入口结果；失败不会由 C++ 选择备用阶段。 */
USTRUCT(BlueprintType)
struct FCatFishingPhaseResult
{
	GENERATED_BODY()

	/** 本次资产选择的阶段是否已应用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	/** 处理前阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase PreviousPhase = ECatFishingPhase::Created;

	/** 处理后阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase CurrentPhase = ECatFishingPhase::Created;

	/** 失败原因；成功为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;

	/** 结果对应的 Session Revision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 近岸抢抄命令；身份由服务器 Controller 重建，Guard Revision 绑定实际目标容量事实。 */
USTRUCT(BlueprintType)
struct FCatScoopCommand
{
	GENERATED_BODY()

	/** 抢抄 RequestId、Session ExpectedRevision 与服务器身份。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 抢抄者个人鱼护 ID；RPC 到达服务器后由当前 Pawn 的 authority 注册事实覆盖，客户端值不参与授权。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid TargetGuardContainerId;

	/** 客户端表现命中位置，仅用于载荷诊断；授权只比较服务器 Character 与 StateTree 绑定的权威近岸目标。 */
	UPROPERTY(BlueprintReadWrite)
	FVector ScoopWorldLocation = FVector::ZeroVector;
};

/** 抢抄事务终态；只在首个合法请求成功时包含 CaptureCommittedResult。 */
USTRUCT(BlueprintType)
struct FCatScoopResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 对应 FishingSession 而非容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** Items 唯一捕获提交结果；拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatCaptureCommittedResult Capture;
};

/** Boundary Start 返回给 Fishing 的 PreCast 上下文；它只描述可进入 Cast 的只读事实，不包含鱼种或重量。 */
USTRUCT(BlueprintType)
struct FCatFishingStartContext
{
	GENERATED_BODY()

	/** 与本次外部开始意图一致的 RequestId；Service 用它保持旧返回值关联。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** Fishing 在 Start/PreCast 时获得的 Attempt 身份；Cast 必须携带它，不能在 CastAccepted 时才生成。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingAttemptId AttemptId;

	/** authority 规范化后的钓手身份；Service 用它维护单活跃会话，不再自己解析 PlayerState。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingStableId PrincipalId;

	/** Start 看到的 Run Revision；Cast 会重新读取并可用它诊断 Start 到 Cast 之间的环境变化。 */
	UPROPERTY(BlueprintReadOnly)
	int64 RunRevision = 0;

	/** 钓手当前个人鱼护；Session 创建时消费它，Boundary 不写入 Items。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FisherGuardContainerId;
};

/** Fishing 接受 Cast 后交给 Boundary 的冻结请求；它显式携带 Start 生成的 Attempt 和 Cast 时刻位置。 */
USTRUCT(BlueprintType)
struct FCatFishingCastAcceptedRequest
{
	GENERATED_BODY()

	/** 本次 Cast 语义意图的幂等键；当前过渡期与外部 Start RequestId 相同也不会和 Start operation 冲突。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** Start/PreCast 阶段生成的 Attempt；缺失时 Cast 必须 fail-closed，不能临时补一个。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingAttemptId AttemptId;

	/**
	 * 浮漂落在水面上的 authority 世界位置。
	 * 它由服务器按钓手朝向、当前鱼漂射程和精准度偏移算出，不是钓手自己站的地方；水域校验、窝料查询和 D₀ 都以它为准。
	 * 项目还没有点击瞄准输入，所以"点哪落哪"暂时退化成"朝哪落哪"，接上瞄准后只需换掉落点来源，本字段语义不变。
	 */
	UPROPERTY(BlueprintReadWrite)
	FVector CastWorldLocation = FVector::ZeroVector;

	/** Start 上下文携带的 Run Revision；它进入 PayloadHash，防止同 Cast RequestId 混用不同 PreCast 事实。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRunRevision = 0;
};

/** Cast 冻结的 encounter 事实；Session 只消费这份规格，不重新抽鱼或重新查询水域。 */
USTRUCT(BlueprintType)
struct FCatFishingEncounterSpec
{
	GENERATED_BODY()

	/** Encounter 所属 Attempt；它把冻结结果绑定到 Fishing 的 PreCast 生命周期。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingAttemptId AttemptId;

	/** 被鱼表选中的稳定鱼种 ID；Service 用它反查 runtime definition 指针。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 生成该鱼种结果时 FishCatalog 支持的 SchemaVersion；Session 不解释它，只把它作为内容兼容证据保留下来。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DataSchemaVersion = 1;

	/** 生成该鱼种结果时 FishCatalog 声明的数据修订；用于把一次 encounter 追溯到明确的一版目录内容。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DataRevision = 0;

	/** 生成该鱼种结果时 FishCatalog 的稳定内容摘要；同修订但内容不同会暴露为不同 Hash。 */
	UPROPERTY(BlueprintReadOnly)
	FString ContentHashHex;

	/** 生成鱼种和重量时使用的服务器种子；它只用于审计和复现，不允许客户端指定或修改。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectionSeed = 0;

	/** Cast 时刻冻结的水域快照；Session 后续 NearShore/Fight 使用它，不重新查询环境。 */
	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionSnapshot WaterRegion;

	/** Cast 时刻冻结的服务器鱼重；它来自同一次 FishCatalog 抽取，不在 Session 初始化时重抽。 */
	UPROPERTY(BlueprintReadOnly)
	double FishWeightKilograms = 0.0;

	/** Cast 时刻冻结的 Giant 判断；Session Snapshot 用它决定是否公开 Giant 协作事实。 */
	UPROPERTY(BlueprintReadOnly)
	bool bGiant = false;

	/** Cast 时刻冻结的鱼短周期体力；StateTree 进入搏斗时从这里开始消耗。 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStamina = 0.0;

	/** Cast 时刻合法参与者人数；初始 Snapshot 用它保留 Boundary 决策证据。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FightParticipantCount = 0;

	/** Cast 时刻合法参与者 FishingStrength 合计；Session 初始化不再重新汇总这一轮抽鱼输入。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFishingStrength = 0.0;

	/** Cast 时刻合法参与者 FightStamina 合计；它与人数和力量一起解释鱼表候选可达性。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFightStamina = 0.0;

	/**
	 * Cast 时刻按落点所在窝点的窝料 Total 算出并冻结的本次咬钩间隔，单位秒：T_actual = T_base / (1 + Total / K)，
	 * 有窝 T_base=15、无窝 T_base=120、K=100（飞书钓鱼规则 §2 已裁，数值在 UCatEnvironmentSettings）。
	 * 计时器挂在浮漂级——每个浮漂用它自己落点那一刻的 Total，之后窝料衰减或补窝都不改这个已冻结的值。
	 * ST_FishingSession 的 Probe 等这个时长才进入 TrueBiteWindow；0 或负数表示没有算出合法间隔，Probe 会 fail-closed。
	 */
	UPROPERTY(BlueprintReadOnly)
	double BiteIntervalSeconds = 0.0;

	/**
	 * Cast 时刻冻结的猫到浮漂落点的真实距离，单位米；它就是遛鱼 D/L 模型开局的 D₀（= 已放线长 L₀）。
	 * 飞书 D/L 模型写"初始 = 浮漂落点距离"，所以这个值直接来自本次落点，不再是全局配置常量（决策记录 D-19 已退休）。
	 * 换一只射程不同的漂，落点更远，D₀ 就更大，遛鱼开局要收的线也更长。
	 * 0 或负数表示 Cast 没有算出合法落点距离，会话不会开局。
	 */
	UPROPERTY(BlueprintReadOnly)
	double InitialFishDistanceMeters = 0.0;
};

/** Boundary Cast 的 typed 上下文包装；后续原子可在这里追加 Receipt，而不是引入任意 payload。 */
USTRUCT(BlueprintType)
struct FCatFishingCastContext
{
	GENERATED_BODY()

	/** Cast 语义意图的幂等键；与 EncounterSpec 分开，方便调用方关联重试。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** Cast 冻结出的 encounter 规格；默认值表示尚未成功冻结。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingEncounterSpec EncounterSpec;
};

/** Boundary Start 的 typed result；公共头描述事务状态，Context 只在 Committed 时有效。 */
USTRUCT(BlueprintType)
struct FCatFishingBoundaryStartResult
{
	GENERATED_BODY()

	/** Start operation 的 Boundary 事务头；Rejected 时没有副作用。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingBoundaryResultHeader Header;

	/** Start 成功后交给 Fishing 的 PreCast 上下文；Rejected 时只保留 RequestId 便于关联。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingStartContext Context;
};

/** Boundary Cast 的 typed result；公共头描述事务状态，EncounterSpec 只在 Committed 时有效。 */
USTRUCT(BlueprintType)
struct FCatFishingBoundaryCastResult
{
	GENERATED_BODY()

	/** Cast operation 的 Boundary 事务头；Rejected 时没有冻结 encounter。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingBoundaryResultHeader Header;

	/** Cast 成功冻结的 encounter 规格；重放必须返回同一份规格。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingEncounterSpec EncounterSpec;
};

/** Fishing 确认有效 Bite 后交给 Boundary 的 bait 语义请求；普通饵只发行语义 Receipt，特殊饵必须绑定真实消耗事实。 */
USTRUCT(BlueprintType)
struct FCatFishingBiteAcceptedRequest
{
	GENERATED_BODY()

	/** 本次 Bite/Bait 语义的幂等键；同 Attempt 下同 RequestId 重放必须返回首次 Receipt。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** Bait 所属 Fishing Attempt；缺失时不能临时创建 Attempt 或绕过 Start/Cast。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingAttemptId AttemptId;

	/** authority 规范化身份；Boundary/Equipment 使用它拒绝身份漂移，不信任客户端显示名。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingStableId PrincipalId;

	/** Fishing 或 Equipment 读取 bait 相关聚合时看到的 Revision；它保护特殊饵消耗不覆盖较新事实。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRevision = 0;

	/** 当前 bait 定义的稳定 ID；普通饵可无限确认，特殊饵需要 Equipment writer 消耗一份。 */
	UPROPERTY(BlueprintReadWrite)
	FName BaitDefinitionId = NAME_None;

	/** Bite 由 Fishing 侧生成的短生命周期证明；特殊饵没有有效 Bite 时不得被扣除。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid BiteToken;

	/** 是否要求消耗特殊饵库存；false 表示普通饵只发行语义 Receipt，不写 Equipment 数量。 */
	UPROPERTY(BlueprintReadWrite)
	bool bConsumesSpecialBait = false;
};

/** Boundary Bait 的 typed result；Receipt 只证明 bait 语义或特殊饵消耗已被接受，不推进 Fishing phase。 */
USTRUCT(BlueprintType)
struct FCatFishingBaitResult
{
	GENERATED_BODY()

	/** Bait operation 的事务头；Rejected 时 Receipt 保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingBoundaryResultHeader Header;

	/** Bait writer 或普通饵规则发行的稳定提交证明；重放必须返回同一个 ReceiptId。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingDomainReceipt Receipt;
};

/** StateTree 请求一次 HookedFight 资源交换时交给 Boundary 的 typed payload。 */
USTRUCT(BlueprintType)
struct FCatFishingFightExchangeRequest
{
	GENERATED_BODY()

	/** 本次 Fight 资源帧的幂等键；同 cursor 同 payload 重放必须返回首次 operation。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** Fight 帧所属 Attempt；它把搏斗资源顺序绑定到 Start/Cast 生命周期。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingAttemptId AttemptId;

	/** authority 规范化身份；生产路径由服务器填写，防止客户端伪造资源提交者。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingStableId PrincipalId;

	/** Attempt 内单调递增的搏斗资源帧；Last+1 才能接受，跳号返回 CursorGap。 */
	UPROPERTY(BlueprintReadWrite)
	int64 Cursor = 0;

	/** Session 或资源聚合在提交前看到的 Revision；陈旧提交必须拒绝而不是覆盖较新资源事实。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRevision = 0;

	/** 本帧要扣除的鱼短周期体力；必须为正有限值，单位沿用 Session 现有 FightStamina。 */
	UPROPERTY(BlueprintReadWrite)
	double FishStaminaCost = 0.0;

	/** 本帧每个参与者要扣除的短周期体力；Boundary 只协调，真实扣除由资源 writer 完成。 */
	UPROPERTY(BlueprintReadWrite)
	double ParticipantStaminaCost = 0.0;

	/** 本帧要扣除的鱼竿耐久；它和鱼/猫体力共用同一个 Fight cursor，防止捕获前补扣或重复扣。 */
	UPROPERTY(BlueprintReadWrite)
	double RodDurabilityCost = 0.0;
};

/** Capture 前由 Fishing 封存的最终搏斗 Cursor；封存后更大的 Fight cursor 都必须拒绝。 */
USTRUCT(BlueprintType)
struct FCatFishingFinalFightCursor
{
	GENERATED_BODY()

	/** 被封存的 Attempt；不同 Attempt 的 cursor 互不影响。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingAttemptId AttemptId;

	/** 最后允许存在的 Fight cursor；Capture 之后不能再补写更大的资源帧。 */
	UPROPERTY(BlueprintReadWrite)
	int64 Cursor = 0;
};

/** Boundary Fight 的 typed result；它汇总 GAS/Equipment 的资源 Receipt 与最终 cursor seal。 */
USTRUCT(BlueprintType)
struct FCatFishingFightResult
{
	GENERATED_BODY()

	/** Fight operation 的事务头；Rejected 时不允许 Session 扣鱼体力或推进搏斗结果。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingBoundaryResultHeader Header;

	/** 本次 Fight operation 已取得的领域 Receipt；每个 Receipt 都绑定同一个 OperationKey。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatFishingDomainReceipt> Receipts;

	/** 本结果观察到的最终 Fight cursor；未 seal 时 Cursor 为 0。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingFinalFightCursor FinalCursor;

	/** 资源 writer 明确报告鱼竿断裂时为 true；系统故障不得伪装成 RodBroken。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
/** FishingService 建立会话的同步结果；成功只表示 StateTree 已启动，不表示已经咬钩或捕获。 */
USTRUCT(BlueprintType)
struct FCatFishingStartResult
{
	GENERATED_BODY()

	/** 与开始意图一致的 RequestId；相同服务器身份重试会重放包含同一会话 ID 的首次结果。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 会话是否成功创建并启动唯一 StateTree。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStarted = false;

	/** 新会话 ID；失败保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 启动失败原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;
};
