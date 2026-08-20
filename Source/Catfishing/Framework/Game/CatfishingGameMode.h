#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameplayTagContainer.h"
#include "GameFramework/GameModeBase.h"
#include "Online/CatOnlineTypes.h"
#include "CatfishingGameMode.generated.h"

class UStateTreeComponent;

/**
 * Lake 地图的服务器权威宿主，一局里"谁在场、现在是第几天的哪个阶段、这局能不能收摊"三件事的唯一真相持有者。
 *
 * 它拥有：
 * - 身份准入表（PreLogin 建 Reserved、PostLogin 配对成 Active、Logout 精确清理），以及重连窗口、被踢压制、
 *   主动离局标记这几组只活在服务器内存里的身份事实；
 * - Run 聚合 FCatRunPublicState 与它的 Revision，且是唯一写入者，客户端只能读 GameState 上的复制副本；
 * - 唯一一棵 ST_RunFlow 及其阶段入口 gate，白天截止与时段分界这两个计时器，普通夜的 ready 资格与结果；
 * - Host 退出的收口：关命令门、等远端 Destroy ACK 与 Profile Grant ACK、超时兜底、广播 teardown 终态。
 *
 * 它不拥有：具体玩法结果。鱼、装备、社交协议、商店流水都由各自领域服务裁决并持有，GameMode 只在阶段切换和
 * teardown 时按固定顺序调它们的开关，或者把它们已经算好的公开快照转发到 GameState 复制出去。
 * 它也不做任何客户端侧判断——这个类的方法全部假定 HasAuthority()。
 */
UCLASS()
class CATFISHING_API ACatfishingGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	/** 建立 Lake 原生宿主装配；身份注册表属于 GameMode 实例，不进入类默认对象或客户端。 */
	ACatfishingGameModeBase();
	/** Lake 开始玩法时建立 Run 聚合并经正式 runtime gate 显式启动唯一 StateTree；依赖缺失时保持 NotStarted/StartupFailed。 */
	virtual void StartPlay() override;
	/** World 退出时关闭命令，清白天截止/HostExit ACK 计时与等待集合，再停 StateTree，避免旧 Run 回调进入下一地图。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 在 Login 前校验身份与唯一占用；正式玩家建立 Reserved，PIE 无会话远端的无效身份只放行到服务器 InitNewPlayer 分配。 */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	/** 在引擎注册 PlayerState 前为 PIE 无会话玩家注入仅本次 World 有效的服务器身份；正式平台身份仍原样进入父类流程。 */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	/** 在父类生成 Character 之前把 Reserved 提升为与 Controller 匹配的 Active，再进入引擎标准 PostLogin 链。 */
	virtual void PostLogin(APlayerController* NewPlayer) override;
	/** 生成 Character 前再次验证 PlayerState 的继承 UniqueId 与 Active Controller 匹配，失败时不调用父类生成。 */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	/** Controller 离开时只清除与它精确匹配的 Active 记录；不把主动离局猜成可恢复网络异常。 */
	virtual void Logout(AController* Exiting) override;

	/** StateTree EnterPhase Task 的唯一阶段写入口；先验证阶段策略，再成对切换截止计时、献祭窗口、ready 资格与命令 gate，最后只发布一份组合快照。 */
	FCatRunTransitionResult EnterRunPhaseFromStateTree(ECatRunPhase NewPhase, ECatRunTransitionReason Reason);
	/** StateTree Condition 只读比较最近一次外部结果原因，不从当前 Phase 反推事件来源。 */
	bool DoesLastRunFlowResultMatch(ECatRunTransitionReason ExpectedReason) const;
	/** 献祭协调器在消费鱼前只读验证额度命令；不写缓存、Revision 或 StateTree 事件。 */
	FCatRunCommandResult ValidateCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command) const;
	/**
	 * Items 已 committed 后由一局协调器提交额度条数与世界进度增减；使用服务器私有 StableNetId，不要求玩家仍在线。献祭
	 * 是唯一的额度来源，玩家不能自报贡献值。
	 */
	FCatRunCommandResult SubmitCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& ServerCommand);
	/** 写入普通夜晚的个人翻天确认；只有启动时冻结的合资格集合可以影响全员事件。 */
	FCatRunCommandResult SubmitNextDayReady(AController* RequestingController, const FCatNextDayReadyCommand& Command);
	/** 供服务器结算协调器提交完成终态；本方法只发送 StateTree 事件，不在 C++ 选择目标 Phase。 */
	FCatRunCommandResult CompleteSettlementFromCoordinator(FGuid RequestId, int64 ExpectedRevision);
	/** Host 离局前关闭新命令、计时器和 StateTree；结果原样携带 Online RequestId/epoch。 */
	FCatRunTeardownResult RequestRunTeardown(const FCatRunTeardownRequest& Request);

	/**
	 * 本局四个领域的关门总开关：按固定顺序终止 Fishing、收口 Social，再关 Items 和 ShopEconomy。
	 * 四个服务都在位且 Social 收口成功才返回 true；返回 false 意味着本局还有未结的不可逆事务，Host 不得继续 teardown。
	 */
	bool CloseRunDomainCommands();
	/** 返回 Run teardown 终态委托；Online 必须在回调中再次核对 RequestId/epoch。 */
	FCatRunTeardownCompleted& OnRunTeardownCompleted();
	/** 返回服务器 Run 聚合的只读公开事实；客户端应读取 GameState 的复制副本。 */
	const FCatRunPublicState& GetRunPublicState() const;
