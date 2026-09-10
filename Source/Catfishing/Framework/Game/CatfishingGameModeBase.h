#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayTagContainer.h"
#include "CatfishingGameModeBase.generated.h"

class ACatCharacter;

class UStateTreeComponent;
class ACatCampHubActor;
struct FUniqueNetIdRepl;
#if WITH_DEV_AUTOMATION_TESTS
class FCatGameModeCommandIntentGateTest;
class FCatGameModeReconnectAdmissionWhitelistTest;
class FCatGameModeRunEnvironmentSocialPlayerEntrypointContractTest;
#endif

namespace CatGameplayPlayerLimits
{
	/** 当前固定营地自动出生布局支持的玩家上限；Online 会话容量和 GameMode 出生裁决必须读取同一口径，避免允许无法落地的第 5 名玩家进入玩法世界。 */
	inline constexpr int32 MaxCampSpawnPlayers = 4;
}

#if !UE_BUILD_SHIPPING
/** 服务器 Run 私有调试快照；只在非 Shipping 的服务器本机生成给诊断面板读取，字段来自 GameMode 当前内存事实，不复制给客户端，也不成为第二份玩法真相。 */
struct FCatRunAuthorityDebugSnapshot
{
	/** 当前读取方是否处在 authority GameMode 上；false 表示面板只能依赖 GameState 复制结果，看不到服务器私有门禁。 */
	bool bHasAuthorityGameMode = false;

	/** Run 玩法命令总门当前是否打开；GameMode 是唯一写方，调试面板只用它解释公开 Phase 与服务器命令门是否一致。 */
	bool bRunCommandsOpen = false;

	/** ST_RunFlow 组件当前是否挂在 GameMode 上；组件缺失时服务器无法消费供品或结算事件。 */
	bool bRunStateTreeAssigned = false;

	/** ST_RunFlow 组件当前是否处于运行态；事件只会在运行态下被 StateTree 正式接收。 */
	bool bRunStateTreeRunning = false;

	/** Run 启动期间首个 EnterPhase Task 是否仍在回调窗口内；它解释启动阶段短暂“StateTree 已启动但公开阶段尚未稳定”的合法状态。 */
	bool bRunStartupInProgress = false;

	/** 本轮普通夜晚是否已经发过继续事件；供品结算后用于诊断当前 StateTree 资产是否消费了当前事件。 */
	bool bAllEligibleReadyEventSent = false;

	/** 最近一次 StateTree 事件或 EnterPhase 的处理结果；用来判断事件已经发出但资产没有发生阶段转移的情况。 */
	FCatRunTransitionResult LastRunFlowResult;

	/** 开发期跳天加速是否正在等待正式流程推进；只暴露调试请求状态，不参与客户端玩法判断。 */
	bool bDebugSkipToNextDayRequested = false;

	/** 开发期跳天加速请求所属 Run；面板用它判断请求是否仍对应当前一局。 */
	FGuid DebugSkipToNextDayRunId;

	/** 开发期跳天加速请求所属天数；面板用它解释当前是在等本天进夜晚还是等下一天白天。 */
	int32 DebugSkipToNextDayDayIndex = 0;
};
#endif

