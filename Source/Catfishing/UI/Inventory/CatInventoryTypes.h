#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "CatInventoryTypes.generated.h"

/** 背包相关的玩家动作意图；它只描述 UI 想对选中物体或鱼领域动作做什么，权限、Revision 和结果仍由服务器裁决。 */
UENUM(BlueprintType)
enum class ECatInventoryAction : uint8
{
	/** 当前没有可提交动作；View 和 Model 用它表示空选择或清空 pending。 */
	None,
	/** 请求吃掉当前选中的个人鱼护实物鱼；身体和成长效果只能在服务器 Items 提交成功后发生。 */
	ConsumeSelectedFish,
	/** 请求把拖拽源物体移动到另一个格子；UI 只提交物体身份、源/目标容器槽位和两个 Revision。 */
	MoveObjectBetweenContainers,
	/** 请求把当前选中的鱼交给献祭协议；Items 与 Run 的不可逆点继续由 SacrificeCoordinator 处理。 */
	SacrificeSelectedFish
};

/** 背包格子鼠标交互的 UI 层含义；格子不是 Button，因此点击、右键和拖拽都从原生鼠标重写函数发出。 */
UENUM(BlueprintType)
enum class ECatInventorySlotPointerAction : uint8
{
	/** 左键选择该格；主界面收到后让 Model 基于最新快照裁剪选择。 */
	Select,
	/** 右键打开或刷新该格上下文；默认只改变选择，不直接执行吃鱼或删除。 */
	Context,
	/** 拖拽开始；它只驱动 UMG 视觉链路，真正移动必须等待目标格 Drop 后走服务器事务。 */
	DragStarted
};

/** 背包格子投影的后端事实来源；UI 用它区分可拖拽容器格和当前装备鱼竿槽，避免把 Equipment 混进 Items 容器。 */
UENUM(BlueprintType)
enum class ECatInventorySlotSource : uint8
{
	/** 还没有可靠来源；这类格子只能展示占位，不能提交任何后端命令。 */
	Unknown,
	/** 当前角色 EquipmentComponent 中的鱼竿槽；它证明后端 Equipment 快照里当前手上是哪根竿。 */
	CurrentRod,
	/** Items 容器中的槽位；只有这种来源可以作为鱼或容器物体拖拽的源和目标。 */
	ContainerObject
};

/** 单个背包格子的只读显示投影；它可能来自当前鱼竿槽或 Items 容器，但不携带任何后端写入口。 */
USTRUCT(BlueprintType)
struct FCatInventorySlotView
{
	GENERATED_BODY()

	/** 该格在当前背包 WrapBox 中的全局显示下标；主界面用它把 Slot Widget 事件映射回 Model 选择。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** 该格来自哪类后端读模型；PageController 用它确认只有容器槽能提交容器移动。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventorySlotSource SlotSource = ECatInventorySlotSource::Unknown;

	/** 该格所属后端容器的公开种类；PageController 用它识别源目标容器类别，服务器仍会重建真实容器。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainerKind ContainerKind = ECatContainerKind::Unknown;

	/** 该格所属后端容器的一局公开 ID；它只用于识别当前 ViewState 中的来源和目标，不授权客户端写容器。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 该格所属后端容器在生成投影时的 Revision；拖拽提交时会作为服务器乐观并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 ContainerRevision = 0;

	/** 该格在所属容器内部的槽位下标；它与 Items 权威数组下标一致，是拖拽源/目标复核的一部分。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ContainerSlotIndex = INDEX_NONE;

	/** 该格是否有后端可展示内容；容器空格和空鱼竿槽只展示占位，不允许构造移动或鱼领域命令载荷。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOccupied = false;

	/** 该格是否允许启动拖拽；当前只对 Items 容器物体开放，鱼竿槽只能选中查看。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanDrag = false;

	/** 该格是否是当前 Model 高亮选择；蓝图只用它表现边框或颜色。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

	/** 该格展示的通用容器物体；只有 SlotSource 为 ContainerObject 且 ObjectInstanceId 有效时才可参与槽位移动。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance Object;

	/** 该格物体的领域类别；容器槽用它转发移动，鱼竿槽只用 Equipment 类别做展示区分。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainedObjectKind ObjectKind = ECatContainedObjectKind::Unknown;

	/** 容器物体的局内稳定实例 ID；拖拽时会和容器槽位一起复核，当前鱼竿槽不使用它授权。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ObjectInstanceId;

	/** 该格展示的鱼实例副本；只有容器槽的 ObjectKind 为 Fish 时有效，吃鱼和献祭继续读取这份鱼领域载荷。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;

	/** 该格对应的装备类别；当前只用于展示本人 Equipment 快照里的鱼竿槽。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEquipmentKind EquipmentKind = ECatEquipmentKind::Unknown;

	/** 该格对应的装备定义 ID；鱼竿槽从 Equipment 快照读取它来证明后端当前装了哪根竿。 */
	UPROPERTY(BlueprintReadOnly)
	FName EquipmentDefinitionId = NAME_None;

	/** 给蓝图 TextBlock 直接读取的中文摘要；C++ 只生成文本，不写具体 TextBlock。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 背包一次打开时纳入显示的一份容器投影；它把具体容器统一成可拖拽列表，不把背包写死到鱼缸或任一 Actor。 */
USTRUCT(BlueprintType)
struct FCatInventoryContainerView
{
	GENERATED_BODY()

