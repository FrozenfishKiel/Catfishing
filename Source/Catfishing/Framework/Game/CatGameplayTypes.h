#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatRunContracts.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "GameplayTagContainer.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Social/CatSocialTypes.h"
#include "CatGameplayTypes.generated.h"

class UStateTreeComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class ACatCampHubActor;
class ACatCharacter;
class ACatWaterRegion;
struct FInputActionValue;

/** GameState Run/Environment 完整公开快照变化通知；本机 UI 必须重新读取 GetRunPublicState。 */
DECLARE_MULTICAST_DELEGATE(FCatRunPublicStateChanged);

/** GameState 最近求助完整快照变化通知；本机 UI 必须重新读取 GetLastHelpSignal。 */
DECLARE_MULTICAST_DELEGATE(FCatHelpSignalChanged);

/** 前台专用模式；明确不生成默认 Pawn，只承载 LocalPlayer Online UI。 */
UCLASS()
class CATFISHING_API ACatFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	/** 在类默认对象上关闭默认 Pawn 生成；菜单保留 Controller 承载 LocalPlayer UI。 */
	ACatFrontendGameMode();
	/** Frontend 开始玩法时记录地图和无 Pawn 装配；不创建 Session 或直接旅行。 */
	virtual void StartPlay() override;
};