#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试专用：在没有 ST_RunFlow 资产时种入最小 Run 状态，让阶段入口 gate 可被公共测试覆盖；正式构建不暴露该入口。 */
	void SeedRunPhaseEntryForAutomation(ECatRunPhase CurrentPhase, int32 CurrentDayIndex, int64 CurrentRevision);
	/** 自动化测试专用：读取最近一次阶段入口或事件提交写下的结构化结果，用于断言服务器在翻天确认点选了哪条结算边；正式构建不暴露该入口。 */
	const FCatRunTransitionResult& GetLastRunFlowResultForAutomation() const;
#endif
	/** Online Client 主动离局前标记当前 Controller；Logout 据此按 VoluntaryLeaveRecovery 决定是否保留重连准入。 */
	void MarkVoluntaryLeave(AController* Controller);
	/** 远端 Client 完成本地 DestroySession 后确认同一 Host exit RequestId；全部确认会提前结束有界等待。 */
	void AcknowledgeHostExitClient(AController* Controller, FGuid RequestId);
	/** owning client 完成真实 Profile Grant ACK 后复核 Host exit 的全部依赖；只在远端 Destroy ACK 也齐全时提前 Ready。 */
	void NotifyHostExitGrantAckProgress();
	/** 只读判断当前 Controller 是否仍为 Active 且 Run 玩法命令门开放；teardown/回执协议不调用该 gate。 */
	bool CanAcceptGameplayCommand(const AController* Controller) const;
	/**
	 * 房主把某个成员踢出本局的服务器唯一收口。请求者身份一律从它自己的 PlayerState 重建，客户端载荷只能指定“踢谁”。
	 * 会话成员关系与房主资格由 Online 裁决；裁决通过后本方法做两件本地事：压制该身份的重连准入，再走 GameSession 断开。
	 * 目前不向被踢者发任何通知，所以他本机看到的和一次普通掉线没有区别；等 UI 有地方显示"被房主移出"时再补这条回执。
	 * 返回 None 表示已经执行踢出，其余取值原样来自 Online 的裁决结果，调用方不必再翻译一次。
	 */
	ECatOnlineError RequestHostKick(AController* RequesterController, const FString& TargetStableNetId);

