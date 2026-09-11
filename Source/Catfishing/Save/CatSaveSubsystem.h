#pragma once

#include "CoreMinimal.h"
#include "Save/CatRunSaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CatSaveSubsystem.generated.h"

class ACatCharacter;
class ACatfishingGameModeBase;
class AController;

/** 存档目录或活动槽状态变化通知；前端收到后重新读取摘要与结果文本，不持有可写镜像。 */
DECLARE_MULTICAST_DELEGATE(FCatSaveChanged);
/** 一次异步世界存档落盘完成通知；Online 用 RequestId 只匹配自身离开前的保存。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatSaveCompleted, FGuid, bool);

/** 游戏实例级世界持久化入口；它协调异步磁盘 I/O 与领域受控恢复，不直接改 FastArray 或 Profile。 */
UCLASS()
class CATFISHING_API UCatSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	friend class FCatPhysicalCharacterSaveRestoreConsumerTest;
public:
	/** 初始化槽摘要缓存与恢复状态；磁盘文件仍由 RefreshSlotSummaries 读取，避免前端把未扫描目录当成空档。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 反初始化时清除只属于本游戏实例的委托与待恢复快照；不假设 EndPlay 期间还能完成异步写盘。 */
	virtual void Deinitialize() override;

	/** 扫描当前世界槽单文件并在完成后广播目录变化；磁盘读取进行中重复调用会被合并。 */
	void RefreshSlotSummaries();

	/** 槽摘要缓存是最近一次磁盘扫描后的只读视图；前端只能据此渲染列表，不能把数组当作可编辑目录。 */
	const TArray<FCatSaveSlotSummary>& GetSlotSummaries() const;

	/** 新世界槽创建请求会分配稳定 SlotId 并写入单一空载荷文件；成功只表示槽可被后续正式读取，不直接进入玩法。 */
	FCatSaveResult RequestCreateSlot(const FString& DisplayName);

	/** 异步读取指定槽的真实载荷；成功时暂存待恢复快照并允许 Online 发起玩法旅行。 */
	FCatSaveResult RequestLoadSlot(FName SlotId);

	/** 删除请求只接受非活动槽，且会移除该槽唯一载荷文件；忙碌或当前局被拒绝以免玩家跨槽丢档。 */
	FCatSaveResult RequestDeleteSlot(FName SlotId);

	/** 由 Host 在检查点或离开前请求异步保存当前世界；客户端、无活动槽、领域未就绪和可追回的偷鱼窗口都会拒绝。 */
	FCatSaveResult RequestSaveActiveRun();

	/** 已确认取消房间、创建失败或返回前端后释放本局载荷和旅行许可；busy 时拒绝且不清任何状态，不代替离开前保存。 */
	bool ReleaseActiveRun();

	/** 返回当前已成功读取且尚未完成世界恢复的旅行许可；Online 只读它，不接触私有快照。 */
	bool HasLoadedRunForTravel() const;

	/** 忙碌状态表示槽扫描或世界载荷已有磁盘 I/O 在途；前端据此合并重复点击，避免同一逻辑槽并发写删。 */
	bool IsBusy() const;

	/** 最近结果文本是给前端看的同步受理或异步完成反馈；错误必须在这里可见，不能只依赖日志定位。 */
	FText GetLastResultText() const;

	/** 活动槽标识代表当前已加载并授予旅行许可的存档；None 表示还没读入世界或本局已释放。 */
	FName GetActiveSlotId() const;

	/** 存档目录、活动槽或结果文本变化时的本机通知；不携带可写状态。 */
	FCatSaveChanged OnChanged;

	/** 异步保存完成通知；Online 的 HostRunTeardown 通过 RequestId 只等待自己的那一次保存。 */
	FCatSaveCompleted OnSaveCompleted;

	/** 在玩法 World 的 GameMode 和营地宿主已可用后恢复共享营地与世界鱼容器；失败会保留明确错误且撤销旅行许可。 */
	bool RestoreWorldAfterHostsReady(ACatfishingGameModeBase& GameMode);

	/** 在 GameMode 生成并占有本机 Character 后恢复反序列化出的玩家快照，并记录保存位置与实际落点。 */
	bool RestorePlayerAfterSpawn(AController& Controller, ACatCharacter& Character);

	/** 在本机玩家解除占有或 Logout 清理前把末次库存和位置写入当前 SaveGame 对象；失败封锁后续覆盖已有磁盘文件。 */
	bool CapturePlayerBeforeLogout(AController& Controller, ACatCharacter* DepartingCharacter = nullptr);