/** Lake 服务器权威根；先以 APlayerState::UniqueId 维护 Reserved/Active 占用，再装配 Character，并独占 Run/StateTree、截止计时与 Host teardown 写权。 */
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

	/** StateTree EnterPhase Task 的唯一阶段写入口；先验证阶段策略，再成对切换截止计时、ready 资格与命令 gate，最后只发布一份组合快照。 */
	FCatRunTransitionResult EnterRunPhaseFromStateTree(ECatRunPhase NewPhase, ECatRunTransitionReason Reason);
	/** StateTree Condition 只读比较最近一次外部结果原因，不从当前 Phase 反推事件来源。 */
	bool DoesLastRunFlowResultMatch(ECatRunTransitionReason ExpectedReason) const;
	/** 消费服务器已确认的额度贡献；StableNetId 由 Controller 适配并以 RequestId/Revision 保证幂等。 */
	FCatRunCommandResult SubmitQuotaContribution(AController* RequestingController, const FCatQuotaContributionCommand& Command);
	/** 献祭协调器在消费鱼前只读验证额度命令；不写缓存、Revision 或 StateTree 事件。 */
	FCatRunCommandResult ValidateCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command) const;
	/** Items 已 committed 后由一局协调器提交额度；使用服务器私有 StableNetId，不要求玩家仍在线。 */
	FCatRunCommandResult SubmitCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command);
	/** 写入普通夜晚的个人翻天确认；只有启动时冻结的合资格集合可以影响全员事件。 */
	FCatRunCommandResult SubmitNextDayReady(AController* RequestingController, const FCatNextDayReadyCommand& Command);
	/** 供服务器结算协调器提交完成终态；本方法只发送 StateTree 事件，不在 C++ 选择目标 Phase。 */
	FCatRunCommandResult CompleteSettlementFromCoordinator(FGuid RequestId, int64 ExpectedRevision);
	/** Host 离局前关闭新命令、计时器和 StateTree；结果原样携带 Online RequestId/epoch。 */
	FCatRunTeardownResult RequestRunTeardown(const FCatRunTeardownRequest& Request);
	/** 返回 Run teardown 终态委托；Online 必须在回调中再次核对 RequestId/epoch。 */
	FCatRunTeardownCompleted& OnRunTeardownCompleted();
	/** 返回服务器 Run 聚合的只读公开事实；客户端应读取 GameState 的复制副本。 */
	const FCatRunPublicState& GetRunPublicState() const;
	/** Online Client 主动离局前标记当前 Controller；Logout 据此按 VoluntaryLeaveRecovery 决定是否保留重连准入。 */
	void MarkVoluntaryLeave(AController* Controller);
	/** 远端 Client 完成本地 DestroySession 后确认同一 Host exit RequestId；全部确认会提前结束有界等待。 */
	void AcknowledgeHostExitClient(AController* Controller, FGuid RequestId);
	/** owning client 完成真实 Profile Grant ACK 后复核 Host exit 的全部依赖；只在远端 Destroy ACK 也齐全时提前 Ready。 */
	void NotifyHostExitGrantAckProgress();
	/** 只读判断当前 Controller 是否仍为 Active 且 Run 玩法命令门开放；teardown/回执协议不调用该 gate。 */
	bool CanAcceptGameplayCommand(const AController* Controller) const;

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

	/** 将 APlayerState 继承的有效 UniqueId 转成仅服务器使用的映射键；无效 ID 返回空串。 */
	static FString MakeStableNetIdKey(const FUniqueNetIdRepl& UniqueId);
	/** 按 StableNetIdExposure 策略生成日志表示；未裁时只输出 Valid(Redacted)，不公开原始 Steam 标识。 */
	static FString MakeStableNetIdLogValue(const FUniqueNetIdRepl& UniqueId);
	/** 检查 Controller 的 PlayerState UniqueId 是否命中 Active 且弱引用精确相等；不做恢复或替换。 */
	bool IsControllerActive(const AController* Controller) const;
	/** 判断当前服务器是否处于可使用开发身份的 Editor PIE 无会话环境；任何非 PIE、已有会话或活动 Online 操作都返回 false。 */
	bool IsPieNoSessionAdmissionAllowed() const;
	/** PostLogin 身份接缝失败时通过 GameSession 拒绝连接；不调用父类 PostLogin，也不释放无法安全归属的 Reserved 记录，因而不会生成 Character 或擅自实施未裁的过期策略。 */
	void RejectPostLoginController(APlayerController* NewPlayer, const FString& Reason);
	/** 从已激活 Controller 读取唯一身份并写入命令上下文；客户端提交的 StableNetId 永远被覆盖。 */
	bool FillServerCommandIdentity(const AController* Controller, FCatRunCommandContext& Context) const;
	/** 组合身份、命令类别与 RequestId 的服务器幂等键；该字符串不复制到公开快照或日志。 */
	static FString MakeRunCommandCacheKey(const FString& StableNetId, ECatRunCommandType CommandType, const FGuid& RequestId);
	/** 创建与当前 Revision/Phase 对齐的命令结果，集中保证拒绝和提交返回相同事实字段。 */
	FCatRunCommandResult MakeRunCommandResult(const FGuid& RequestId, bool bCommitted, ECatRunCommandError Error, ECatRunTransitionReason Reason = ECatRunTransitionReason::None) const;
	/** 命中首次终态时返回只读重放结果；重复请求只报告 AlreadyResolved，不再次写真相。 */
	bool TryReplayRunCommand(const FString& CacheKey, FCatRunCommandResult& OutResult) const;
	/** 保存命令的首次同步终态；后续相同身份、类别与 RequestId 只能读取该记录。 */
	FCatRunCommandResult CacheRunCommandResult(const FString& CacheKey, const FCatRunCommandResult& Result);
	/** 已由服务器适配好 StableNetId 的额度唯一实现；玩家 RPC 与献祭协调器都汇入此处。 */
	FCatRunCommandResult SubmitQuotaContributionInternal(const FCatQuotaContributionCommand& ServerCommand);
	/** 进入普通夜晚时冻结当前 Active 身份集合并清空个人 ready，未裁的晚加入不会隐式扩容。 */
	void CaptureNightReadyEligibility();
	/** ready 集合首次全部完成时发布公开事实并只发送 AllEligibleReady 事件，不在 C++ 改写 Phase。 */
	void EvaluateAllEligibleReady();
	/** 清除旧白天计时器与公开截止时间；任何新 Phase 在建立自己的副作用前都先调用。 */
	void ClearDayDeadline();
	/** 白天唯一截止回调关闭额度写口并发送 QuotaFailed 事件；夜晚没有倒计时器。 */
	void HandleDayDeadlineElapsed();
	/** 把当前 Run Revision 的只读 DTO交给 Environment，并将同一组合快照发布到 GameState。 */
	bool RefreshEnvironmentAndPublish();
	/** 当前环境事件首次出现时把显式自然输入提交给唯一 WaterRegion；成功键按 Day+Event 去重，失败保留重试机会。 */
	void SubmitNaturalAggregationIfConfigured();
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
	/** 本普通夜晚的合资格 StableNetId 快照；加入/重连策略未裁时不会自动添加。 */
	TSet<FString> NightReadyEligibleIds;
	/** 本普通夜晚已确认 ready 的 StableNetId 集合；玩家撤销或离开时同步移除。 */
	TSet<FString> NightReadyIds;
	/** 本夜是否已经发出全员确认事件；发出后关闭撤销窗口，避免重复 StateTree 事件。 */
	bool bAllEligibleReadyEventSent = false;
	/** 身份、命令类别与 RequestId 到首次终态的缓存；保证 RPC 重试不会重复提交。 */
	TMap<FString, FCatRunCommandResult> RunCommandTerminalCache;
	/** 白天截止的唯一计时器句柄；每次 Phase 进入和 teardown 都先清除。 */
	FTimerHandle DayDeadlineTimerHandle;
	/** Host teardown 完成通知；它不复制且只在服务器 GameMode 生命周期内有效。 */
	FCatRunTeardownCompleted RunTeardownCompleted;
	/** 已成功提交自然聚鱼的 Day+Event 键；只活在本 GameMode，防止同一环境刷新重复消耗共享预算。 */
	TSet<FString> SubmittedNaturalAggregationKeys;
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