private:
	/** 服务器最小身份记录阶段；Reserved 来自 PreLogin，Active 只在 PostLogin 与具体 Controller 配对。 */
	enum class EAdmissionPhase : uint8
	{
		/** 登录已通过唯一性检查，但尚未产生可安全调用 RPC 的 Controller。 */
		Reserved,
		/** PlayerState 已带有效 UniqueId，且记录已与当前 Controller 精确配对。 */
		Active
	};

	/** 单个 StableNetId 的服务器内存记录；不复制、不持有 Character，也不成为第二份 StableNetId 真相。 */
	struct FAdmissionRecord
	{
		/** 当前装配阶段；PreLogin 写 Reserved，PostLogin 写 Active。 */
		EAdmissionPhase Phase = EAdmissionPhase::Reserved;
		/** Active 阶段的 Controller 弱引用；Logout 只在相等时清理，避免旧连接删除新占用。 */
		TWeakObjectPtr<AController> Controller;
	};

	/** 按 StableNetIdExposure 策略生成日志表示；未裁时只输出 Valid(Redacted)，不公开原始 Steam 标识。 */
	static FString MakeStableNetIdLogValue(const FUniqueNetIdRepl& UniqueId);
	/** 检查 Controller 的 PlayerState UniqueId 是否命中 Active 且弱引用精确相等；不做恢复或替换。 */
	bool IsControllerActive(const AController* Controller) const;
	/** 判断当前服务器是否处于可使用开发身份的 Editor PIE 无会话环境；任何非 PIE、已有会话或活动 Online 操作都返回 false。 */
	bool IsPieNoSessionAdmissionAllowed() const;
	/**
	 * PostLogin 身份接缝失败时通过 GameSession 拒绝连接；不调用父类 PostLogin，也不释放无法安全归属的 Reserved 记录，
	 * 因而不会生成 Character 或擅自实施未裁的过期策略。
	 */
	void RejectPostLoginController(APlayerController* NewPlayer, const FString& Reason);
	/** 从已激活 Controller 读取唯一身份并写入命令上下文；客户端提交的 StableNetId 永远被覆盖。 */
	bool FillServerCommandIdentity(const AController* Controller, FCatRunCommandContext& Context) const;
	/** 组合身份、命令类别与 RequestId 的服务器幂等键；该字符串不复制到公开快照或日志。 */
	static FString MakeRunCommandCacheKey(const FString& StableNetId, ECatRunCommandType CommandType, const FGuid& RequestId);
	/** 创建与当前 Revision/Phase 对齐的命令结果，集中保证拒绝和提交返回相同事实字段。 */
	FCatRunCommandResult MakeRunCommandResult(const FGuid& RequestId, bool bCommitted, ECatRunCommandError Error, ECatRunTransitionReason Reason = ECatRunTransitionReason::None) const;
	/** 命中首次终态时先比较业务载荷；相同载荷返回只读重放，载荷漂移返回 InvalidPayload 且不改写 Run。 */
	bool TryReplayRunCommand(const FString& CacheKey, const FString& PayloadSignature, const FGuid& RequestId, FCatRunCommandResult& OutResult) const;
	/** 保存命令的首次同步终态和载荷签名；后续相同身份、类别与 RequestId 只能读取同一业务事实。 */
	FCatRunCommandResult CacheRunCommandResult(const FString& CacheKey, const FString& PayloadSignature, const FCatRunCommandResult& Result);
	/** 进入普通夜晚时冻结当前 Active 身份集合并清空个人 ready，未裁的晚加入不会隐式扩容。 */
	void CaptureNightReadyEligibility();
	/** ready 集合首次全部完成时裁定当日额度成败，并只发送对应的一个 StateTree 事件；不在 C++ 改写 Phase。 */
	void EvaluateAllEligibleReady();
	/** 清除旧白天计时器与公开截止时间；任何新 Phase 在建立自己的副作用前都先调用。 */
	void ClearDayDeadline();
	/** 白天唯一截止回调关闭钓鱼并发送 DayElapsed 事件；它不判定额度，夜晚也没有倒计时器。 */
	void HandleDayDeadlineElapsed();
	/** 进入新白天时对在场玩家执行清晨副作用：清个人 ready、打断进行中的钓鱼会话、把仍未获救的倒地者按营地休息路径救起。 */
	void ApplyDayBreakSideEffects();
	/** 按当前 Run Phase 发布 GameState 组合快照；需要环境事实的阶段调用 Environment provider，终局阶段只发布对齐 Revision 的中性环境快照。 */
	bool RefreshEnvironmentAndPublish();
	/** 按 Environment 给出的下一个时段分界重排唯一一次性计时器；当前拿不到分界时改为清掉计时器，不留跨阶段回调。 */
	void ScheduleNextTimeOfDayBoundary();
	/** 时段分界到点回调；只在仍持有截止点的白天推进一次 Revision 并重新发布，让 Morning/Day/Dusk 真的往前走。 */
	void HandleTimeOfDayBoundaryReached();
	/** 进入两种结算夜时收摊商人猫与团队装备库，让购买、免费饵、售鱼入账、交付确认和入库统一落到 CommandsClosed；重复调用无副作用。 */
	void CloseShopForSettlementNight();
	/**
	 * 把商人猫当前的公开经济事实整体重建后发布到 GameState。
	 * 每次都重建全量而不是追加单笔：一局的流水条数是几十量级，重建代价可以忽略；
	 * 而增量拼接一旦漏掉一次广播，客户端账本就会和服务器永久分叉，事后没有任何自校正机会。
	 */
	void PublishShopEconomySnapshot();
	/** 把团队装备库当前快照整体发布到 GameState；库快照里没有服务器私有身份，直接原样复制。 */
	void PublishTeamEquipmentLibrarySnapshot();
	/**
	 * 把服务器私有 StableNetId 解析成全场可见的 PlayerState。
	 * 复制出口需要它，是因为领域服务手上只有 StableNetId，而按项目约定 StableNetId 不能进复制 DTO。
	 * 身份不在场（已离局、尚未 Active）时返回 nullptr，表现层据此显示为未知操作者，而不是猜一个人顶上。
	 */
	APlayerState* ResolvePlayerStateByStableNetId(const FString& StableNetId) const;

	/**
	 * 在环境事件真的换了一个之后落实它的副作用：森林湖鱼群向那片水域投一次窝料，会抛印记的那几类提交一条印记候选。
	 * 只在事件 ID 与上一次发布不同时才做事，因为同一段事件会随每次环境重发布反复出现在快照里，
	 * 而"出场"在产品口径上是一次性的；同一事件连着两槽还在，算同一次出场，不该再投一次窝或再拍一张。
	 */
	void ApplyEnvironmentEventSideEffects();
	/** 把森林湖鱼群这一次出场折算成一条投窝命令，提交给玩家投窝共用的那个写口；缺水域、缺三轴或缺子系统时不投。 */
	void SubmitNaturalAggregationForActiveEvent();
	/** 为会抛印记的环境事件提交一条印记候选；印记册准入名单未放行该事件时只留一行诊断，不伪造候选。 */
	void SubmitEnvironmentImprintCandidateForActiveEvent();
	/** 只向正在运行的 StateTree 发送稳定 GameplayTag；本方法不包含 Phase 转移表。 */
	bool SendRunStateTreeEvent(FGameplayTag EventTag, ECatRunTransitionReason Reason);
	/** 启动 gate 失败时保持 NotStarted、关闭写口并发布 StartupFailed，不回退为 C++ 状态机。 */
	void FailRunStartup(const TCHAR* Reason);
	/** Host exit 的远端 Destroy ACK 与 Profile Grant ACK 全部到达或统一超时后广播 Ready；重复完成不会触发第二次 Online Destroy。 */
	void CompleteHostExitAckWait(bool bTimedOut);
	/** Host exit 统一有界等待的超时回调；只消费当前 GameMode 的单一 Timer，且不把超时写成真实 ACK。 */
	void HandleHostExitAckTimeout();

	/** StableNetId 到最小装配记录的服务器唯一映射；GameMode 不复制到客户端，World 销毁时整体释放。 */
	TMap<FString, FAdmissionRecord> AdmissionRecords;
	/** 连接丢失身份到服务器世界时间过期点；只在显式 TTL/失败白名单下建立，不恢复旧 FishingSession。 */
	TMap<FString, double> ReconnectExpiryByStableNetId;
	/** Online 在主动 Client leave 前提交的短生命周期身份标记；Logout 消费后立即移除。 */
	TSet<FString> VoluntaryLeaveStableNetIds;
	/** 本次 PreLogin 命中未过期记录的身份；PostLogin 用它区分重连和普通中途加入后立即清除。 */
	TSet<FString> PendingReconnectStableNetIds;
	/**
	 * 已被房主踢出、因此不允许再借重连窗口回来的身份。
	 * 踢人和掉线在引擎眼里都是同一次 Logout；不留这个标记，Logout 会照常给被踢的人写 60 秒重连准入，
	 * 他下一秒就能重新进来，踢人等于没发生。标记由 Logout 精确消费，但消费后仍然保留：
	 * 同一局内被踢过就一直算被踢过，不给“再连一次”留缝。
	 */
	TSet<FString> KickedStableNetIds;
	/** 商人猫公开流水广播的订阅句柄；与订阅成对解除，避免上一局的经济服务回调进入下一张地图。 */
	FDelegateHandle ShopPublicTransactionHandle;
	/** 团队装备库变化广播的订阅句柄；与上面同一套成对规则。 */
	FDelegateHandle TeamEquipmentLibraryHandle;

	/** 承载 ST_RunFlow 的唯一运行组件；关闭自动启动后只由 StartPlay gate 显式装载。 */
	UPROPERTY(VisibleAnywhere, Category = "Run")
	TObjectPtr<UStateTreeComponent> RunStateTreeComponent;
	/** Environment Core 接口的配置实现对象；GameMode 只通过只读合同消费结果，配置未裁时保持 Unknown。 */
	UPROPERTY(VisibleAnywhere, Category = "Run")
	TObjectPtr<UObject> EnvironmentProvider;
	/** Run 的服务器权威聚合与公开复制 DTO 来源；只有本 GameMode 写入。 */
	FCatRunPublicState RunPublicState;
	/** 最近一次外部事件或 EnterPhase 结果；StateTree 条件只读该结构而不猜测拓扑。 */
	FCatRunTransitionResult LastRunFlowResult;
	/** 当前 Run 是否仍接受玩法命令；启动失败、Ending 与 teardown 会永久关闭。 */
	bool bRunCommandsOpen = false;
	/** StateTree 正在同步 StartLogic 的短生命周期标记；允许首个 EnterPhase Task 在启动返回前写入。 */
	bool bRunStartupInProgress = false;
