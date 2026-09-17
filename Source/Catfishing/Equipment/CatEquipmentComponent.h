#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/CatEquipmentLoadoutSnapshot.h"
#include "Fishing/CatFishingUseResults.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatEquipmentComponent.generated.h"

class UCatEquipmentItemDefinition;
class UCatEquipmentInventoryItemInstance;
class UCatInventoryComponent;
class APlayerState;
struct FCatInventoryEntry;

/** Equipment 钓鱼选择读模型发生提交或复制变化的本机通知；UI 只把它当重读信号。 */
DECLARE_MULTICAST_DELEGATE(FCatEquipmentSnapshotChanged);

/** Character 的一局钓鱼选择组件；真实物品事实由 InventoryComponent 持有，Equipment 只复制钓鱼链仍需读取的选择载荷。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class UCatFishingService;

public:
	/** 开启默认复制并关闭 Tick；所有写入由 authority 命令提交。 */
	UCatEquipmentComponent();
	/** Whether this deployed instance is bound by a live fishing transaction, including a borrower. */
	bool IsFishingRodInUse(FGuid ItemInstanceId) const;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool TryGetInventoryRodForDeployment(FCatInventoryEntry& OutRod) const;
	bool MoveFishingResourcesToCustodian(UCatEquipmentComponent* Target, const TArray<FGuid>& SessionIds, const TArray<FGuid>& RodItemInstanceIds);
	FString FishingResourceOwnerStableId;

	/** 注册钓鱼选择读模型；终态缓存不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终钓鱼选择读模型；调用方只能显示或校验版本，不能通过引用补耐久或改库存。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	const FCatEquipmentLoadoutSnapshot& GetSnapshot() const;

	/** 导出存档需要的钓具选择；随身库存必须由 InventoryComponent 单独导出，未结算 Fishing 使用冻结时明确失败。 */
	bool ExportSnapshotFromAuthority(FCatEquipmentLoadoutSnapshot& OutSnapshot, FText& OutFailure) const;

	/** 退出快照已被 Save 接收后移除本玩家已登记的部署鱼竿表现；实例仍由退出记录持有，重连只恢复一份。 */
	bool RetireDeploymentAfterPersistentCapture(APlayerState& PlayerState);

	/** 从反序列化快照恢复钓具选择；调用前随身库存必须已由 InventoryComponent 恢复，入口只校验选择引用和鱼竿摘要。 */
	bool RestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot, FText& OutFailure);

	/** 根据正式库存目录、可信解锁证明和随身库存实例设置当前钓鱼选择；每个非空选择都必须指向正式库存里的具体实例。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32  RodItemId, int32  BaitItemId, int32  FloatItemId,
		int32  ScoopNetItemId, FName RodSkinDefinitionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, FGuid ScoopNetItemInstanceId);

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化夹具只读预检数量型库存授予；用于构造可复查库存场景，不参与商店或运行时发货入口。 */
	ECatDomainCommandError ValidateInventoryQuantityGrant(FGuid RequestId, int32  ItemId,
		int32 Quantity) const;

	/** 自动化夹具只读预检非数量库存授予；用于验证装备定义能进入正式库存，不裁决运行时购买流程。 */
	ECatDomainCommandError ValidateEquipmentGrantFromAuthority(FGuid RequestId, int32  ItemId) const;

	/** 自动化夹具授予数量型库存物品；先写 InventoryComponent，再刷新 Equipment 钓鱼选择读模型。 */
	FCatDomainCommandResult GrantInventoryQuantityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32  ItemId, int32 Quantity);

	/** 自动化夹具授予非数量库存物品；先写 InventoryComponent，再刷新 Equipment 读模型和钓鱼选择。 */
	FCatDomainCommandResult GrantEquipmentFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32  ItemId);