/** Lake 服务器权威根；先以 APlayerState::UniqueId 维护 Reserved/Active 占用，再装配 Character，并独占 Run/StateTree、截止计时与 Host teardown 写权。 */
UCLASS()
class CATFISHING_API ACatfishingGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	/** 建立 Lake 原生宿主装配；身份注册表属于 GameMode 实例，不进入类默认对象或客户端。 */
	ACatfishingGameModeBase();
	/** 建立 Run 并验证依赖，先完成 Save 共享世界和已有 Pawn 恢复再启动 StateTree 与检查点；任何恢复失败保持 StartupFailed。 */
	virtual void StartPlay() override;
	/** World 退出时解除 Pawn 捕获通知并清检查点、白天与 HostExit 计时，关闭命令和 StateTree；最后写盘由离开前协议负责。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 在 Login 前校验身份与唯一占用；正式玩家建立 Reserved，PIE 无会话远端的无效身份只放行到服务器 InitNewPlayer 分配。 */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	/** 在引擎注册 PlayerState 前为 PIE 无会话玩家注入仅本次 World 有效的服务器身份；正式平台身份仍原样进入父类流程。 */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	/** 在父类生成 Character 之前把 Reserved 提升为与 Controller 匹配的 Active，再进入引擎标准 PostLogin 链。 */
	virtual void PostLogin(APlayerController* NewPlayer) override;
	/** 生成 Character 前再次验证 PlayerState 的继承 UniqueId 与 Active Controller 匹配，失败时不调用父类生成。 */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	/** 玩家重启时只允许从唯一固定营地进入默认生成链；生成和占有完成后再应用存档位置，找不到合法营地时保持无 Pawn。 */
	virtual void RestartPlayer(AController* NewPlayer) override;
	/** 查找玩家出生点时忽略客户端 Portal 和上一次 StartSpot，只返回当前 World 唯一营地；缺失或重复营地会返回空。 */
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName = TEXT("")) override;
	/** 选择玩家出生点时只扫描唯一 ACatCampHubActor；普通 PlayerStart 运行时不会成为候选。 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** 禁止沿用 Controller 上一次 StartSpot；每次生成都重新按当前唯一营地和当前玩家队列解析。 */
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;
	/** 在唯一营地附近生成 Pawn 并接入退出捕获；存档恢复统一由 RestartPlayer 在占有完成后提交。 */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
	/** 对精确 Active 连接捕获或复核最后库存与位置，再解除 Pawn 通知并清准入；无 Pawn 时要求解除占有阶段已捕获。 */
	virtual void Logout(AController* Exiting) override;

	/** StateTree EnterPhase Task 的唯一阶段写入口；先验证阶段策略，再成对切换截止/时段刷新计时、供品窗口与命令 gate，最后只发布一份组合快照。 */
	FCatRunTransitionResult EnterRunPhaseFromStateTree(ECatRunPhase NewPhase, ECatRunTransitionReason Reason);
	/** StateTree Condition 只读比较最近一次外部结果原因，不从当前 Phase 反推事件来源。 */
	bool DoesLastRunFlowResultMatch(ECatRunTransitionReason ExpectedReason) const;
	/** 消费服务器已确认的夜晚供品结算；StableNetId 由 Controller 适配并以 RequestId/Revision 保证幂等。 */
	FCatRunCommandResult SubmitOfferingSettlement(AController* RequestingController, const FCatOfferingSettlementCommand& Command);
	/** 供 owning client 在成像归档已收口后提交结算完成终态；本方法只发送 StateTree 事件，不在 C++ 选择目标 Phase。 */
	FCatRunCommandResult CompleteSettlementFromServerRequest(FGuid RequestId, int64 ExpectedRevision);
	/** Host 离局前关闭新命令、计时器和 StateTree；结果原样携带 Online RequestId/epoch。 */
	FCatRunTeardownResult RequestRunTeardown(const FCatRunTeardownRequest& Request);
	/** 返回 Run teardown 终态委托；Online 必须在回调中再次核对 RequestId/epoch。 */
	FCatRunTeardownCompleted& OnRunTeardownCompleted();
	/** 返回服务器 Run 聚合的只读公开事实；客户端应读取 GameState 的复制副本。 */
	const FCatRunPublicState& GetRunPublicState() const;
