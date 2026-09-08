#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentComponent.generated.h"

class UCatEquipmentDefinition;
class UCatEquipmentInventoryItemInstance;
class UCatInventoryComponent;
class ACatCampInventoryActor;
class APlayerState;
struct FCatInventoryEntry;

/** Equipment 旧随身库存投影与钓鱼选择快照发生提交或复制变化的本机通知；UI 只把它当重读信号。 */
DECLARE_MULTICAST_DELEGATE(FCatEquipmentSnapshotChanged);

/** Character 的一局钓鱼选择和旧库存投影组件；正式库存事实由 InventoryComponent 持有，Equipment 在迁移期只复制旧投影并服务钓鱼选择消费者。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启默认复制并关闭 Tick；所有写入由 authority 命令提交。 */
	UCatEquipmentComponent();

	/** 注册钓鱼选择和旧库存投影快照；终态缓存不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终钓鱼选择与迁移期库存投影；调用方只能显示或校验 Revision，不能通过引用补耐久或改正式库存。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	const FCatEquipmentLoadoutSnapshot& GetSnapshot() const;

	/** Character 被服务器占有后应用配置的开局装备选择；仅在组件所属 Pawn、authority 与设置均有效且尚未选竿时提交，成功后可补发配置窝料。 */
	void ApplyConfiguredStarterLoadoutFromAuthority();

	/** 导出存档需要的钓具选择和库存格；正式角色读取 InventoryComponent 投影，兼容宿主沿用旧快照读模型，未结算 Fishing 预留或收回后超容量时明确失败。 */
	bool ExportSnapshotFromAuthority(FCatEquipmentLoadoutSnapshot& OutSnapshot, FText& OutFailure) const;

	/** 退出快照已被 Save 接收后移除本玩家已登记的部署鱼竿表现；实例仍由退出记录持有，重连只恢复一份。 */
	bool RetireDeploymentAfterPersistentCapture(APlayerState& PlayerState);

	/** 只读验证一份跨地图随身库存快照是否可被本组件接收；检查 authority、定义、容量、实例唯一性和选择引用，但不写入现有库存。 */
	bool CanRestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot, FText& OutFailure) const;

	/** 在 Save 已完成全局预检后恢复钓具选择与随身库存载荷；正式角色会先把载荷导入 InventoryComponent，兼容宿主只替换旧快照读模型，局部导入失败时不改当前状态。 */
	bool RestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot);

	/** 根据服务器目录、可信解锁证明和随身库存持有量设置当前钓鱼选择；当前已部署鱼竿可作为原选择继续沿用，但不能借此切换到另一根鱼竿。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName RodDefinitionId, FName BaitDefinitionId, FName FloatDefinitionId,
		FName ScoopNetDefinitionId = NAME_None, FName RodSkinDefinitionId = NAME_None,
		FGuid RodItemInstanceId = FGuid(), FGuid BaitItemInstanceId = FGuid(),
		FGuid FloatItemInstanceId = FGuid(), FGuid ScoopNetItemInstanceId = FGuid());

	/** 只读预检数量型物品能否进入正式随身库存；商店用它保证扣款前已经确认角色确实收得下这组数量。 */
	ECatDomainCommandError ValidateInventoryQuantityGrant(FGuid RequestId, FName DefinitionId,
		int32 Quantity) const;

	/** 只读预检商店非数量物品能否进入本人正式随身库存；商店用它在扣款前确认买家 Pawn 和定义都能接收。 */
	ECatDomainCommandError ValidateEquipmentGrantFromAuthority(FGuid RequestId, FName DefinitionId) const;

	/** 一局拾取、商店或奖励上层提交数量型库存物品；正式角色先写 InventoryComponent，再刷新 Equipment 旧投影和钓鱼选择。 */
	FCatDomainCommandResult GrantInventoryQuantityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 商店或其他服务器权威来源授予非数量物品；正式角色先写 InventoryComponent，再刷新 Equipment 旧投影并修正缺失或不可用的钓鱼选择。 */
	FCatDomainCommandResult GrantEquipmentFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 临时测试入口，仅由玩家占有后的服务器调用；已有抄网则复用，没有抄网时只在库存容量足以容纳开局四件套后补给，商店获取接通后删除。 */
	void GrantStarterScoopNetIfConfigured();

	/** 背包点击或玩法入口共用的物品使用入口；ExpectedRevision 是旧装备选择投影版本，调用方提供的正式库存版本只交给 InventoryComponent 做并发复核。 */
	FCatInventoryItemUseResult Use(FGuid RequestId, int64 ExpectedRevision, FGuid ItemInstanceId,
		int32 Quantity = 1, int64 ExpectedInventoryRevision = 0);

	/** 只读查询同一 Use 请求是否已有终态；库存版本属于载荷签名的一部分，命中时返回可诊断重放，不命中时不读写当前库存。 */
	bool TryReplayInventoryItemUseTerminal(FGuid RequestId, int64 ExpectedRevision, FGuid ItemInstanceId,
		int32 Quantity, FCatInventoryItemUseResult& OutResult, int64 ExpectedInventoryRevision = 0) const;

	/** 部署型物品收口时共用的停止使用入口；它按实例调用定义侧 UnUse 裁决，成功才通过正式库存归还同一物品实例。 */
	FCatInventoryItemUseResult UnUse(FGuid RequestId, FGuid ItemInstanceId);

	/** 正式库存提交后刷新旧随身库存投影；钓鱼选择、存档和旧消费者靠它追上 InventoryComponent 的格位事实，返回 false 表示投影未能完整重建。 */
	bool RefreshInventoryProjectionFromInventoryComponentFromAuthority();

	/** 提交一次钓鱼失败预算；特殊饵和伤竿优先写正式库存事实，一个 RequestId 只能选择一种惩罚且绝不双罚。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	/** Fishing 会话开始前按 SessionId 申请当前钓鱼选择使用权；Begin 从正式库存暂存一份选中鱼饵，后续由本会话消耗或归还。 */
	FCatFishingUseReservationResult BeginFishingUse(FGuid FishingSessionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId, int64 ExpectedRevision);
	/** 确认消耗 Begin 已暂存的鱼饵；正式库存数量已经在 Begin 扣减，本函数只收口会话内的饵料事务。 */
	FCatFishingUseOperationResult CommitFishingBaitDeferred(FGuid FishingSessionId);
	/** 按递增累计磨损的差额立即扣减 Begin 绑定的鱼竿实例；重复序号不重扣，Release 不回滚。 */
	FCatFishingUseOperationResult ApplyFishingRodWear(FGuid FishingSessionId, int64 WearSequence,
		double AbsoluteTotal);
	/** 从该会话绑定的正式库存鱼竿实例读取耐久，不读取当前选择的另一根竿。 */
	bool GetFishingRodDurability(FGuid FishingSessionId, double& OutDurability, bool& OutBroken) const;
	/** 结束 Fishing 使用记录；未消耗的暂存饵会回到随身库存，已消耗的记录只关闭自身。 */
	FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
	/** 当前是否有仍未结束的 Fishing 使用记录；维修和失败预算用它避开进行中的钓鱼结算。 */
	bool HasActiveFishingUse() const;
	/** 指定 Fishing 会话是否仍处于活动状态；Commit/Release 用它防止旧会话重复改写。 */
	bool IsFishingUseActive(FGuid FishingSessionId) const;

	/** 固定营地修竿点提交维修；必须从正式库存扣一份浮木并恢复当前鱼竿实例，旧 Snapshot 只接收结果投影。 */
	FCatDomainCommandResult RepairRodAtCamp(FGuid RequestId, int64 ExpectedRevision, bool bAtCamp);

	/** 本机随身库存或钓鱼选择变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	/** 仅验证库存载荷的容量、定义、数量和选择；导出与恢复共用，authority 和活动事务由各自入口控制。 */
	bool ValidatePersistentSnapshotPayload(const FCatEquipmentLoadoutSnapshot& Candidate, FText& OutFailure) const;

	/** 营地公共仓库负责背包和公共仓库之间的服务器拖放事务；只允许它在同一提交里同时改双方快照并发布广播。 */
	friend class ACatCampInventoryActor;

	struct FCatFishingUseRecord
	{
		/** Begin 冻结的鱼竿实例与定义；后续磨损不得按当前选择重新选竿。 */
		FGuid RodItemInstanceId;
		FName RodDefinitionId = NAME_None;
		/** Begin 从正式库存移出的一份鱼饵定义；数量型物品脱离原堆栈后不再复用原 ItemInstanceId。 */
		FName ReservedBaitDefinitionId = NAME_None;
		/** 已接收的竿磨损序号；磨损事件按递增序号提交，重复或跳号不会改耐久。 */
		int64 LastWearSequence = 0;
		/** 已按差额写入绑定实例的累计磨损；仅用于序号去重，不是另一份剩余耐久。 */
		double AbsoluteRodWear = 0.0;
		/** 当前记录是否仍持有 Begin 移出的那份鱼饵；Commit 消耗或 Release 归还后清掉，防止同一份饵重复收口。 */
		bool bBaitQuantityReserved = false;
		/** 鱼饵是否已经被本会话确认消耗；它让重复结算只返回终态，不再次处理暂存物。 */
		bool bBaitCommitted = false;
		/** 本会话是否已经结束；结束后的记录只作为重放终态，不再保护鱼饵或接受耐久事件。 */
		bool bReleased = false;
	};

	struct FCatInventoryItemUseRecord
	{
		/** 正在使用的物品实例身份；同一实例只能存在一条活动记录，防止背包和场景同时持有它。 */
		FGuid ItemInstanceId;
		/** Use 时记录的旧槽位投影副本；UnUse 只用它还原定义裁决和耐久状态，归还仍由正式库存执行。 */
		FCatRunInventorySlot Item;
		/** Use 成功时的 Equipment 版本；诊断用它串联正式库存借出和后续世界 Actor 生成。 */
		int64 UseRevision = 0;
		/** 活动记录是否已经收口；收口后的记录不再参与可用性判断。 */
		bool bReleased = false;
	};

	FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId);
	const FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId) const;
	FCatInventoryItemUseRecord* FindInventoryItemUseRecord(FGuid ItemInstanceId);
	const FCatInventoryItemUseRecord* FindInventoryItemUseRecord(FGuid ItemInstanceId) const;
	/** 正式库存里部署物品的旧槽位投影；配置和 Fishing Begin 用它从 held entry 确认当前部署实例。 */
	bool TryBuildHeldInventoryUseSlot(FGuid ItemInstanceId, FCatRunInventorySlot& OutSlot) const;
	/** Begin 冻结鱼竿的正式实例解析；正式库存存在时，耐久读写必须落到可见格或 held entry 里的同一 UObject。 */
	UCatEquipmentInventoryItemInstance* ResolveFishingRodFormalInstanceFromInventory(
		const FCatFishingUseRecord& Record, FCatRunInventorySlot& OutProjectedSlot) const;
	/** 是否存在正式库存活动区尚未收口的物品 Use；维修和失败预算用它避免改写正在由场景持有的物品状态。 */
	bool HasActiveInventoryItemUse() const;
	/** 新入库或收回物品后修正钓鱼选择；已收回的坏竿可跨型号替换为库存里的可用竿，部署中与健康选择保持不变。 */
	void AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition, FName DefinitionId);
	/** 把当前选择中的鱼竿状态同步到正式库存实例和活动记录镜像；耐久和断竿事实必须跟最终归还的实例一致。 */
	void SyncSelectedRodStateToSelectedInstance();
	FCatFishingUseReservationResult MakeFishingUseReservationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bReserved, const FCatFishingUseRecord* Record = nullptr) const;
	FCatFishingUseOperationResult MakeFishingUseOperationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bApplied, const FCatFishingUseRecord* Record = nullptr) const;
	/** 客户端收到钓鱼选择和旧库存投影后只供 UI/玩法只读消费；不反向请求自动选择。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 读取随身库存配置容量；0 表示本局没有可用格子，写入路径必须拒绝新物品。 */
	int32 GetConfiguredInventorySlotCapacity() const;

	/** 解析 Owner 身上的正式随身库存组件；存在时物品发放、消耗和存档导入以它为库存事实源，Equipment 只从它刷新旧投影和钓鱼选择。 */
	UCatInventoryComponent* ResolveOwnerInventoryComponent() const;

	/** 读取一个定义在单格里的最大堆叠数；装备型物品固定为 1，数量型物品使用项目配置。 */
	int32 GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const;

	/** 让复制快照至少拥有配置声明的格子数；只追加空格，不截断已有物品。 */
	void EnsureInventorySlotArray();

	/** 补齐现有库存格的实例身份和工具状态；返回值表示本次是否修正了旧数据。 */
	bool NormalizeInventorySlots();

	/** 把指定兼容随身库存载荷转换成正式库存 entries；只服务存档恢复或旧格式导入，转换失败时不写入目标组件。 */
	bool BuildFormalEntriesFromSnapshot(const FCatEquipmentLoadoutSnapshot& SourceSnapshot,
		UCatInventoryComponent& TargetInventory,
		TArray<FCatInventoryEntry>& OutEntries);

	/** 为一格旧投影物品创建或复用正式装备实例；实例 ID、定义和鱼竿状态必须跟旧格保持一致。 */
	UCatEquipmentInventoryItemInstance* CreateOrUpdateFormalItemInstanceFromSlot(
		const FCatRunInventorySlot& Slot,
		UCatEquipmentDefinition& Definition,
		const TMap<FGuid, UCatEquipmentInventoryItemInstance*>& ExistingInstances);

	/** 把存档或旧格式中的随身库存载荷显式导入 Owner 正式库存；普通 Equipment 发布不能调用它反写背包事实。 */
	bool ImportSnapshotInventoryToOwnerInventoryComponent(const FCatEquipmentLoadoutSnapshot& SourceSnapshot);

	/** 从 Owner 的正式库存组件重建旧随身格数组；只做只读投影，不提交库存命令或推进 Equipment Revision。 */
	bool BuildSnapshotInventorySlotsFromOwnerInventoryComponent(TArray<FCatRunInventorySlot>& OutSlots) const;

	/** 把正式库存 entry 投成旧物品格；Equipment 的 Use 裁决还读旧结构时用它从库存事实创建只读载荷。 */
	bool BuildLegacyRunInventorySlotFromFormalEntry(const FCatInventoryEntry& Entry,
		FCatRunInventorySlot& OutSlot) const;

	/** 从正式库存定位当前选择的鱼竿实例；修竿和失败伤竿用它直接写实例状态，旧 Snapshot 只接收投影。 */
	UCatEquipmentInventoryItemInstance* ResolveSelectedFormalRodInstanceFromInventory(
		UCatInventoryComponent& OwnerInventory, const UCatEquipmentDefinition& RodDefinition,
		FCatRunInventorySlot& OutProjectedSlot) const;

	/** 从 Owner 正式库存刷新 Equipment 旧格位并可按新增物品修正当前选择；营地正式转移用它把背包事实和钓具选择放进同一次旧快照发布。 */
	bool RefreshInventoryProjectionFromInventoryComponentFromAuthority(
		const UCatEquipmentDefinition* GrantedDefinition, FName GrantedDefinitionId);

	/** 按实例身份查找旧随身库存投影格；选择和诊断用它避免只按 DefinitionId 误认同类物品。 */
	FCatRunInventorySlot* FindInventorySlotByInstanceId(FGuid ItemInstanceId);
	const FCatRunInventorySlot* FindInventorySlotByInstanceId(FGuid ItemInstanceId) const;

	/** 按定义和实例身份解析钓鱼选择候选；正式库存容量完整时只从 InventoryComponent 投影，旧 Snapshot 仅在迁移期未绑定或容量未齐时只读回退。 */
	bool TryResolveSelectionInventorySlot(FName DefinitionId, FGuid ItemInstanceId,
		FCatRunInventorySlot& OutSlot) const;

	/** 读取某个定义在旧投影中可见的第一份实例；旧 UI 仍按定义选择时用它落到具体实例身份，鱼竿会优先返回未断且有耐久的那份。 */
	const FCatRunInventorySlot* FindFirstInventorySlotByDefinition(FName DefinitionId) const;

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** 发布钓鱼选择和旧库存投影读模型；正式背包事实必须已经由 InventoryComponent 或恢复导入入口提交。 */
	void PublishSnapshot();

	/** 钓鱼选择、鱼竿耐久和旧库存投影的复制读模型；正式库存组件持有物品事实，Snapshot 只服务旧消费者。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 装备选择、物品发放和迁移期物品命令的首次终态缓存；背包整理已由 InventoryComponent 自己维护幂等结果。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；普通入库和 Use/UnUse 共用它防止同一 RequestId 被换定义、数量或实例后再次利用。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;

	/** 当前 Character 生命周期内按 SessionId 隔离的 fishing reservation/tombstone；不复制也不持久化。 */
	TMap<FGuid, FCatFishingUseRecord> FishingUseRecords;
	/** 当前 Character 生命周期内部署型 Use 的玩法镜像；正式 UObject 已由 InventoryComponent 活动区保管，这里只服务收口诊断和迁移期投影。 */
	TMap<FGuid, FCatInventoryItemUseRecord> InventoryItemUseRecords;
	/** 物品 Use/UnUse 首次终态缓存；简单消耗品重试会读它而不是再次扣量，部署/收回重试也不会重复移动同一实例。 */
	TMap<FString, FCatInventoryItemUseResult> InventoryItemUseTerminalCache;
	/** 临时测试发放的角色生命周期记录；不复制、不存档，避免把抄网移出背包后重占有刷出第二把。 */
	bool bStarterScoopNetGrantHandled = false;
	/** 抄网选择复制日志只在定义或实例变化时输出，不参与玩法裁决。 */
	FName LastLoggedScoopNetDefinitionId = NAME_None;
	FGuid LastLoggedScoopNetItemInstanceId;
	/** 仅用于客户端复制诊断限频，不参与耐久或玩法裁决。 */
	FGuid LastLoggedRodInstanceId;
	int32 LastLoggedRodDurabilityBand = INDEX_NONE;
	bool bLastLoggedRodBroken = false;
};