#endif

	/** 鱼竿部署的资源入口；按原实例借出保管并更新装备选择，不执行消耗品效果。 */
	FCatInventoryItemUseResult Use(FGuid RequestId, int64 ExpectedRevision, FGuid ItemInstanceId,
		int32 Quantity = 1);

	/** 部署型物品收口时共用的停止使用入口；InventoryComponent 归还同一实例，Equipment 只同步钓鱼选择读模型。 */
	FCatInventoryItemUseResult UnUse(FGuid RequestId, FGuid ItemInstanceId);

	/** InventoryComponent 提交后刷新钓具选择读模型；返回 false 表示当前库存事实无法支持选择校正。 */
	bool RefreshLoadoutFromInventoryComponentFromAuthority();


	/** Fishing 会话开始前按 SessionId 申请当前钓鱼选择使用权；Begin 只验有饵，不扣数量，并把世界鱼竿归属库存记录为耐久写回目标。 */
	FCatFishingUseFreezeResult BeginFishingUse(FGuid FishingSessionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, int32  RodItemId,
		int32  BaitItemId, int32  FloatItemId, int64 ExpectedRevision,
		UCatInventoryComponent* RodInventoryComponent = nullptr);
	/** 真咬时从原抛竿者当前选中的正式鱼饵实例扣一份；同会话重放不重复扣。 */
	FCatFishingUseOperationResult CommitFishingBaitDeferred(FGuid FishingSessionId);
	/** 按递增累计磨损的差额立即扣减 Begin 绑定的鱼竿实例；重复序号不重扣，Release 不回滚。 */
	FCatFishingUseOperationResult ApplyFishingRodWear(FGuid FishingSessionId, int64 WearSequence,
		double AbsoluteTotal);
	/** 从该会话绑定的正式库存鱼竿实例读取耐久，不读取当前选择的另一根竿。 */
	bool GetFishingRodDurability(FGuid FishingSessionId, double& OutDurability, bool& OutBroken) const;
	/**
	 * 声明：把这场钓鱼绑定的那根**已断**鱼竿从库存事实里整条移除——不返还、不占格、不进存档。
	 * 依据：道具册「鱼竿断裂后直接消失」（不返还／不替换／不回退三条此前已成立，缺的是「消失」这一条）。
	 * 实现：按会话记录找到绑定实例，确认它确实已断，再按它当前所在的位置移除：
	 *       正在部署 → 退役 held entry；已经收回可见格 → 直接清那一格。
	 *       随后清掉指向它的钓具选择并重新校正，让自动改选按「库存里已经没有这根竿」跑。
	 * 边界：竿没断、会话记录不存在或不是服务器时返回 false 且不动任何状态；本函数不销毁世界里的鱼竿 Actor，
	 *       那是钓鱼侧的事（Actor 与库存实例是两件东西，同一次断竿要两边都收）。
	 */
	bool RetireBrokenFishingRodFromAuthority(FGuid FishingSessionId);

	/** 结束使用记录并解除竿锁；不改变鱼饵数量，不建立退款或延迟退款。 */
	// 墓碑（2026-09-13）：删除 bReturnCaughtBait 参数；钓鱼规则 §3.3/§3.4 规定真咬后任何结局均消耗 1 份饵。
	FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
	/** 当前是否有仍未结束的 Fishing 使用记录；失败预算用它避开进行中的钓鱼结算。 */
	bool HasActiveFishingUse() const;
	/** 指定 Fishing 会话是否仍处于活动状态；Commit/Release 用它防止已结束会话重复改写。 */
	bool IsFishingUseActive(FGuid FishingSessionId) const;
	/** 捕获收口只检查真咬已扣饵，不允许在捕获时补扣当前选择。 */
	bool IsFishingBaitCommitted(FGuid FishingSessionId) const;
	/** 钓鱼选鱼只读原抛竿者当前饵种；托管协调器不改来源，不扣数量。 */
	int32  GetCurrentFishingBaitItemId(FGuid FishingSessionId) const;

	/** 本机随身库存或钓鱼选择变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

	/** 按已部署鱼竿的精确实例 ID 从 held entry 解析原始库存实例；RodActor 的操作能力授予以它作 SourceObject。 */
	UCatEquipmentInventoryItemInstance* ResolveDeployedRodItemInstanceFromAuthority(FGuid RodItemInstanceId) const;