#if !UE_BUILD_SHIPPING
	/** 开发期调试入口：在服务器开放的 DayActive 上，把当前白天从此刻起的剩余时长重设为可进入 UE timer 的正秒数；成功会重写时间窗口、重排 timer、递增 Revision 并发布 RunPublicState，失败返回 false 且不改天数或客户端本地状态。 */
	bool ApplyDebugDayLengthSeconds(double NewDayLengthSeconds);
	/** 开发期调试入口：请求服务器把当前开放白天推进到普通夜晚；已经处于普通夜晚时只确认状态，不提交供品、不改天数、不直接写 Phase。 */
	bool ApplyDebugSkipToNight();
	/** 开发期调试入口：请求服务器用正式白天结束与夜晚供品结算把当前 Run 加速到下一天；返回 true 表示请求已被接收或同日请求已在等待推进，返回 false 表示当前无 authority、无 World、阶段不支持或正式命令 gate 拒绝；Phase 与 DayIndex 仍只由 StateTree 阶段入口写入。 */
	bool ApplyDebugSkipToNextDay();
	/** 开发期作弊救援入口：在非 Shipping 的服务器上重启 ST_RunFlow 到下一次 DayActive，仅用于失败结算夜继续人工测试或普通夜供品结算事件后解卡；它绕过产品拓扑但仍让 StateTree 的 DayActive 入口写正式 Run 快照。 */
	bool ApplyDebugForceNextDay();
	/** 开发期只读诊断入口：把 GameMode 不复制的 StateTree、命令门和跳天请求折成一次性副本；调用者只能展示，不能据此推进 Run。 */
	FCatRunAuthorityDebugSnapshot GetAuthorityDebugSnapshotForDebug() const;
#endif
	/** Online Client 主动离局前标记当前 Controller；Logout 据此按 VoluntaryLeaveRecovery 决定是否保留重连准入。 */
	void MarkVoluntaryLeave(AController* Controller);
	/** 远端 Client 完成本地 DestroySession 后确认同一 Host exit RequestId；全部确认后再复核最终 Grant ACK。 */
	void AcknowledgeHostExitClient(AController* Controller, FGuid RequestId);
	/** owning client 完成真实 Profile Grant ACK 后复核 Host exit 的全部依赖；只在远端 Destroy ACK 也齐全时提前 Ready。 */
	void NotifyHostExitGrantAckProgress();
	/** 只读判断当前 Controller 是否仍为 Active 且 Run 玩法命令门开放；teardown/回执协议不调用该 gate。 */
	bool CanAcceptGameplayCommand(const AController* Controller) const;
	/** 只读判断当前 Controller 是否可发起新的 Fishing/玩家打窝命令；Social 和结算收口不使用这个更窄的白天 gate。 */
	bool CanAcceptFishingCommand(const AController* Controller) const;
	/** 只读确认 Controller 的继承 UniqueId 已与当前 Active 记录精确配对；Save 据此筛选正式玩家，不受玩法命令开关影响，也不读取或复制私有准入表。 */
	bool IsControllerActive(const AController* Controller) const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化夹具直接种入 Active 身份与 Phase，用来验证 Fishing gate 不会误封 Social/Settlement 宽命令。 */
	friend class FCatGameModeCommandIntentGateTest;
	/** 自动化夹具只在测试体内种入并检查私有准入/重连记录，用来证明 Logout、PreLogin 与白名单策略的 fail-closed 边界。 */
	friend class FCatGameModeReconnectAdmissionWhitelistTest;
	/** 自动化夹具只给 RunEnvironmentSocial 玩家入口闭环开放最小私有状态访问；测试用同一名 Active 玩家和普通夜晚 Phase 证明 Social 宽 gate 与 Chum 窄 gate 没有分叉，不把求助、保护牌和打窝拆成可独立关闭的小任务。 */
	friend class FCatGameModeRunEnvironmentSocialPlayerEntrypointContractTest;