/** Lake 共享比赛状态；复制由服务器 GameMode 组合的 Run/Environment 快照与 Social 最近求助事实。 */
UCLASS()
class CATFISHING_API ACatfishingGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	/** 注册 RunPublicState 与 LastHelpSignal 两份整结构复制；客户分别经 RepNotify 消费各自 Revision 对齐的完整快照。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅允许 authority GameMode 写入组合公开事实；每次写入都会触发网络更新。 */
	void SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState);
	/** 提供服务器最终值或客户端最近复制值，调用方据此渲染一局状态；返回 const 引用保证外部不能绕过 GameMode 写口推进 Run。 */
	const FCatRunPublicState& GetRunPublicState() const;
	/** 仅允许 authority Social 服务发布最近一次求助；它不启动任务或自动加入 Fishing。 */
	void SetHelpSignalFromAuthority(const FCatHelpSignalSnapshot& NewSignal);
	/** 提供最近一次服务器求助信号供表现去重；它不是任务分配或自动加入玩法的授权依据。 */
	const FCatHelpSignalSnapshot& GetLastHelpSignal() const;
	/** 本机 Run/Environment 完整快照变化通知；不授权订阅者推进 StateTree。 */
	FCatRunPublicStateChanged OnRunPublicStateChanged;
	/** 本机最近求助完整快照变化通知；不授权订阅者接受任务或改变 Social 权限。 */
	FCatHelpSignalChanged OnHelpSignalChanged;
protected:
	/** 实例进入 World 后记录实际类；不增加可写玩法状态。 */
	virtual void BeginPlay() override;
	/** 客户端收到新 Revision 后记录结构化诊断，UI/玩法只能继续读取复制快照。 */
	UFUNCTION()
	void OnRep_RunPublicState();
	/** 客户端收到新求助 Revision 后只记录/供表现读取，不自动执行互动。 */
	UFUNCTION()
	void OnRep_HelpSignal();

private:
	/** Run 的唯一公开复制快照；服务器 GameMode 写，客户端 RepNotify 读。 */
	UPROPERTY(ReplicatedUsing = OnRep_RunPublicState)
	FCatRunPublicState RunPublicState;

	/** Social 在 authority 写入的最近一条手动/巨鱼信号；客户 OnRep 只通知表现，范围和全局标记始终由服务器裁决。 */
	UPROPERTY(ReplicatedUsing = OnRep_HelpSignal)
	FCatHelpSignalSnapshot LastHelpSignal;
};

/** Lake 玩家身份与个人局状态宿主；复用 APlayerState::UniqueId，只增加普通夜 ready 与主动公开的鱼图鉴摘要。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	/** 注册个人翻天确认与公开鱼图鉴摘要复制；StableNetId 继续复用 APlayerState::UniqueId。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅由服务器 GameMode 写入本人的普通夜晚 ready 事实，客户端 RPC 参数不能直接赋值。 */
	void SetNextDayReadyFromAuthority(bool bNewReady);
	/** 返回本人的最终 ready 事实；它不代表全员资格或 StateTree 已经转移。 */
	bool IsReadyForNextDay() const;
	/** 仅服务器接收 owning client 提交的公开鱼图鉴摘要；严格校验后整体复制给局内其他玩家查看。 */
	bool SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records);
	/** 提供局内玩家可见的鱼图鉴摘要；相册、Journal 和解锁被排除，避免 PlayerState 成为第二份 Profile。 */
	const TArray<FCatFishCollectionRecord>& GetPublicFishCollection() const;
	/** 查询服务器是否持有指定装备解锁的可信证明；当前未接服务端 Profile 证明源，None 视为 starter，其余默认拒绝。 */
	bool HasServerAuthorizedEquipmentUnlock(FName UnlockId) const;