private:
	/** 仅验证钓具选择是否仍由当前 InventoryComponent 中的实例支撑；导出与恢复共用，authority 和活动事务由各自入口控制。 */
	bool ValidatePersistentSnapshotPayload(const FCatEquipmentLoadoutSnapshot& Candidate, FText& OutFailure) const;

	/** 单个 Fishing 会话的短生命周期使用记录；它只保存 原扣饵来源和鱼竿磨损，不给库存拖放提供通用占用规则。 */
	struct FCatFishingUseRecord
	{
		/** Begin 冻结的鱼竿实例 ID；后续磨损必须写回这根正式库存实例，不能按当前选择重新选竿。 */
		FGuid RodItemInstanceId;
		/** Begin 冻结的鱼竿定义 ID；耐久写回时和实例 ID 一起复核，防止同实例误挂到别的定义。 */
		int32  RodItemId = 0;
		/** 世界鱼竿背后的正式库存组件；借竿时属于部署者，磨损查询和写入都只从这里找同一实例。 */
		TWeakObjectPtr<UCatInventoryComponent> RodInventory;
		/** 原抛竿者的装备选择来源；托管不复制背包，真咬读取该来源当前的饵。 */
		TWeakObjectPtr<UCatEquipmentComponent> BaitSourceEquipment;
		/** 已接收的竿磨损序号；磨损事件按递增序号提交，重复或跳号不会改耐久。 */
		int64 LastWearSequence = 0;
		/** 已按差额写入绑定实例的累计磨损；仅用于序号去重，不是另一份剩余耐久。 */
		double AbsoluteRodWear = 0.0;
		/** 已完成一次真咬扣饵；通知前置为 true，防止回调重入重复消费。 */
		bool bBaitCommitted = false;
		/** 会话已关闭；释放只解锁鱼竿，不回滚鱼饵。 */
		bool bReleased = false;
	};

	/** 按 Fishing SessionId 读取可写短记录；Commit、Wear 和 Release 用它收口同一会话的饵料与耐久事实。 */
	FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId);
	/** 按 Fishing SessionId 读取只读短记录；查询和重放结果组装不能借此修改库存冻结状态。 */
	const FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId) const;
	/** 库存活动区里部署物品的只读格载荷；配置和 Fishing Begin 用它从 held entry 确认当前部署实例。 */
	bool TryBuildHeldInventoryUseSlot(FGuid ItemInstanceId, FCatInventoryEntry& OutSlot) const;
	/** Begin 冻结鱼竿的正式实例解析；正式库存存在时，耐久读写必须落到可见格或 held entry 里的同一 UObject。 */
	UCatEquipmentInventoryItemInstance* ResolveFishingRodFormalInstanceFromInventory(
		const FCatFishingUseRecord& Record, FCatInventoryEntry& OutSlot) const;

	/** 是否存在正式库存活动区尚未收口的物品 Use；失败预算用它避免改写正在由场景持有的物品状态。 */
	bool HasActiveInventoryItemUse() const;
	/** 正式库存刷新后校正钓鱼选择；选中实例离开随身库存时换到仍存在的同类实例或清空选择。 */
	void ReconcileLoadoutSelectionsWithInventory(
		const UCatEquipmentItemDefinition* PreferredDefinition, int32  PreferredItemId);
	/** 组装 Fishing Begin 回包；只读取会话记录和绑定鱼竿实例，不创建新记录或改写库存。 */
	FCatFishingUseFreezeResult MakeFishingUseFreezeResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bUseAccepted, const FCatFishingUseRecord* Record = nullptr) const;
	/** 组装 Fishing 后续操作回包；Commit、Wear 和 Release 共用它输出同一份诊断口径。 */
	FCatFishingUseOperationResult MakeFishingUseOperationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bApplied, const FCatFishingUseRecord* Record = nullptr) const;
	/** 客户端收到钓鱼选择读模型后只供 UI/玩法只读消费；不反向请求自动选择。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 读取随身库存配置容量；0 表示本局没有可用格子，写入路径必须拒绝新物品。 */
	int32 GetConfiguredInventorySlotCapacity() const;

	/** 解析 Owner 身上的随身库存组件；物品发放、消耗和存档导入都以它为唯一库存事实源。 */
	UCatInventoryComponent* ResolveOwnerInventoryComponent() const;

	/** 读取一个定义在单格里的最大堆叠数；装备型物品固定为 1，数量型物品使用项目配置。 */
	int32 GetInventoryStackLimit(const UCatEquipmentItemDefinition& Definition) const;

	/** 从库存定位当前选择的鱼竿实例；失败伤竿用它直接写实例状态，读模型只接收实例状态。 */
	UCatEquipmentInventoryItemInstance* ResolveSelectedFormalRodInstanceFromInventory(
		UCatInventoryComponent& OwnerInventory, const UCatEquipmentItemDefinition& RodDefinition,
		FCatInventoryEntry& OutSlot) const;

	/** 从 Owner 库存校正当前钓具选择；发货、移动和使用收口都靠它让选择跟随真实背包。 */
	bool RefreshLoadoutFromInventoryComponentFromAuthority(
		const UCatEquipmentItemDefinition* GrantedDefinition, int32  GrantedItemId);

	/** 按实例身份从正式可见库存读取运行格；选择和诊断用它避免只按 ItemId 误认同类物品。 */
	bool TryFindInventorySlotByInstanceId(FGuid ItemInstanceId, FCatInventoryEntry& OutSlot) const;

	/** 按定义和实例身份解析钓鱼选择候选；只从 InventoryComponent 读取可用于选择的库存事实。 */
	bool TryResolveSelectionInventorySlot(int32  ItemId, FGuid ItemInstanceId,
		FCatInventoryEntry& OutSlot) const;


	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** 发布钓鱼选择读模型；真实背包事实必须已经由 InventoryComponent 提交。 */
	void PublishSnapshot();

	/** 钓鱼选择和鱼竿摘要的复制读模型；InventoryComponent 持有物品事实。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 部署和收回的首次终态；缓存保活原实例，网络重放不重新移动实物。 */
	UPROPERTY(Transient)
	TMap<FString, FCatInventoryItemUseResult> ItemUseTerminalCache;

	/** 装备选择、物品发放和物品使用命令的首次终态缓存；背包整理已由 InventoryComponent 自己维护幂等结果。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；普通入库和 Use/UnUse 共用它防止同一 RequestId 被换定义、数量或实例后再次利用。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 当前 Character 生命周期内按 SessionId 隔离的 Fishing 使用冻结记录；不复制也不持久化。 */
	TMap<FGuid, FCatFishingUseRecord> FishingUseRecords;

	/** 抄网选择复制日志只在定义或实例变化时输出，不参与玩法裁决。 */
	int32  LastLoggedScoopNetItemId = 0;
	/** 最近一次已记录的抄网实例 ID；只用于减少重复日志，不代表装备选择状态。 */
	FGuid LastLoggedScoopNetItemInstanceId;
	/** 最近一次已记录的鱼竿实例 ID；只用于客户端复制诊断限频，不参与耐久或玩法裁决。 */
	FGuid LastLoggedRodInstanceId;
	/** 最近一次已记录的鱼竿耐久档位；日志按档位过滤，真实耐久仍来自正式库存实例。 */
	int32 LastLoggedRodDurabilityBand = INDEX_NONE;
	/** 最近一次已记录的鱼竿断裂状态；只用于日志边沿过滤，不影响钓鱼命令。 */
	bool bLastLoggedRodBroken = false;
	/** 核对条目的装备实例、身份与数量，返回引用同一实例的条目；拒绝非装备实例供钓具选择使用。 */
	bool TryReadEquipmentInventoryEntry(const FCatInventoryEntry& Entry, FCatInventoryEntry& OutSlot) const;
};