#endif

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
		/** Active 阶段的 Controller 弱引用；Logout 只在相等时清理，避免失效连接删除新占用。 */
		TWeakObjectPtr<AController> Controller;
	};

	/** 将 APlayerState 继承的有效 UniqueId 转成仅服务器使用的映射键；无效 ID 返回空串。 */
	static FString MakeStableNetIdKey(const FUniqueNetIdRepl& UniqueId);
	/** 按 StableNetIdExposure 策略生成日志表示；未裁时只输出 Valid(Redacted)，不公开原始 Steam 标识。 */
	static FString MakeStableNetIdLogValue(const FUniqueNetIdRepl& UniqueId);
	/** 判断当前服务器是否处于可使用开发身份的 Editor PIE 无会话环境；任何非 PIE、已有会话或活动 Online 操作都返回 false。 */
	bool IsPieNoSessionAdmissionAllowed() const;
	/** PostLogin 身份接缝失败时通过 GameSession 拒绝连接；不调用父类 PostLogin，也不释放无法安全归属的 Reserved 记录，因而不会生成 Character 或擅自实施未裁的失效策略。 */
	void RejectPostLoginController(APlayerController* NewPlayer, const FString& Reason);
	/** 从已激活 Controller 读取唯一身份并写入命令上下文；客户端提交的 StableNetId 永远被覆盖。 */
	bool FillServerCommandIdentity(const AController* Controller, FCatRunCommandContext& Context) const;
	/** 组合身份、命令类别与 RequestId 的服务器幂等键；该字符串不复制到公开快照或日志。 */
	static FString MakeRunCommandCacheKey(const FString& StableNetId, ECatRunCommandType CommandType, const FGuid& RequestId);
	/** 创建与当前 Revision/Phase 对齐的命令结果，集中保证拒绝和提交返回相同事实字段。 */
	FCatRunCommandResult MakeRunCommandResult(const FGuid& RequestId, bool bCommitted, ECatRunCommandError Error, ECatRunTransitionReason Reason = ECatRunTransitionReason::None) const;
	/** 命中首次终态时返回只读重放结果；重复请求只报告 AlreadyResolved，真相只由首次提交写入。 */
	bool TryReplayRunCommand(const FString& CacheKey, FCatRunCommandResult& OutResult) const;
	/** 保存命令的首次同步终态；后续相同身份、类别与 RequestId 只能读取该记录。 */
	FCatRunCommandResult CacheRunCommandResult(const FString& CacheKey, const FCatRunCommandResult& Result);
	/** 只读预演当前 Run ASC 会接受的夜晚供品结算；预检和正式提交都通过它在不可逆结算前发现坏倍率、坏输入或投影不一致。 */
	ECatRunCommandError PreviewRunOfferingSettlement(const FCatOfferingSettlementCommand& Command, int32& OutOfferedPoints,
		int32& OutWorldProgressDelta, int32& OutNewWorldProgress, bool& bOutMetDailyTarget) const;
	/** 已由服务器适配好 StableNetId 的夜晚供品结算唯一实现；祭坛 Actor、调试入口和其他服务器调用方都汇入此处。 */
	FCatRunCommandResult SubmitOfferingSettlementInternal(const FCatOfferingSettlementCommand& ServerCommand);
	/** 清除上一白天计时回调；只停止 deadline 与环境刷新 Timer，不改公开截止字段，供 DayActive 收口缝隙安全使用。 */
	void ClearDayTimers();
	/** 清除上一白天计时器与公开截止时间；任何新 Phase 在建立自己的副作用前都先调用。 */
	void ClearDayDeadline();
	/** 按当前白天截止窗口安排 Morning/Day/Dusk 语义刷新；无效配置只记录诊断，不创建第二套昼夜状态。 */
	void ScheduleDayEnvironmentRefreshes();
	/** 白天时段分界到达时重新发布同一 RunPublicState；只有服务器仍处于有效 DayActive 才递增 Revision。 */
	void HandleDayEnvironmentRefreshElapsed();
	/** 白天唯一截止回调关闭捕鱼并发送 DayEnded 事件进夜晚；夜晚没有倒计时器。 */
	void HandleDayDeadlineElapsed();
	/** 把当前 Run Revision 的只读 DTO 交给 Environment，并将同 Revision 的组合快照发布到 GameState；不改变角色身体或表现状态。 */
	bool RefreshEnvironmentAndPublish();
	/** 当前环境事件首次出现时把显式自然输入提交给唯一 WaterRegion；成功键按 Run+Day+Event+Anchor 去重，失败保留重试机会。 */
	void SubmitNaturalChumFieldIfConfigured();
	/** 只向正在运行的 StateTree 发送稳定 GameplayTag；本方法不包含 Phase 转移表。 */
	bool SendRunStateTreeEvent(FGameplayTag EventTag, ECatRunTransitionReason Reason);
	/** 在玩法 World 已完成存档恢复后按显式设置启动定期检查点；未加载世界槽或配置无效时保持不调度。 */
	void StartPersistenceCheckpointTimer();
	/** 定期检查点触发时只提交 Save 请求并记录受理结果；异步成功仍由 Save 回调决定，EndPlay 不会假设写盘可完成。 */
	void HandlePersistenceCheckpoint();
	/** Pawn 解除占有时由 authority 收口该 Character 的跨系统会话；先终止 Fishing 再取消 Social，避免角色身体生命周期直接持有服务职责。 */
	void HandleCharacterUnavailable(ACatCharacter* Character);