protected:
	/** 玩家状态进入 World 后记录继承 UniqueId 是否有效；原始值是否输出由 StableNetIdExposure 策略控制。 */
	virtual void BeginPlay() override;
	/** 客户端接收个人 ready 变化后记录诊断；不从 RepNotify 发送 Run 事件。 */
	UFUNCTION()
	void OnRep_ReadyForNextDay();
private:
	/** 当前普通夜晚的个人翻天确认；仅 authority GameMode 写入/在新夜资格冻结前清零，客户 RepNotify 只更新本人表现。 */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyForNextDay)
	bool bReadyForNextDay = false;

	/** authority 在严格校验 owning client 摘要后整体替换的公开鱼图鉴；局内其他玩家可读，不含相册、Journal、解锁、装备或原始 StableNetId。 */
	UPROPERTY(Replicated)
	TArray<FCatFishCollectionRecord> PublicFishCollection;
};

/** Lake owning-client 的网络适配器；将 Run、Fishing、Camp、Condition、Items、Social 意图转给 authority，并承接 Profile Grant/CapturePlan/HostExit 回执，不自存领域真相。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	/** 控制器接管 Pawn 后记录装配结果；不缓存 Pawn 或创建第二条 Online 旅行入口。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** owning client 收到 Pawn 复制变化后重置疾跑意图，并把普通移动速度应用到新 Pawn。 */
	virtual void OnRep_Pawn() override;
	/** 把客户端额度意图转发给 authority GameMode；身份由服务器 PlayerState 派生。 */
	UFUNCTION(Server, Reliable)
	void ServerSubmitQuotaContribution(FGuid RequestId, int64 ExpectedRevision, int32 Contribution);
	/** 把客户端翻天确认转发给 authority GameMode；GameMode 负责资格、Revision 与幂等裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerSetNextDayReady(FGuid RequestId, int64 ExpectedRevision, bool bReady);

	/** 结算夜请求检查本局成像终态与 Grant ACK；只有归档已收口才向 Run StateTree 发送 SettlementComplete。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSettlementCompletion(FGuid RequestId, int64 ExpectedRevision);

	/** 服务器向 owning client 投递不可变永久 Grant；客户端只有 durable Profile 完成后才发 ACK。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveProfileGrant(const FCatProfileGrant& Grant);

	/** 客户端在本地 Journal 完整落盘后确认 GrantId；服务器重建身份，并在真实 ACK 后复核 Host exit 有界等待。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeProfileGrant(FGuid GrantId);

	/** 服务器向 owning client 投递独立 CapturePlan；本 RPC 不表示图片或 Grant 已成功。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveImprintCapturePlan(const FCatCapturePlan& Plan);

	/** 外部本地成像桥回报真实结果；成功必须携带 durable ImprintId，服务器随后才生成 Grant。 */
	UFUNCTION(Server, Reliable)
	void ServerReportImprintCaptureResult(FGuid CapturePlanId, bool bSucceeded, FGuid ImprintId);

	/** 请求服务器按当前 Run/Environment/Water 与可参战协作能力快照开始一次钓鱼会话；RequestId 只用于重放，不参与抽鱼。 */
	UFUNCTION(Server, Reliable)
	void ServerStartFishingSession(FGuid RequestId);

	/** 把 Giant HookedFight 的手动协作意图转给指定 FishingSession。 */
	UFUNCTION(Server, Reliable)
	void ServerAssistFishingSession(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);

	/** 把 NearShore 抢抄意图转给指定 FishingSession；服务器覆盖本人的鱼护 ID，首个合法提交者由 Compare-and-Commit 决定。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	/** 把唯一献祭命令转给 SacrificeCoordinator；Controller 不直接删鱼或增加 Run 额度。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSacrifice(FCatSacrificeCommand Command);

	/** 在固定营地范围请求本人休息；Camp/Condition 负责位置和身体裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampRest(ACatCampHubActor* Camp, FGuid RequestId);

	/** 在固定营地请求可跳过的篝火回看；结算夜全员在场时同时建立本局封面 CapturePlan。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampfirePlayback(ACatCampHubActor* Camp, FGuid RequestId);

	/** 在固定营地把本人鱼护的一条鱼原子转入共享鱼缸。 */
	UFUNCTION(Server, Reliable)
	void ServerTransferFishToTank(ACatCampHubActor* Camp, FGuid RequestId, FGuid FishInstanceId,
		int64 ExpectedGuardRevision, int64 ExpectedTankRevision);

	/** 伙伴把倒地目标送到固定营地 RescuePoint；没有死亡/重生旁路。 */
	UFUNCTION(Server, Reliable)
	void ServerRescueCharacterToCamp(ACatCampHubActor* Camp, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 提交三个功能装备 ID；服务器目录与可信解锁证明共同通过后才允许首次装配，客户端 Profile 选择本身不授予权限。 */
	UFUNCTION(Server, Reliable)
	void ServerConfigureEquipment(FGuid RequestId, int64 ExpectedRevision, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId);

	/** 在固定营地消费浮木并修复当前鱼竿；不升级或替换装备。 */
	UFUNCTION(Server, Reliable)
	void ServerRepairRodAtCamp(ACatCampHubActor* Camp, FGuid RequestId, int64 ExpectedEquipmentRevision);

	/** 消费本人一份草药后恢复目标 Character；库存提交成功前不会修改身体。 */
	UFUNCTION(Server, Reliable)
	void ServerUseHerbOnCharacter(ACatCharacter* TargetCharacter, FGuid RequestId,
		int64 ExpectedEquipmentRevision, FName HerbDefinitionId);

	/** 从本人鱼护或共享缸直接吃一条鱼；Items 移除成功后才按 FishDefinition 修改 Hunger/Poison。 */
	UFUNCTION(Server, Reliable)
	void ServerConsumeFish(ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command);

	/** 开始一条鱼的偷取与追回窗口；Social 覆盖客户端身份并保证每个小偷最多一条。 */
	UFUNCTION(Server, Reliable)
	void ServerBeginTheft(FCatTheftCommand Command);

	/** 服务器把 Begin/Catch 的首次或重放结果发回 owning client；ProtocolId 只能通过该权威结果取得。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveTheftResult(const FCatTheftResult& Result);

	/** 提供本机最近收到的偷鱼协议结果供 UI 读取；它不授权客户端直接访问 Social 或 Items 写口。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Social")
	FCatTheftResult GetLastTheftResult() const;

	/** 在进食窗口内按服务器返回的 ProtocolId 追回；Social 按权威主人、状态、距离与共享缸策略授权。 */
	UFUNCTION(Server, Reliable)
	void ServerCatchTheft(FGuid TheftProtocolId);

	/** 手动发布普通钓鱼或倒地求助；普通信号不会升级为全局任务。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestManualHelp(FGuid RequestId, ECatHelpSignalKind Kind);

	/** 请求一次普通恶作剧许可；Social 重新验证目标 Controller、冷却与 ProtectionSign。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestMischief(APlayerState* TargetPlayerState, FGuid RequestId, FVector InteractionLocation);

	/** 在本人附近放置或移动唯一防骚扰牌子；Social 用显式范围配置保护普通恶作剧。 */
	UFUNCTION(Server, Reliable)
	void ServerPlaceProtectionSign(FGuid RequestId, FVector SignLocation);

	/** 本人完成抖水表现后请求清除 Wet；该入口只改表现状态，不恢复生存数值或提供玩法增益。 */
	UFUNCTION(Server, Reliable)
	void ServerCompleteShakeDry(FGuid RequestId);

	/** 消费一份正式 Chum 并向脚下唯一 WaterRegion 提交同一 RequestId；定义拥有三轴值，客户端不能自报贡献。 */
	UFUNCTION(Server, Reliable)
	void ServerContributeChum(ACatWaterRegion* WaterRegion, FGuid RequestId, int64 ExpectedEquipmentRevision,
		int64 ExpectedAggregationRevision, FName ChumDefinitionId);

	/** Online Client 在 DestroySession 前通知服务器这是主动离局；GameMode 不把它误判为连接故障。 */
	UFUNCTION(Server, Reliable)
	void ServerMarkVoluntaryLeave();

	/** Host exit 通知远端在本地执行统一 Online Destroy/Frontend 链；不把它标成玩家主动离局。 */
	UFUNCTION(Client, Reliable)
	void ClientPrepareForHostExit(FGuid RequestId);

	/** 远端本地 DestroySession 成功后向 Host 回 ACK；GameMode 只接受当前 Active Controller 与同 RequestId。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeHostExit(FGuid RequestId);

	/** 服务器登录完成后让 owning client 从 durable Profile 刷新公开图鉴摘要。 */
	UFUNCTION(Client, Reliable)
	void ClientRefreshPublicFishCollection();

	/** owning client 只提交鱼图鉴记录；PlayerState 在服务器验证唯一 ID、数量和数值后整体发布。 */
	UFUNCTION(Server, Reliable)
	void ServerPublishPublicFishCollection(const TArray<FCatFishCollectionRecord>& Records);

protected:
	/** 为本地 Controller 安装显式配置的玩法 Mapping Context；服务端远端 Controller 不接触本地输入子系统。 */
	virtual void BeginPlay() override;
	/** 绑定 Move、Look、Jump、Sprint Enhanced Input Action，并保留父类输入初始化。 */
	virtual void SetupInputComponent() override;
	/** Pawn 断开前恢复普通速度并清除疾跑意图，避免状态泄漏到下一次占有。 */
	virtual void OnUnPossess() override;
	/** 只移除本 Controller 实际安装的 Mapping Context，不清空 LocalPlayer 的其他输入层。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 玩法输入映射；在 PlayerController 蓝图默认值中接入 IMC。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** 二维移动输入：X 为左右，Y 为前后。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> MoveAction;

	/** 二维视角输入：X 为 Yaw，Y 为 Pitch；反转与灵敏度由 IMC Modifier 配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> LookAction;

	/** 跳跃输入；Started 调用 Jump，Completed/Canceled 调用 StopJumping。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> JumpAction;

	/** 长按疾跑输入；Started 开启疾跑，Completed/Canceled 恢复普通移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> SprintAction;

	/** 未按疾跑键时 CharacterMovement 的最大地面移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input|Movement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float WalkMaxSpeed = 100.0f;

	/** 按住疾跑键时 CharacterMovement 的最大地面移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input|Movement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float SprintMaxSpeed = 350.0f;

	/** 本 Controller 的输入层优先级；不影响其他系统已经安装的 Mapping Context。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	int32 InputMappingPriority = 0;

private:
	/** 幂等安装当前配置的玩法 Mapping Context；BeginPlay/输入初始化均可安全调用。 */
	void ApplyInputMappingContext();
	/** 移除本 Controller 安装的玩法 Mapping Context，并清空弱绑定记录。 */
	void RemoveInputMappingContext();
	/** 按 Controller 的水平朝向把二维输入转成当前 Pawn 的前后/左右移动。 */
	void Move(const FInputActionValue& Value);
	/** 把二维输入写入 Controller 的 Yaw/Pitch。 */
	void Look(const FInputActionValue& Value);
	/** 对当前已占有的 Character 开始跳跃。 */
	void StartJump();
	/** 对当前已占有的 Character 停止跳跃。 */
	void StopJump();
	/** 本地 Started 输入开启疾跑，并把布尔意图可靠同步给 authority。 */
	void StartSprint();
	/** 本地 Completed/Canceled 输入关闭疾跑，并把布尔意图可靠同步给 authority。 */
	void StopSprint();
	/** 更新本 Controller 的疾跑意图并应用速度；仅本地输入路径需要向服务器转发。 */
	void SetSprintRequested(bool bNewSprintRequested, bool bNotifyServer);
	/** 把服务器配置的普通/疾跑速度应用到指定 Character；非 Character Pawn 安全跳过。 */
	void ApplySprintSpeed(APawn* TargetPawn, bool bSprinting) const;

	/** owning client 只提交疾跑开关；最终速度始终取服务器 PlayerController 类默认配置。 */
	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	/** 实际接收 AddMappingContext 的本地输入子系统；只用于成对 Remove。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;

	/** 实际安装的 Context；与蓝图配置分开记录以支持安全清理。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;

	/** 当前 Controller 的疾跑按键意图；客户端和 authority 分别维护，不作为远端动画事实复制。 */
	UPROPERTY(Transient)
	bool bSprintRequested = false;

	/** 统一向 authority GameMode 查询运行内玩法命令 gate；缺少 GameMode、非 Active 或 teardown 关门时返回 false。 */
	bool CanForwardGameplayCommand() const;

	/** owning client 最近收到的 Social 协议读模型；由可靠结果 RPC 整体替换，不复制回服务器或作为权限事实。 */
	UPROPERTY(Transient)
	FCatTheftResult LastTheftResult;

	/** 本 Controller 的玩家窝料首次终态；覆盖 Equipment→WaterRegion 同步协调，重放不会再次扣耗材或增加池。 */
	TMap<FGuid, FCatAggregationResult> PlayerChumTerminalCache;
};