#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试的 StateTree 入口旁路；只用于缺少资产时验证 GameMode 服务端 gate，不参与正式运行或复制。 */
	bool bAutomationRunPhaseEntryBypass = false;
#endif
	/** 本普通夜晚的合资格 StableNetId 快照；加入/重连策略未裁时不会自动添加。 */
	TSet<FString> NightReadyEligibleIds;
	/** 本普通夜晚已确认 ready 的 StableNetId 集合；玩家撤销或离开时同步移除。 */
	TSet<FString> NightReadyIds;
	/** 本夜是否已经发出全员确认事件；发出后关闭撤销窗口，避免重复 StateTree 事件。 */
	bool bAllEligibleReadyEventSent = false;
	/** 身份、命令类别与 RequestId 到首次终态的缓存；保证 RPC 重试不会重复提交。 */
	TMap<FString, FCatRunCommandResult> RunCommandTerminalCache;
	/** Run 命令首次终态对应的业务载荷签名；同 RequestId 换贡献、ready 值或结算 Revision 会被拒绝。 */
	TMap<FString, FString> RunCommandPayloadByKey;
	/** 白天截止的唯一计时器句柄；每次 Phase 进入和 teardown 都先清除。 */
	FTimerHandle DayDeadlineTimerHandle;
	/** 白天内下一个时段分界（晨末或暮初）的唯一一次性计时器句柄；夜晚、结算夜、收口和 teardown 一律清空，
	 *  因此它只可能在 DayActive 期间存在，不会在别的阶段把过期的时段推进唤醒。 */
	FTimerHandle TimeOfDayBoundaryTimerHandle;
	/** Host teardown 完成通知；它不复制且只在服务器 GameMode 生命周期内有效。 */
	FCatRunTeardownCompleted RunTeardownCompleted;
	/** 上一次发布出去的环境事件 ID；它只用来识别"事件换了没有"，因此环境刷新多少次都只在真的换事件时触发一次副作用。 */
	FName LastPublishedEnvironmentEventId = NAME_None;
	/** 当前 Host exit 仍待确认的远端 StableNetId；服务器只保存私有键，不复制原始身份。 */
	TSet<FString> PendingHostExitAckStableNetIds;
	/** 当前 Host exit 的关联 RequestId；远端 ACK 和超时必须匹配它。 */
	FGuid ActiveHostExitRequestId;
	/** 当前 Host exit 的 Online epoch；完成广播原样返回，迟到 ACK 不进入下一代。 */
	int64 ActiveHostExitOperationEpoch = 0;
	/** 远端 ACK 的唯一有界等待计时器；EndPlay 和完成路径成对清除。 */
	FTimerHandle HostExitAckTimerHandle;
	/** 当前 Host exit 是否已完成远端 Destroy ACK 与最终 Grant ACK 的统一有界等待；超时完成不修改各自真实 ACK 记录。 */
	bool bHostExitAckWaitComplete = false;
};