#if !UE_BUILD_SHIPPING
	/** 开发期跳天加速的当前 Run/Day 是否仍匹配；只用于避免迟到的调试结算碰到下一局或下一天。 */
	bool IsDebugSkipToNextDayRequestCurrent() const;
	/** 开发期跳天加速收口；进入新天、结算或 World 结束时清掉调试请求，不改正式 Run 状态。 */
	void ClearDebugSkipToNextDayRequest();
	/** 开发期结算玩家选择入口；返回当前仍能走正式 Run 命令 gate 的第一名服务器可见玩家，供夜晚调试结算复用。 */
	APlayerController* FindDebugOfferingController() const;
	/** 开发期结束当前白天入口；只在开放 DayActive 上关闭捕鱼并发送入夜事件，返回 false 表示白天 gate 不满足且不会推进 StateTree。 */
	bool SubmitDebugDayEndForCurrentDay(const TCHAR* Trigger);
	/** 开发期跳天加速的夜晚结算提交入口；构造一份达标调试供品并调用正式结算写口，返回 true 表示已发送继续推进事件。 */
	bool SubmitDebugOfferingSettlementForCurrentDay(const TCHAR* Trigger);
	/** 开发期跳天加速的阶段回调入口；正式 Phase 进入后决定是否安排下一步加速或清掉请求。 */
	void ContinueDebugSkipToNextDayAfterPhaseEntered(ECatRunPhase EnteredPhase);
	/** 开发期跳天加速的夜晚结算延迟入口；用下一帧提交结算，避免在 StateTree Enter 回调内重入发送事件。 */
	void ScheduleDebugSkipToNextDayOfferingSettlement();
	/** 开发期跳天加速的夜晚结算延迟回调；重新核对当前 Run/Day 后只走正式结算写口。 */
	void HandleDebugSkipToNextDayOfferingElapsed();