private:
	/** 根据稳定槽标识生成唯一世界槽文件名；显示名称不参与路径，避免玩家改名导致读不到文件。 */
	static FString MakeRunSlotFileName(FName SlotId);

	/** 在开始异步操作前创建受理结果并同步刷新最后消息；调用方只能以 bAccepted 判断是否排队成功。 */
	FCatSaveResult MakeResult(bool bAccepted, const FText& Message);

	/** 从当前 authority World 采集完整已提交状态；任一领域快照或本机玩家记录不完整时不启动写盘。 */
	bool BuildActiveRunSaveGame(UCatRunSaveGame& OutSaveGame, FText& OutFailure) const;

	/** 校验读取载荷的版本、槽归属、本机玩家记录和跨聚合实例唯一性；领域细节仍由恢复 API 再次复核。 */
	bool ValidateLoadedRunSaveGame(const UCatRunSaveGame& SaveGame, FName ExpectedSlotId, FText& OutFailure) const;

	/** 新建或检查点的单文件写入完成后更新内存摘要并记录写入位置；失败时保留上一份摘要并要求玩家重试。 */
	void HandleRunSaved(UCatRunSaveGame* SavedGame, bool bSuccess, FGuid RequestId);

	/** 世界槽异步读取完成后核对请求槽、真实类型和内容，再设置待恢复快照与旅行许可并记录读取位置。 */
	void HandleRunLoaded(ULocalPlayerSaveGame* LoadedGame, FGuid RequestId, FName RequestedSlotId);

	/** 结束已串行化的磁盘请求，清候选强引用并发布结果；有效 RequestId 才产生外部完成通知。 */
	void FinishDiskRequest(FGuid RequestId, bool bSuccess, const FText& Message);

	/** 取消当前待恢复许可并释放载荷强引用；恢复拒绝后 Online 不能用失效载荷继续旅行或重试局部写入。 */
	void RejectPendingRestore(const FText& Failure);

	/** 当前机器可见世界槽的内存目录；磁盘扫描和成功写入后整体替换或局部更新，前端只读它。 */
	TArray<FCatSaveSlotSummary> SlotSummaries;

	/** 槽目录是否已至少完成一次磁盘扫描或由本轮创建成功建立；目录未就绪时拒绝 CRUD，不能拿空数组误判没有已有存档。 */
	bool bSlotDirectoryLoaded = false;

	/** 当前已读入的正式运行槽；加载成功写入，终态释放清除，新建目录项不会隐式占用它。 */
	FName ActiveSlotId = NAME_None;

	/** 活动槽的待恢复世界载荷；读档反序列化得到，恢复、采样和退出都直接读写同一个 SaveGame 对象。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatRunSaveGame> PendingRestoreSaveGame;

	/** 当前交给异步世界保存的不可变载荷；反射强引用覆盖序列化、工作线程写盘与游戏线程回调之间的 GC 窗口。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatRunSaveGame> ActiveAsyncRunSaveGame;

	/** 当前待恢复世界是否已完整写入营地和鱼容器；防止 StartPlay 与重试重复覆写同一 World。 */
	bool bWorldRestoreApplied = false;

	/** 本机玩家快照在当前 World 是否已经应用过；重生或重复 PostLogin 不会再次覆盖玩家在本局中新产生的库存。 */
	bool bLocalPlayerRestoredInCurrentWorld = false;

	/** 已解除占有并完成末次捕获的连接；Logout 据此识别 Pawn 已销毁的正常路径，弱引用不延长连接寿命。 */
	TSet<TWeakObjectPtr<AController>> CapturedDepartingControllers;

	/** 无法捕获末次状态的不可恢复缺口；当前 World 后续保存必须拒绝，避免用失效快照覆盖较新的玩家库存。 */
	FText PlayerCaptureFailure;

	/** 异步操作占用标记；所有磁盘入口串行化，避免创建、删除、加载和检查点交叉覆盖目录。 */
	bool bBusy = false;

	/** 读取成功到世界恢复完成之间允许 Host 旅行的事实；失败、删除活动槽或恢复拒绝都会清掉它。 */
	bool bLoadedRunForTravel = false;

	/** 当前已恢复 World 开始计入玩法时长的世界秒数；负值表示尚未进入可采样的玩法 World。 */
	double ActiveRunStartedWorldSeconds = -1.0;

	/** 当前槽在进入本 World 前已累计的玩法秒数；每次保存加上真实 World 运行时长，不计前端或磁盘 I/O 等待。 */
	double ActiveRunBasePlayedDurationSeconds = 0.0;

	/** 最近一次请求或异步完成的可展示说明；每次状态改变后与 OnChanged 一起对前端可见。 */
	FText LastResultText;
};