	/** 容器的公开复制快照；UI 只读其中 ID、Kind、Revision、容量和对象投影，不能通过它写回 Items。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot Snapshot;

	/** 该容器在本次背包里的玩家可读名称；默认来自容器种类，具体交互对象以后可以提供更细名字。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayName;

	/** 该容器在 Slots 数组中的第一个显示下标；蓝图可用它分组或画标题，不参与服务器权限。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FirstSlotIndex = INDEX_NONE;

	/** 该容器在 Slots 数组中占用的显示格数量；容量为 0 时可能只保留一个占位格。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** 该容器是否是当前玩家自己的个人鱼护；吃鱼和献祭只对它开放。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPrimaryPersonalContainer = false;
};

/** 背包主界面的完整只读显示投影；它聚合鱼护、当前装配和随身耗材，但不提供任何写口。 */
USTRUCT(BlueprintType)
struct FCatInventoryViewState
{
	GENERATED_BODY()

	/** 当前 Character 个人鱼护复制快照；Items 仍是唯一写者，UI 只读鱼数组、容量和 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot PersonalFishGuard;

	/** 当前背包展示的所有容器；第一个通常是本人鱼护，后续来自本次交互对象提供的外部容器上下文。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventoryContainerView> Containers;

	/** 当前背包是否带有个人鱼护以外的容器；蓝图可用它决定是否显示跨容器拖拽分组。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasExternalContainers = false;

	/** 当前 Character 的装配和随身耗材快照；EquipmentComponent 写入，背包只读展示鱼竿、鱼饵、鱼漂和耗材数量。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** 当前是否已经绑定到本角色 EquipmentComponent；false 表示装备事实还不能可靠展示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bEquipmentAvailable = false;

	/** WrapBox 应创建的格子总数；当前鱼竿槽、个人鱼护和外部容器公开容量会合并到同一个显示列表。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** WrapBox 每个条目对应的格子显示投影；主界面刷新时按数组重建 Slot Widget。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> Slots;

	/** 当前选中显示格子下标；没有可选格或下标已失效时保持 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedSlotIndex = INDEX_NONE;

	/** 当前被选中的通用容器物体；没有选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance SelectedObject;

	/** 当前是否存在一个可展示和可拖拽的选中容器物体；当前鱼竿槽不进入这条容器选择事实。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedObject = false;

	/** 当前选中物体是否来自个人主容器；鱼领域按钮和未来个人容器动作都从这里派生 gate。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedObjectInPersonalContainer = false;

	/** 当前被选中的实物鱼副本；没有鱼选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFish;

	/** 当前是否存在一条可展示和可提交的选中鱼；蓝图用它控制详情区显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFish = false;

	/** 当前选中鱼是否来自个人鱼护；吃鱼和献祭按钮只允许使用本人鱼护选择。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedFishInPersonalGuard = false;

	/** 背包窗口当前是否打开；Model 只投影 PageController 的状态，不反查 Widget 可见性。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOpen = false;

	/** 当前是否可提交吃鱼或献祭意图；pending、空选择或外部容器选择时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSubmitAction = false;

	/** 当前是否已经发出背包动作但还没收到服务器终态；View 用它禁用重复提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionPending = false;

	/** 当前等待服务器回包的动作类型；没有 pending 时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction PendingAction = ECatInventoryAction::None;

	/** 当前等待服务器回包的 RequestId；PageController 生成，Model 用它匹配迟到结果。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingRequestId;

	/** 最近一次完成或本地拒绝的背包动作；蓝图用它把反馈归类到吃鱼、移动或献祭。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction LastAction = ECatInventoryAction::None;

	/** 最近一次背包动作的公共结果头；成功和拒绝都来自服务器或 PageController 适配层。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult LastCommandResult;

	/** 最近是否存在可展示的背包动作结果；false 时结果区不应展示旧文本。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasCommandResult = false;

	/** 最近一次吃鱼动作的详细结果；只有 LastAction 为 ConsumeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishConsumeResult LastConsumeResult;

	/** 最近一次献祭动作的详细结果；只有 LastAction 为 SacrificeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSacrificeResult LastSacrificeResult;

	/** 打开或关闭背包所用的键名；它从既有 InputContext 反查而来，只用于提示和模态焦点下关闭。 */
	UPROPERTY(BlueprintReadOnly)
	FName ToggleKeyName = NAME_None;

	/** 给蓝图直接展示的背包总览；它同时概括鱼、装备和耗材，不再把背包说成只有鱼护。 */
	UPROPERTY(BlueprintReadOnly)
	FText SummaryText;

	/** 给蓝图直接展示的当前装备摘要；没有同步到 Equipment 时会明确显示等待同步。 */
	UPROPERTY(BlueprintReadOnly)
	FText EquipmentText;

	/** 给蓝图直接展示的随身耗材数量；鱼饵、窝料和其他 RunConsumable 都来自 Equipment 快照。 */
	UPROPERTY(BlueprintReadOnly)
	FText ConsumablesText;

	/** 给蓝图直接展示的当前选择摘要；鱼会显示重量，其他物体显示类别和定义，字段名保留给既有 WBP 兼容。 */
	UPROPERTY(BlueprintReadOnly)
	FText SelectedFishText;

	/** 给蓝图直接展示的最近动作反馈；pending、成功和错误都只读这条文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;
};