#endif
	/** 启动 gate 失败时保持 NotStarted、关闭写口并发布 StartupFailed，不回退为 C++ 状态机。 */
	void FailRunStartup(const TCHAR* Reason);
	/** Host exit 的远端 Destroy ACK 与 Profile Grant ACK 全部真实到达后广播 Ready；重复完成不会触发第二次 Online Destroy。 */
	void CompleteHostExitAckWait();
	/** 把商店当前余额、货架库存和公开交易记录整体发布给 GameState。 */
	void PublishShopEconomySnapshot();
	/** 将服务器私有 StableNetId 解析成可复制的 PlayerState。 */
	APlayerState* ResolvePlayerStateByStableNetId(const FString& StableNetId) const;
	/** 进入最终结算夜后关闭新商店订单；已经交付的购买物继续留在营地公共仓库正式库存中。 */
	void CloseShopForSettlementNight();
	/** StableNetId 到最小装配记录的服务器唯一映射；GameMode 不复制到客户端，World 销毁时整体释放。 */
	TMap<FString, FAdmissionRecord> AdmissionRecords;
	/** 连接丢失身份到服务器世界时间失效点；只在显式 TTL/失败白名单下建立，不恢复失效 FishingSession。 */
	TMap<FString, double> ReconnectExpiryByStableNetId;
	/** Online 在主动 Client leave 前提交的短生命周期身份标记；Logout 消费后立即移除。 */
	TSet<FString> VoluntaryLeaveStableNetIds;
	/** 本次 PreLogin 命中仍有效记录的身份；PostLogin 用它区分重连和普通中途加入后立即清除。 */
	TSet<FString> PendingReconnectStableNetIds;

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
	/** 本夜是否已经发出当前 StateTree 资产继续事件；它只诊断供品结算后的事件消费，不表示玩家个人状态。 */
	bool bAllEligibleReadyEventSent = false;
	/** 身份、命令类别与 RequestId 到首次终态的缓存；保证 RPC 重试不会重复提交。 */
	TMap<FString, FCatRunCommandResult> RunCommandTerminalCache;
	/** 白天截止的唯一计时器句柄；每次 Phase 进入和 teardown 都先清除，达标/截止收口时只停回调不伪造公开 deadline。 */
	FTimerHandle DayDeadlineTimerHandle;
	/** 白天 Morning 转 Day 的语义刷新句柄；它只触发同一 RunPublicState 重发，不决定 Phase。 */
	FTimerHandle DayMorningEnvironmentRefreshTimerHandle;
	/** 白天 Day 转 Dusk 的语义刷新句柄；它只触发同一 RunPublicState 重发，不决定 Phase。 */
	FTimerHandle DayDuskEnvironmentRefreshTimerHandle;
	/** Host teardown 完成通知；它不复制且只在服务器 GameMode 生命周期内有效。 */
	FCatRunTeardownCompleted RunTeardownCompleted;
	/** 已成功发布自然空间窝点的 Run+Day+Event+Anchor 键；只活在本 GameMode，防止环境刷新重复创建。 */
	TSet<FString> SubmittedNaturalChumFieldKeys;
	/** 当前 Host exit 仍待确认的远端 StableNetId；服务器只保存私有键，不复制原始身份。 */
	TSet<FString> PendingHostExitAckStableNetIds;
	/** 当前 Host exit 的关联 RequestId；远端 Destroy ACK 与最终 Grant ACK 进度必须匹配它。 */
	FGuid ActiveHostExitRequestId;
	/** 当前 Host exit 的 Online epoch；完成广播原样返回，迟到 ACK 不进入下一代。 */
	int64 ActiveHostExitOperationEpoch = 0;
	/** 当前 Host exit 是否已完成远端 Destroy ACK 与最终 Grant ACK 的真实等待；只有全部到达才会推进回主菜单链路。 */
	bool bHostExitAckWaitComplete = false;
	/** 商店公开经济变化的服务器本机订阅；EndPlay 成对解除，避免失效 World 回调。 */
	FDelegateHandle ShopPublicTransactionHandle;

	/** 商店货架刷新变化的服务器本机订阅；它只触发快照重建，不创建交易广播。 */
	FDelegateHandle ShopInventoryRefreshedHandle;
	/** 玩法 World 的定期持久化计时器；StartPlay 配对设置、EndPlay 只清理而不发起不可靠的最后异步保存。 */
	FTimerHandle PersistenceCheckpointTimerHandle;
#if !UE_BUILD_SHIPPING
	/** 开发期跳天加速请求是否正在等待正式阶段推进；它只表达作弊输入的短生命周期请求，不代表 Run 阶段。 */
	bool bDebugSkipToNextDayRequested = false;
	/** 开发期跳天加速请求所属 Run；用于防止上一局延迟供品结算影响新局。 */
	FGuid DebugSkipToNextDayRunId;
	/** 开发期跳天加速请求所属天数；只有同一天进入普通夜晚时才会自动提交供品结算。 */
	int32 DebugSkipToNextDayDayIndex = 0;
#endif
};
